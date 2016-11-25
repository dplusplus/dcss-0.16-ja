/**
 * @file
 * @brief Traps related functions.
**/

#include "AppHdr.h"

#include "traps.h"
#include "trap_def.h"

#include <algorithm>
#include <cmath>

#include "act-iter.h"
#include "areas.h"
#include "bloodspatter.h"
#include "branch.h"
#include "cloud.h"
#include "coordit.h"
#include "database.h"
#include "delay.h"
#include "describe.h"
#include "directn.h"
#include "dungeon.h"
#include "english.h"
#include "exercise.h"
#include "hints.h"
#include "itemprop.h"
#include "items.h"
#include "libutil.h"
#include "mapmark.h"
#include "mon-enum.h"
#include "mon-tentacle.h"
#include "mgen_enum.h"
#include "message.h"
#include "misc.h"
#include "mon-place.h"
#include "mon-transit.h"
#include "output.h"
#include "prompt.h"
#include "random-weight.h"
#include "religion.h"
#include "shout.h"
#include "spl-miscast.h"
#include "stash.h"
#include "state.h"
#include "stringutil.h"
#include "terrain.h"
#include "travel.h"
#include "view.h"
#include "xom.h"

bool trap_def::active() const
{
    return type != TRAP_UNASSIGNED;
}

bool trap_def::type_has_ammo() const
{
    switch (type)
    {
#if TAG_MAJOR_VERSION == 34
    case TRAP_DART:
#endif
    case TRAP_ARROW:  case TRAP_BOLT:
    case TRAP_NEEDLE: case TRAP_SPEAR:
        return true;
    default:
        break;
    }
    return false;
}

// Used for when traps run out of ammo.
void trap_def::disarm()
{
    if (type == TRAP_NET)
    {
        item_def trap_item = generate_trap_item();
        copy_item_to_grid(trap_item, pos);
    }
    destroy();
}

void trap_def::destroy(bool known)
{
    if (!in_bounds(pos))
        die("Trap position out of bounds!");

    grd(pos) = DNGN_FLOOR;
    ammo_qty = 0;
    type     = TRAP_UNASSIGNED;

    if (known)
    {
        env.map_knowledge(pos).set_feature(DNGN_FLOOR);
        StashTrack.update_stash(pos);
    }

    pos      = coord_def(-1,-1);
}

void trap_def::hide()
{
    grd(pos) = DNGN_UNDISCOVERED_TRAP;
}

void trap_def::prepare_ammo(int charges)
{
    skill_rnd = random2(256);

    if (charges)
    {
        ammo_qty = charges;
        return;
    }
    switch (type)
    {
    case TRAP_ARROW:
    case TRAP_BOLT:
    case TRAP_NEEDLE:
        ammo_qty = 3 + random2avg(9, 3);
        break;
    case TRAP_SPEAR:
        ammo_qty = 2 + random2avg(6, 3);
        break;
    case TRAP_GOLUBRIA:
        // really, turns until it vanishes
        ammo_qty = 30 + random2(20);
        break;
    case TRAP_TELEPORT:
        if (crawl_state.game_is_zotdef())
            ammo_qty = 2 + random2(2);
        else
            ammo_qty = 1;
        break;
    default:
        ammo_qty = 0;
        break;
    }
    // Zot def: traps have 10x as much ammo
    if (crawl_state.game_is_zotdef() && type != TRAP_GOLUBRIA)
        ammo_qty *= 10;
}

void trap_def::reveal()
{
    grd(pos) = category();
}

string trap_def::name(description_level_type desc) const
{
    if (type >= NUM_TRAPS)
        return "buggy";

    string basename = full_trap_name(type);
    if (desc == DESC_A)
    {
        string prefix = "a";
        if (is_vowel(basename[0]))
            prefix += 'n';
        prefix += ' ';
        return prefix + basename;
    }
    else if (desc == DESC_THE)
        return string("the ") + basename;
    else                        // everything else
        return basename;
}

bool trap_def::is_known(const actor* act) const
{
    const bool player_knows = (grd(pos) != DNGN_UNDISCOVERED_TRAP);

    if (act == nullptr || act->is_player())
        return player_knows;
    else if (act->is_monster())
    {
        const monster* mons = act->as_monster();
        const int intel = mons_intel(mons);

        // Smarter trap handling for intelligent monsters
        // * monsters native to a branch can be assumed to know the trap
        //   locations and thus be able to avoid them
        // * friendlies and good neutrals can be assumed to have been warned
        //   by the player about all traps s/he knows about
        // * very intelligent monsters can be assumed to have a high T&D
        //   skill (or have memorised part of the dungeon layout ;))

        if (type == TRAP_SHAFT)
        {
            // Slightly different rules for shafts:
            // * Lower intelligence requirement for native monsters.
            // * Allied zombies won't fall through shafts. (No herding!)
            // * Highly intelligent monsters never fall through shafts.
            return intel >= I_HIGH
                   || intel > I_PLANT && mons_is_native_in_branch(mons)
                   || player_knows && mons->wont_attack();
        }
        else
        {
            if (intel < I_NORMAL)
                return false;
            if (player_knows && mons->wont_attack())
                return true;

            // This should ultimately be removed, but only after monsters
            // learn to make use of the trap being limited in ZotDef if
            // there is no other way.  Golubria and Malign Gateway are a
            // problem, too...
            if (crawl_state.game_is_zotdef())
                return false;

            return mons_is_native_in_branch(mons)
                   || intel >= I_HIGH && one_chance_in(3);
        }
    }
    die("invalid actor type");
}

bool trap_def::is_safe(actor* act) const
{
    if (!act)
        act = &you;

    // TODO: For now, just assume they're safe; they don't damage outright,
    // and the messages get old very quickly
    if (category() == DNGN_TRAP_WEB) // && act->is_web_immune()
        return true;

    if (type == TRAP_SHADOW_DORMANT)
        return true;

    if (!act->is_player())
        return false;

    // No prompt (teleport traps are ineffective if wearing an amulet of
    // stasis or a -Tele item)
    if ((type == TRAP_TELEPORT || type == TRAP_TELEPORT_PERMANENT)
        && you.no_tele(false))
    {
        return true;
    }

    if (!is_known(act))
        return false;

    if (type == TRAP_GOLUBRIA || type == TRAP_SHAFT
        || crawl_state.game_is_zotdef())
    {
        return true;
    }

#ifdef CLUA_BINDINGS
    // Let players specify traps as safe via lua.
    if (clua.callbooleanfn(false, "c_trap_is_safe", "s", trap_name(type).c_str()))
        return true;
#endif

    if (type == TRAP_NEEDLE)
        return you.hp > 15;
    else if (type == TRAP_ARROW)
        return you.hp > 35;
    else if (type == TRAP_BOLT)
        return you.hp > 45;
    else if (type == TRAP_SPEAR)
        return you.hp > 40;
    else if (type == TRAP_BLADE)
        return you.hp > 95;

    return false;
}

/**
 * Get the item index of the first net on the square.
 *
 * @param where The location.
 * @param trapped If true, the index of the stationary net (trapping a victim)
 *                is returned.
 * @return  The item index of the net.
*/
int get_trapping_net(const coord_def& where, bool trapped)
{
    for (stack_iterator si(where); si; ++si)
    {
        if (si->is_type(OBJ_MISSILES, MI_THROWING_NET)
            && (!trapped || item_is_stationary_net(*si)))
        {
            return si->index();
        }
    }
    return NON_ITEM;
}

/**
 * Return a string describing the reason a given actor is ensnared. (Since nets
 * & webs use the same status.
 *
 * @param actor     The ensnared actor.
 * @return          Either 'held in a net' or 'caught in a web'.
 */
const char* held_status(actor *act)
{
    act = act ? act : &you;

    if (get_trapping_net(act->pos(), true) != NON_ITEM)
        return "held in a net";
    else
        return "caught in a web";
}

// If there are more than one net on this square
// split off one of them for checking/setting values.
static void _maybe_split_nets(item_def &item, const coord_def& where)
{
    if (item.quantity == 1)
    {
        set_net_stationary(item);
        return;
    }

    item_def it;

    it.base_type = item.base_type;
    it.sub_type  = item.sub_type;
    it.net_durability      = item.net_durability;
    it.net_placed  = item.net_placed;
    it.flags     = item.flags;
    it.special   = item.special;
    it.quantity  = --item.quantity;
    item_colour(it);

    item.quantity = 1;
    set_net_stationary(item);

    copy_item_to_grid(it, where);
}

static void _mark_net_trapping(const coord_def& where)
{
    int net = get_trapping_net(where);
    if (net == NON_ITEM)
    {
        net = get_trapping_net(where, false);
        if (net != NON_ITEM)
            _maybe_split_nets(mitm[net], where);
    }
}

/**
 * Attempt to trap a monster in a net.
 *
 * @param mon       The monster being trapped.
 * @param agent     The entity doing the trapping.
 * @return          Whether the monster was successfully trapped.
 */
bool monster_caught_in_net(monster* mon, actor* agent)
{
    if (mon->body_size(PSIZE_BODY) >= SIZE_GIANT)
    {
        if (mons_near(mon) && !mon->visible_to(&you))
            mpr(jtrans("The net bounces off something gigantic!"));
        else
            simple_monster_message(mon, jtransc(" is too large for the net to hold!"));
        return false;
    }

    if (mons_class_is_stationary(mon->type))
    {
        if (you.see_cell(mon->pos()))
        {
            if (mon->visible_to(&you))
            {
                mprf(jtransc("The net is caught on %s!"),
                     jtransc(mon->name(DESC_THE)));
            }
            else
                mpr(jtrans("The net is caught on something unseen!"));
        }
        return false;
    }

    if (mon->is_insubstantial())
    {
        if (you.can_see(mon))
        {
            mprf(jtransc("The net passes right through %s!"),
                 jtransc(mon->name(DESC_THE)));
        }
        return false;
    }

    if (mon->type == MONS_OOZE)
    {
        simple_monster_message(mon, jtransc(" oozes right through the net!"));
        return false;
    }

    if (!mon->caught() && mon->add_ench(ENCH_HELD))
    {
        if (mons_near(mon) && !mon->visible_to(&you))
            mpr(jtrans("Something gets caught in the net!"));
        else
            simple_monster_message(mon, jtransc(" is caught in the net!"));
        return true;
    }

    return false;
}

bool player_caught_in_net()
{
    if (you.body_size(PSIZE_BODY) >= SIZE_GIANT)
        return false;

    if (!you.attribute[ATTR_HELD])
    {
        mpr(jtrans("You become entangled in the net!"));
        stop_running();

        // Set the attribute after the mpr, otherwise the screen updates
        // and we get a glimpse of a web because there isn't a trapping net
        // item yet
        you.attribute[ATTR_HELD] = 10;

        stop_delay(true); // even stair delays
        return true;
    }
    return false;
}

void check_net_will_hold_monster(monster* mons)
{
    if (mons->body_size(PSIZE_BODY) >= SIZE_GIANT)
    {
        int net = get_trapping_net(mons->pos());
        if (net != NON_ITEM)
            destroy_item(net);

        if (you.see_cell(mons->pos()))
        {
            if (mons->visible_to(&you))
            {
                mprf(jtransc("The net rips apart, and %s comes free!"),
                     jtransc(mons->name(DESC_THE)));
            }
            else
                mpr(jtrans("All of a sudden the net rips apart!"));
        }
    }
    else if (mons->is_insubstantial()
             || mons->type == MONS_OOZE)
    {
        const int net = get_trapping_net(mons->pos());
        if (net != NON_ITEM)
            free_stationary_net(net);

        if (mons->is_insubstantial())
        {
            simple_monster_message(mons,
                                   jtransc(" drifts right through the net!"));
        }
        else
        {
            simple_monster_message(mons,
                                   jtransc(" oozes right through the net!"));
        }
    }
    else
        mons->add_ench(ENCH_HELD);
}

static bool _player_caught_in_web()
{
    if (you.attribute[ATTR_HELD])
        return false;

    you.attribute[ATTR_HELD] = 10;
    // No longer stop_running() and stop_delay().
    redraw_screen(); // Account for changes in display.
    return true;
}

vector<coord_def> find_golubria_on_level()
{
    vector<coord_def> ret;
    for (rectangle_iterator ri(coord_def(0, 0), coord_def(GXM-1, GYM-1)); ri; ++ri)
    {
        trap_def *trap = find_trap(*ri);
        if (trap && trap->type == TRAP_GOLUBRIA)
            ret.push_back(*ri);
    }
    return ret;
}

static bool _find_other_passage_side(coord_def& to)
{
    vector<coord_def> clear_passages;
    for (coord_def passage : find_golubria_on_level())
    {
        if (passage != to && !actor_at(passage))
            clear_passages.push_back(passage);
    }
    const int choices = clear_passages.size();
    if (choices < 1)
        return false;
    to = clear_passages[random2(choices)];
    return true;
}

/**
 * Spawn a single shadow creature or band from the current level. The creatures
 * are always hostile to the player.
 *
 * @param triggerer     The creature that set off the trap.
 * @return              Whether any monsters were successfully created.
 */
bool trap_def::weave_shadow(const actor& triggerer)
{
    // forbid early packs
    const bool bands_ok = env.absdepth0 > 3;
    mgen_data mg = mgen_data::hostile_at(RANDOM_MOBILE_MONSTER,
                                         "a shadow trap", // blame
                                         you.see_cell(pos), // alerted?
                                         5, // abj duration
                                         MON_SUMM_SHADOW,
                                         pos,
                                         bands_ok ? 0 : MG_FORBID_BANDS);

    monster *leader = create_monster(mg);
    if (!leader)
        return false;

    const string triggerer_name = triggerer.is_player() ?
                                        "the player character" :
                                        triggerer.name(DESC_PLAIN, true);
    const string blame = "triggered by " + triggerer_name;
    mons_add_blame(leader, blame);

    for (monster_iterator mi; mi; ++mi)
    {
        monster *follower = *mi;
        if (!follower || !follower->alive())
            continue;

        if (mid_t(follower->props["band_leader"].get_int()) == leader->mid)
        {
            ASSERT(follower->mid != leader->mid);
            mons_add_blame(follower, blame);
        }
    }

    return true;
}

/**
 * Is the given monster non-summoned, & therefore capable of triggering a
 * shadow trap that it stepped on?
 */
bool can_trigger_shadow_trap(const monster &mons)
{
    // this is very silly and there is probably a better way
    return !mons.has_ench(ENCH_ABJ)
        && !mons.has_ench(ENCH_FAKE_ABJURATION)
        && !mons.is_perm_summoned();
}

/**
 * Trigger a shadow creature trap.
 *
 * Temporarily summons some number of shadow creatures/bands from the current
 * level. 3-5 creatures/bands, increasing with depth.
 *
 * @param triggerer     The creature that set off the trap.
 */
void trap_def::trigger_shadow_trap(const actor& triggerer)
{
    if (triggerer.is_monster()
        && !can_trigger_shadow_trap(*triggerer.as_monster()))
    {
        return; // no summonsplosions
    }

    if (mons_is_tentacle_or_tentacle_segment(triggerer.type))
        return; // no krakensplosions

    if (!you.see_cell(pos))
        return;

    const int to_summon = 3 + div_rand_round(env.absdepth0, 16);
    dprf ("summoning %d dudes from %d", to_summon, env.absdepth0);

    bool summoned_any = false;
    for (int i = 0; i < to_summon; ++i)
    {
        const bool successfully_summoned = weave_shadow(triggerer);
        summoned_any = summoned_any || successfully_summoned;
    }

    mprf(jtransc("Shadows whirl around %s..."), jtransc(triggerer.name(DESC_THE)));
    if (!summoned_any)
        mpr(jtrans("...but the shadows disperse without effect."));

    // now we go dormant for a bit
    type = TRAP_SHADOW_DORMANT;
    grd(pos) = category();
    env.map_knowledge(pos).set_feature(grd(pos), 0, type);
    // store the time until the trap will become active again
    // I apologize sincerely for this flagrant field misuse
    // TODO: don't do this
    ammo_qty = 2 + random2(3); // 2-4 turns
    dprf("trap deactivating until %d turns pass", ammo_qty);
}

// Returns a direction string from you.pos to the
// specified position. If fuzz is true, may be wrong.
// Returns an empty string if no direction could be
// determined (if fuzz if false, this is only if
// you.pos==pos).
static string _direction_string(coord_def pos, bool fuzz)
{
    int dx = you.pos().x - pos.x;
    if (fuzz)
        dx += random2avg(41,2) - 20;
    int dy = you.pos().y - pos.y;
    if (fuzz)
        dy += random2avg(41,2) - 20;
    const char *ew=((dx > 0) ? "西" : ((dx < 0) ? "東" : ""));
    const char *ns=((dy < 0) ? "南" : ((dy > 0) ? "北" : ""));
    if (abs(dy) > 2 * abs(dx))
        ew="";
    if (abs(dx) > 2 * abs(dy))
        ns="";
    return string(ns) + ew;
}

void trap_def::trigger(actor& triggerer, bool flat_footed)
{
    const bool you_know = is_known();
    const bool trig_knows = !flat_footed && is_known(&triggerer);

    const bool you_trigger = triggerer.is_player();
    const bool in_sight = you.see_cell(pos);

    // Zot def - player never sets off known traps
    if (crawl_state.game_is_zotdef() && you_trigger && you_know)
    {
        mpr(jtrans("You step safely past the trap."));
        return;
    }

    // If set, the trap will be removed at the end of the
    // triggering process.
    bool trap_destroyed = false, know_trap_destroyed = false;;

    monster* m = triggerer.as_monster();

    // Smarter monsters and those native to the level will simply
    // side-step known shafts. Unless they are already looking for
    // an exit, of course.
    if (type == TRAP_SHAFT && m)
    {
        if (!m->will_trigger_shaft()
            || trig_knows && !mons_is_fleeing(m) && !m->pacified())
        {
            if (you_know)
                simple_monster_message(m, jtransc(" carefully avoids the shaft."));
            return;
        }
    }

    // Zot def - friendly monsters never set off known traps
    if (crawl_state.game_is_zotdef() && m && m->friendly() && trig_knows)
    {
        simple_monster_message(m, jtransc(" carefully avoids a trap."));
        return;
    }

    // Anything stepping onto a trap almost always reveals it.
    // (We can rehide it later for the exceptions.)
    if (in_sight)
        reveal();

    // Store the position now in case it gets cleared in between.
    const coord_def p(pos);

    if (type_has_ammo())
        shoot_ammo(triggerer, trig_knows);
    else switch (type)
    {
    case TRAP_GOLUBRIA:
    {
        coord_def to = p;
        if (_find_other_passage_side(to))
        {
            if (you_trigger)
                mpr(jtrans("You enter the passage of Golubria."));
            else
                simple_monster_message(m, jtransc(" enters the passage of Golubria."));

            if (triggerer.move_to_pos(to))
            {
                if (you_trigger)
                    place_cloud(CLOUD_TLOC_ENERGY, p, 1 + random2(3), &you);
                else
                    place_cloud(CLOUD_TLOC_ENERGY, p, 1 + random2(3), m);
                trap_destroyed = true;
                know_trap_destroyed = you_trigger;
            }
            else
                mpr(jtrans("But it is blocked!"));
        }
        break;
    }
    case TRAP_TELEPORT:
    case TRAP_TELEPORT_PERMANENT:
        // Never revealed by monsters.
        // except when it's in sight, it's pretty obvious what happened. -doy
        if (!you_trigger && !you_know && !in_sight)
            hide();
        if (you_trigger)
            mprf(jtransc("You enter %s!"), jtransc(name(DESC_PLAIN)));
        if (ammo_qty > 0 && !--ammo_qty)
        {
            // can't use trap_destroyed, as we might recurse into a shaft
            // or be banished by a Zot trap
            if (in_sight)
            {
                env.map_knowledge(pos).set_feature(DNGN_FLOOR);
                mprf(jtransc("%s disappears."), jtransc(name(DESC_PLAIN)));
            }
            disarm();
        }
        if (!triggerer.no_tele(true, you_know || you_trigger))
            triggerer.teleport(true);
        break;

    case TRAP_ALARM:
        // In ZotDef, alarm traps don't go away after use.
        if (!crawl_state.game_is_zotdef())
            trap_destroyed = true;

        if (silenced(pos))
        {
            if (you_know && in_sight)
            {
                mprf(jtransc("%s vibrates slightly, failing to make a sound."),
                     jtransc(name(DESC_PLAIN)));
            }
        }
        else
        {
            string msg;
            if (you_trigger)
            {
                msg = make_stringf(jtransc("%s emits a blaring wail!"),
                                   jtransc(name(DESC_PLAIN)));
            }
            else
            {
                string dir = _direction_string(pos, !in_sight);
                msg = string("あなたは")
                    + (!dir.empty() ? (dir + "の方向から") : (in_sight ? "真後ろから" : "真後ろで"))
                    + ((in_sight) ? "" : "遠く")
                    + "鳴り響く警報音を耳にした。";
            }

            // XXX: this is very goofy and probably should be replaced with
            // const mid_t source = triggerer.mid;
            mid_t source = !m ? MID_PLAYER :
                            mons_intel(m) >= I_NORMAL ? m->mid : MID_NOBODY;

            noisy(40, pos, msg.c_str(), source, NF_MESSAGE_IF_UNSEEN);
            if (crawl_state.game_is_zotdef())
                more();
        }

        if (you_trigger)
            you.sentinel_mark(true);
        break;

    case TRAP_BLADE:
        if (you_trigger)
        {
            if (trig_knows && one_chance_in(3))
                mpr(jtrans("You avoid triggering a blade trap."));
            else if (random2limit(player_evasion(), 40)
                     + (random2(you.dex()) / 3) + (trig_knows ? 3 : 0) > 8)
            {
                mpr(jtrans("A huge blade swings just past you!"));
            }
            else
            {
                mpr(jtrans("A huge blade swings out and slices into you!"));
                const int damage = you.apply_ac(48 + random2avg(29, 2));
                string n = name(DESC_A);
                ouch(damage, KILLED_BY_TRAP, MID_NOBODY, n.c_str());
                bleed_onto_floor(you.pos(), MONS_PLAYER, damage, true);
            }
        }
        else if (m)
        {
            if (one_chance_in(5) || (trig_knows && coinflip()))
            {
                // Trap doesn't trigger. Don't reveal it.
                if (you_know)
                {
                    simple_monster_message(m,
                                           jtransc(" fails to trigger a blade trap."));
                }
                else
                    hide();
            }
            else if (random2(m->evasion()) > 8
                     || (trig_knows && random2(m->evasion()) > 8))
            {
                if (in_sight
                    && !simple_monster_message(m,
                                               jtransc(" avoids a huge, swinging blade.")))
                {
                    mpr(jtrans("A huge blade swings out!"));
                }
            }
            else
            {
                if (in_sight)
                {
                    string msg = jtrans("A huge blade swings out");
                    if (m->visible_to(&you))
                    {
                        msg += "て、";
                        msg += jtrans(m->name(DESC_THE));
                        msg += "に突き刺さっ";
                    }
                    msg += "た！";
                    mpr(msg);
                }

                int damage_taken = m->apply_ac(10 + random2avg(29, 2));

                if (!m->is_summoned())
                    bleed_onto_floor(m->pos(), m->type, damage_taken, true);

                m->hurt(nullptr, damage_taken);
                if (in_sight && m->alive())
                    print_wounds(m);

                // zotdef: blade traps break eventually
                if (crawl_state.game_is_zotdef() && one_chance_in(200))
                {
                    if (in_sight)
                        mpr(jtrans("The blade breaks!"));
                    disarm();
                }
            }
        }
        break;

    case TRAP_NET:
        if (you_trigger)
        {
            if (trig_knows && one_chance_in(3))
                mpr(jtrans("A net swings high above you."));
            else
            {
                item_def item = generate_trap_item();
                copy_item_to_grid(item, triggerer.pos());

                if (random2limit(player_evasion(), 40)
                    + (random2(you.dex()) / 3) + (trig_knows ? 3 : 0) > 12)
                {
                    mpr(jtrans("A net drops to the ground!"));
                }
                else
                {
                    mpr(jtrans("A large net falls onto you!"));
                    if (player_caught_in_net())
                    {
                        if (player_in_a_dangerous_place())
                            xom_is_stimulated(50);

                        // Mark the item as trapping; after this it's
                        // safe to update the view
                        _mark_net_trapping(you.pos());
                    }
                }

                trap_destroyed = true;
            }
        }
        else if (m)
        {
            bool triggered = false;
            if (one_chance_in(3) || (trig_knows && coinflip()))
            {
                // Not triggered, trap stays.
                triggered = false;
                if (you_know)
                    simple_monster_message(m, jtransc(" fails to trigger a net trap."));
                else
                    hide();
            }
            else if (random2(m->evasion()) > 8
                     || (trig_knows && random2(m->evasion()) > 8))
            {
                // Triggered but evaded.
                triggered = true;

                if (in_sight)
                {
                    if (!simple_monster_message(m,
                                                jtransc(" nimbly jumps out of the way "
                                                        "of a falling net.")))
                    {
                        mpr(jtrans("A large net falls down!"));
                    }
                }
            }
            else
            {
                // Triggered and hit.
                triggered = true;

                if (in_sight)
                {
                    if (m->visible_to(&you))
                    {
                        mprf(jtransc("A large net falls down onto %s!"),
                             jtransc(m->name(DESC_THE)));
                    }
                    else
                        mpr(jtrans("A large net falls down!"));
                }

                // actually try to net the monster
                if (monster_caught_in_net(m, nullptr))
                {
                    // Don't try to escape the net in the same turn
                    m->props[NEWLY_TRAPPED_KEY] = true;
                }
            }

            if (triggered)
            {
                item_def item = generate_trap_item();
                copy_item_to_grid(item, triggerer.pos());

                if (m->caught())
                    _mark_net_trapping(m->pos());

                trap_destroyed = true;
            }
        }
        break;

    case TRAP_WEB:
        if (triggerer.body_size(PSIZE_BODY) >= SIZE_GIANT)
        {
            trap_destroyed = true;
            if (you_trigger)
                mpr(jtrans("You tear through %s web."));
            else if (m)
                simple_monster_message(m, jtransc(" tears through a web."));
            break;
        }

        if (triggerer.is_web_immune())
        {
            if (m)
            {
                if (m->is_insubstantial())
                    simple_monster_message(m, jtransc(" passes through a web."));
                else if (mons_genus(m->type) == MONS_JELLY)
                    simple_monster_message(m, jtransc(" oozes through a web."));
                // too spammy for spiders, and expected
            }
            break;
        }

        if (you_trigger)
        {
            if (trig_knows && one_chance_in(3))
                mpr(jtrans("You pick your way through the web."));
            else
            {
                mpr(jtrans("You are caught in the web!"));

                if (_player_caught_in_web())
                {
                    check_monsters_sense(SENSE_WEB_VIBRATION, 100, you.pos());
                    if (player_in_a_dangerous_place())
                        xom_is_stimulated(50);
                }
            }
        }
        else if (m)
        {
            if (one_chance_in(3) || (trig_knows && coinflip()))
            {
                // Not triggered, trap stays.
                if (you_know)
                    simple_monster_message(m, jtransc(" evades a web."));
                else
                    hide();
            }
            else
            {
                // Triggered and hit.
                if (in_sight)
                {
                    if (m->visible_to(&you))
                        simple_monster_message(m, jtransc(" is caught in a web!"));
                    else
                        mpr(jtrans("A web moves frantically as something is caught in it!"));
                }

                // If somehow already caught, make it worse.
                m->add_ench(ENCH_HELD);

                // Don't try to escape the web in the same turn
                m->props[NEWLY_TRAPPED_KEY] = true;

                // Alert monsters.
                check_monsters_sense(SENSE_WEB_VIBRATION, 100, triggerer.position);
            }
        }
        break;

    case TRAP_ZOT:
        if (you_trigger)
        {
            mpr(jtrans((trig_knows) ? "You enter the Zot trap."
                                    : "Oh no! You have blundered into a Zot trap!"));
            if (!trig_knows)
                xom_is_stimulated(25);

            MiscastEffect(&you, nullptr, ZOT_TRAP_MISCAST, SPTYP_RANDOM,
                           3, name(DESC_PLAIN));
        }
        else if (m)
        {
            // Zot traps are out to get *the player*! Hostile monsters
            // benefit and friendly monsters suffer. Such is life.

            // The old code rehid the trap, but that's pure interface screw
            // in 99% of cases - a player can just watch who stepped where
            // and mark the trap on an external paper map.  Not good.

            actor* targ = nullptr;
            if (you.see_cell_no_trans(pos))
            {
                if (m->wont_attack() || crawl_state.game_is_arena())
                    targ = m;
                else if (one_chance_in(5))
                    targ = &you;
            }

            // Give the player a chance to figure out what happened
            // to their friend.
            if (player_can_hear(pos) && (!targ || !in_sight))
            {
                mpr_nojoin(MSGCH_SOUND, jtrans(make_stringf("You hear a %s \"Zot\"!",
                                                            in_sight ? "loud" : "distant")));
            }

            if (targ)
            {
                if (in_sight)
                {
                    mprf(jtransc("The power of Zot is invoked against %s!"),
                         jtransc(targ->name(DESC_THE)));
                }
                MiscastEffect(targ, nullptr, ZOT_TRAP_MISCAST, SPTYP_RANDOM,
                              3, "the power of Zot");
            }
        }
        break;

    case TRAP_SHAFT:
        // Unknown shafts are traps triggered by walking onto them.
        // Known shafts are used as escape hatches

        // Paranoia
        if (!is_valid_shaft_level())
        {
            if (you_know && in_sight)
                mpr(jtrans("The shaft disappears in a puff of logic!"));

            trap_destroyed = true;
            break;
        }

        // If the shaft isn't known, don't reveal it.
        // The shafting code in downstairs() needs to know
        // whether it's undiscovered.
        if (!you_know)
            hide();

        // Known shafts don't trigger as traps.
        if (trig_knows)
            break;

        // A chance to escape.
        if (one_chance_in(4))
            break;

        // Fire away!
        triggerer.do_shaft();

        // Player-used shafts are destroyed
        // after one use in down_stairs(), misc.cc
        if (!you_trigger)
        {
            if (in_sight)
            {
                mpr(jtrans("The shaft crumbles and collapses."));
                know_trap_destroyed = true;
            }
            trap_destroyed = true;
        }
        break;

#if TAG_MAJOR_VERSION == 34
    case TRAP_GAS:
        if (in_sight && you_know)
            mpr("The gas trap seems to be inoperative.");
        trap_destroyed = true;
        break;
#endif

    case TRAP_PLATE:
        dungeon_events.fire_position_event(DET_PRESSURE_PLATE, pos);
        break;

    case TRAP_SHADOW:
        trigger_shadow_trap(triggerer);
        break;

    case TRAP_SHADOW_DORMANT:
    default:
        break;
    }

    if (you_trigger)
        learned_something_new(HINT_SEEN_TRAP, p);

    if (trap_destroyed)
        destroy(know_trap_destroyed);
}

int trap_def::max_damage(const actor& act)
{
    // Trap damage to monsters is a lot smaller, because they are fairly
    // stupid and tend to have fewer hp than players -- this choice prevents
    // traps from easily killing large monsters.
    bool mon = act.is_monster();

    switch (type)
    {
        case TRAP_NEEDLE: return 0;
        case TRAP_ARROW:  return mon ?  7 : 15;
        case TRAP_SPEAR:  return mon ? 10 : 26;
        case TRAP_BOLT:   return mon ? 18 : 40;
        case TRAP_BLADE:  return mon ? 38 : 76;
        default:          return 0;
    }

    return 0;
}

int trap_def::shot_damage(actor& act)
{
    const int dam = max_damage(act);

    if (!dam)
        return 0;
    return random2(dam) + 1;
}

int trap_def::difficulty()
{
    switch (type)
    {
    // To-hit:
    case TRAP_ARROW:
        return 7;
    case TRAP_SPEAR:
        return 10;
    case TRAP_BOLT:
        return 15;
    case TRAP_NET:
        return 5;
    case TRAP_NEEDLE:
        return 8;
    // Irrelevant:
    default:
        return 0;
    }
}

int reveal_traps(const int range)
{
    int traps_found = 0;

    for (int i = 0; i < MAX_TRAPS; i++)
    {
        trap_def& trap = env.trap[i];

        if (!trap.active())
            continue;

        if (distance2(you.pos(), trap.pos) < dist_range(range) && !trap.is_known())
        {
            traps_found++;
            trap.reveal();
            env.map_knowledge(trap.pos).set_feature(grd(trap.pos), 0, trap.type);
            set_terrain_mapped(trap.pos);
        }
    }

    return traps_found;
}

void destroy_trap(const coord_def& pos)
{
    if (trap_def* ptrap = find_trap(pos))
        ptrap->destroy();
}

trap_def* find_trap(const coord_def& pos)
{
    if (!feat_is_trap(grd(pos), true))
        return nullptr;

    unsigned short t = env.tgrid(pos);

    ASSERT(t != NON_ENTITY);
    ASSERT(t < MAX_TRAPS);
    ASSERT(env.trap[t].pos == pos);
    ASSERT(env.trap[t].type != TRAP_UNASSIGNED);

    return &env.trap[t];
}

trap_type get_trap_type(const coord_def& pos)
{
    if (trap_def* ptrap = find_trap(pos))
        return ptrap->type;

    return TRAP_UNASSIGNED;
}

void search_around()
{
    ASSERT(!crawl_state.game_is_arena());

    int base_skill = you.experience_level * 100 / 3;
    int skill = (2/(1+exp(-(base_skill+120)/325.0))-1) * 225
    + (base_skill/200.0) + 15;

    if (in_good_standing(GOD_ASHENZARI))
        skill += you.piety * 2;

    int max_dist = div_rand_round(skill, 32);
    if (max_dist > 5)
        max_dist = 5;
    if (max_dist < 1)
        max_dist = 1;

    for (radius_iterator ri(you.pos(), max_dist, C_ROUND, LOS_NO_TRANS); ri; ++ri)
    {
        if (grd(*ri) != DNGN_UNDISCOVERED_TRAP)
            continue;

        int dist = ri->range(you.pos());

        // Own square is not excluded; may be flying.
        // XXX: Currently, flying over a trap will always detect it.

        int effective = (dist <= 1) ? skill : skill / (dist * 2 - 1);

        trap_def* ptrap = find_trap(*ri);
        if (!ptrap)
        {
            // Maybe we shouldn't kill the trap for debugging
            // purposes - oh well.
            grd(*ri) = DNGN_FLOOR;
            dprf("You found a buggy trap! It vanishes!");
            continue;
        }

        if (effective > ptrap->skill_rnd)
        {
            ptrap->reveal();
            mprf(jtransc("You found %s!"),
                 jtransc(ptrap->name(DESC_PLAIN)));
            learned_something_new(HINT_SEEN_TRAP, *ri);
        }
    }
}

// Decides whether you will try to tear the net (result <= 0)
// or try to slip out of it (result > 0).
// Both damage and escape could be 9 (more likely for damage)
// but are capped at 5 (damage) and 4 (escape).
static int damage_or_escape_net(int hold)
{
    // Spriggan: little (+2)
    // Halfling, Kobold: small (+1)
    // Human, Elf, ...: medium (0)
    // Ogre, Troll, Centaur, Naga: large (-1)
    // transformations: spider, bat: tiny (+3); ice beast: large (-1)
    int escape = SIZE_MEDIUM - you.body_size(PSIZE_BODY);

    int damage = -escape;

    // your weapon may damage the net, max. bonus of 2
    if (you.weapon())
    {
        if (can_cut_meat(*you.weapon()))
            damage++;

        int brand = get_weapon_brand(*you.weapon());
        if (brand == SPWPN_FLAMING || brand == SPWPN_VORPAL)
            damage++;
    }
    else if (you.form == TRAN_BLADE_HANDS)
        damage += 2;
    else if (you.has_usable_claws())
    {
        int level = you.has_claws();
        if (level == 1)
            damage += coinflip();
        else
            damage += level - 1;
    }

    // Berserkers get a fighting bonus.
    if (you.berserk())
        damage += 2;

    // Check stats.
    if (x_chance_in_y(you.strength(), 18))
        damage++;
    if (x_chance_in_y(you.dex(), 12))
        escape++;
    if (x_chance_in_y(player_evasion(), 20))
        escape++;

    // Dangerous monsters around you add urgency.
    if (there_are_monsters_nearby(true))
    {
        damage++;
        escape++;
    }

    // Confusion makes the whole thing somewhat harder
    // (less so for trying to escape).
    if (you.confused())
    {
        if (escape > 1)
            escape--;
        else if (damage >= 2)
            damage -= 2;
    }

    // Damaged nets are easier to destroy.
    if (hold < 0)
    {
        damage += random2(-hold/3 + 1);

        // ... and easier to slip out of (but only if escape looks feasible).
        if (you.attribute[ATTR_HELD] < 5 || escape >= damage)
            escape += random2(-hold/2) + 1;
    }

    // If undecided, choose damaging approach (it's quicker).
    if (damage >= escape)
        return -damage; // negate value

    return escape;
}

/**
 * Let the player attempt to unstick themself from a web.
 */
static void _free_self_from_web()
{
    // Check if there's actually a web trap in your tile.
    trap_def *trap = find_trap(you.pos());
    if (trap && trap->type == TRAP_WEB)
    {
        // if so, roll a chance to escape the web (based on str).
        if (x_chance_in_y(40 - you.stat(STAT_STR), 66))
        {
            mpr(jtrans("You struggle to detach yourself from the web."));
            // but you actually accomplished nothing!
            return;
        }

        // destroy or escape the web.
        maybe_destroy_web(&you);
    }

    // whether or not there was a web trap there, you're free now.
    you.attribute[ATTR_HELD] = 0;
    you.redraw_quiver = true;
    you.redraw_evasion = true;
}

// Calls the above function to decide on how to get free.
// Note that usually the net will be damaged until trying to slip out
// becomes feasible (for size etc.), so it may take even longer.
void free_self_from_net()
{
    int net = get_trapping_net(you.pos());

    if (net == NON_ITEM)
    {
        // If there's no net, it must be a web.
        _free_self_from_web();
        return;
    }

    int hold = mitm[net].net_durability;
    int do_what = damage_or_escape_net(hold);
    dprf("net.net_durability: %d, ATTR_HELD: %d, do_what: %d",
         hold, you.attribute[ATTR_HELD], do_what);

    if (do_what <= 0) // You try to destroy the net
    {
        // For previously undamaged nets this takes at least 2 and at most
        // 8 turns.
        bool can_slice =
            (you.form == TRAN_BLADE_HANDS)
            || (you.weapon() && can_cut_meat(*you.weapon()));

        int damage = -do_what;

        if (damage < 1)
            damage = 1;

        if (you.berserk())
            damage *= 2;

        // Medium sized characters are at a disadvantage and sometimes
        // get a bonus.
        if (you.body_size(PSIZE_BODY) == SIZE_MEDIUM)
            damage += coinflip();

        if (damage > 5)
            damage = 5;

        hold -= damage;
        mitm[net].net_durability = hold;

        if (hold < -7)
        {
            mprf(jtransc(make_stringf("You %s the net and break free!",
                                      can_slice ? (damage >= 4? "slice" : "cut") :
                                                  (damage >= 4? "shred" : "rip"))));

            destroy_item(net);

            you.attribute[ATTR_HELD] = 0;
            you.redraw_quiver = true;
            you.redraw_evasion = true;
            return;
        }

        if (damage >= 4)
        {
            mpr(jtrans(make_stringf("You %s into the net.",
                                    can_slice? "slice" : "tear a large gash")));
        }
        else
            mpr(jtrans("You struggle against the net."));

        // Occasionally decrease duration a bit
        // (this is so switching from damage to escape does not hurt as much).
        if (you.attribute[ATTR_HELD] > 1 && coinflip())
        {
            you.attribute[ATTR_HELD]--;

            if (you.attribute[ATTR_HELD] > 1 && hold < -random2(5))
                you.attribute[ATTR_HELD]--;
        }
    }
    else
    {
        // You try to escape (takes at least 3 turns, and at most 10).
        int escape = do_what;

        if (you.duration[DUR_HASTE] || you.duration[DUR_BERSERK]) // extra bonus
            escape++;

        // Medium sized characters are at a disadvantage and sometimes
        // get a bonus.
        if (you.body_size(PSIZE_BODY) == SIZE_MEDIUM)
            escape += coinflip();

        if (escape > 4)
            escape = 4;

        if (escape >= you.attribute[ATTR_HELD])
        {
            if (escape >= 3)
                mpr(jtrans("You slip out of the net!"));
            else
                mpr(jtrans("You break free from the net!"));

            you.attribute[ATTR_HELD] = 0;
            you.redraw_quiver = true;
            you.redraw_evasion = true;
            free_stationary_net(net);
            return;
        }

        if (escape >= 3)
            mpr(jtrans("You try to slip out of the net."));
        else
            mpr(jtrans("You struggle to escape the net."));

        you.attribute[ATTR_HELD] -= escape;
    }
}

void mons_clear_trapping_net(monster* mon)
{
    if (!mon->caught())
        return;

    const int net = get_trapping_net(mon->pos());
    if (net != NON_ITEM)
        free_stationary_net(net);

    mon->del_ench(ENCH_HELD, true);
}

void free_stationary_net(int item_index)
{
    item_def &item = mitm[item_index];
    if (item.is_type(OBJ_MISSILES, MI_THROWING_NET))
    {
        const coord_def pos = item.pos;
        // Probabilistically mulch net based on damage done, otherwise
        // reset damage counter (ie: item.net_durability).
        if (x_chance_in_y(-item.net_durability, 9))
            destroy_item(item_index);
        else
        {
            item.net_durability = 0;
            item.net_placed = false;
        }

        // Make sure we don't leave a bad trapping net in the stash
        // FIXME: may leak info if a monster escapes an out-of-sight net.
        StashTrack.update_stash(pos);
        StashTrack.unmark_trapping_nets(pos);
    }
}

void clear_trapping_net()
{
    if (!you.attribute[ATTR_HELD])
        return;

    if (!in_bounds(you.pos()))
        return;

    const int net = get_trapping_net(you.pos());
    if (net != NON_ITEM)
        free_stationary_net(net);

    you.attribute[ATTR_HELD] = 0;
    you.redraw_quiver = true;
    you.redraw_evasion = true;
}

item_def trap_def::generate_trap_item()
{
    item_def item;
    object_class_type base;
    int sub;

    switch (type)
    {
#if TAG_MAJOR_VERSION == 34
    case TRAP_DART:   base = OBJ_MISSILES; sub = MI_DART;         break;
#endif
    case TRAP_ARROW:  base = OBJ_MISSILES; sub = MI_ARROW;        break;
    case TRAP_BOLT:   base = OBJ_MISSILES; sub = MI_BOLT;         break;
    case TRAP_SPEAR:  base = OBJ_WEAPONS;  sub = WPN_SPEAR;       break;
    case TRAP_NEEDLE: base = OBJ_MISSILES; sub = MI_NEEDLE;       break;
    case TRAP_NET:    base = OBJ_MISSILES; sub = MI_THROWING_NET; break;
    default:          return item;
    }

    item.base_type = base;
    item.sub_type  = sub;
    item.quantity  = 1;

    if (base == OBJ_MISSILES)
    {
        set_item_ego_type(item, base,
                          (sub == MI_NEEDLE) ? SPMSL_POISONED : SPMSL_NORMAL);
    }
    else
        set_item_ego_type(item, base, SPWPN_NORMAL);

    item_colour(item);
    return item;
}

// Shoot a single piece of ammo at the relevant actor.
void trap_def::shoot_ammo(actor& act, bool was_known)
{
    if (ammo_qty <= 0)
    {
        if (was_known && act.is_player())
            mpr(jtrans("The trap is out of ammunition!"));
        else if (player_can_hear(pos) && you.see_cell(pos))
            mpr(jtrans("You hear a soft click."));

        disarm();
        return;
    }

    bool force_hit = (env.markers.property_at(pos, MAT_ANY,
                            "force_hit") == "true");

    if (act.is_player())
    {
        if (!force_hit && (one_chance_in(5) || was_known && !one_chance_in(4)))
        {
            mprf(jtransc("You avoid triggering %s."), jtransc(name(DESC_PLAIN)));
            return;         // no ammo generated either
        }
    }
    else if (!force_hit && one_chance_in(5))
    {
        if (was_known && you.see_cell(pos) && you.can_see(&act))
        {
            mprf(jtransc("%s avoids triggering %s."), jtransc(act.name(DESC_PLAIN)),
                 jtransc(name(DESC_A)));
        }
        return;
    }

    item_def shot = generate_trap_item();

    int trap_hit = (20 + (difficulty()*2)) * random2(200) / 100;
    if (int defl = act.missile_deflection())
        trap_hit = random2(trap_hit / defl);

    const int con_block = random2(20 + act.shield_block_penalty());
    const int pro_block = act.shield_bonus();
    dprf("%s: hit %d EV %d, shield hit %d block %d", name(DESC_PLAIN).c_str(),
         trap_hit, act.melee_evasion(0), con_block, pro_block);

    // Determine whether projectile hits.
    if (!force_hit && trap_hit < act.melee_evasion(nullptr))
    {
        if (act.is_player())
        {
            mprf(jtransc("%s shoots out and misses you."), jtransc(shot.name(DESC_A)));
            practise(EX_DODGE_TRAP);
        }
        else if (you.see_cell(act.pos()))
        {
            mprf(jtransc("%s misses %s!"), jtransc(shot.name(DESC_A)),
                 jtransc(act.name(DESC_THE)));
        }
    }
    else if (!force_hit
             && pro_block >= con_block
             && you.see_cell(act.pos()))
    {
        string owner;
        if (act.is_player())
            owner = jtrans("your");
        else if (you.can_see(&act))
            owner = jtrans(act.name(DESC_THE)) + "の";
        else // "its" sounds abysmal; animals don't use shields
            owner = jtrans("someone's");
        mprf(jtransc("%s shoots out and hits %s shield."), jtransc(shot.name(DESC_A)),
             owner.c_str());

        act.shield_block_succeeded(0);
    }
    else // OK, we've been hit.
    {
        bool force_poison = (env.markers.property_at(pos, MAT_ANY,
                                "poisoned_needle_trap") == "true");

        bool poison = (type == TRAP_NEEDLE
                       && (x_chance_in_y(50 - (3*act.armour_class()) / 2, 100)
                            || force_poison));

        int damage_taken = act.apply_ac(shot_damage(act));

        if (act.is_player())
        {
            mprf(jtransc("%s shoots out and hits you!"), jtransc(shot.name(DESC_A)));

            string n = name(DESC_PLAIN);

            // Needle traps can poison.
            if (poison)
                poison_player(1 + roll_dice(2, 9), "", n);

            ouch(damage_taken, KILLED_BY_TRAP, MID_NOBODY, n.c_str());
        }
        else
        {
            if (you.see_cell(act.pos()))
            {
                mprf(jtransc("%s hits %s%s!"),
                     jtransc(shot.name(DESC_A)),
                     jtransc(act.name(DESC_THE)),
                     jtransc((damage_taken == 0 && !poison) ?
                             ", but does no damage" : ""));
            }

            if (poison)
                act.poison(nullptr, 3 + roll_dice(2, 5));
            act.hurt(nullptr, damage_taken);
        }
    }
    ammo_qty--;
}

// returns appropriate trap symbol
dungeon_feature_type trap_def::category() const
{
    return trap_category(type);
}

dungeon_feature_type trap_category(trap_type type)
{
    switch (type)
    {
    case TRAP_WEB:
        return DNGN_TRAP_WEB;
    case TRAP_SHAFT:
        return DNGN_TRAP_SHAFT;

    case TRAP_TELEPORT:
    case TRAP_TELEPORT_PERMANENT:
        return DNGN_TRAP_TELEPORT;
    case TRAP_ALARM:
        return DNGN_TRAP_ALARM;
    case TRAP_ZOT:
        return DNGN_TRAP_ZOT;
    case TRAP_GOLUBRIA:
        return DNGN_PASSAGE_OF_GOLUBRIA;
    case TRAP_SHADOW:
        return DNGN_TRAP_SHADOW;
    case TRAP_SHADOW_DORMANT:
        return DNGN_TRAP_SHADOW_DORMANT;

    case TRAP_ARROW:
    case TRAP_SPEAR:
    case TRAP_BLADE:
    case TRAP_BOLT:
    case TRAP_NEEDLE:
    case TRAP_NET:
#if TAG_MAJOR_VERSION == 34
    case TRAP_GAS:
    case TRAP_DART:
#endif
    case TRAP_PLATE:
        return DNGN_TRAP_MECHANICAL;

    default:
        die("placeholder trap type %d used", type);
    }
}

bool is_valid_shaft_level(bool known)
{
    const level_id place = level_id::current();
    if (crawl_state.test
        || crawl_state.game_is_sprint()
        || crawl_state.game_is_zotdef())
    {
        return false;
    }

    if (!is_connected_branch(place))
        return false;

    // Don't generate shafts in branches where teleport control
    // is prevented.  Prevents player from going down levels without
    // reaching stairs, and also keeps player from getting stuck
    // on lower levels with the innability to use teleport control to
    // get back up.
    if (testbits(env.level_flags, LFLAG_NO_TELE_CONTROL))
        return false;

    const Branch &branch = branches[place.branch];

    // When generating levels, don't place an unknown shaft on the level
    // immediately above the bottom of a branch if that branch is
    // significantly more dangerous than normal.
    int min_delta = 1;
    if (!known && env.turns_on_level == -1
        && branch.branch_flags & BFLAG_DANGEROUS_END)
    {
        min_delta = 2;
    }

    return (brdepth[place.branch] - place.depth) >= min_delta;
}

static level_id _generic_shaft_dest(level_pos lpos, bool known = false)
{
    level_id  lid   = lpos.id;

    if (!is_connected_branch(lid))
        return lid;

    int curr_depth = lid.depth;
    int max_depth = brdepth[lid.branch];

    // Shaft traps' behavior depends on whether it is entered intentionally.
    // Knowingly entering one is more likely to drop you 1 level.
    // Falling in unknowingly can drop you 1/2/3 levels with equal chance.

    if (known)
    {
        // Chances are 5/8s for 1 level, 2/8s for 2 levels, 1/8 for 3 levels
        int s = random2(8) + 1;
        if (s == 1)
            lid.depth += 3;
        else if (s <= 3)
            lid.depth += 2;
        else
            lid.depth += 1;
    }
    else
    {
        // 33.3% for 1, 2, 3 from D:3, less before
        lid.depth += 1 + random2(min(lid.depth, 3));
    }

    if (lid.depth > max_depth)
        lid.depth = max_depth;

    if (lid.depth == curr_depth)
        return lid;

    // Only shafts on the level immediately above a dangerous branch
    // bottom will take you to that dangerous bottom, and shafts can't
    // be created during level generation time.
    if (branches[lid.branch].branch_flags & BFLAG_DANGEROUS_END
        && lid.depth == max_depth
        && (max_depth - curr_depth) > 1)
    {
        lid.depth--;
    }

    return lid;
}

level_id generic_shaft_dest(coord_def pos, bool known = false)
{
    return _generic_shaft_dest(level_pos(level_id::current(), pos));
}

/**
 * When a player falls through a shaft at a location, disperse items on the
 * target level.
 *
 * @param pos The location.
 * @param open_shaft If True and the location was seen, print a shaft opening
 *                   message, otherwise don't.
*/
void handle_items_on_shaft(const coord_def& pos, bool open_shaft)
{
    if (!is_valid_shaft_level())
        return;

    level_id dest = generic_shaft_dest(pos);

    if (dest == level_id::current())
        return;

    int o = igrd(pos);

    if (o == NON_ITEM)
        return;

    bool need_open_message = env.map_knowledge(pos).seen() && open_shaft;

    while (o != NON_ITEM)
    {
        int next = mitm[o].link;

        if (mitm[o].defined() && !item_is_stationary_net(mitm[o]))
        {
            if (need_open_message)
            {
                mpr(jtrans("A shaft opens up in the floor!"));
                grd(pos) = DNGN_TRAP_SHAFT;
                need_open_message = false;
            }

            if (env.map_knowledge(pos).visible())
            {
                mprf(jtransc("%s fall%s through the shaft."),
                     jtransc(mitm[o].name(DESC_INVENTORY)),
                     mitm[o].quantity == 1 ? "s" : "");

                env.map_knowledge(pos).clear_item();
                StashTrack.update_stash(pos);
            }

            // Item will be randomly placed on the destination level.
            unlink_item(o);
            mitm[o].pos = INVALID_COORD;
            add_item_to_transit(dest, mitm[o]);

            mitm[o].base_type = OBJ_UNASSIGNED;
            mitm[o].quantity = 0;
            mitm[o].props.clear();
        }

        o = next;
    }
}

/**
 * Get a number of traps to place on the current level.
 *
 * No traps are placed in either Temple or disconnected branches other than
 * Pandemonium. For other branches, we place 0-2 traps per level, averaged over
 * two dice. This value is increased for deeper levels; roughly one additional
 * trap for every 10 levels of absdepth, capping out at max 9 traps in a level.
 *
 * @return  A number of traps to be placed.
*/
int num_traps_for_place()
{
    if (player_in_branch(BRANCH_TEMPLE)
        || (!player_in_connected_branch()
            && !player_in_branch(BRANCH_PANDEMONIUM)))
    {
        return 0;
    }

    const int depth_bonus = div_rand_round(env.absdepth0, 5);
    return random2avg(3 + depth_bonus, 2);
}

/**
 * Choose a weighted random trap type for the currently-generated level.
 *
 * Odds of generating zot traps vary by depth (and are depth-limited). Alarm
 * traps also can't be placed before D:4. All other traps are depth-agnostic.
 *
 * @return                    A random trap type.
 *                            May be NUM_TRAPS, if no traps were valid.
 */

trap_type random_trap_for_place()
{
    // zot traps are Very Special.
    // very common in zot...
    if (player_in_branch(BRANCH_ZOT) && coinflip())
        return TRAP_ZOT;

    // and elsewhere, increasingly common with depth
    // possible starting at depth 15 (end of D, late lair, lair branches)
    // XXX: is there a better way to express this?
    if (random2(1 + env.absdepth0) > 14 && one_chance_in(3))
        return TRAP_ZOT;

    const bool shaft_ok = is_valid_shaft_level();
    const bool tele_ok = !crawl_state.game_is_sprint();
    const bool alarm_ok = env.absdepth0 > 3;
    const bool shadow_ok = env.absdepth0 > 1;

    const pair<trap_type, int> trap_weights[] =
    {
        { TRAP_TELEPORT, tele_ok  ? 2 : 0},
        { TRAP_SHADOW,  shadow_ok ? 1 : 0 },
        { TRAP_SHAFT,   shaft_ok  ? 1 : 0},
        { TRAP_ALARM,   alarm_ok  ? 1 : 0},
    };

    const trap_type *trap = random_choose_weighted(trap_weights);
    return trap ? *trap : NUM_TRAPS;
}

/**
 * Oldstyle trap algorithm, used for vaults. Very bad. Please remove ASAP.
 */
trap_type random_vault_trap()
{
    const int level_number = env.absdepth0;
    trap_type type = TRAP_ARROW;

    if ((random2(1 + level_number) > 1) && one_chance_in(4))
        type = TRAP_NEEDLE;
    if (random2(1 + level_number) > 3)
        type = TRAP_SPEAR;

    if (type == TRAP_ARROW && one_chance_in(15))
        type = TRAP_NET;

    if (random2(1 + level_number) > 7)
        type = TRAP_BOLT;
    if (random2(1 + level_number) > 14)
        type = TRAP_BLADE;

    if (random2(1 + level_number) > 14 && one_chance_in(3)
        || (player_in_branch(BRANCH_ZOT) && coinflip()))
    {
        type = TRAP_ZOT;
    }

    if (one_chance_in(20) && is_valid_shaft_level())
        type = TRAP_SHAFT;
    if (one_chance_in(20) && !crawl_state.game_is_sprint())
        type = TRAP_TELEPORT;
    if (one_chance_in(40) && level_number > 3)
        type = TRAP_ALARM;

    return type;
}

int count_traps(trap_type ttyp)
{
    int num = 0;
    for (int tcount = 0; tcount < MAX_TRAPS; tcount++)
        if (env.trap[tcount].type == ttyp)
            num++;
    return num;
}

void place_webs(int num)
{
    int slot = 0;
    for (int j = 0; j < num; j++)
    {
        for (;; slot++)
        {
            if (slot >= MAX_TRAPS)
                return;
            if (env.trap[slot].type == TRAP_UNASSIGNED)
                break;
        }
        trap_def& ts(env.trap[slot]);

        int tries;
        // this is hardly ever enough to place many webs, most of the time
        // it will fail prematurely.  Which is fine.
        for (tries = 0; tries < 200; ++tries)
        {
            ts.pos.x = random2(GXM);
            ts.pos.y = random2(GYM);
            if (in_bounds(ts.pos)
                && grd(ts.pos) == DNGN_FLOOR
                && !map_masked(ts.pos, MMT_NO_TRAP))
            {
                // Calculate weight.
                int weight = 0;
                for (adjacent_iterator ai(ts.pos); ai; ++ai)
                {
                    // Solid wall?
                    int solid_weight = 0;
                    // Orthogonals weight three, diagonals 1.
                    if (cell_is_solid(*ai))
                    {
                        solid_weight = (ai->x == ts.pos.x || ai->y == ts.pos.y)
                                        ? 3 : 1;
                    }
                    weight += solid_weight;
                }

                // Maximum weight is 4*3+4*1 = 16
                // *But* that would imply completely surrounded by rock (no point there)
                if (weight <= 16 && x_chance_in_y(weight + 2, 34))
                    break;
            }
        }

        if (tries >= 200)
            break;

        ts.type = TRAP_WEB;
        grd(ts.pos) = DNGN_UNDISCOVERED_TRAP;
        env.tgrid(ts.pos) = slot;
        ts.prepare_ammo();
        // Reveal some webs
        if (coinflip())
            ts.reveal();
    }
}

bool maybe_destroy_web(actor *oaf)
{
    trap_def *trap = find_trap(oaf->pos());
    if (!trap || trap->type != TRAP_WEB)
        return false;

    if (coinflip())
    {
        if (oaf->is_monster())
            simple_monster_message(oaf->as_monster(), jtransc(" pulls away from the web."));
        else
            mpr(jtrans("You disentangle yourself."));
        return false;
    }

    if (oaf->is_monster())
        simple_monster_message(oaf->as_monster(), jtransc(" tears the web."));
    else
        mpr(jtrans("The web tears apart."));
    destroy_trap(oaf->pos());
    return true;
}

bool ensnare(actor *fly)
{
    if (fly->is_web_immune())
        return false;

    if (fly->caught())
    {
        // currently webs are stateless so except for flavour it's a no-op
        if (fly->is_player())
            mpr(jtrans("You are even more entangled."));
        return false;
    }

    if (fly->body_size() >= SIZE_GIANT)
    {
        if (you.can_see(fly))
            mprf(jtransc("A web harmlessly splats on %s."), jtransc(fly->name(DESC_THE)));
        return false;
    }

    // If we're over water, an open door, shop, portal, etc, the web will
    // fail to attach and you'll be released after a single turn.
    // Same if we're at max traps already.
    if (grd(fly->pos()) == DNGN_FLOOR
        && place_specific_trap(fly->pos(), TRAP_WEB)
        // succeeded, mark the web known discovered
        && grd(fly->pos()) == DNGN_UNDISCOVERED_TRAP
        && you.see_cell(fly->pos()))
    {
        grd(fly->pos()) = DNGN_TRAP_WEB;
    }

    if (fly->is_player())
    {
        if (_player_caught_in_web()) // no fail, returns false if already held
            mpr(jtrans("You are caught in a web!"));
    }
    else
    {
        simple_monster_message(fly->as_monster(), jtransc(" is caught in a web!"));
        fly->as_monster()->add_ench(ENCH_HELD);
    }

    // Drowned?
    if (!fly->alive())
        return true;

    check_monsters_sense(SENSE_WEB_VIBRATION, 100, fly->pos());
    return true;
}

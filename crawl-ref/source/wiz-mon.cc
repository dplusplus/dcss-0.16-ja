/**
 * @file
 * @brief Monster related debugging functions.
**/

#include "AppHdr.h"

#include "wiz-mon.h"

#include <sstream>

#include "abyss.h"
#include "act-iter.h"
#include "areas.h"
#include "colour.h"
#include "dbg-util.h"
#include "delay.h"
#include "directn.h"
#include "dungeon.h"
#include "english.h"
#include "files.h"
#include "ghost.h"
#include "godblessing.h"
#include "invent.h"
#include "itemprop.h"
#include "items.h"
#include "jobs.h"
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "mon-death.h"
#include "mon-pathfind.h"
#include "mon-place.h"
#include "mon-poly.h"
#include "mon-speak.h"
#include "output.h"
#include "prompt.h"
#include "religion.h"
#include "shout.h"
#include "spl-miscast.h"
#include "state.h"
#include "stringutil.h"
#include "unwind.h"
#include "view.h"
#include "viewmap.h"

#ifdef WIZARD
// Creates a specific monster by mon type number.
void wizard_create_spec_monster()
{
    int mon = prompt_for_int(jtrans("Which monster by number? ") + " ", true);

    if (mon == -1 || (mon >= NUM_MONSTERS
                      && mon != RANDOM_MONSTER
                      && mon != RANDOM_DRACONIAN
                      && mon != RANDOM_BASE_DRACONIAN
                      && mon != RANDOM_NONBASE_DRACONIAN
                      && mon != WANDERING_MONSTER))
    {
        canned_msg(MSG_OK);
    }
    else
    {
        create_monster(
            mgen_data::sleeper_at(
                static_cast<monster_type>(mon), you.pos()));
    }
}

// Creates a specific monster by name. Uses the same patterns as
// map definitions.
void wizard_create_spec_monster_name()
{
    char specs[1024];
    mpr_nojoin(MSGCH_PROMPT, jtrans("Enter monster name (or MONS spec): ") + " ");
    if (cancellable_get_line_autohist(specs, sizeof specs) || !*specs)
    {
        canned_msg(MSG_OK);
        return;
    }

    mons_list mlist;
    string err = mlist.add_mons(specs);

    if (!err.empty())
    {
        string newerr = "yes";
        // Try for a partial match, but not if the user accidentally entered
        // only a few letters.
        monster_type partial = get_monster_by_name(specs, true);
        if (strlen(specs) >= 3 && partial != MONS_PROGRAM_BUG)
        {
            mlist.clear();
            newerr = mlist.add_mons(mons_type_name(partial, DESC_PLAIN));
        }

        if (!newerr.empty())
        {
            mpr(err);
            return;
        }
    }

    mons_spec mspec = mlist.get_monster(0);
    if (mspec.type == MONS_NO_MONSTER)
    {
        mpr_nojoin(MSGCH_DIAGNOSTICS, jtrans("Such a monster couldn't be found."));
        return;
    }

    monster_type type =
        fixup_zombie_type(static_cast<monster_type>(mspec.type),
                          mspec.monbase);

    coord_def place = find_newmons_square(type, you.pos());
    if (!in_bounds(place))
    {
        // Try again with habitat HT_LAND.
        // (Will be changed to the necessary terrain type in dgn_place_monster.)
        place = find_newmons_square(MONS_NO_MONSTER, you.pos());
    }

    if (!in_bounds(place))
    {
        mpr_nojoin(MSGCH_DIAGNOSTICS, jtrans("Found no space to place monster."));
        return;
    }

    // Wizmode users should be able to conjure up uniques even if they
    // were already created. Yay, you can meet 3 Sigmunds at once! :p
    if (mons_is_unique(type) && you.unique_creatures[type])
        you.unique_creatures.set(type, false);

    if (!dgn_place_monster(mspec, place, true, false))
    {
        mpr_nojoin(MSGCH_DIAGNOSTICS, jtrans("Unable to place monster."));
        return;
    }

    if (mspec.type == MONS_KRAKEN)
    {
        unsigned short idx = mgrd(place);

        if (idx >= MAX_MONSTERS || menv[idx].type != MONS_KRAKEN)
        {
            for (idx = 0; idx < MAX_MONSTERS; idx++)
            {
                if (menv[idx].type == MONS_KRAKEN && menv[idx].alive())
                {
                    menv[idx].colour = element_colour(ETC_KRAKEN);
                    return;
                }
            }
        }
        if (idx >= MAX_MONSTERS)
        {
            mpr(jtrans("Couldn't find player kraken!"));
            return;
        }
    }

    // FIXME: This is a bit useless, seeing how you cannot set the
    // ghost's stats, brand or level, among other things.
    if (mspec.type == MONS_PLAYER_GHOST)
    {
        unsigned short idx = mgrd(place);

        if (idx >= MAX_MONSTERS || menv[idx].type != MONS_PLAYER_GHOST)
        {
            for (idx = 0; idx < MAX_MONSTERS; idx++)
            {
                if (menv[idx].type == MONS_PLAYER_GHOST
                    && menv[idx].alive())
                {
                    break;
                }
            }
        }

        if (idx >= MAX_MONSTERS)
        {
            mpr(jtrans("Couldn't find player ghost, probably going to crash."));
            more();
            return;
        }

        monster    &mon = menv[idx];
        ghost_demon ghost;

        ghost.name = "John Doe";

        char input_str[80];
        msgwin_get_line(jtrans("Make player ghost which species? (case-sensitive) ") + " ",
                        input_str, sizeof(input_str));

        species_type sp_id = get_species_by_abbrev(input_str);
        if (sp_id == SP_UNKNOWN)
            sp_id = str_to_species(input_str);
        if (sp_id == SP_UNKNOWN)
        {
            mpr(jtrans("No such species, making it Human."));
            sp_id = SP_HUMAN;
        }
        ghost.species = static_cast<species_type>(sp_id);

        msgwin_get_line(jtrans("Give player ghost which background? ") + " ",
                        input_str, sizeof(input_str));

        int job_id = get_job_by_abbrev(input_str);

        if (job_id == JOB_UNKNOWN)
            job_id = get_job_by_name(input_str);

        if (job_id == JOB_UNKNOWN)
        {
            mpr(jtrans("No such background, making it a Fighter."));
            job_id = JOB_FIGHTER;
        }
        ghost.job = static_cast<job_type>(job_id);
        ghost.xl = 7;

        mon.set_ghost(ghost);

        ghosts.push_back(ghost);
    }
}

static bool _sort_monster_list(int a, int b)
{
    const monster* m1 = &menv[a];
    const monster* m2 = &menv[b];

    if (m1->alive() != m2->alive())
        return m1->alive();
    else if (!m1->alive())
        return a < b;

    if (m1->type == m2->type)
    {
        if (!m1->alive() || !m2->alive())
            return false;

        return m1->name(DESC_PLAIN, true) < m2->name(DESC_PLAIN, true);
    }

    const unsigned glyph1 = mons_char(m1->type);
    const unsigned glyph2 = mons_char(m2->type);
    if (glyph1 != glyph2)
        return glyph1 < glyph2;

    return m1->type < m2->type;
}

void debug_list_monsters()
{
    vector<string> mons;
    int nfound = 0;

    int mon_nums[MAX_MONSTERS];

    for (int i = 0; i < MAX_MONSTERS; ++i)
        mon_nums[i] = i;

    sort(mon_nums, mon_nums + MAX_MONSTERS, _sort_monster_list);

    int total_exp = 0, total_adj_exp = 0, total_nonuniq_exp = 0;

    string prev_name = "";
    int    count     = 0;

    for (int i = 0; i < MAX_MONSTERS; ++i)
    {
        const int idx = mon_nums[i];
        if (invalid_monster_index(idx))
            continue;

        const monster* mi(&menv[idx]);
        if (!mi->alive())
            continue;

        string name = mi->name(DESC_PLAIN, true);

        if (prev_name != name && count > 0)
        {
            char buf[80];
            if (count > 1)
            {
                snprintf(buf, sizeof(buf), "%sx%d",
                         prev_name.c_str(), count);
            }
            else
                snprintf(buf, sizeof(buf), "%s", prev_name.c_str());
            mons.push_back(buf);

            count = 0;
        }
        nfound++;
        count++;
        prev_name = name;

        int exp = exper_value(mi);
        total_exp += exp;
        if (!mons_is_unique(mi->type))
            total_nonuniq_exp += exp;

        if ((mi->flags & (MF_WAS_NEUTRAL | MF_NO_REWARD))
            || mi->has_ench(ENCH_ABJ))
        {
            continue;
        }
        if (mi->flags & MF_GOT_HALF_XP)
            exp /= 2;

        total_adj_exp += exp;
    }

    char buf[80];
    if (count > 1)
        snprintf(buf, sizeof(buf), "%sx%d",
                 prev_name.c_str(), count);
    else
        snprintf(buf, sizeof(buf), "%s", prev_name.c_str());
    mons.emplace_back(buf);

    if (nfound)
        mpr_comma_separated_list((jtrans("Monsters: ") + " "), mons, ", ", ", ",
                                 MSGCH_PLAIN, 0, ".");
    else
        mpr(jtrans("Monsters:"));

    if (total_adj_exp == total_exp)
    {
        mprf(jtransc("%d monsters, %d total exp value (%d non-uniq)"),
             nfound, total_exp, total_nonuniq_exp);
    }
    else
    {
        mprf(jtransc("%d monsters, %d total exp value (%d non-uniq, %d adjusted)"),
             nfound, total_exp, total_nonuniq_exp, total_adj_exp);
    }
}

void wizard_spawn_control()
{
    mpr_nojoin(MSGCH_PROMPT, jtrans("(c)hange spawn rate or (s)pawn monsters? ") + " ");
    const int c = toalower(getchm());

    char specs[256];
    bool done = false;

    if (c == 'c')
    {
        mprf(MSGCH_PROMPT, (jtrans("Set monster spawn rate to what? (now %d, lower value = higher rate) ") + " ").c_str(),
             env.spawn_random_rate);

        if (!cancellable_get_line(specs, sizeof(specs)))
        {
            const int rate = atoi(specs);
            if (rate || specs[0] == '0')
            {
                env.spawn_random_rate = rate;
                done = true;
            }
        }
    }
    else if (c == 's')
    {
        // 50 spots are reserved for non-wandering monsters.
        int max_spawn = MAX_MONSTERS - 50;
        for (monster_iterator mi; mi; ++mi)
            if (mi->alive())
                max_spawn--;

        if (max_spawn <= 0)
        {
            mpr_nojoin(MSGCH_PROMPT, jtrans("Level already filled with monsters, "
                                            "get rid of some of them first."));
            return;
        }

        mprf(MSGCH_PROMPT, (jtrans("Spawn how many random monsters (max %d)? ") + " ").c_str(),
             max_spawn);

        if (!cancellable_get_line(specs, sizeof(specs)))
        {
            const int num = min(atoi(specs), max_spawn);
            if (num > 0)
            {
                int curr_rate = env.spawn_random_rate;
                // Each call to spawn_random_monsters() will spawn one with
                // the rate at 5 or less.
                env.spawn_random_rate = 5;

                for (int i = 0; i < num; ++i)
                    spawn_random_monsters();

                env.spawn_random_rate = curr_rate;
                done = true;
            }
        }
    }

    if (!done)
        canned_msg(MSG_OK);
}

static const char* ht_names[] =
{
    "land",
    "amphibious",
    "water",
    "lava",
    "amphibious_lava",
};

// Prints a number of useful (for debugging, that is) stats on monsters.
void debug_stethoscope(int mon)
{
    dist stth;
    coord_def stethpos;

    int i;

    if (mon != RANDOM_MONSTER)
        i = mon;
    else
    {
        mpr_nojoin(MSGCH_PROMPT, jtrans("Which monster?"));
        direction(stth, direction_chooser_args());

        if (!stth.isValid)
            return;

        if (stth.isTarget)
            stethpos = stth.target;
        else
            stethpos = you.pos() + stth.delta;

        if (env.cgrid(stethpos) != EMPTY_CLOUD)
        {
            mprf(MSGCH_DIAGNOSTICS, jtransc("cloud type: %d delay: %d"),
                 env.cloud[ env.cgrid(stethpos) ].type,
                 env.cloud[ env.cgrid(stethpos) ].decay);
        }

        if (!monster_at(stethpos))
        {
            mprf(MSGCH_DIAGNOSTICS, jtransc("item grid = %d"), igrd(stethpos));
            return;
        }

        i = mgrd(stethpos);
    }

    monster& mons(menv[i]);

    // Print type of monster.
    mprf(MSGCH_DIAGNOSTICS, jtransc("%s (id #%d; type=%d loc=(%d,%d) align=%s)"),
         mons.name(DESC_THE, true).c_str(),
         i, mons.type, mons.pos().x, mons.pos().y,
         ((mons.attitude == ATT_HOSTILE)        ? "hostile" :
          (mons.attitude == ATT_FRIENDLY)       ? "friendly" :
          (mons.attitude == ATT_NEUTRAL)        ? "neutral" :
          (mons.attitude == ATT_GOOD_NEUTRAL)   ? "good neutral":
          (mons.attitude == ATT_STRICT_NEUTRAL) ? "strictly neutral"
                                                : "unknown alignment"));

    // Print stats and other info.
    mprf(MSGCH_DIAGNOSTICS,
         "HD=%d/%d (%u) HP=%d/%d AC=%d(%d) EV=%d(%d) MR=%d XP=%d SP=%d "
         "energy=%d%s%s mid=%u num=%d stealth=%d flags=%04" PRIx64,
         mons.get_hit_dice(),
         mons.get_experience_level(),
         mons.experience,
         mons.hit_points, mons.max_hit_points,
         mons.base_armour_class(), mons.armour_class(),
         mons.base_evasion(), mons.evasion(),
         mons.res_magic(),
         exper_value(&mons),
         mons.speed, mons.speed_increment,
         mons.base_monster != MONS_NO_MONSTER ? " base=" : "",
         mons.base_monster != MONS_NO_MONSTER ?
         get_monster_data(mons.base_monster)->name : "",
         mons.mid, mons.number, mons.stealth(), mons.flags);

    if (mons.damage_total)
    {
        mprf(MSGCH_DIAGNOSTICS,
             "pdam=%1.1f/%d (%d%%)",
             0.5 * mons.damage_friendly, mons.damage_total,
             50 * mons.damage_friendly / mons.damage_total);
    }

    // Print habitat and behaviour information.
    const habitat_type hab = mons_habitat(&mons);

    COMPILE_CHECK(ARRAYSZ(ht_names) == NUM_HABITATS);
    const actor * const summoner = actor_by_mid(mons.summoner);
    mprf(MSGCH_DIAGNOSTICS,
         "hab=%s beh=%s(%d) foe=%s(%d) mem=%d target=(%d,%d) "
         "firing_pos=(%d,%d) patrol_point=(%d,%d) god=%s%s",
         (hab >= 0 && hab < NUM_HABITATS) ? ht_names[hab] : "INVALID",
         mons.asleep()                    ? "sleep"
         : mons_is_wandering(&mons)       ? "wander"
         : mons_is_seeking(&mons)         ? "seek"
         : mons_is_fleeing(&mons)         ? "flee"
         : mons.behaviour == BEH_RETREAT  ? "retreat"
         : mons_is_cornered(&mons)        ? "cornered"
         : mons_is_lurking(&mons)         ? "lurk"
         : mons.behaviour == BEH_WITHDRAW ? "withdraw"
         :                                  "unknown",
         mons.behaviour,
         mons.foe == MHITYOU                      ? "you"
         : mons.foe == MHITNOT                    ? "none"
         : menv[mons.foe].type == MONS_NO_MONSTER ? "unassigned monster"
         : menv[mons.foe].name(DESC_PLAIN, true).c_str(),
         mons.foe,
         mons.foe_memory,
         mons.target.x, mons.target.y,
         mons.firing_pos.x, mons.firing_pos.y,
         mons.patrol_point.x, mons.patrol_point.y,
         god_name(mons.god).c_str(),
         (summoner ? make_stringf(" summoner=%s(%d)",
                                  summoner->name(DESC_PLAIN, true).c_str(),
                                  summoner->mindex()).c_str()
                   : ""));

    // Print resistances.
    mprf(MSGCH_DIAGNOSTICS, jtransc("resist: fire=%d cold=%d elec=%d pois=%d neg=%d "
                                    "acid=%d sticky=%s rot=%s"),
         mons.res_fire(),
         mons.res_cold(),
         mons.res_elec(),
         mons.res_poison(),
         mons.res_negative_energy(),
         mons.res_acid(),
         mons.res_sticky_flame() ? "yes" : "no",
         mons.res_rotting() ? "yes" : "no");

    mprf(MSGCH_DIAGNOSTICS, jtransc("ench: %s"),
         mons.describe_enchantments().c_str());

    mprf(MSGCH_DIAGNOSTICS, jtransc("props: %s"),
         mons.describe_props().c_str());

    ostringstream spl;
    const monster_spells &hspell_pass = mons.spells;
    bool found_spell = false;
    for (unsigned int k = 0; k < hspell_pass.size(); ++k)
    {
        if (found_spell)
            spl << ", ";

        found_spell = true;

        spl << k << ": ";

        if (hspell_pass[k].spell >= NUM_SPELLS)
            spl << "buggy spell";
        else
            spl << spell_title(hspell_pass[k].spell);

        spl << "." << (int)hspell_pass[k].freq;

        spl << " (" << static_cast<int>(hspell_pass[k].spell) << ")";
    }
    if (found_spell)
        mprf(MSGCH_DIAGNOSTICS, "spells: %s", spl.str().c_str());

    ostringstream inv;
    bool found_item = false;
    for (int k = 0; k < NUM_MONSTER_SLOTS; ++k)
    {
        if (mons.inv[k] != NON_ITEM)
        {
            if (found_item)
                inv << ", ";

            found_item = true;

            inv << k << ": ";

            if (mons.inv[k] >= MAX_ITEMS)
                inv << " buggy item";
            else
                inv << item_base_name(mitm[mons.inv[k]]);

            inv << " (" << static_cast<int>(mons.inv[k]) << ")";
        }
    }
    if (found_item)
        mprf(MSGCH_DIAGNOSTICS, "inv: %s", inv.str().c_str());

    if (mons_is_ghost_demon(mons.type))
    {
        ASSERT(mons.ghost.get());
        const ghost_demon &ghost = *mons.ghost;
        mprf(MSGCH_DIAGNOSTICS, jtransc("Ghost damage: %d; brand: %d; att_type: %d; "
                                        "att_flav: %d"),
             ghost.damage, ghost.brand, ghost.att_type, ghost.att_flav);
    }
}

// Detects all monsters on the level, using their exact positions.
void wizard_detect_creatures()
{
    for (monster_iterator mi; mi; ++mi)
    {
        env.map_knowledge(mi->pos()).set_monster(monster_info(*mi));
        env.map_knowledge(mi->pos()).set_detected_monster(mi->type);
#ifdef USE_TILE
        tiles.update_minimap(mi->pos());
#endif
    }
}

// Dismisses all monsters on the level or all monsters that match a user
// specified regex.
void wizard_dismiss_all_monsters(bool force_all)
{
    char buf[1024] = "";
    if (!force_all)
    {
        mpr_nojoin(MSGCH_PROMPT, jtrans("What monsters to dismiss (ENTER for all, "
                                        "\"harmful\", \"mobile\" or a regex)? ") + " ");
        bool validline = !cancellable_get_line_autohist(buf, sizeof buf);

        if (!validline)
        {
            canned_msg(MSG_OK);
            return;
        }
    }

    dismiss_monsters(buf);
    // If it was turned off turn autopickup back on if all monsters went away.
    if (!*buf)
        autotoggle_autopickup(false);
}

void debug_make_monster_shout(monster* mon)
{
    mpr_nojoin(MSGCH_PROMPT, jtrans("Make the monster (S)hout or (T)alk? ") + " ");

    char type = (char) getchm(KMC_DEFAULT);
    type = toalower(type);

    if (type != 's' && type != 't')
    {
        canned_msg(MSG_OK);
        return;
    }

    int num_times = prompt_for_int(type == 's' ? "何回叫ばせますか？ " : "何回喋らせますか？ ", false);

    if (num_times <= 0)
    {
        canned_msg(MSG_OK);
        return;
    }

    if (type == 's')
    {
        if (silenced(you.pos()))
            mpr(jtrans("You are silenced and likely won't hear any shouts."));
        else if (silenced(mon->pos()))
            mpr(jtrans("The monster is silenced and likely won't give any shouts."));

        for (int i = 0; i < num_times; ++i)
            handle_monster_shouts(mon, true);
    }
    else
    {
        if (mon->invisible())
            mpr(jtrans("The monster is invisible and likely won't speak."));

        if (silenced(you.pos()) && !silenced(mon->pos()))
        {
            mpr(jtrans("You are silenced but the monster isn't; you will "
                       "probably hear/see nothing."));
        }
        else if (!silenced(you.pos()) && silenced(mon->pos()))
            mpr(jtrans("The monster is silenced and likely won't say anything."));
        else if (silenced(you.pos()) && silenced(mon->pos()))
        {
            mpr(jtrans("Both you and the monster are silenced, so you likely "
                       "won't hear anything."));
        }

        for (int i = 0; i< num_times; ++i)
            mons_speaks(mon);
    }

    mpr(jtrans("== Done =="));
}

void wizard_gain_monster_level(monster* mon)
{
    // Give monster as much experience as it can hold,
    // but cap the levels gained to just 1.
    bool worked = mon->gain_exp(INT_MAX - mon->experience, 1);
    if (!worked)
        simple_monster_message(mon, jtransc(" seems unable to mature further."), MSGCH_WARN);

    // (The gain_exp() method will chop the monster's experience down
    // to half-way between its new level and the next, so we needn't
    // worry about it being left with too much experience.)
}

void wizard_apply_monster_blessing(monster* mon)
{
    mpr_nojoin(MSGCH_PROMPT, jtrans("Apply blessing of (B)eogh, The (S)hining One, or (R)andomly? ") + " ");

    char type = (char) getchm(KMC_DEFAULT);
    type = toalower(type);

    if (type != 'b' && type != 's' && type != 'r')
    {
        canned_msg(MSG_OK);
        return;
    }
    god_type god = GOD_NO_GOD;
    if (type == 'b' || type == 'r' && coinflip())
        god = GOD_BEOGH;
    else
        god = GOD_SHINING_ONE;

    if (!bless_follower(mon, god, true))
        mprf(jtransc("%s won't bless this monster for you!"), jtransc(god_name(god)));
}

void wizard_give_monster_item(monster* mon)
{
    mon_itemuse_type item_use = mons_itemuse(mon);
    if (item_use < MONUSE_STARTING_EQUIPMENT)
    {
        mpr(jtrans("That type of monster can't use any items."));
        return;
    }

    int player_slot = prompt_invent_item(jtransc("Give which item to monster?"),
                                          MT_DROP, -1);

    if (prompt_failed(player_slot))
        return;

    for (int i = 0; i < NUM_EQUIP; ++i)
        if (you.equip[i] == player_slot)
        {
            mpr(jtrans("Can't give equipped items to a monster."));
            return;
        }

    item_def     &item = you.inv[player_slot];
    mon_inv_type mon_slot = NUM_MONSTER_SLOTS;

    switch (item.base_type)
    {
    case OBJ_WEAPONS:
    case OBJ_STAVES:
    case OBJ_RODS:
        // Let wizard specify which slot to put weapon into via
        // inscriptions.
        if (item.inscription.find("first") != string::npos
            || item.inscription.find("primary") != string::npos)
        {
            mpr(jtrans("Putting weapon into primary slot by inscription"));
            mon_slot = MSLOT_WEAPON;
            break;
        }
        else if (item.inscription.find("second") != string::npos
                 || item.inscription.find("alt") != string::npos)
        {
            mpr(jtrans("Putting weapon into alt slot by inscription"));
            mon_slot = MSLOT_ALT_WEAPON;
            break;
        }

        // For monsters which can wield two weapons, prefer whichever
        // slot is empty (if there is an empty slot).
        if (mons_wields_two_weapons(mon))
        {
            if (mon->inv[MSLOT_WEAPON] == NON_ITEM)
            {
                mpr(jtrans("Dual wielding monster, putting into empty primary slot"));
                mon_slot = MSLOT_WEAPON;
                break;
            }
            else if (mon->inv[MSLOT_ALT_WEAPON] == NON_ITEM)
            {
                mpr(jtrans("Dual wielding monster, putting into empty alt slot"));
                mon_slot = MSLOT_ALT_WEAPON;
                break;
            }
        }

        // Try to replace a ranged weapon with a ranged weapon and
        // a non-ranged weapon with a non-ranged weapon
        if (mon->inv[MSLOT_WEAPON] != NON_ITEM
            && (is_range_weapon(mitm[mon->inv[MSLOT_WEAPON]])
                == is_range_weapon(item)))
        {
            mpr(jtrans("Replacing primary slot with similar weapon"));
            mon_slot = MSLOT_WEAPON;
            break;
        }
        if (mon->inv[MSLOT_ALT_WEAPON] != NON_ITEM
            && (is_range_weapon(mitm[mon->inv[MSLOT_ALT_WEAPON]])
                == is_range_weapon(item)))
        {
            mpr(jtrans("Replacing alt slot with similar weapon"));
            mon_slot = MSLOT_ALT_WEAPON;
            break;
        }

        // Prefer the empty slot (if any)
        if (mon->inv[MSLOT_WEAPON] == NON_ITEM)
        {
            mpr(jtrans("Putting weapon into empty primary slot"));
            mon_slot = MSLOT_WEAPON;
            break;
        }
        else if (mon->inv[MSLOT_ALT_WEAPON] == NON_ITEM)
        {
            mpr(jtrans("Putting weapon into empty alt slot"));
            mon_slot = MSLOT_ALT_WEAPON;
            break;
        }

        // Default to primary weapon slot
        mpr(jtrans("Defaulting to primary slot"));
        mon_slot = MSLOT_WEAPON;
        break;

    case OBJ_ARMOUR:
    {
        // May only return shield or armour slot.
        equipment_type eq = get_armour_slot(item);

        // Force non-shield, non-body armour to be worn anyway.
        if (eq == EQ_NONE)
            eq = EQ_BODY_ARMOUR;

        mon_slot = equip_slot_to_mslot(eq);
        break;
    }
    case OBJ_MISSILES:
        mon_slot = MSLOT_MISSILE;
        break;
    case OBJ_WANDS:
        mon_slot = MSLOT_WAND;
        break;
    case OBJ_SCROLLS:
        mon_slot = MSLOT_SCROLL;
        break;
    case OBJ_POTIONS:
        mon_slot = MSLOT_POTION;
        break;
    case OBJ_MISCELLANY:
        mon_slot = MSLOT_MISCELLANY;
        break;
    case OBJ_JEWELLERY:
        mon_slot = MSLOT_JEWELLERY;
        break;
    default:
        mpr(jtrans("You can't give that type of item to a monster."));
        return;
    }

    if (item_use == MONUSE_STARTING_EQUIPMENT
        && !mons_is_unique(mon->type))
    {
        switch (mon_slot)
        {
        case MSLOT_WEAPON:
        case MSLOT_ALT_WEAPON:
        case MSLOT_ARMOUR:
        case MSLOT_JEWELLERY:
        case MSLOT_MISSILE:
            break;

        default:
            mpr(jtrans("That type of monster can only use weapons and armour."));
            return;
        }
    }

    int index = get_mitm_slot(10);
    if (index == NON_ITEM)
    {
        mpr(jtrans("Too many items on level, bailing."));
        return;
    }

    // Move monster's old item to player's inventory as last step.
    int  old_eq     = NON_ITEM;
    bool unequipped = false;
    if (mon_slot != NUM_MONSTER_SLOTS
        && mon->inv[mon_slot] != NON_ITEM
        && !items_stack(item, mitm[mon->inv[mon_slot]]))
    {
        old_eq = mon->inv[mon_slot];
        // Alternative weapons don't get (un)wielded unless the monster
        // can wield two weapons.
        if (mon_slot != MSLOT_ALT_WEAPON || mons_wields_two_weapons(mon))
        {
            mon->unequip(*(mon->mslot_item(mon_slot)), mon_slot, 1, true);
            unequipped = true;
        }
        mon->inv[mon_slot] = NON_ITEM;
    }

    mitm[index] = item;

    unwind_var<int> save_speedinc(mon->speed_increment);
    if (!mon->pickup_item(mitm[index], false, true))
    {
        mpr(jtrans("Monster wouldn't take item."));
        if (old_eq != NON_ITEM && mon_slot != NUM_MONSTER_SLOTS)
        {
            mon->inv[mon_slot] = old_eq;
            if (unequipped)
                mon->equip(mitm[old_eq], mon_slot, 1);
        }
        unlink_item(index);
        destroy_item(mitm[index]);
        return;
    }

    // Item is gone from player's inventory.
    dec_inv_item_quantity(player_slot, item.quantity);

    if ((mon->flags & MF_HARD_RESET) && !(item.flags & ISFLAG_SUMMONED))
    {
        mpr_nojoin(MSGCH_WARN, jtrans("WARNING: Monster has MF_HARD_RESET and all its "
             "items will disappear when it does."));
    }
    else if ((item.flags & ISFLAG_SUMMONED) && !mon->is_summoned())
    {
        mpr_nojoin(MSGCH_WARN, jtrans("WARNING: Item is summoned and will disappear when "
             "the monster does."));
    }
    // Monster's old item moves to player's inventory.
    if (old_eq != NON_ITEM)
    {
        mpr(jtrans("Fetching monster's old item."));
        if (mitm[old_eq].flags & ISFLAG_SUMMONED)
        {
            mpr_nojoin(MSGCH_WARN, jtrans("WARNING: Item is summoned and shouldn't really "
                "be anywhere but in the inventory of a summoned monster."));
        }
        mitm[old_eq].pos.reset();
        mitm[old_eq].link = NON_ITEM;
        move_item_to_inv(old_eq, mitm[old_eq].quantity);
    }
}

static void _move_player(const coord_def& where)
{
    if (!you.can_pass_through_feat(grd(where)))
        grd(where) = DNGN_FLOOR;
    move_player_to_grid(where, false);
    // If necessary, update the Abyss.
    if (player_in_branch(BRANCH_ABYSS))
        maybe_shift_abyss_around_player();
}

static void _move_monster(const coord_def& where, int idx1)
{
    dist moves;
    direction_chooser_args args;
    args.needs_path = false;
    args.top_prompt = jtrans("Move monster to where?");
    direction(moves, args);

    if (!moves.isValid || !in_bounds(moves.target))
        return;

    monster* mon1 = &menv[idx1];

    const int idx2 = mgrd(moves.target);
    monster* mon2 = monster_at(moves.target);

    mon1->moveto(moves.target);
    mgrd(moves.target) = idx1;
    mon1->check_redraw(moves.target);

    mgrd(where) = idx2;

    if (mon2 != nullptr)
    {
        mon2->moveto(where);
        mon1->check_redraw(where);
    }
}

void wizard_move_player_or_monster(const coord_def& where)
{
    crawl_state.cancel_cmd_again();
    crawl_state.cancel_cmd_repeat();

    static bool already_moving = false;

    if (already_moving)
    {
        mpr(jtrans("Already doing a move command."));
        return;
    }

    already_moving = true;

    int idx = mgrd(where);

    if (idx == NON_MONSTER)
    {
        if (crawl_state.arena_suspended)
        {
            mpr(jtrans("You can't move yourself into the arena."));
            more();
            return;
        }
        _move_player(where);
    }
    else
        _move_monster(where, idx);

    already_moving = false;
}

void wizard_make_monster_summoned(monster* mon)
{
    int summon_type = 0;
    if (mon->is_summoned(nullptr, &summon_type) || summon_type != 0)
    {
        mpr_nojoin(MSGCH_PROMPT, jtrans("Monster is already summoned."));
        return;
    }

    int dur = prompt_for_int(jtrans("What summon longevity (1 to 6)? ") + " ", true);

    if (dur < 1 || dur > 6)
    {
        canned_msg(MSG_OK);
        return;
    }

    mpr_nojoin(MSGCH_PROMPT, jtrans("[a] clone [b] animated [c] chaos [d] miscast [e] zot"));
    mpr_nojoin(MSGCH_PROMPT, jtrans("[f] wrath [g] lantern  [h] aid   [m] misc    [s] spell"));

    mpr_nojoin(MSGCH_PROMPT, jtrans("Which summon type? ") + " ");

    char choice = toalower(getchm());

    if (!(choice >= 'a' && choice <= 'h') && choice != 'm' && choice != 's')
    {
        canned_msg(MSG_OK);
        return;
    }

    int type = 0;

    switch (choice)
    {
        case 'a': type = MON_SUMM_CLONE; break;
        case 'b': type = MON_SUMM_ANIMATE; break;
        case 'c': type = MON_SUMM_CHAOS; break;
        case 'd': type = MON_SUMM_MISCAST; break;
        case 'e': type = MON_SUMM_ZOT; break;
        case 'f': type = MON_SUMM_WRATH; break;
        case 'g': type = MON_SUMM_LANTERN; break;
        case 'h': type = MON_SUMM_AID; break;
        case 'm': type = 0; break;

        case 's':
        {
            char specs[80];

            msgwin_get_line(jtrans("Cast which spell by name? ") + " ",
                            specs, sizeof(specs));

            if (specs[0] == '\0')
            {
                canned_msg(MSG_OK);
                return;
            }

            spell_type spell = spell_by_name(specs, true);
            if (spell == SPELL_NO_SPELL)
            {
                mpr_nojoin(MSGCH_PROMPT, jtrans("No such spell."));
                return;
            }
            type = (int) spell;
            break;
        }

        default:
            die("Invalid summon type choice.");
            break;
    }

    mon->mark_summoned(dur, true, type);
    mpr(jtrans("Monster is now summoned."));
}

void wizard_polymorph_monster(monster* mon)
{
    monster_type old_type =  mon->type;
    monster_type type     = debug_prompt_for_monster();

    if (type == NUM_MONSTERS)
    {
        canned_msg(MSG_OK);
        return;
    }

    if (invalid_monster_type(type))
    {
        mpr_nojoin(MSGCH_PROMPT, jtrans("Invalid monster type."));
        return;
    }

    if (type == old_type)
    {
        mpr(jtrans("Old type and new type are the same, not polymorphing."));
        return;
    }

    if (mons_species(type) == mons_species(old_type))
    {
        mpr(jtrans("Target species must be different from current species."));
        return;
    }

    monster_polymorph(mon, type, PPT_SAME, true);

    if (!mon->alive())
    {
        mpr_nojoin(MSGCH_ERROR, jtrans("Polymorph killed monster?"));
        return;
    }

    mon->check_redraw(mon->pos());

    if (mon->type == old_type)
    {
        mpr(jtrans("Trying harder"));
        change_monster_type(mon, type);
        if (!mon->alive())
        {
            mpr_nojoin(MSGCH_ERROR, jtrans("Polymorph killed monster?"));
            return;
        }

        mon->check_redraw(mon->pos());
    }

    if (mon->type == old_type)
        mpr(jtrans("Polymorph failed."));
    else if (mon->type != type)
        mpr(jtrans("Monster turned into something other than the desired type."));
}

void debug_pathfind(int idx)
{
    if (idx == NON_MONSTER)
        return;

    mpr(jtrans("Choose a destination!"));
#ifndef USE_TILE_LOCAL
    more();
#endif
    coord_def dest;
    level_pos ldest;
    bool chose = show_map(ldest, false, true, false);
    dest = ldest.pos;
    redraw_screen();
    if (!chose)
    {
        canned_msg(MSG_OK);
        return;
    }

    monster& mon = menv[idx];
    mprf(jtransc("Attempting to calculate a path from (%d, %d) to (%d, %d)..."),
         mon.pos().x, mon.pos().y, dest.x, dest.y);
    monster_pathfind mp;
    bool success = mp.init_pathfind(&mon, dest, true, true);
    if (success)
    {
        vector<coord_def> path = mp.backtrack();
        env.travel_trail = path;
#ifdef USE_TILE_WEB
        for (coord_def pos : env.travel_trail)
            tiles.update_minimap(pos);
#endif
        string path_str;
        mpr(jtrans("Here's the shortest path: ") + " ");
        for (coord_def pos : path)
            path_str += make_stringf("(%d, %d)  ", pos.x, pos.y);
        mpr(path_str);
        mprf(jtransc("-> path length: %u"), (unsigned int)path.size());

        mpr("");
        path = mp.calc_waypoints();
        path_str = "";
        mpr("");
        mpr(jtrans("And here are the needed waypoints: ") + " ");
        for (coord_def pos : path)
            path_str += make_stringf("(%d, %d)  ", pos.x, pos.y);
        mpr(path_str);
        mprf(jtransc("-> #waypoints: %u"), (unsigned int)path.size());
    }
}

static void _miscast_screen_update()
{
    viewwindow();

    you.redraw_status_flags =
        REDRAW_LINE_1_MASK | REDRAW_LINE_2_MASK | REDRAW_LINE_3_MASK;
    print_stats();

#ifndef USE_TILE_LOCAL
    update_monster_pane();
#endif
}

void debug_miscast(int target_index)
{
    crawl_state.cancel_cmd_repeat();

    actor* target;
    if (target_index == NON_MONSTER)
        target = &you;
    else
        target = &menv[target_index];

    if (!target->alive())
    {
        mpr(jtrans("Can't make already dead target miscast."));
        return;
    }

    char specs[100];
    mpr_nojoin(MSGCH_PROMPT, jtrans("Miscast which school or spell, by name? ") + " ");
    if (cancellable_get_line_autohist(specs, sizeof specs) || !*specs)
    {
        canned_msg(MSG_OK);
        return;
    }

    spell_type         spell  = spell_by_name(specs, true);
    spschool_flag_type school = school_by_name(specs);

    // Prefer exact matches for school name over partial matches for
    // spell name.
    if (school != SPTYP_NONE
        && (strcasecmp(specs, spelltype_short_name(school)) == 0
            || strcasecmp(specs, spelltype_long_name(school)) == 0))
    {
        spell = SPELL_NO_SPELL;
    }

    if (spell == SPELL_NO_SPELL && school == SPTYP_NONE)
    {
        mpr(jtrans("No matching spell or spell school."));
        return;
    }
    else if (spell != SPELL_NO_SPELL && school != SPTYP_NONE)
    {
        mprf(jtransc("Ambiguous: can be spell '%s' or school '%s'."),
             tagged_jtransc("[spell]", spell_title(spell)),
             jtransc(spelltype_short_name(school)));
        return;
    }

    spschools_type disciplines = SPTYP_NONE;
    if (spell != SPELL_NO_SPELL)
    {
        disciplines = get_spell_disciplines(spell);

        if (!disciplines)
        {
            mprf(jtransc("Spell '%s' has no disciplines."), spell_title(spell));
            return;
        }
    }

    if (spell != SPELL_NO_SPELL)
        mprf(jtransc("Miscasting spell %s."), tagged_jtransc("[spell]", spell_title(spell)));
    else
        mprf(jtransc("Miscasting school %s."), spelltype_long_name(school));

    if (spell != SPELL_NO_SPELL)
        mpr_nojoin(MSGCH_PROMPT, jtrans("Enter spell_power,raw_spell_failure: ") + " ");
    else
        mpr_nojoin(MSGCH_PROMPT, jtrans("Enter miscast_level or spell_power,raw_spell_failure: ") + " ");

    if (cancellable_get_line_autohist(specs, sizeof specs) || !*specs)
    {
        canned_msg(MSG_OK);
        return;
    }

    int level = -1, pow = -1, fail = -1;

    if (strchr(specs, ','))
    {
        vector<string> nums = split_string(",", specs);
        pow  = atoi(nums[0].c_str());
        fail = atoi(nums[1].c_str());

        if (pow <= 0 || fail <= 0)
        {
            canned_msg(MSG_OK);
            return;
        }
    }
    else
    {
        if (spell != SPELL_NO_SPELL)
        {
            mpr(jtrans("Can only enter fixed miscast level for schools, not spells."));
            return;
        }

        level = atoi(specs);
        if (level < 0)
        {
            canned_msg(MSG_OK);
            return;
        }
        else if (level > 3)
        {
            mpr(jtrans("Miscast level can be at most 3."));
            return;
        }
    }

    // Handle repeats ourselves since miscasts are likely to interrupt
    // command repetions, especially if the player is the target.
    int repeats = prompt_for_int(jtrans("Number of repetitions? ") + " ", true);
    if (repeats < 1)
    {
        canned_msg(MSG_OK);
        return;
    }

    // Suppress "nothing happens" message for monster miscasts which are
    // only harmless messages, since a large number of those are missing
    // monster messages.
    nothing_happens_when_type nothing = NH_DEFAULT;
    if (target_index != NON_MONSTER && level == 0)
        nothing = NH_NEVER;

    MiscastEffect *miscast;

    if (spell != SPELL_NO_SPELL)
    {
        miscast = new MiscastEffect(target, target, WIZARD_MISCAST, spell, pow,
                                    fail, "", nothing);
    }
    else
    {
        if (level != -1)
        {
            miscast = new MiscastEffect(target, target, WIZARD_MISCAST, school,
                                        level, "wizard testing miscast",
                                        nothing);
        }
        else
        {
            miscast = new MiscastEffect(target, target, WIZARD_MISCAST, school,
                                        pow, fail, "wizard testing miscast",
                                        nothing);
        }
    }
    // Merely creating the miscast object causes one miscast effect to
    // happen.
    repeats--;
    if (level != 0)
        _miscast_screen_update();

    while (target->alive() && repeats-- > 0)
    {
        if (kbhit())
        {
            mpr(jtrans("Key pressed, interrupting miscast testing."));
            getchm();
            break;
        }

        miscast->do_miscast();
        if (level != 0)
            _miscast_screen_update();
    }

    delete miscast;
}

#ifdef DEBUG_BONES
void debug_ghosts()
{
    mpr_nojoin(MSGCH_PROMPT, jtrans("(C)reate or (L)oad bones file?"));
    const char c = toalower(getchm());

    if (c == 'c')
        save_ghost(true);
    else if (c == 'l')
        load_ghost(false);
    else
        canned_msg(MSG_OK);
}
#endif

#endif

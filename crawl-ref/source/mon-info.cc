/**
 * @file
 * @brief Monster information that may be passed to the user.
 *
 * Used to fill the monster pane and to pass monster info to Lua.
**/

#include "AppHdr.h"

#include "mon-info.h"

#include <algorithm>
#include <sstream>

#include "act-iter.h"
#include "artefact.h"
#include "colour.h"
#include "coordit.h"
#include "database.h"
#include "english.h"
#include "env.h"
#include "fight.h"
#include "ghost.h"
#include "itemname.h"
#include "itemprop.h"
#include "japanese.h"
#include "libutil.h"
#include "los.h"
#include "message.h"
#include "misc.h"
#include "mon-book.h"
#include "mon-chimera.h"
#include "mon-death.h" // ELVEN_IS_ENERGIZED_KEY
#include "mon-tentacle.h"
#include "options.h"
#include "religion.h"
#include "skills.h"
#include "spl-summoning.h"
#include "state.h"
#include "stringutil.h"
#ifdef USE_TILE
#include "tilepick.h"
#endif
#include "transform.h"
#include "traps.h"

static monster_info_flags ench_to_mb(const monster& mons, enchant_type ench)
{
    // Suppress silly-looking combinations, even if they're
    // internally valid.
    if (mons.paralysed() && (ench == ENCH_SLOW || ench == ENCH_HASTE
                      || ench == ENCH_SWIFT
                      || ench == ENCH_PETRIFIED
                      || ench == ENCH_PETRIFYING))
    {
        return NUM_MB_FLAGS;
    }

    if (ench == ENCH_PETRIFIED && mons.has_ench(ENCH_PETRIFYING))
        return NUM_MB_FLAGS;

    // Don't claim that naturally 'confused' monsters are especially bewildered
    if (ench == ENCH_CONFUSION && mons_class_flag(mons.type, M_CONFUSED))
        return NUM_MB_FLAGS;

    switch (ench)
    {
    case ENCH_BERSERK:
        return MB_BERSERK;
    case ENCH_POISON:
        return MB_POISONED;
    case ENCH_SICK:
        return MB_SICK;
    case ENCH_ROT:
        return MB_ROTTING;
    case ENCH_CORONA:
    case ENCH_SILVER_CORONA:
        return MB_GLOWING;
    case ENCH_SLOW:
        return MB_SLOWED;
    case ENCH_INSANE:
        return MB_INSANE;
    case ENCH_BATTLE_FRENZY:
        return MB_FRENZIED;
    case ENCH_ROUSED:
        return MB_ROUSED;
    case ENCH_HASTE:
        return MB_HASTED;
    case ENCH_MIGHT:
        return MB_STRONG;
    case ENCH_CONFUSION:
        return MB_CONFUSED;
    case ENCH_INVIS:
    {
        you.seen_invis = true;
        return MB_INVISIBLE;
    }
    case ENCH_CHARM:
        return MB_CHARMED;
    case ENCH_STICKY_FLAME:
        return MB_BURNING;
    case ENCH_HELD:
        return get_trapping_net(mons.pos(), true) == NON_ITEM
               ? MB_WEBBED : MB_CAUGHT;
    case ENCH_PETRIFIED:
        return MB_PETRIFIED;
    case ENCH_PETRIFYING:
        return MB_PETRIFYING;
    case ENCH_LOWERED_MR:
        return MB_VULN_MAGIC;
    case ENCH_SWIFT:
        return MB_SWIFT;
    case ENCH_SILENCE:
        return MB_SILENCING;
    case ENCH_PARALYSIS:
        return MB_PARALYSED;
    case ENCH_SOUL_RIPE:
        return MB_POSSESSABLE;
    case ENCH_REGENERATION:
        return MB_REGENERATION;
    case ENCH_RAISED_MR:
        return MB_RAISED_MR;
    case ENCH_MIRROR_DAMAGE:
        return MB_MIRROR_DAMAGE;
    case ENCH_FEAR_INSPIRING:
        return MB_FEAR_INSPIRING;
    case ENCH_WITHDRAWN:
        return MB_WITHDRAWN;
    case ENCH_BLEED:
        return MB_BLEEDING;
    case ENCH_DAZED:
        return MB_DAZED;
    case ENCH_MUTE:
        return MB_MUTE;
    case ENCH_BLIND:
        return MB_BLIND;
    case ENCH_DUMB:
        return MB_DUMB;
    case ENCH_MAD:
        return MB_MAD;
    case ENCH_INNER_FLAME:
        return MB_INNER_FLAME;
    case ENCH_BREATH_WEAPON:
        return MB_BREATH_WEAPON;
    case ENCH_DEATHS_DOOR:
        return MB_DEATHS_DOOR;
    case ENCH_ROLLING:
        return MB_ROLLING;
    case ENCH_STONESKIN:
        return MB_STONESKIN;
    case ENCH_OZOCUBUS_ARMOUR:
        return MB_OZOCUBUS_ARMOUR;
    case ENCH_WRETCHED:
        return MB_WRETCHED;
    case ENCH_SCREAMED:
        return MB_SCREAMED;
    case ENCH_WORD_OF_RECALL:
        return MB_WORD_OF_RECALL;
    case ENCH_INJURY_BOND:
        return MB_INJURY_BOND;
    case ENCH_WATER_HOLD:
        if (mons.res_water_drowning())
            return MB_WATER_HOLD;
        else
            return MB_WATER_HOLD_DROWN;
    case ENCH_FLAYED:
        return MB_FLAYED;
    case ENCH_WEAK:
        return MB_WEAK;
    case ENCH_DIMENSION_ANCHOR:
        return MB_DIMENSION_ANCHOR;
    case ENCH_CONTROL_WINDS:
        return MB_CONTROL_WINDS;
    case ENCH_TOXIC_RADIANCE:
        return MB_TOXIC_RADIANCE;
    case ENCH_GRASPING_ROOTS:
        return MB_GRASPING_ROOTS;
    case ENCH_FIRE_VULN:
        return MB_FIRE_VULN;
    case ENCH_TORNADO:
        return MB_TORNADO;
    case ENCH_TORNADO_COOLDOWN:
        return MB_TORNADO_COOLDOWN;
    case ENCH_BARBS:
        return MB_BARBS;
    case ENCH_POISON_VULN:
        return MB_POISON_VULN;
    case ENCH_ICEMAIL:
        return MB_ICEMAIL;
    case ENCH_AGILE:
        return MB_AGILE;
    case ENCH_FROZEN:
        return MB_FROZEN;
    case ENCH_BLACK_MARK:
        return MB_BLACK_MARK;
    case ENCH_SAP_MAGIC:
        return MB_SAP_MAGIC;
    case ENCH_SHROUD:
        return MB_SHROUD;
    case ENCH_CORROSION:
        return MB_CORROSION;
    case ENCH_DRAINED:
        {
            const bool heavily_drained = mons.get_ench(ench).degree
                                         >= mons.get_experience_level() / 2;
            return heavily_drained ? MB_HEAVILY_DRAINED : MB_LIGHTLY_DRAINED;
        }
    case ENCH_REPEL_MISSILES:
        return MB_REPEL_MSL;
    case ENCH_DEFLECT_MISSILES:
        return MB_DEFLECT_MSL;
    case ENCH_CONDENSATION_SHIELD:
        return MB_CONDENSATION_SHIELD;
    case ENCH_RESISTANCE:
        return MB_RESISTANCE;
    case ENCH_HEXED:
        return MB_HEXED;
    case ENCH_BONE_ARMOUR:
        return MB_BONE_ARMOUR;
    default:
        return NUM_MB_FLAGS;
    }
}

static bool _blocked_ray(const coord_def &where,
                         dungeon_feature_type* feat = nullptr)
{
    if (exists_ray(you.pos(), where, opc_solid_see)
        || !exists_ray(you.pos(), where, opc_default))
    {
        return false;
    }
    if (feat == nullptr)
        return true;
    *feat = ray_blocker(you.pos(), where);
    return true;
}

static bool _is_public_key(string key)
{
    if (key == "helpless"
     || key == "wand_known"
     || key == "feat_type"
     || key == "glyph"
     || key == "dbname"
     || key == "monster_tile"
#ifdef USE_TILE
     || key == TILE_NUM_KEY
#endif
     || key == "tile_idx"
     || key == "chimera_part_2"
     || key == "chimera_part_3"
     || key == "chimera_batty"
     || key == "chimera_wings"
     || key == "chimera_legs"
     || key == "custom_spells"
     || key == ELVEN_IS_ENERGIZED_KEY)
    {
        return true;
    }

    return false;
}

static int quantise(int value, int stepsize)
{
    return value + stepsize - value % stepsize;
}

// Returns true if using a directional tentacle tile would leak
// information the player doesn't have about a tentacle segment's
// current position.
static bool _tentacle_pos_unknown(const monster *tentacle,
                                  const coord_def orig_pos)
{
    // We can see the segment, no guessing necessary.
    if (!tentacle->submerged())
        return false;

    const coord_def t_pos = tentacle->pos();

    // Checks whether there are any positions adjacent to the
    // original tentacle that might also contain the segment.
    for (adjacent_iterator ai(orig_pos); ai; ++ai)
    {
        if (*ai == t_pos)
            continue;

        if (!in_bounds(*ai))
            continue;

        if (you.pos() == *ai)
            continue;

        // If there's an adjacent deep water tile, the segment
        // might be there instead.
        if (grd(*ai) == DNGN_DEEP_WATER)
        {
            const monster *mon = monster_at(*ai);
            if (mon && you.can_see(mon))
            {
                // Could originate from the kraken.
                if (mon->type == MONS_KRAKEN)
                    return true;

                // Otherwise, we know the segment can't be there.
                continue;
            }
            return true;
        }

        if (grd(*ai) == DNGN_SHALLOW_WATER)
        {
            const monster *mon = monster_at(*ai);

            // We know there's no segment there.
            if (!mon)
                continue;

            // Disturbance in shallow water -> might be a tentacle.
            if (mon->type == MONS_KRAKEN || mon->submerged())
                return true;
        }
    }

    // Using a directional tile leaks no information.
    return false;
}

static void _translate_tentacle_ref(monster_info& mi, const monster* m,
                                    const string &key)
{
    if (!m->props.exists(key))
        return;

    const monster* other = monster_by_mid(m->props[key].get_int());
    if (other)
    {
        coord_def h_pos = other->pos();
        // If the tentacle and the other segment are no longer adjacent
        // (distortion etc.), just treat them as not connected.
        if (adjacent(m->pos(), h_pos)
            && other->type != MONS_KRAKEN
            && other->type != MONS_ZOMBIE
            && other->type != MONS_SPECTRAL_THING
            && other->type != MONS_SIMULACRUM
            && !_tentacle_pos_unknown(other, m->pos()))
        {
            mi.props[key] = h_pos - m->pos();
        }
    }
}

monster_info::monster_info(monster_type p_type, monster_type p_base_type)
{
    mb.reset();
    attitude = ATT_HOSTILE;
    pos = coord_def(0, 0);

    type = p_type;
    base_type = p_base_type;

    draco_type = mons_genus(type) == MONS_DRACONIAN  ? MONS_DRACONIAN :
                 mons_genus(type) == MONS_DEMONSPAWN ? MONS_DEMONSPAWN
                                                     : type;

    if (mons_genus(type) == MONS_HYDRA || mons_genus(base_type) == MONS_HYDRA)
        num_heads = 1;
    else
        number = 0;

    _colour = COLOUR_INHERIT;

    holi = mons_class_holiness(type);

    mintel = mons_class_intel(type);

    ac = get_mons_class_ac(type);
    ev = base_ev = get_mons_class_ev(type);
    mresists = get_mons_class_resists(type);

    mitemuse = mons_class_itemuse(type);

    mbase_speed = mons_class_base_speed(type);
    menergy = mons_class_energy(type);

    fly = max(mons_class_flies(type), mons_class_flies(base_type));

    if (mons_class_wields_two_weapons(type)
        || mons_class_wields_two_weapons(base_type))
    {
        mb.set(MB_TWO_WEAPONS);
    }

    if (!mons_class_can_regenerate(type)
        || !mons_class_can_regenerate(base_type))
    {
        mb.set(MB_NO_REGEN);
    }

    threat = MTHRT_UNDEF;

    dam = MDAM_OKAY;

    fire_blocker = DNGN_UNSEEN;

    u.ghost.acting_part = MONS_0;
    if (mons_is_pghost(type))
    {
        u.ghost.species = SP_HUMAN;
        u.ghost.job = JOB_WANDERER;
        u.ghost.religion = GOD_NO_GOD;
        u.ghost.best_skill = SK_FIGHTING;
        u.ghost.best_skill_rank = 2;
        u.ghost.xl_rank = 3;
        u.ghost.ac = 5;
        u.ghost.damage = 5;
    }

    // Don't put a bad base type on ?/mdraconian annihilator etc.
    if (base_type == MONS_NO_MONSTER && !mons_is_job(type))
        base_type = type;

    if (mons_is_job(type))
    {
        const monster_type race = (base_type == MONS_NO_MONSTER) ? draco_type
                                                                 : base_type;
        ac += get_mons_class_ac(race);
        ev += get_mons_class_ev(race);
    }

    if (mons_is_unique(type))
    {
        if (type == MONS_LERNAEAN_HYDRA
            || type == MONS_ROYAL_JELLY
            || mons_species(type) == MONS_SERPENT_OF_HELL)
        {
            mb.set(MB_NAME_THE);
        }
        else
        {
            mb.set(MB_NAME_UNQUALIFIED);
            mb.set(MB_NAME_THE);
        }
    }

    for (int i = 0; i < MAX_NUM_ATTACKS; ++i)
    {
        attack[i] = get_monster_data(type)->attack[i];
        attack[i].damage = 0;
    }

    props.clear();

    client_id = 0;
}

monster_info::monster_info(const monster* m, int milev)
{
    mb.reset();
    attitude = ATT_HOSTILE;
    pos = m->pos();

    attitude = mons_attitude(m);

    bool nomsg_wounds = false;

    type = m->type;
    threat = mons_threat_level(m);

    props.clear();
    // CrawlHashTable::begin() const can fail if the hash is empty.
    if (!m->props.empty())
    {
        for (const auto &entry : m->props)
            if (_is_public_key(entry.first))
                props[entry.first] = entry.second;
    }

    // Translate references to tentacles into just their locations
    if (mons_is_tentacle_or_tentacle_segment(type))
    {
        _translate_tentacle_ref(*this, m, "inwards");
        _translate_tentacle_ref(*this, m, "outwards");
    }

    draco_type =
        (mons_genus(type) == MONS_DRACONIAN
        || mons_genus(type) == MONS_DEMONSPAWN)
            ? ::draco_or_demonspawn_subspecies(m)
            : type;

    if (!mons_can_display_wounds(m)
        || !mons_class_can_display_wounds(type))
    {
        nomsg_wounds = true;
    }

    base_type = m->base_monster;
    if (base_type == MONS_NO_MONSTER)
        base_type = type;

    if (type == MONS_SLIME_CREATURE)
        slime_size = m->blob_size;
    else if (type == MONS_BALLISTOMYCETE)
        is_active = !!m->ballisto_activity;
    else if (mons_genus(type) == MONS_HYDRA
             || mons_genus(base_type) == MONS_HYDRA)
    {
        num_heads = m->num_heads;
    }
    // others use number for internal information
    else
        number = 0;

    _colour = m->colour;

    if (m->is_summoned()
        && (!m->has_ench(ENCH_PHANTOM_MIRROR) || m->friendly()))
    {
        mb.set(MB_SUMMONED);
    }
    else if (m->is_perm_summoned())
        mb.set(MB_PERM_SUMMON);

    if (m->has_ench(ENCH_SUMMON_CAPPED))
        mb.set(MB_SUMMONED_CAPPED);

    if (mons_is_unique(type))
    {
        if (type == MONS_LERNAEAN_HYDRA
            || type == MONS_ROYAL_JELLY
            || mons_species(type) == MONS_SERPENT_OF_HELL)
        {
            mb.set(MB_NAME_THE);
        }
        else
        {
            mb.set(MB_NAME_UNQUALIFIED);
            mb.set(MB_NAME_THE);
        }
    }

    mname = m->mname;

    const uint64_t name_flags = m->flags & MF_NAME_MASK;

    if (name_flags == MF_NAME_SUFFIX)
        mb.set(MB_NAME_SUFFIX);
    else if (name_flags == MF_NAME_ADJECTIVE)
        mb.set(MB_NAME_ADJECTIVE);
    else if (name_flags == MF_NAME_REPLACE)
        mb.set(MB_NAME_REPLACE);

    const bool need_name_desc =
        name_flags == MF_NAME_SUFFIX
            || name_flags == MF_NAME_ADJECTIVE
            || (m->flags & MF_NAME_DEFINITE);

    if (!mname.empty()
        && !(m->flags & MF_NAME_DESCRIPTOR)
        && !need_name_desc)
    {
        mb.set(MB_NAME_UNQUALIFIED);
        mb.set(MB_NAME_THE);
    }
    else if (m->flags & MF_NAME_DEFINITE)
        mb.set(MB_NAME_THE);
    if (m->flags & MF_NAME_ZOMBIE)
        mb.set(MB_NAME_ZOMBIE);
    if (m->flags & MF_NAME_SPECIES)
        mb.set(MB_NO_NAME_TAG);

    // Chimera acting head needed for name
    u.ghost.acting_part = MONS_0;
    if (mons_class_is_chimeric(type))
    {
        ASSERT(m->ghost.get());
        ghost_demon& ghost = *m->ghost;
        u.ghost.acting_part = ghost.acting_part;
    }
    // As is ghostliness
    if (testbits(m->flags, MF_SPECTRALISED))
        mb.set(MB_SPECTRALISED);

    if (milev <= MILEV_NAME)
    {
        if (type == MONS_DANCING_WEAPON
            && m->inv[MSLOT_WEAPON] != NON_ITEM)
        {
            inv[MSLOT_WEAPON].reset(
                new item_def(get_item_info(mitm[m->inv[MSLOT_WEAPON]])));
        }
        return;
    }

    holi = m->holiness();

    mintel = mons_intel(m);
    ac = m->armour_class(false);
    ev = m->evasion(false);
    base_ev = m->base_evasion();
    mresists = get_mons_resists(m);
    mitemuse = mons_itemuse(m);
    mbase_speed = mons_base_speed(m);
    menergy = mons_energy(m);
    fly = mons_flies(m);

    if (mons_wields_two_weapons(m))
        mb.set(MB_TWO_WEAPONS);
    if (!mons_can_regenerate(m))
        mb.set(MB_NO_REGEN);
    if (m->haloed() && !m->umbraed())
        mb.set(MB_HALOED);
    if (!m->haloed() && m->umbraed())
        mb.set(MB_UMBRAED);
    if (mons_looks_stabbable(m))
        mb.set(MB_STABBABLE);
    if (mons_looks_distracted(m))
        mb.set(MB_DISTRACTED);
    if (m->liquefied_ground())
        mb.set(MB_SLOW_MOVEMENT);
    if (m->is_wall_clinging())
        mb.set(MB_CLINGING);

    dam = mons_get_damage_level(m);

    // If no messages about wounds, don't display damage level either.
    if (nomsg_wounds)
        dam = MDAM_OKAY;

    if (!mons_class_flag(m->type, M_NO_EXP_GAIN)) // Firewood, butterflies, etc.
    {
        if (m->asleep())
        {
            if (!m->can_hibernate(true))
                mb.set(MB_DORMANT);
            else
                mb.set(MB_SLEEPING);
        }
        // Applies to both friendlies and hostiles
        else if (mons_is_fleeing(m))
            mb.set(MB_FLEEING);
        else if (mons_is_wandering(m) && !mons_is_batty(m))
        {
            if (m->is_stationary())
                mb.set(MB_UNAWARE);
            else
                mb.set(MB_WANDERING);
        }
        // TODO: is this ever needed?
        else if (m->foe == MHITNOT && !mons_is_batty(m)
                 && m->attitude == ATT_HOSTILE)
        {
            mb.set(MB_UNAWARE);
        }
    }

    for (auto &entry : m->enchantments)
    {
        monster_info_flags flag = ench_to_mb(*m, entry.first);
        if (flag != NUM_MB_FLAGS)
            mb.set(flag);
    }

    if (type == MONS_SILENT_SPECTRE)
        mb.set(MB_SILENCING);

    if (you.beheld_by(m))
        mb.set(MB_MESMERIZING);

    // Evilness of attacking
    switch (attitude)
    {
    case ATT_NEUTRAL:
    case ATT_HOSTILE:
        if (you_worship(GOD_SHINING_ONE)
            && !tso_unchivalric_attack_safe_monster(m)
            && find_stab_type(&you, m) != STAB_NO_STAB)
        {
            mb.set(MB_EVIL_ATTACK);
        }
        break;
    default:
        break;
    }

    if (testbits(m->flags, MF_ENSLAVED_SOUL))
        mb.set(MB_ENSLAVED);

    if (m->is_shapeshifter() && (m->flags & MF_KNOWN_SHIFTER))
        mb.set(MB_SHAPESHIFTER);

    if (m->known_chaos())
        mb.set(MB_CHAOTIC);

    if (m->submerged())
        mb.set(MB_SUBMERGED);

    if (mons_is_pghost(type))
    {
        ASSERT(m->ghost.get());
        ghost_demon& ghost = *m->ghost;
        u.ghost.species = ghost.species;
        if (species_genus(u.ghost.species) == GENPC_DRACONIAN && ghost.xl < 7)
            u.ghost.species = SP_BASE_DRACONIAN;
        u.ghost.job = ghost.job;
        u.ghost.religion = ghost.religion;
        u.ghost.best_skill = ghost.best_skill;
        u.ghost.best_skill_rank = get_skill_rank(ghost.best_skill_level);
        u.ghost.xl_rank = ghost_level_to_rank(ghost.xl);
        u.ghost.ac = quantise(ghost.ac, 5);
        u.ghost.damage = quantise(ghost.damage, 5);

        // describe abnormal (branded) ghost weapons
        if (ghost.brand != SPWPN_NORMAL)
            props[SPECIAL_WEAPON_KEY] = ghost_brand_name(ghost.brand);
    }

    if (mons_is_ghost_demon(type))
        u.ghost.can_sinv = m->ghost->see_invis;

    // book loading for player ghost and vault monsters
    spells.clear();
    if (m->props.exists("custom_spells") || mons_is_pghost(type))
        spells = m->spells;
    if (m->is_priest())
        props["priest"] = true;
    else if (m->is_actual_spellcaster())
        props["actual_spellcaster"] = true;

    for (int i = 0; i < MAX_NUM_ATTACKS; ++i)
    {
        attack[i] = mons_attack_spec(m, i, true);
        attack[i].damage = 0;
    }

    for (unsigned i = 0; i <= MSLOT_LAST_VISIBLE_SLOT; ++i)
    {
        bool ok;
        if (m->inv[i] == NON_ITEM)
            ok = false;
        else if (i == MSLOT_MISCELLANY)
            ok = false;
        else if (attitude == ATT_FRIENDLY)
            ok = true;
        else if (i == MSLOT_WAND)
            ok = props.exists("wand_known") && props["wand_known"];
        else if (m->props.exists("ash_id") && item_type_known(mitm[m->inv[i]]))
            ok = true;
        else if (i == MSLOT_ALT_WEAPON)
            ok = wields_two_weapons();
        else if (i == MSLOT_MISSILE)
            ok = false;
        else
            ok = true;
        if (ok)
            inv[i].reset(new item_def(get_item_info(mitm[m->inv[i]])));
    }

    fire_blocker = DNGN_UNSEEN;
    if (!crawl_state.arena_suspended
        && m->pos() != you.pos())
    {
        _blocked_ray(m->pos(), &fire_blocker);
    }

    if (m->props.exists("quote"))
        quote = m->props["quote"].get_string();

    if (m->props.exists("description"))
        description = m->props["description"].get_string();

    // init names of constrictor and constrictees
    constrictor_name = "";
    constricting_name.clear();

    // name of what this monster is constricted by, if any
    if (m->is_constricted())
    {
        actor * const constrictor = actor_by_mid(m->constricted_by);
        if (constrictor)
        {
            constrictor_name = jtrans(constrictor->name(DESC_A, true))
                + jtrans((m->held == HELD_MONSTER ? "held by "
                                                  : "constricted by "));
        }
    }

    // names of what this monster is constricting, if any
    if (m->constricting)
    {
        const string gerund = jtrans(m->constriction_damage() ? "を拘束している"
                                                              : "を押さえつけている");
        for (const auto &entry : *m->constricting)
        {
            if (const actor* const constrictee = actor_by_mid(entry.first))
            {
                constricting_name.push_back(jtrans(constrictee->name(DESC_A, true))
                                            + gerund);
            }
        }
    }

    if (mons_has_known_ranged_attack(m))
        mb.set(MB_RANGED_ATTACK);

    // this must be last because it provides this structure to Lua code
    if (milev > MILEV_SKIP_SAFE)
    {
        if (mons_is_safe(m))
            mb.set(MB_SAFE);
        else
            mb.set(MB_UNSAFE);
        if (mons_is_firewood(m))
            mb.set(MB_FIREWOOD);
    }

    client_id = m->get_client_id();
}

string monster_info::db_name() const
{
    if (type == MONS_DANCING_WEAPON && inv[MSLOT_WEAPON].get())
    {
        iflags_t ignore_flags = ISFLAG_KNOW_CURSE | ISFLAG_KNOW_PLUSES;
        bool     use_inscrip  = false;
        return inv[MSLOT_WEAPON]->name(DESC_DBNAME, false, false, use_inscrip, false,
                         ignore_flags);
    }

    if (type == MONS_SENSED)
        return get_monster_data(base_type)->name;

    return get_monster_data(type)->name;
}

string monster_info::_core_name() const
{
    monster_type nametype = type;

    switch (type)
    {
    case MONS_ZOMBIE:
    case MONS_SKELETON:
    case MONS_SIMULACRUM:
#if TAG_MAJOR_VERSION == 34
    case MONS_ZOMBIE_SMALL:     case MONS_ZOMBIE_LARGE:
    case MONS_SKELETON_SMALL:   case MONS_SKELETON_LARGE:
    case MONS_SIMULACRUM_SMALL: case MONS_SIMULACRUM_LARGE:
#endif
    case MONS_SPECTRAL_THING:
        nametype = mons_species(base_type);
        break;

    case MONS_PILLAR_OF_SALT:
    case MONS_BLOCK_OF_ICE:     case MONS_CHIMERA:
    case MONS_SENSED:
        nametype = base_type;
        break;

    default:
        break;
    }

    string s;

    if (is(MB_NAME_REPLACE))
        s = jtrans(mname);
    else if (nametype == MONS_LERNAEAN_HYDRA)
        s = jtrans("Lernaean hydra"); // TODO: put this into mon-data.h
    else if (nametype == MONS_ROYAL_JELLY)
        s = jtrans("the royal jelly");
    else if (mons_species(nametype) == MONS_SERPENT_OF_HELL)
        s = jtrans("Serpent of Hell");
    else if (invalid_monster_type(nametype) && nametype != MONS_PROGRAM_BUG)
        s = "INVALID MONSTER";
    else
    {
        const char* slime_sizes[] = {"buggy ", "", "large ", "very large ",
                                               "enormous ", "titanic "};
        s = jtrans(get_monster_data(nametype)->name);

        switch (type)
        {
        case MONS_SLIME_CREATURE:
            ASSERT((size_t) slime_size <= ARRAYSZ(slime_sizes));
            s = jtrans(string(slime_sizes[slime_size]) +
                       get_monster_data(nametype)->name);
            break;
        case MONS_UGLY_THING:
        case MONS_VERY_UGLY_THING:
            s = jtrans(ugly_thing_colour_name(_colour)) + s;
            break;

        case MONS_DRACONIAN_CALLER:
        case MONS_DRACONIAN_MONK:
        case MONS_DRACONIAN_ZEALOT:
        case MONS_DRACONIAN_SHIFTER:
        case MONS_DRACONIAN_ANNIHILATOR:
        case MONS_DRACONIAN_KNIGHT:
        case MONS_DRACONIAN_SCORCHER:
            if (base_type != MONS_NO_MONSTER)
                s = jtrans(draconian_colour_name(base_type)) + s;
            break;

        case MONS_BLOOD_SAINT:
        case MONS_CHAOS_CHAMPION:
        case MONS_WARMONGER:
        case MONS_CORRUPTER:
        case MONS_BLACK_SUN:
            if (base_type != MONS_NO_MONSTER)
                s = jtrans(demonspawn_base_name(base_type)) + s;
            break;

        case MONS_DANCING_WEAPON:
        case MONS_SPECTRAL_WEAPON:
            if (inv[MSLOT_WEAPON].get())
            {
                iflags_t ignore_flags = ISFLAG_KNOW_CURSE | ISFLAG_KNOW_PLUSES;
                bool     use_inscrip  = true;
                const item_def& item = *inv[MSLOT_WEAPON];
                s = type==MONS_SPECTRAL_WEAPON ? jtrans("spectral ") : "";
                s += (item.name(DESC_PLAIN, false, false, use_inscrip, false,
                                ignore_flags));
            }
            break;

        case MONS_PLAYER_GHOST:
            s = mname + "の" + jtrans("ghost");
            break;
        case MONS_PLAYER_ILLUSION:
            s = mname + "の" + jtrans("illusion");
            break;
        case MONS_PANDEMONIUM_LORD:
            s = jtrans(" the pandemonium lord") + "『" + mname + "』";
            break;
        default:
            break;
        }
    }

    //XXX: Hack to get poly'd TLH's name on death to look right.
    if (is(MB_NAME_SUFFIX) && type != MONS_LERNAEAN_HYDRA)
    {
        if (mname == "necromancer" || // big kobold necromancer
            mname == "captain" || // vault guard captain
            mname == "wizard" || // oklob plant|sapling xxx
            mname == "conjurer" ||
            mname == "summoner" ||
            mname == "shifter" ||
            mname == "meteorologist" ||
            mname == "demonologist" ||
            mname == "annihilator" ||
            mname == "priest")
            s += "の" + jtrans(mname);
        else
            s += jtrans(mname);
    }
    else if (is(MB_NAME_ADJECTIVE))
    {
        if (mname == "apprentice") // apprentice kobold demonologist
            s = replace_all(s, "の", "の" + jtrans(mname));
        else if(mname == "conjurer" || // conjurer statue
                mname == "fire elementalist" || // sprint_mu
                mname == "water elementalist" ||
                mname == "air elementalist" ||
                mname == "earth elementalist" ||
                mname == "zot")
            s = jtrans(mname) + "の" + s;
        else if(mname == "giant") // giant anaconda
            s = "巨大" + s;
        else
            s = jtrans(mname) + s;
    }

    return s;
}

string monster_info::_core_name_en() const
{
    monster_type nametype = type;

    switch (type)
    {
    case MONS_ZOMBIE:
    case MONS_SKELETON:
    case MONS_SIMULACRUM:
#if TAG_MAJOR_VERSION == 34
    case MONS_ZOMBIE_SMALL:     case MONS_ZOMBIE_LARGE:
    case MONS_SKELETON_SMALL:   case MONS_SKELETON_LARGE:
    case MONS_SIMULACRUM_SMALL: case MONS_SIMULACRUM_LARGE:
#endif
    case MONS_SPECTRAL_THING:
        nametype = mons_species(base_type);
        break;

    case MONS_PILLAR_OF_SALT:
    case MONS_BLOCK_OF_ICE:     case MONS_CHIMERA:
    case MONS_SENSED:
        nametype = base_type;
        break;

    default:
        break;
    }

    string s;

    if (is(MB_NAME_REPLACE))
        s = mname;
    else if (nametype == MONS_LERNAEAN_HYDRA)
        s = "Lernaean hydra"; // TODO: put this into mon-data.h
    else if (nametype == MONS_ROYAL_JELLY)
        s = "royal jelly";
    else if (mons_species(nametype) == MONS_SERPENT_OF_HELL)
        s = "Serpent of Hell";
    else if (invalid_monster_type(nametype) && nametype != MONS_PROGRAM_BUG)
        s = "INVALID MONSTER";
    else
    {
        const char* slime_sizes[] = {"buggy ", "", "large ", "very large ",
                                     "enormous ", "titanic "};
        s = get_monster_data(nametype)->name;

        switch (type)
        {
        case MONS_SLIME_CREATURE:
            ASSERT((size_t) slime_size <= ARRAYSZ(slime_sizes));
            s = slime_sizes[slime_size] + s;
            break;
        case MONS_UGLY_THING:
        case MONS_VERY_UGLY_THING:
            s = ugly_thing_colour_name(_colour) + " " + s;
            break;

        case MONS_DRACONIAN_CALLER:
        case MONS_DRACONIAN_MONK:
        case MONS_DRACONIAN_ZEALOT:
        case MONS_DRACONIAN_SHIFTER:
        case MONS_DRACONIAN_ANNIHILATOR:
        case MONS_DRACONIAN_KNIGHT:
        case MONS_DRACONIAN_SCORCHER:
            if (base_type != MONS_NO_MONSTER)
                s = draconian_colour_name(base_type) + " " + s;
            break;

        case MONS_BLOOD_SAINT:
        case MONS_CHAOS_CHAMPION:
        case MONS_WARMONGER:
        case MONS_CORRUPTER:
        case MONS_BLACK_SUN:
            if (base_type != MONS_NO_MONSTER)
                s = demonspawn_base_name(base_type) + " " + s;
            break;

        case MONS_DANCING_WEAPON:
        case MONS_SPECTRAL_WEAPON:
            if (inv[MSLOT_WEAPON].get())
            {
                iflags_t ignore_flags = ISFLAG_KNOW_CURSE | ISFLAG_KNOW_PLUSES;
                bool     use_inscrip  = true;
                const item_def& item = *inv[MSLOT_WEAPON];
                s = type==MONS_SPECTRAL_WEAPON ? "spectral " : "";
                s += (item.name(DESC_PLAIN, false, false, use_inscrip, false,
                                ignore_flags));
            }
            break;

        case MONS_PLAYER_GHOST:
            s = apostrophise(mname) + " ghost";
            break;
        case MONS_PLAYER_ILLUSION:
            s = apostrophise(mname) + " illusion";
            break;
        case MONS_PANDEMONIUM_LORD:
            s = mname;
            break;
        default:
            break;
        }
    }

    //XXX: Hack to get poly'd TLH's name on death to look right.
    if (is(MB_NAME_SUFFIX) && type != MONS_LERNAEAN_HYDRA)
        s += " " + mname;
    else if (is(MB_NAME_ADJECTIVE))
        s = mname + " " + s;

    return s;
}

string monster_info::_apply_adjusted_description(description_level_type desc,
                                                 const string& s) const
{
    if (desc == DESC_ITS)
        desc = DESC_THE;

    if (is(MB_NAME_THE) && desc == DESC_A)
        desc = DESC_THE;

    if (attitude == ATT_FRIENDLY && desc == DESC_THE)
        desc = DESC_YOUR;

    return apply_description(desc, s);
}

string monster_info::_apply_adjusted_description_j(description_level_type desc,
                                                 const string& s) const
{
    if (desc == DESC_ITS)
        desc = DESC_THE;

    if (is(MB_NAME_THE) && desc == DESC_A)
        desc = DESC_THE;

    if (attitude == ATT_FRIENDLY && desc == DESC_THE)
        desc = DESC_YOUR;

    return apply_description_j(desc, s);
}

string monster_info::common_name(description_level_type desc) const
{
    const string core = _core_name();
    const bool nocore = mons_class_is_zombified(type)
                        && mons_is_unique(base_type)
                        && base_type == mons_species(base_type)
                        || mons_class_is_chimeric(type);

    ostringstream ss;

    if (props.exists("helpless"))
        ss << jtrans("helpless ");

    if (is(MB_SUBMERGED))
        ss << jtrans("submerged ");

    if (type == MONS_SPECTRAL_THING && !is(MB_NAME_ZOMBIE) && !nocore)
        ss << jtrans("spectral ");

    if (is(MB_SPECTRALISED))
        ss << jtrans("ghostly ");

    if (type == MONS_SENSED && !mons_is_sensed(base_type))
        ss << jtrans("sensed ");

    if (type == MONS_BALLISTOMYCETE)
        ss << (is_active ? jtrans("active ") : "");

    if ((mons_genus(type) == MONS_HYDRA || mons_genus(base_type) == MONS_HYDRA)
        && type != MONS_SENSED
        && type != MONS_BLOCK_OF_ICE
        && type != MONS_PILLAR_OF_SALT)
    {
        ASSERT(num_heads > 0);

        if (type == MONS_LERNAEAN_HYDRA || ends_with(mname, "Lernaean hydra"))
            ss << jnumber_for_hydra_heads(num_heads) << "の首を持つ";
        else
            ss << jnumber_for_hydra_heads(num_heads) << jtrans("-headed ");
    }

    if (mons_class_is_chimeric(type))
    {
        ss << jtrans("chimera");
        monsterentry *me = nullptr;
        if (u.ghost.acting_part != MONS_0
            && (me = get_monster_data(u.ghost.acting_part)))
        {
            // Specify an acting head
            ss << "に生えた" << jtrans(me->name) << "の頭";
        }
        else
            // Suffix parts in brackets
            // XXX: Should have a desc level that disables this
            ss << " (" << core << chimera_part_names() << ")";
    }

    if (!nocore)
        ss << core;

    // Add suffixes.
    switch (type)
    {
    case MONS_ZOMBIE:
#if TAG_MAJOR_VERSION == 34
    case MONS_ZOMBIE_SMALL:
    case MONS_ZOMBIE_LARGE:
#endif
        if (!is(MB_NAME_ZOMBIE))
            ss << (nocore ? "" : "の") << jtrans("zombie");
        break;
    case MONS_SKELETON:
#if TAG_MAJOR_VERSION == 34
    case MONS_SKELETON_SMALL:
    case MONS_SKELETON_LARGE:
#endif
        if (!is(MB_NAME_ZOMBIE))
            ss << (nocore ? "" : "の") << jtrans("skeleton");
        break;
    case MONS_SIMULACRUM:
#if TAG_MAJOR_VERSION == 34
    case MONS_SIMULACRUM_SMALL:
    case MONS_SIMULACRUM_LARGE:
#endif
        if (!is(MB_NAME_ZOMBIE))
            ss << (nocore ? "" : "の") << jtrans("simulacrum");
        break;
    case MONS_SPECTRAL_THING:
        if (nocore)
            ss << "spectre";
        break;
    case MONS_PILLAR_OF_SALT:
        ss << (nocore ? "" : "の") << jtrans("shaped pillar of salt");
        break;
    case MONS_BLOCK_OF_ICE:
        ss << (nocore ? "" : "の") << jtrans("shaped block of ice");
        break;
    default:
        break;
    }

    if (is(MB_SHAPESHIFTER))
    {
        // If momentarily in original form, don't display "shaped
        // shifter".
        if (mons_genus(type) != MONS_SHAPESHIFTER)
            ss << jtrans("shaped shifter");
    }

    string s;
    // only respect unqualified if nothing was added ("Sigmund" or "The spectral Sigmund")
    if (!is(MB_NAME_UNQUALIFIED) || has_proper_name() || ss.str() != core)
        s = _apply_adjusted_description_j(desc, ss.str());
    else
        s = ss.str();

    if (desc == DESC_ITS)
        s += "の";

    return s;
}

string monster_info::common_name_en(description_level_type desc) const
{
    const string core = _core_name_en();
    const bool nocore = mons_class_is_zombified(type)
                        && mons_is_unique(base_type)
                        && base_type == mons_species(base_type)
        || mons_class_is_chimeric(type);

    ostringstream ss;

    if (props.exists("helpless"))
        ss << "helpless ";

    if (is(MB_SUBMERGED))
        ss << "submerged ";

    if (type == MONS_SPECTRAL_THING && !is(MB_NAME_ZOMBIE) && !nocore)
        ss << "spectral ";

    if (is(MB_SPECTRALISED))
        ss << "ghostly ";

    if (type == MONS_SENSED && !mons_is_sensed(base_type))
        ss << "sensed ";

    if (type == MONS_BALLISTOMYCETE)
        ss << (is_active ? "active " : "");

    if ((mons_genus(type) == MONS_HYDRA || mons_genus(base_type) == MONS_HYDRA)
        && type != MONS_SENSED
        && type != MONS_BLOCK_OF_ICE
        && type != MONS_PILLAR_OF_SALT)
    {
        ASSERT(num_heads > 0);
        if (num_heads < 11)
            ss << number_in_words(num_heads);
        else
            ss << std::to_string(num_heads);

        ss << "-headed ";
    }

    if (mons_class_is_chimeric(type))
    {
        ss << "chimera";
        monsterentry *me = nullptr;
        if (u.ghost.acting_part != MONS_0
            && (me = get_monster_data(u.ghost.acting_part)))
        {
            // Specify an acting head
            ss << "'s " << me->name << " head";
        }
        else
            // Suffix parts in brackets
            // XXX: Should have a desc level that disables this
            ss << " (" << core << chimera_part_names() << ")";
    }

    if (!nocore)
        ss << core;

    // Add suffixes.
    switch (type)
    {
    case MONS_ZOMBIE:
#if TAG_MAJOR_VERSION == 34
    case MONS_ZOMBIE_SMALL:
    case MONS_ZOMBIE_LARGE:
#endif
        if (!is(MB_NAME_ZOMBIE))
            ss << (nocore ? "" : " ") << "zombie";
        break;
    case MONS_SKELETON:
#if TAG_MAJOR_VERSION == 34
    case MONS_SKELETON_SMALL:
    case MONS_SKELETON_LARGE:
#endif
        if (!is(MB_NAME_ZOMBIE))
            ss << (nocore ? "" : " ") << "skeleton";
        break;
    case MONS_SIMULACRUM:
#if TAG_MAJOR_VERSION == 34
    case MONS_SIMULACRUM_SMALL:
    case MONS_SIMULACRUM_LARGE:
#endif
        if (!is(MB_NAME_ZOMBIE))
            ss << (nocore ? "" : " ") << "simulacrum";
        break;
    case MONS_SPECTRAL_THING:
        if (nocore)
            ss << "spectre";
        break;
    case MONS_PILLAR_OF_SALT:
        ss << (nocore ? "" : " ") << "shaped pillar of salt";
        break;
    case MONS_BLOCK_OF_ICE:
        ss << (nocore ? "" : " ") << "shaped block of ice";
        break;
    default:
        break;
    }

    if (is(MB_SHAPESHIFTER))
    {
        // If momentarily in original form, don't display "shaped
        // shifter".
        if (mons_genus(type) != MONS_SHAPESHIFTER)
            ss << " shaped shifter";
    }

    string s;
    // only respect unqualified if nothing was added ("Sigmund" or "The spectral Sigmund")
    if (!is(MB_NAME_UNQUALIFIED) || has_proper_name() || ss.str() != core)
        s = _apply_adjusted_description(desc, ss.str());
    else
        s = ss.str();

    if (desc == DESC_ITS)
        s = apostrophise(s);

    return s;
}

bool monster_info::has_proper_name() const
{
    return !mname.empty() && !mons_is_ghost_demon(type)
            && !is(MB_NAME_REPLACE) && !is(MB_NAME_ADJECTIVE) && !is(MB_NAME_SUFFIX);
}

string monster_info::proper_name(description_level_type desc) const
{
    if (has_proper_name())
    {
        if (desc == DESC_ITS)
            return mname + "の";
        else
            return mname;
    }
    else
        return common_name(desc);
}

string monster_info::proper_name_en(description_level_type desc) const
{
    if (has_proper_name())
    {
        if (desc == DESC_ITS)
            return apostrophise(mname);
        else
            return mname;
    }
    else
        return common_name_en(desc);
}

string monster_info::full_name(description_level_type desc, bool use_comma) const
{
    if (desc == DESC_NONE)
        return "";

    if (has_proper_name())
    {
        string bra = "『", ket = "』";
        string stripped_mname = replace_all(mname, ket, "");
        string::size_type found;

        if ((found = mname.find(bra, 0)) != string::npos)
        {
            stripped_mname.replace(0, found + bra.length(), "");
            bra = "の" + bra;
        }

        string s = common_name() + bra + stripped_mname + ket;

        if (desc == DESC_ITS)
            s += "の";
        return s;
    }
    else
        return common_name(desc);
}

string monster_info::full_name_en(description_level_type desc, bool use_comma) const
{
    if (desc == DESC_NONE)
        return "";

    if (has_proper_name())
    {
        string s = mname + (use_comma ? ", the " : " the ") + common_name_en();
        if (desc == DESC_ITS)
            s = apostrophise(s);
        return s;
    }
    else
        return common_name_en(desc);
}

// Needed because gcc 4.3 sort does not like comparison functions that take
// more than 2 arguments.
bool monster_info::less_than_wrapper(const monster_info& m1,
                                     const monster_info& m2)
{
    return monster_info::less_than(m1, m2, true);
}

// Sort monsters by (in that order):    attitude, difficulty, type, brand
bool monster_info::less_than(const monster_info& m1, const monster_info& m2,
                             bool zombified, bool fullname)
{
    if (m1.attitude < m2.attitude)
        return true;
    else if (m1.attitude > m2.attitude)
        return false;

    // Force plain but different coloured draconians to be treated like the
    // same sub-type.
    if (!zombified
        && mons_is_base_draconian(m1.type)
        && mons_is_base_draconian(m2.type))
    {
        return false;
    }

    // Treat base demonspawn identically, as with draconians.
    if (!zombified && m1.type >= MONS_FIRST_BASE_DEMONSPAWN
        && m1.type <= MONS_LAST_BASE_DEMONSPAWN
        && m2.type >= MONS_FIRST_BASE_DEMONSPAWN
        && m2.type <= MONS_LAST_BASE_DEMONSPAWN)
    {
        return false;
    }

    int diff_delta = mons_avg_hp(m1.type) - mons_avg_hp(m2.type);

    // By descending difficulty
    if (diff_delta > 0)
        return true;
    else if (diff_delta < 0)
        return false;

    if (m1.type < m2.type)
        return true;
    else if (m1.type > m2.type)
        return false;

    // Never distinguish between dancing weapons.
    // The above checks guarantee that *both* monsters are of this type.
    if (m1.type == MONS_DANCING_WEAPON)
        return false;

    if (m1.type == MONS_SLIME_CREATURE)
        return m1.slime_size > m2.slime_size;

    if (m1.type == MONS_BALLISTOMYCETE)
        return m1.is_active && !m2.is_active;

    // Shifters after real monsters of the same type.
    if (m1.is(MB_SHAPESHIFTER) != m2.is(MB_SHAPESHIFTER))
        return m2.is(MB_SHAPESHIFTER);

    // Spectralised after the still-living. There's not terribly much
    // difference, but this keeps us from combining them in the monster
    // list so they all appear to be spectralised.
    if (m1.is(MB_SPECTRALISED) != m2.is(MB_SPECTRALISED))
        return m2.is(MB_SPECTRALISED);

    if (zombified)
    {
        if (mons_class_is_zombified(m1.type))
        {
            // Because of the type checks above, if one of the two is zombified, so
            // is the other, and of the same type.
            if (m1.base_type < m2.base_type)
                return true;
            else if (m1.base_type > m2.base_type)
                return false;
        }

        if (m1.type == MONS_CHIMERA)
        {
            for (int part = 1; part <= 3; part++)
            {
                const monster_type p1 = get_chimera_part(&m1, part);
                const monster_type p2 = get_chimera_part(&m2, part);

                if (p1 < p2)
                    return true;
                else if (p1 > p2)
                    return false;
            }
        }

        // Both monsters are hydras or hydra zombies, sort by number of heads.
        if (mons_genus(m1.type) == MONS_HYDRA || mons_genus(m1.base_type) == MONS_HYDRA)
        {
            if (m1.num_heads > m2.num_heads)
                return true;
            else if (m1.num_heads < m2.num_heads)
                return false;
        }
    }

    if (fullname || mons_is_pghost(m1.type))
        return m1.mname < m2.mname;

#if 0 // for now, sort mb together.
    // By descending mb, so no mb sorts to the end
    if (m1.mb > m2.mb)
        return true;
    else if (m1.mb < m2.mb)
        return false;
#endif

    return false;
}

static string _verbose_info0(const monster_info& mi)
{
    if (mi.is(MB_BERSERK))
        return "バーサーク";
    if (mi.is(MB_INSANE))
        return "狂気";
    if (mi.is(MB_FRENZIED))
        return "狂乱化";
    if (mi.is(MB_ROUSED))
        return "興奮";
    if (mi.is(MB_INNER_FLAME))
        return "内炎";
    if (mi.is(MB_DUMB))
        return "茫然自失";
    if (mi.is(MB_PARALYSED))
        return "麻痺";
    if (mi.is(MB_CAUGHT))
        return "捕縛";
    if (mi.is(MB_WEBBED))
        return "蜘蛛の巣";
    if (mi.is(MB_PETRIFIED))
        return "石化";
    if (mi.is(MB_PETRIFYING))
        return "石化中";
    if (mi.is(MB_MAD))
        return "憤怒";
    if (mi.is(MB_CONFUSED))
        return "混乱";
    if (mi.is(MB_FLEEING))
        return "逃走中";
    if (mi.is(MB_DORMANT))
        return "休息中";
    if (mi.is(MB_SLEEPING))
        return "睡眠中";
    if (mi.is(MB_UNAWARE))
        return "未発見";
    if (mi.is(MB_WITHDRAWN))
        return "待避中";
    if (mi.is(MB_DAZED))
        return "眩暈";
    if (mi.is(MB_MUTE))
        return "静寂";
    if (mi.is(MB_BLIND))
        return "盲目";
    // avoid jelly (wandering) (fellow slime)
    if (mi.is(MB_WANDERING) && mi.attitude != ATT_STRICT_NEUTRAL)
        return "放浪";
    if (mi.is(MB_BURNING))
        return "炎上";
    if (mi.is(MB_ROTTING))
        return "腐敗";
    if (mi.is(MB_BLEEDING))
        return "出血";
    if (mi.is(MB_INVISIBLE))
        return "透明";

    return "";
}

static string _verbose_info(const monster_info& mi)
{
    string inf = _verbose_info0(mi);
    if (!inf.empty())
        inf = " (" + inf + ")";
    return inf;
}

string monster_info::pluralised_name(bool fullname) const
{
    // Don't pluralise uniques, ever.  Multiple copies of the same unique
    // are unlikely in the dungeon currently, but quite common in the
    // arena.  This prevens "4 Gra", etc. {due}
    // Unless it's Mara, who summons illusions of himself.
    if (mons_is_unique(type) && type != MONS_MARA)
        return common_name();
    else if (mons_genus(type) == MONS_DRACONIAN)
        return pluralise(mons_type_name(MONS_DRACONIAN, DESC_PLAIN));
    else if (mons_genus(type) == MONS_DEMONSPAWN)
        return pluralise(mons_type_name(MONS_DEMONSPAWN, DESC_PLAIN));
    else if (type == MONS_UGLY_THING || type == MONS_VERY_UGLY_THING
             || type == MONS_DANCING_WEAPON || !fullname)
    {
        return pluralise(mons_type_name(type, DESC_PLAIN));
    }
    else
        return pluralise(common_name());
}

enum _monster_list_colour_type
{
    _MLC_FRIENDLY, _MLC_NEUTRAL, _MLC_GOOD_NEUTRAL, _MLC_STRICT_NEUTRAL,
    _MLC_TRIVIAL, _MLC_EASY, _MLC_TOUGH, _MLC_NASTY,
    _NUM_MLC
};

static const char * const _monster_list_colour_names[_NUM_MLC] =
{
    "friendly", "neutral", "good_neutral", "strict_neutral",
    "trivial", "easy", "tough", "nasty"
};

static int _monster_list_colours[_NUM_MLC] =
{
    GREEN, BROWN, BROWN, BROWN,
    DARKGREY, LIGHTGREY, YELLOW, LIGHTRED,
};

bool set_monster_list_colour(string key, int colour)
{
    for (int i = 0; i < _NUM_MLC; ++i)
    {
        if (key == _monster_list_colour_names[i])
        {
            _monster_list_colours[i] = colour;
            return true;
        }
    }
    return false;
}

void clear_monster_list_colours()
{
    for (int i = 0; i < _NUM_MLC; ++i)
        _monster_list_colours[i] = -1;
}

void monster_info::to_string(int count, string& desc, int& desc_colour,
                             bool fullname, const char *adj) const
{
    ostringstream out;
    _monster_list_colour_type colour_type = _NUM_MLC;

    string full = jtrans(full_name());

    if (adj && starts_with(full, "the "))
        full.erase(0, 4);

    // TODO: this should be done in a much cleaner way, with code to
    // merge multiple monster_infos into a single common structure
    if (count != 1)
        out << count << "体の";
    if (adj)
        out << tagged_jtrans("[adj]", adj);
    out << full;

#ifdef DEBUG_DIAGNOSTICS
    out << " av" << mons_avg_hp(type);
#endif

    if (count == 1)
       out << _verbose_info(*this);

    // Friendliness
    switch (attitude)
    {
    case ATT_FRIENDLY:
        //out << " (friendly)";
        colour_type = _MLC_FRIENDLY;
        break;
    case ATT_GOOD_NEUTRAL:
        //out << " (neutral)";
        colour_type = _MLC_GOOD_NEUTRAL;
        break;
    case ATT_NEUTRAL:
        //out << " (neutral)";
        colour_type = _MLC_NEUTRAL;
        break;
    case ATT_STRICT_NEUTRAL:
        out << " " << jtrans(" (fellow slime)");
        colour_type = _MLC_STRICT_NEUTRAL;
        break;
    case ATT_HOSTILE:
        // out << " (hostile)";
        switch (threat)
        {
        case MTHRT_TRIVIAL: colour_type = _MLC_TRIVIAL; break;
        case MTHRT_EASY:    colour_type = _MLC_EASY;    break;
        case MTHRT_TOUGH:   colour_type = _MLC_TOUGH;   break;
        case MTHRT_NASTY:   colour_type = _MLC_NASTY;   break;
        default:;
        }
        break;
    }

    if (count == 1 && is(MB_EVIL_ATTACK))
        desc_colour = Options.evil_colour;
    else if (colour_type < _NUM_MLC)
        desc_colour = _monster_list_colours[colour_type];

    // We still need something, or we'd get the last entry's colour.
    if (desc_colour < 0)
        desc_colour = LIGHTGREY;

    desc = out.str();
}

vector<string> monster_info::attributes() const
{
    vector<string> v;

    if (is(MB_BERSERK))
        v.emplace_back("バーサーク中");
    if (is(MB_HASTED) || is(MB_BERSERK))
    {
        if (!is(MB_SLOWED))
            v.emplace_back("加速中");
        else
            v.emplace_back("加減速中");
    }
    else if (is(MB_SLOWED))
        v.emplace_back("減速中");
    if (is(MB_STRONG) || is(MB_BERSERK))
        v.emplace_back("unusually strong");

    if (is(MB_POISONED))
        v.emplace_back("poisoned");
    if (is(MB_SICK))
        v.emplace_back("sick");
    if (is(MB_ROTTING))
        v.emplace_back("rotting away"); //jmf: "covered in sores"?
    if (is(MB_GLOWING))
        v.emplace_back("softly glowing");
    if (is(MB_INSANE))
        v.emplace_back("frenzied and insane");
    if (is(MB_FRENZIED))
        v.emplace_back("consumed by blood-lust");
    if (is(MB_ROUSED))
        v.emplace_back("inspired to greatness");
    if (is(MB_CONFUSED))
        v.emplace_back("bewildered and confused");
    if (is(MB_INVISIBLE))
        v.emplace_back("slightly transparent");
    if (is(MB_CHARMED))
        v.emplace_back("in your thrall");
    if (is(MB_BURNING))
        v.emplace_back("covered in liquid flames");
    if (is(MB_CAUGHT))
        v.emplace_back("entangled in a net");
    if (is(MB_WEBBED))
        v.emplace_back("entangled in a web");
    if (is(MB_PETRIFIED))
        v.emplace_back("petrified");
    if (is(MB_PETRIFYING))
        v.emplace_back("slowly petrifying");
    if (is(MB_VULN_MAGIC))
        v.emplace_back("susceptible to magic");
    if (is(MB_SWIFT))
        v.emplace_back("covering ground quickly");
    if (is(MB_SILENCING))
        v.emplace_back("radiating silence");
    if (is(MB_PARALYSED))
        v.emplace_back("paralysed");
    if (is(MB_BLEEDING))
        v.emplace_back("bleeding");
    if (is(MB_REPEL_MSL))
        v.emplace_back("repelling missiles");
    if (is(MB_DEFLECT_MSL))
        v.emplace_back("deflecting missiles");
    if (is(MB_FEAR_INSPIRING))
        v.emplace_back("inspiring fear");
    if (is(MB_BREATH_WEAPON))
    {
        v.emplace_back("catching its breath");
    }
    if (is(MB_WITHDRAWN))
    {
        v.emplace_back("regenerating health quickly");
        v.emplace_back("protected by its shell");
    }
    if (is(MB_DAZED))
        v.emplace_back("dazed");
    if (is(MB_MUTE))
        v.emplace_back("mute");
    if (is(MB_BLIND))
        v.emplace_back("blind");
    if (is(MB_DUMB))
        v.emplace_back("stupefied");
    if (is(MB_MAD))
        v.emplace_back("lost in madness");
    if (is(MB_DEATHS_DOOR))
        v.emplace_back("standing in death's doorway");
    if (is(MB_REGENERATION))
        v.emplace_back("regenerating");
    if (is(MB_ROLLING))
        v.emplace_back("rolling");
    if (is(MB_STONESKIN))
        v.emplace_back("stone skin");
    if (is(MB_OZOCUBUS_ARMOUR))
        v.emplace_back("covered in an icy film");
    if (is(MB_WRETCHED))
        v.emplace_back("misshapen and mutated");
    if (is(MB_WORD_OF_RECALL))
        v.emplace_back("chanting recall");
    if (is(MB_INJURY_BOND))
        v.emplace_back("sheltered from injuries");
    if (is(MB_WATER_HOLD))
        v.emplace_back("engulfed in water");
    if (is(MB_WATER_HOLD_DROWN))
    {
        v.emplace_back("engulfed in water");
        v.emplace_back("unable to breathe");
    }
    if (is(MB_FLAYED))
        v.emplace_back("covered in terrible wounds");
    if (is(MB_WEAK))
        v.emplace_back("弱体化中");
    if (is(MB_DIMENSION_ANCHOR))
        v.emplace_back("unable to translocate");
    if (is(MB_CONTROL_WINDS))
        v.emplace_back("controlling the winds");
    if (is(MB_TOXIC_RADIANCE))
        v.emplace_back("radiating toxic energy");
    if (is(MB_GRASPING_ROOTS))
        v.emplace_back("movement impaired by roots");
    if (is(MB_FIRE_VULN))
        v.emplace_back("more vulnerable to fire");
    if (is(MB_TORNADO))
        v.emplace_back("surrounded by raging winds");
    if (is(MB_TORNADO_COOLDOWN))
        v.emplace_back("surrounded by restless winds");
    if (is(MB_BARBS))
        v.emplace_back("skewered by manticore barbs");
    if (is(MB_POISON_VULN))
        v.emplace_back("more vulnerable to poison");
    if (is(MB_ICEMAIL))
        v.emplace_back("surrounded by an icy envelope");
    if (is(MB_AGILE))
        v.emplace_back("unusually agile");
    if (is(MB_FROZEN))
        v.emplace_back("encased in ice");
    if (is(MB_BLACK_MARK))
        v.emplace_back("absorbing vital energies");
    if (is(MB_SAP_MAGIC))
        v.emplace_back("magic-sapped");
    if (is(MB_SHROUD))
        v.emplace_back("shrouded");
    if (is(MB_CORROSION))
        v.emplace_back("covered in acid");
    if (is(MB_SLOW_MOVEMENT))
        v.emplace_back("covering ground slowly");
    if (is(MB_LIGHTLY_DRAINED))
        v.emplace_back("lightly drained");
    if (is(MB_HEAVILY_DRAINED))
        v.emplace_back("heavily drained");
    if (is(MB_CONDENSATION_SHIELD))
        v.emplace_back("protected by a disc of dense vapour");
    if (is(MB_RESISTANCE))
        v.emplace_back("unusually resistant");
    if (is(MB_HEXED))
        v.emplace_back("control wrested from you");
    if (is(MB_BONE_ARMOUR))
        v.emplace_back("corpse armoured");
    return v;
}

string monster_info::wounds_description_sentence() const
{
    const string wounds = wounds_description();
    if (wounds.empty())
        return "";
    else
        return string(pronoun(PRONOUN_SUBJECTIVE)) + "は" + wounds + "。";
}

string monster_info::wounds_description(bool use_colour) const
{
    if (dam == MDAM_OKAY)
        return "";

    string desc = get_damage_level_string(holi, dam);
    if (use_colour)
    {
        const int col = channel_to_colour(MSGCH_MONSTER_DAMAGE, dam);
        desc = colour_string(desc, col);
    }
    desc = replace_all(desc, "傷ついた", "傷ついている");
    desc = replace_all(desc, "傷つかなかった", "無傷");
    desc = replace_all(desc, "死にかけている", "死にかけ");

    return desc;
}

string monster_info::constriction_description() const
{
    string cinfo = "";
    bool bymsg = false;

    if (constrictor_name != "")
    {
        cinfo += constrictor_name;
        bymsg = true;
    }

    string constricting = comma_separated_line(constricting_name.begin(),
                                               constricting_name.end(), ", ");

    if (constricting != "")
    {
        if (bymsg)
            cinfo += ", ";
        cinfo += constricting;
    }
    return cinfo;
}

int monster_info::randarts(artefact_prop_type ra_prop) const
{
    int ret = 0;

    if (itemuse() >= MONUSE_STARTING_EQUIPMENT)
    {
        item_def* weapon = inv[MSLOT_WEAPON].get();
        item_def* second = inv[MSLOT_ALT_WEAPON].get(); // Two-headed ogres, etc.
        item_def* armour = inv[MSLOT_ARMOUR].get();
        item_def* shield = inv[MSLOT_SHIELD].get();
        item_def* ring   = inv[MSLOT_JEWELLERY].get();

        if (weapon && weapon->base_type == OBJ_WEAPONS && is_artefact(*weapon))
            ret += artefact_property(*weapon, ra_prop);

        if (second && second->base_type == OBJ_WEAPONS && is_artefact(*second))
            ret += artefact_property(*second, ra_prop);

        if (armour && armour->base_type == OBJ_ARMOUR && is_artefact(*armour))
            ret += artefact_property(*armour, ra_prop);

        if (shield && shield->base_type == OBJ_ARMOUR && is_artefact(*shield))
            ret += artefact_property(*shield, ra_prop);

        if (ring && ring->base_type == OBJ_JEWELLERY && is_artefact(*ring))
            ret += artefact_property(*ring, ra_prop);
    }

    return ret;
}

bool monster_info::can_see_invisible() const
{
    // This should match the logic in monster::can_see_invisible().
    if (mons_is_ghost_demon(type))
        return u.ghost.can_sinv;

    return mons_class_flag(type, M_SEE_INVIS)
           || mons_is_demonspawn(type)
              && mons_class_flag(draco_or_demonspawn_subspecies(), M_SEE_INVIS);
}

int monster_info::res_magic() const
{
    int mr = (get_monster_data(type))->resist_magic;
    if (mr == MAG_IMMUNE)
        return MAG_IMMUNE;

    const int hd = mons_is_pghost(type) ? ghost_rank_to_level(u.ghost.xl_rank)
                                        : mons_class_hit_dice(type);

    // Negative values get multiplied with monster hit dice.
    if (mr < 0)
        mr = hd * (-mr) * 4 / 3;

    // Randarts
    mr += 40 * randarts(ARTP_MAGIC);

    // ego armour resistance
    if (inv[MSLOT_ARMOUR].get()
        && get_armour_ego_type(*inv[MSLOT_ARMOUR]) == SPARM_MAGIC_RESISTANCE)
    {
        mr += 40;
    }

    if (inv[MSLOT_SHIELD].get()
        && get_armour_ego_type(*inv[MSLOT_SHIELD]) == SPARM_MAGIC_RESISTANCE)
    {
        mr += 40;
    }

    item_def *jewellery = inv[MSLOT_JEWELLERY].get();

    if (jewellery
        && jewellery->is_type(OBJ_JEWELLERY, RING_PROTECTION_FROM_MAGIC))
    {
        mr += 40;
    }

    if (is(MB_VULN_MAGIC))
        mr /= 2;

    return mr;
}

string monster_info::speed_description() const
{
    if (mbase_speed < 7)
        return "very slow";
    else if (mbase_speed < 10)
        return "slow";
    else if (mbase_speed > 20)
        return "extremely fast";
    else if (mbase_speed > 15)
        return "very fast";
    else if (mbase_speed > 10)
        return "fast";

    // This only ever displays through Lua.
    return "normal";
}

bool monster_info::wields_two_weapons() const
{
    return is(MB_TWO_WEAPONS);
}

bool monster_info::can_regenerate() const
{
    return !is(MB_NO_REGEN);
}

reach_type monster_info::reach_range() const
{
    const monsterentry *e = get_monster_data(mons_class_is_zombified(type)
                                             ? base_type : type);
    ASSERT(e);

    reach_type range = e->attack[0].flavour == AF_REACH
                       || e->attack[0].type == AT_REACH_STING
                          ? REACH_TWO : REACH_NONE;

    const item_def *weapon = inv[MSLOT_WEAPON].get();
    if (weapon)
        range = max(range, weapon_reach(*weapon));

    return range;
}

size_type monster_info::body_size() const
{
    // Using base_type to get the right size for zombies, skeletons and such.
    // For normal monsters, base_type is set to type in the constructor.
    const monsterentry *e = get_monster_data(base_type);
    size_type ret = (e ? e->size : SIZE_MEDIUM);

    // Slime creature size is increased by the number merged.
    if (type == MONS_SLIME_CREATURE)
    {
        if (slime_size == 2)
            ret = SIZE_MEDIUM;
        else if (slime_size == 3)
            ret = SIZE_LARGE;
        else if (slime_size == 4)
            ret = SIZE_BIG;
        else if (slime_size == 5)
            ret = SIZE_GIANT;
    }

    return ret;
}

bool monster_info::cannot_move() const
{
    return is(MB_PARALYSED) || is(MB_PETRIFIED);
}

bool monster_info::airborne() const
{
    return fly == FL_LEVITATE || (fly == FL_WINGED && !cannot_move());
}

bool monster_info::ground_level() const
{
    return !airborne() && !is(MB_CLINGING);
}

// Only checks for spells from preset monster spellbooks.
// Use monster.h's has_spells for knowing a monster has spells
bool monster_info::has_spells() const
{
    // Some monsters have a special book but may not have any spells anyways.
    if (props.exists("custom_spells"))
        return spells.size() > 0 && spells[0].spell != SPELL_NO_SPELL;

    // Almost all draconians have breath spells.
    if (mons_genus(draco_or_demonspawn_subspecies()) == MONS_DRACONIAN
        && draco_or_demonspawn_subspecies() != MONS_GREY_DRACONIAN
        && draco_or_demonspawn_subspecies() != MONS_DRACONIAN)
    {
        return true;
    }

    const vector<mon_spellbook_type> books = get_spellbooks(*this);

    // Random pan lords don't display their spells.
    if (books.size() == 0 || books[0] == MST_NO_SPELLS
        || type == MONS_PANDEMONIUM_LORD)
    {
        return false;
    }

    // Ghosts have a special book but may not have any spells anyways.
    if (books[0] == MST_GHOST)
        return spells.size() > 0;

    return true;
}

unsigned monster_info::colour(bool base_colour) const
{
    if (!base_colour && Options.mon_glyph_overrides.count(type)
        && Options.mon_glyph_overrides[type].col)
    {
        return Options.mon_glyph_overrides[type].col;
    }
    else if (_colour == COLOUR_INHERIT)
        return mons_class_colour(type);
    else
    {
        ASSERT_RANGE(_colour, 0, NUM_COLOURS);
        return _colour;
    }
}

void monster_info::set_colour(int col)
{
    ASSERT_RANGE(col, -1, NUM_COLOURS);
    _colour = col;
}

void get_monster_info(vector<monster_info>& mons)
{
    vector<monster* > visible;
    if (crawl_state.game_is_arena())
    {
        for (monster_iterator mi; mi; ++mi)
            visible.push_back(*mi);
    }
    else
        visible = get_nearby_monsters();

    for (monster *mon : visible)
    {
        if (!mons_class_flag(mon->type, M_NO_EXP_GAIN)
            || mon->is_child_tentacle()
            || mon->type == MONS_BALLISTOMYCETE
                && mon->ballisto_activity > 0)
        {
            mons.emplace_back(mon);
        }
    }
    sort(mons.begin(), mons.end(), monster_info::less_than_wrapper);
}

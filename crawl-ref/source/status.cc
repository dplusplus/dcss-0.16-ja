#include "AppHdr.h"

#include "status.h"
#include "duration-data.h"

#include "areas.h"
#include "branch.h"
#include "cloud.h"
#include "database.h"
#include "env.h"
#include "evoke.h"
#include "food.h"
#include "godabil.h"
#include "itemprop.h"
#include "mutation.h"
#include "options.h"
#include "player-stats.h"
#include "religion.h"
#include "spl-transloc.h"
#include "stringutil.h"
#include "transform.h"
#include "traps.h"

static int duration_index[NUM_DURATIONS];

void init_duration_index()
{
    COMPILE_CHECK(ARRAYSZ(duration_data) == NUM_DURATIONS);
    for (int i = 0; i < NUM_DURATIONS; ++i)
        duration_index[i] = -1;

    for (unsigned i = 0; i < ARRAYSZ(duration_data); ++i)
    {
        duration_type dur = duration_data[i].dur;
        ASSERT_RANGE(dur, 0, NUM_DURATIONS);
        // Catch redefinitions.
        ASSERT(duration_index[dur] == -1);
        duration_index[dur] = i;
    }
}

static const duration_def* _lookup_duration(duration_type dur)
{
    ASSERT_RANGE(dur, 0, NUM_DURATIONS);
    if (duration_index[dur] == -1)
        return nullptr;
    else
        return &duration_data[duration_index[dur]];
}

const char *duration_name(duration_type dur)
{
    return _lookup_duration(dur)->name();
}

bool duration_dispellable(duration_type dur)
{
    return _lookup_duration(dur)->duration_has_flag(D_DISPELLABLE);
}

static void _reset_status_info(status_info* inf)
{
    inf->light_colour = 0;
    inf->light_text = "";
    inf->short_text = "";
    inf->long_text = "";
}

static int _bad_ench_colour(int lvl, int orange, int red)
{
    if (lvl >= red)
        return RED;
    else if (lvl >= orange)
        return LIGHTRED;

    return YELLOW;
}

static int _dur_colour(int exp_colour, bool expiring)
{
    if (expiring)
        return exp_colour;
    else
    {
        switch (exp_colour)
        {
        case GREEN:
            return LIGHTGREEN;
        case BLUE:
            return LIGHTBLUE;
        case MAGENTA:
            return LIGHTMAGENTA;
        case LIGHTGREY:
            return WHITE;
        default:
            return exp_colour;
        }
    }
}

static void _mark_expiring(status_info* inf, bool expiring)
{
    if (expiring)
    {
        if (!inf->short_text.empty())
            inf->short_text = tagged_jtrans("[dur]", inf->short_text)
                            + jtrans(" (expiring)");
        if (!inf->long_text.empty())
            inf->long_text = jtrans("Expiring: ") + " " + inf->long_text;
    }
}

/**
 * Populate a status_info struct from the duration_data struct corresponding
 * to the given duration_type.
 *
 * @param[in] dur    The duration in question.
 * @param[out] inf   The status_info struct to be filled.
 * @return           Whether a duration_data struct was found.
 */
static bool _fill_inf_from_ddef(duration_type dur, status_info* inf)
{
    const duration_def* ddef = _lookup_duration(dur);
    if (!ddef)
        return false;

    inf->light_colour = ddef->light_colour;
    inf->light_text   = ddef->light_text;
    inf->short_text   = ddef->short_text;
    inf->long_text    = ddef->long_text;
    if (ddef->duration_has_flag(D_EXPIRES))
    {
        inf->light_colour = _dur_colour(inf->light_colour, dur_expiring(dur));
        _mark_expiring(inf, dur_expiring(dur));
    }

    return true;
}

static void _describe_airborne(status_info* inf);
static void _describe_glow(status_info* inf);
static void _describe_hunger(status_info* inf);
static void _describe_regen(status_info* inf);
static void _describe_rotting(status_info* inf);
static void _describe_sickness(status_info* inf);
static void _describe_speed(status_info* inf);
static void _describe_poison(status_info* inf);
static void _describe_transform(status_info* inf);
static void _describe_stat_zero(status_info* inf, stat_type st);
static void _describe_terrain(status_info* inf);
static void _describe_missiles(status_info* inf);
static void _describe_invisible(status_info* inf);

bool fill_status_info(int status, status_info* inf)
{
    _reset_status_info(inf);

    bool found = false;

    // Sort out inactive durations, and fill in data from duration_data
    // for the simple durations.
    if (status >= 0 && status < NUM_DURATIONS)
    {
        duration_type dur = static_cast<duration_type>(status);

        if (!you.duration[dur])
            return false;

        found = _fill_inf_from_ddef(dur, inf);
    }

    // Now treat special status types and durations, possibly
    // completing or overriding the defaults set above.
    switch (status)
    {

    case DUR_CORROSION:
        inf->light_text = make_stringf(jtransc("Corr (%d)"),
                          (-3 * you.props["corrosion_amount"].get_int()));
        break;

    case DUR_CONTROL_TELEPORT:
        if (!allow_control_teleport(true))
            inf->light_colour = DARKGREY;
        break;

    case DUR_NO_POTIONS:
        if (you_foodless(true))
            inf->light_colour = DARKGREY;
        break;

    case DUR_SWIFTNESS:
        if (you.attribute[ATTR_SWIFTNESS] < 0)
        {
            inf->light_text   = "-Swift";
            inf->light_colour = RED;
            inf->short_text   = "sluggish";
            inf->long_text    = "You are moving sluggishly";
        }
        if (you.in_liquid())
            inf->light_colour = DARKGREY;
        break;

    case STATUS_AIRBORNE:
        _describe_airborne(inf);
        break;

    case STATUS_BEHELD:
        if (you.beheld())
        {
            inf->light_colour = RED;
            inf->light_text   = "Mesm";
            inf->short_text   = "mesmerised";
            inf->long_text    = "You are mesmerised.";
        }
        break;

    case STATUS_CONTAMINATION:
        _describe_glow(inf);
        break;

    case STATUS_BACKLIT:
        if (you.backlit())
        {
            inf->short_text = "glowing";
            inf->long_text  = "You are glowing.";
        }
        break;

    case STATUS_UMBRA:
        if (you.umbra())
        {
            inf->short_text   = "wreathed by umbra";
            inf->long_text    = "You are wreathed by an umbra.";
        }
        break;

    case STATUS_NET:
        if (you.attribute[ATTR_HELD])
        {
            inf->light_colour = RED;
            inf->light_text   = "Held";
            inf->short_text   = "held";
            inf->long_text    = make_stringf("You are %s.", held_status());
        }
        break;

    case STATUS_HUNGER:
        _describe_hunger(inf);
        break;

    case STATUS_REGENERATION:
        // DUR_REGENERATION + some vampire and non-healing stuff
        _describe_regen(inf);
        break;

    case STATUS_ROT:
        _describe_rotting(inf);
        break;

    case STATUS_SICK:
        _describe_sickness(inf);
        break;

    case STATUS_SPEED:
        _describe_speed(inf);
        break;

    case STATUS_LIQUEFIED:
    {
        if (you.liquefied_ground())
        {
            inf->light_colour = BROWN;
            inf->light_text   = "SlowM";
            inf->short_text   = "slowed movement";
            inf->long_text    = "Your movement is slowed on this liquid ground.";
        }
        break;
    }

    case STATUS_AUGMENTED:
    {
        int level = augmentation_amount();

        if (level > 0)
        {
            inf->light_colour = (level == 3) ? WHITE :
                                (level == 2) ? LIGHTBLUE
                                             : BLUE;

            inf->light_text = "Aug";
        }
        break;
    }

    case DUR_CONFUSING_TOUCH:
    {
        inf->long_text = jtrans("Your") + you.hand_name(true) + jtrans("are glowing red.");
        break;
    }

    case DUR_FIRE_SHIELD:
    {
        // Might be better to handle this with an extra virtual status.
        const bool exp = dur_expiring(DUR_FIRE_SHIELD);
        if (exp)
            inf->long_text += "継続中: ";
        inf->long_text += jtransln("You are surrounded by a ring of flames.\n");
        if (exp)
            inf->long_text += "継続中: ";
        inf->long_text += jtrans("You are immune to clouds of flame.");
        break;
    }

    case DUR_POISONING:
        _describe_poison(inf);
        break;

    case DUR_POWERED_BY_DEATH:
        if (handle_pbd_corpses() > 0)
        {
            inf->light_colour = LIGHTMAGENTA;
            inf->light_text   = "Regen+";
        }
        break;

    case STATUS_MISSILES:
        _describe_missiles(inf);
        break;

    case STATUS_INVISIBLE:
        _describe_invisible(inf);
        break;

    case STATUS_MANUAL:
    {
        string skills = manual_skill_names();
        if (!skills.empty())
        {
            inf->short_text = manual_skill_names(true) + "を習得中";
            inf->long_text = "あなたは" + jtrans(skills) + "を習得中だ。";
        }
        break;
    }

    case DUR_SURE_BLADE:
    {
        inf->light_colour = BLUE;
        inf->light_text   = "SBlade";
        inf->short_text   = "bonded with blade";
        string desc;
        if (you.duration[DUR_SURE_BLADE] > 15 * BASELINE_DELAY)
            desc = "strong ";
        else if (you.duration[DUR_SURE_BLADE] >  5 * BASELINE_DELAY)
            desc = "";
        else
            desc = "weak ";
        inf->long_text = "You have a " + desc + "bond with your blade.";
        break;
    }

    case DUR_TRANSFORMATION:
        _describe_transform(inf);
        break;

    case STATUS_STR_ZERO:
        _describe_stat_zero(inf, STAT_STR);
        break;
    case STATUS_INT_ZERO:
        _describe_stat_zero(inf, STAT_INT);
        break;
    case STATUS_DEX_ZERO:
        _describe_stat_zero(inf, STAT_DEX);
        break;

    case STATUS_FIREBALL:
        if (you.attribute[ATTR_DELAYED_FIREBALL])
        {
            inf->light_colour = LIGHTMAGENTA;
            inf->light_text   = "Fball";
            inf->short_text   = "delayed fireball";
            inf->long_text    = "You have a stored fireball ready to release.";
        }
        break;

    case STATUS_BONE_ARMOUR:
        if (you.attribute[ATTR_BONE_ARMOUR] > 0)
        {
            inf->short_text = "corpse armour";
            inf->long_text = "You are enveloped in carrion and bones.";
        }
        break;

    case STATUS_CONSTRICTED:
        if (you.is_constricted())
        {
            inf->light_colour = YELLOW;
            inf->light_text   = you.held == HELD_MONSTER ? "Held" : "Constr";
            inf->short_text   = you.held == HELD_MONSTER ? "held" : "constricted";
        }
        break;

    case STATUS_TERRAIN:
        _describe_terrain(inf);
        break;

    // Silenced by an external source.
    case STATUS_SILENCE:
        if (silenced(you.pos()) && !you.duration[DUR_SILENCE])
        {
            inf->light_colour = LIGHTRED;
            inf->light_text   = "Sil";
            inf->short_text   = "silenced";
            inf->long_text    = "You are silenced.";
        }
        break;

    case DUR_SONG_OF_SLAYING:
        inf->light_text = make_stringf(jtransc("Slay (%u)"),
                                       you.props["song_of_slaying_bonus"].get_int());
        break;

    case STATUS_NO_CTELE:
        if (!allow_control_teleport(true))
        {
            inf->light_colour = RED;
            inf->light_text = "-cTele";
        }
        break;

    case STATUS_BEOGH:
        if (env.level_state & LSTATE_BEOGH && can_convert_to_beogh())
        {
            inf->light_colour = WHITE;
            inf->light_text = "Beogh";
        }
        break;

    case STATUS_RECALL:
        if (you.attribute[ATTR_NEXT_RECALL_INDEX] > 0)
        {
            inf->light_colour = WHITE;
            inf->light_text   = "Recall";
            inf->short_text   = "recalling";
            inf->long_text    = "You are recalling your allies.";
        }
        break;

    case DUR_WATER_HOLD:
        inf->light_text   = "Engulf";
        if (you.res_water_drowning())
        {
            inf->short_text   = "engulfed";
            inf->long_text    = "You are engulfed in water.";
            if (you.can_swim())
                inf->light_colour = DARKGREY;
            else
                inf->light_colour = YELLOW;
        }
        else
        {
            inf->short_text   = "engulfed (cannot breathe)";
            inf->long_text    = "You are engulfed in water and unable to breathe.";
            inf->light_colour = RED;
        }
        break;

    case STATUS_DRAINED:
        if (you.attribute[ATTR_XP_DRAIN] > 250)
        {
            inf->light_colour = RED;
            inf->light_text   = "Drain";
            inf->short_text   = "very heavily drained";
            inf->long_text    = "Your life force is very heavily drained.";
        }
        else if (you.attribute[ATTR_XP_DRAIN] > 100)
        {
            inf->light_colour = LIGHTRED;
            inf->light_text   = "Drain";
            inf->short_text   = "heavily drained";
            inf->long_text    = "Your life force is heavily drained.";
        }
        else if (you.attribute[ATTR_XP_DRAIN])
        {
            inf->light_colour = YELLOW;
            inf->light_text   = "Drain";
            inf->short_text   = "drained";
            inf->long_text    = "Your life force is drained.";
        }
        break;

    case STATUS_RAY:
        if (you.attribute[ATTR_SEARING_RAY])
        {
            inf->light_colour = WHITE;
            inf->light_text   = "Ray";
        }
        break;

    case STATUS_DIG:
        if (you.digging)
        {
            inf->light_colour = WHITE;
            inf->light_text   = "Dig";
        }
        break;

    case STATUS_ELIXIR:
        if (you.duration[DUR_ELIXIR_HEALTH] || you.duration[DUR_ELIXIR_MAGIC])
        {
            if (you.duration[DUR_ELIXIR_HEALTH] && you.duration[DUR_ELIXIR_MAGIC])
                inf->light_colour = WHITE;
            else if (you.duration[DUR_ELIXIR_HEALTH])
                inf->light_colour = LIGHTGREEN;
            else
                inf->light_colour = LIGHTBLUE;
            inf->light_text   = "Elixir";
        }
        break;

    case STATUS_MAGIC_SAPPED:
        if (you.duration[DUR_MAGIC_SAPPED] > 50 * BASELINE_DELAY)
        {
            inf->light_colour = RED;
            inf->light_text   = "-Wiz";
            inf->short_text   = "extremely magic sapped";
            inf->long_text    = "Your control over your magic has "
                                "been greatly sapped.";
        }
        else if (you.duration[DUR_MAGIC_SAPPED] > 20 * BASELINE_DELAY)
        {
            inf->light_colour = LIGHTRED;
            inf->light_text   = "-Wiz";
            inf->short_text   = "very magic sapped";
            inf->long_text    = "Your control over your magic has "
                                "been significantly sapped.";
        }
        else if (you.duration[DUR_MAGIC_SAPPED])
        {
            inf->light_colour = YELLOW;
            inf->light_text   = "-Wiz";
            inf->short_text   = "magic sapped";
            inf->long_text    = "Your control over your magic has "
                                "been sapped.";
        }
        break;

    case STATUS_BRIBE:
    {
        int bribe = 0;
        vector<const char *> places;
        for (int i = 0; i < NUM_BRANCHES; i++)
        {
            if (branch_bribe[i] > 0)
            {
                if (player_in_branch(static_cast<branch_type>(i)))
                    bribe = branch_bribe[i];

                places.push_back(branches[static_cast<branch_type>(i)]
                                 .longname);
            }
        }

        if (bribe > 0)
        {
            inf->light_colour = (bribe >= 2000) ? WHITE :
                                (bribe >= 1000) ? LIGHTBLUE
                                                : BLUE;

            inf->light_text = "Bribe";
            inf->short_text = make_stringf("[%s]を買収中",
                                           comma_separated_fn(places.begin(),
                                                              places.end(),
                                                              [] (const string &s) { return tagged_jtrans("[branch]", s); },
                                                              ", ", ", ").c_str());
            inf->long_text = "あなたは"
                             + to_separated_fn(places.begin(),
                                               places.end(),
                                               [] (const string &s) { return tagged_jtrans("[branch]", s); })
                             + "を買収している。";
        }
        break;
    }

    case DUR_HORROR:
    {
        const int horror = you.props[HORROR_PENALTY_KEY].get_int();
        inf->light_text = make_stringf(jtransc("Horr(%d)"), -1 * horror);
        if (horror >= HORROR_LVL_OVERWHELMING)
        {
            inf->light_colour = RED;
            inf->short_text   = "overwhelmed with horror";
            inf->long_text    = "Horror overwhelms you!";
        }
        else if (horror >= HORROR_LVL_EXTREME)
        {
            inf->light_colour = LIGHTRED;
            inf->short_text   = "extremely horrified";
            inf->long_text    = "You are extremely horrified!";
        }
        else if (horror)
        {
            inf->light_colour = YELLOW;
            inf->short_text   = "horrified";
            inf->long_text    = "You are horrified!";
        }
        break;
    }

    case STATUS_CLOUD:
    {
        cloud_type cloud = cloud_type_at(you.pos());
        if (Options.cloud_status && cloud != CLOUD_NONE)
        {
            inf->light_text = "Cloud";
            // TODO: make the colour based on the cloud's color; requires elemental
            // status lights, though.
            inf->light_colour =
                is_damaging_cloud(cloud, true, cloud_is_yours_at(you.pos())) ? LIGHTRED : DARKGREY;
        }
        break;
    }

    case DUR_CLEAVE:
    {
        const item_def* weapon = you.weapon();

        if (weapon && item_attack_skill(*weapon) == SK_AXES)
            inf->light_colour = DARKGREY;

        break;
    }

    default:
        if (!found)
        {
            inf->light_colour = RED;
            inf->light_text   = "Missing";
            inf->short_text   = "missing status";
            inf->long_text    = "Missing status description.";
            return false;
        }
        else
            break;
    }
    return true;
}

static void _describe_hunger(status_info* inf)
{
    const bool vamp = (you.species == SP_VAMPIRE);

    switch (you.hunger_state)
    {
    case HS_ENGORGED:
        inf->light_colour = LIGHTGREEN;
        inf->light_text   = (vamp ? "生者" : "腹一杯");
        break;
    case HS_VERY_FULL:
        inf->light_colour = GREEN;
        inf->light_text   = "とても満腹";
        break;
    case HS_FULL:
        inf->light_colour = GREEN;
        inf->light_text   = "満腹";
        break;
    case HS_HUNGRY:
        inf->light_colour = YELLOW;
        inf->light_text   = (vamp ? "乾いている" : "空腹");
        break;
    case HS_VERY_HUNGRY:
        inf->light_colour = YELLOW;
        inf->light_text   = (vamp ? "とても乾いている" : "とても空腹");
        break;
    case HS_NEAR_STARVING:
        inf->light_colour = YELLOW;
        inf->light_text   = (vamp ? "ほとんど血液がない" : "餓死が近い");
        break;
    case HS_STARVING:
        inf->light_colour = RED;
        inf->light_text   = (vamp ? "血液がない" : "餓死しかけ");
        inf->short_text   = (vamp ? "血液がない" : "餓死しかけ");
        break;
    case HS_SATIATED: // no status light
    default:
        break;
    }
}

static void _describe_glow(status_info* inf)
{
    const int signed_cont = get_contamination_level();
    if (signed_cont <= 0)
        return;

    const unsigned int cont = signed_cont; // so we don't get compiler warnings
    inf->light_colour = DARKGREY;
    if (cont > 1)
        inf->light_colour = _bad_ench_colour(cont, 3, 4);
#if TAG_MAJOR_VERSION == 34
    if (cont > 1 || you.species != SP_DJINNI)
#endif
    inf->light_text = "Contam";

    /// Mappings from contamination levels to descriptions.
    static const string contam_adjectives[] =
    {
        "",
        "very slightly ",
        "",
        "heavily ",
        "very heavily ",
        "very very heavily ", // this is silly but no one will ever see it
        "impossibly ",        // (likewise)
    };
    ASSERT(cont >= 0);

    const int adj_i = min((size_t) cont, ARRAYSZ(contam_adjectives) - 1);
    inf->short_text = contam_adjectives[adj_i] + "contaminated";
    inf->long_text = describe_contamination(cont);
}

static void _describe_regen(status_info* inf)
{
    const bool regen = (you.duration[DUR_REGENERATION] > 0
                        || you.duration[DUR_TROGS_HAND] > 0);
    const bool no_heal =
            (you.species == SP_VAMPIRE && you.hunger_state == HS_STARVING)
            || (player_mutation_level(MUT_SLOW_HEALING) == 3);
    // Does vampire hunger level affect regeneration rate significantly?
    const bool vampmod = !no_heal && !regen && you.species == SP_VAMPIRE
                         && you.hunger_state != HS_SATIATED;

    if (regen)
    {
        if (you.duration[DUR_REGENERATION] > you.duration[DUR_TROGS_HAND])
            inf->light_colour = _dur_colour(BLUE, dur_expiring(DUR_REGENERATION));
        else
            inf->light_colour = _dur_colour(BLUE, dur_expiring(DUR_TROGS_HAND));
        inf->light_text   = "Regen";
        if (you.duration[DUR_TROGS_HAND])
            inf->light_text += " MR++";
        else if (no_heal)
            inf->light_colour = DARKGREY;
    }

    if ((you.disease && !regen) || no_heal)
       inf->short_text = "non-regenerating";
    else if (regen)
    {
        if (you.disease)
        {
            inf->short_text = "recuperating";
            inf->long_text  = "You are recuperating from your illness.";
        }
        else
        {
            inf->short_text = "regenerating";
            inf->long_text  = "You are regenerating.";
        }
        _mark_expiring(inf, dur_expiring(DUR_REGENERATION));
    }
    else if (vampmod)
    {
        if (you.disease)
            inf->short_text = "recuperating";
        else
            inf->short_text = "regenerating";

        if (you.hunger_state < HS_SATIATED)
            inf->short_text += " slowly";
        else if (you.hunger_state < HS_ENGORGED)
            inf->short_text += " quickly";
        else
            inf->short_text += " very quickly";
    }
}

static void _describe_poison(status_info* inf)
{
    int pois_perc = (you.hp <= 0) ? 100
                                  : ((you.hp - max(0, poison_survival())) * 100 / you.hp);
    inf->light_colour = (player_res_poison(false) >= 3
                         ? DARKGREY : _bad_ench_colour(pois_perc, 35, 100));
    inf->light_text   = "Pois";
    const string adj =
         (pois_perc >= 100) ? "lethally" :
         (pois_perc > 65)   ? "seriously" :
         (pois_perc > 35)   ? "quite"
                            : "mildly";
    inf->short_text   = jtrans(adj + " poisoned")
        + make_stringf(" (%d -> %d)", you.hp, poison_survival());
    inf->long_text    = "あなたは" + inf->short_text + "。";
}

static void _describe_speed(status_info* inf)
{
    if (you.duration[DUR_SLOW] && you.duration[DUR_HASTE])
    {
        inf->light_colour = MAGENTA;
        inf->light_text   = "Fast+Slow";
        inf->short_text   = "hasted and slowed";
        inf->long_text = "You are under both slowing and hasting effects.";
    }
    else if (you.duration[DUR_SLOW])
    {
        inf->light_colour = RED;
        inf->light_text   = "Slow";
        inf->short_text   = "slowed";
        inf->long_text    = "You are slowed.";
    }
    else if (you.duration[DUR_HASTE])
    {
        inf->light_colour = _dur_colour(BLUE, dur_expiring(DUR_HASTE));
        inf->light_text   = "Fast";
        inf->short_text = "hasted";
        inf->long_text = "Your actions are hasted.";
        _mark_expiring(inf, dur_expiring(DUR_HASTE));
    }
}

static void _describe_airborne(status_info* inf)
{
    if (!you.airborne())
        return;

    const bool perm     = you.permanent_flight();
    const bool expiring = (!perm && dur_expiring(DUR_FLIGHT));
    const string desc   = you.tengu_flight() ? " quickly and evasively" : "";

    inf->light_colour = perm ? WHITE : BLUE;
    inf->light_text   = "Fly";
    inf->short_text   = "flying" + desc;
    inf->long_text    = "You are flying" + desc + ".";
    inf->light_colour = _dur_colour(inf->light_colour, expiring);
    _mark_expiring(inf, expiring);
}

static void _describe_rotting(status_info* inf)
{
    if (you.rotting)
    {
        inf->light_colour = _bad_ench_colour(you.rotting, 5, 9);
        inf->light_text   = "Rot";
    }

    if (you.rotting || you.species == SP_GHOUL)
    {
        inf->short_text = "rotting";
        inf->long_text = "Your flesh is rotting";
        int rot = you.rotting;
        if (you.species == SP_GHOUL)
            rot += 1 + (1 << max(0, HS_SATIATED - you.hunger_state));
        if (rot > 15)
            inf->long_text += " before your eyes";
        else if (rot > 8)
            inf->long_text += " away quickly";
        else if (rot > 4)
            inf->long_text += " badly";
        else if (you.species == SP_GHOUL)
        {
            if (rot > 2)
                inf->long_text += " faster than usual";
            else
                inf->long_text += " at the usual pace";
        }
        inf->long_text += ".";
    }
}

static void _describe_sickness(status_info* inf)
{
    if (you.disease)
    {
        const int high = 120 * BASELINE_DELAY;
        const int low  =  40 * BASELINE_DELAY;

        inf->light_colour   = _bad_ench_colour(you.disease, low, high);
        inf->light_text     = "Sick";

        string mod = (you.disease > high) ? "badly "  :
                     (you.disease >  low) ? ""
                                          : "mildly ";

        inf->short_text = mod + "diseased";
        inf->long_text  = "あなたは" + jtrans("dur " + inf->short_text) + "。";
    }
}

/**
 * Populate a status info struct with a description of the player's current
 * form.
 *
 * @param[out] inf  The status info struct to be populated.
 */
static void _describe_transform(status_info* inf)
{
    if (you.form == TRAN_NONE)
        return;

    const Form * const form = get_form();
    inf->light_text = tagged_jtrans("[form]", form->short_name);
    inf->short_text = jtrans(form->get_long_name());
    inf->long_text = form->get_description();

    const bool vampbat = (you.species == SP_VAMPIRE && you.form == TRAN_BAT);
    const bool expire  = dur_expiring(DUR_TRANSFORMATION) && !vampbat;

    inf->light_colour = _dur_colour(GREEN, expire);
    _mark_expiring(inf, expire);
}

static const char* s0_names[NUM_STATS] = { "Collapse", "Brainless", "Clumsy", };

static void _describe_stat_zero(status_info* inf, stat_type st)
{
    if (you.stat_zero[st])
    {
        inf->light_colour = you.stat(st) ? LIGHTRED : RED;
        inf->light_text   = s0_names[st];
        inf->short_text   = make_stringf("lost %s", stat_desc(st, SD_NAME));
        inf->long_text    = make_stringf(you.stat(st) ?
                "You are recovering from loss of %s." : "You have no %s!",
                stat_desc(st, SD_NAME));
    }
}

static void _describe_terrain(status_info* inf)
{
    switch (grd(you.pos()))
    {
    case DNGN_SHALLOW_WATER:
        inf->light_colour = LIGHTBLUE;
        inf->light_text = "Water";
        break;
    case DNGN_DEEP_WATER:
        inf->light_colour = BLUE;
        inf->light_text = "Water";
        break;
    case DNGN_LAVA:
        inf->light_colour = RED;
        inf->light_text = "Lava";
        break;
    default:
        ;
    }
}

static void _describe_missiles(status_info* inf)
{
    const int level = you.missile_deflection();
    if (!level)
        return;

    if (level > 1)
    {
        bool perm = false; /* in_good_standing(GOD_QAZLAL, 4) */
        inf->light_colour = perm ? WHITE : LIGHTMAGENTA;
        inf->light_text   = "DMsl";
        inf->short_text   = "deflect missiles";
        inf->long_text    = "You deflect missiles.";
    }
    else
    {
        bool perm = player_mutation_level(MUT_DISTORTION_FIELD) == 3
                    || you.scan_artefacts(ARTP_RMSL)
                    || in_good_standing(GOD_QAZLAL, 3);
        inf->light_colour = perm ? WHITE : LIGHTBLUE;
        inf->light_text   = "RMsl";
        inf->short_text   = "repel missiles";
        inf->long_text    = "You repel missiles.";
    }
}

static void _describe_invisible(status_info* inf)
{
    if (!you.duration[DUR_INVIS] && you.form != TRAN_SHADOW)
        return;

    if (you.form == TRAN_SHADOW)
    {
        inf->light_colour = _dur_colour(WHITE,
                                        dur_expiring(DUR_TRANSFORMATION));
    }
    else if (you.attribute[ATTR_INVIS_UNCANCELLABLE])
        inf->light_colour = _dur_colour(BLUE, dur_expiring(DUR_INVIS));
    else
        inf->light_colour = _dur_colour(MAGENTA, dur_expiring(DUR_INVIS));
    inf->light_text   = "Invis";
    inf->short_text   = "invisible";
    if (you.backlit())
    {
        inf->light_colour = DARKGREY;
        inf->short_text += " (but backlit and visible)";
    }
    inf->long_text = "You are " + inf->short_text + ".";
    _mark_expiring(inf, dur_expiring(you.form == TRAN_SHADOW
                                     ? DUR_TRANSFORMATION
                                     : DUR_INVIS));
}

/**
 * @file
 * @brief Functions used to print information about various game objects.
**/

#include "AppHdr.h"

#include "describe.h"

#include <cstdio>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>
#include <string>

#include "adjust.h"
#include "artefact.h"
#include "branch.h"
#include "butcher.h"
#include "cloud.h" // cloud_type_name
#include "clua.h"
#include "database.h"
#include "decks.h"
#include "delay.h"
#include "describe-spells.h"
#include "directn.h"
#include "english.h"
#include "env.h"
#include "evoke.h"
#include "fight.h"
#include "food.h"
#include "ghost.h"
#include "godabil.h"
#include "goditem.h"
#include "hints.h"
#include "japanese.h"
#include "invent.h"
#include "itemprop.h"
#include "items.h"
#include "item_use.h"
#include "jobs.h"
#include "libutil.h"
#include "macro.h"
#include "message.h"
#include "mon-book.h"
#include "mon-cast.h" // mons_spell_range
#include "mon-chimera.h"
#include "mon-tentacle.h"
#include "options.h"
#include "output.h"
#include "process_desc.h"
#include "prompt.h"
#include "religion.h"
#include "skills.h"
#include "spl-book.h"
#include "spl-summoning.h"
#include "spl-util.h"
#include "spl-wpnench.h"
#include "stash.h"
#include "state.h"
#include "terrain.h"
#ifdef USE_TILE_LOCAL
 #include "tilereg-crt.h"
#endif
#include "unicode.h"

// ========================================================================
//      Internal Functions
// ========================================================================

//---------------------------------------------------------------
//
// append_value
//
// Appends a value to the string. If plussed == 1, will add a + to
// positive values.
//
//---------------------------------------------------------------
static void _append_value(string & description, int valu, bool plussed)
{
    char value_str[80];
    sprintf(value_str, plussed ? "%+d" : "%d", valu);
    description += value_str;
}

static void _append_value(string & description, float fvalu, bool plussed)
{
    char value_str[80];
    sprintf(value_str, plussed ? "%+.1f" : "%.1f", fvalu);
    description += value_str;
}

int count_desc_lines(const string &_desc, const int width)
{
    string desc = get_linebreak_string(_desc, width);

    int count = 0;
    for (int i = 0, size = desc.size(); i < size; ++i)
    {
        const char ch = desc[i];

        if (ch == '\n')
            count++;
    }

    return count;
}

static void _adjust_item(item_def &item);

//---------------------------------------------------------------
//
// print_description
//
// Takes a descrip string filled up with stuff from other functions,
// and displays it with minor formatting to avoid cut-offs in mid
// word and such.
//
//---------------------------------------------------------------

void print_description(const string &body)
{
    describe_info inf;
    inf.body << body;
    print_description(inf);
}

class default_desc_proc
{
public:
    int width() { return get_number_of_cols() - 1; }
    int height() { return get_number_of_lines(); }
    void print(const string &str) { cprintf("%s", str.c_str()); }

    void nextline()
    {
        if (wherey() < height())
            cgotoxy(1, wherey() + 1);
        else
            cgotoxy(1, height());
        // Otherwise cgotoxy asserts; let's just clobber the last line
        // instead, which should be noticeable enough.
    }
};

void print_description(const describe_info &inf)
{
    clrscr();
    textcolour(LIGHTGREY);

    default_desc_proc proc;
    process_description<default_desc_proc>(proc, inf);
}

static void _print_quote(const describe_info &inf)
{
    clrscr();
    textcolour(LIGHTGREY);

    default_desc_proc proc;
    process_quote<default_desc_proc>(proc, inf);
}

static const char* _jewellery_base_ability_string(int subtype)
{
    switch (subtype)
    {
    case RING_SUSTAIN_ABILITIES: return "SustAb";
    case RING_WIZARDRY:          return "Wiz";
    case RING_FIRE:              return "Fire";
    case RING_ICE:               return "Ice";
    case RING_TELEPORTATION:     return "+/*Tele";
    case RING_TELEPORT_CONTROL:  return "+cTele";
    case AMU_CLARITY:            return "Clar";
    case AMU_WARDING:            return "Ward";
    case AMU_RESIST_CORROSION:   return "rCorr";
    case AMU_THE_GOURMAND:       return "Gourm";
#if TAG_MAJOR_VERSION == 34
    case AMU_CONSERVATION:       return "Cons";
    case AMU_CONTROLLED_FLIGHT:  return "cFly";
#endif
    case AMU_RESIST_MUTATION:    return "rMut";
    case AMU_GUARDIAN_SPIRIT:    return "Spirit";
    case AMU_FAITH:              return "Faith";
    case AMU_STASIS:             return "Stasis";
    case AMU_INACCURACY:         return "Inacc";
    }
    return "";
}

#define known_proprt(prop) (proprt[(prop)] && known[(prop)])

/// How to display props of a given type?
enum prop_note_type
{
    /// The raw numeral; e.g "Slay+3", "Int-1"
    PROPN_NUMERAL,
    /// Plusses and minuses; "rF-", "rC++"
    PROPN_SYMBOLIC,
    /// Don't note the number; e.g. "rMut"
    PROPN_PLAIN,
};

struct property_annotators
{
    artefact_prop_type prop;
    prop_note_type spell_out;
};

static vector<string> _randart_propnames(const item_def& item,
                                         bool no_comma = false)
{
    artefact_properties_t  proprt;
    artefact_known_props_t known;
    artefact_desc_properties(item, proprt, known);

    vector<string> propnames;

    // list the following in rough order of importance
    const property_annotators propanns[] =
    {
        // (Generally) negative attributes
        // These come first, so they don't get chopped off!
        { ARTP_PREVENT_SPELLCASTING,  PROPN_PLAIN },
        { ARTP_PREVENT_TELEPORTATION, PROPN_PLAIN },
        { ARTP_MUTAGENIC,             PROPN_PLAIN },
        { ARTP_ANGRY,                 PROPN_PLAIN },
        { ARTP_CAUSE_TELEPORTATION,   PROPN_PLAIN },
        { ARTP_NOISES,                PROPN_PLAIN },

        // Evokable abilities come second
        { ARTP_TWISTER,               PROPN_PLAIN },
        { ARTP_BLINK,                 PROPN_PLAIN },
        { ARTP_BERSERK,               PROPN_PLAIN },
        { ARTP_INVISIBLE,             PROPN_PLAIN },
        { ARTP_FLY,                   PROPN_PLAIN },
        { ARTP_FOG,                   PROPN_PLAIN },

        // Resists, also really important
        { ARTP_ELECTRICITY,           PROPN_PLAIN },
        { ARTP_POISON,                PROPN_PLAIN },
        { ARTP_FIRE,                  PROPN_SYMBOLIC },
        { ARTP_COLD,                  PROPN_SYMBOLIC },
        { ARTP_NEGATIVE_ENERGY,       PROPN_SYMBOLIC },
        { ARTP_MAGIC,                 PROPN_SYMBOLIC },
        { ARTP_REGENERATION,          PROPN_SYMBOLIC },
        { ARTP_RMUT,                  PROPN_PLAIN },
        { ARTP_RCORR,                 PROPN_PLAIN },

        // Quantitative attributes
        { ARTP_HP,                    PROPN_NUMERAL },
        { ARTP_MAGICAL_POWER,         PROPN_NUMERAL },
        { ARTP_AC,                    PROPN_NUMERAL },
        { ARTP_EVASION,               PROPN_NUMERAL },
        { ARTP_STRENGTH,              PROPN_NUMERAL },
        { ARTP_INTELLIGENCE,          PROPN_NUMERAL },
        { ARTP_DEXTERITY,             PROPN_NUMERAL },
        { ARTP_SLAYING,               PROPN_NUMERAL },

        // Qualitative attributes (and Stealth)
        { ARTP_EYESIGHT,              PROPN_PLAIN },
        { ARTP_STEALTH,               PROPN_SYMBOLIC },
        { ARTP_CURSED,                PROPN_PLAIN },
        { ARTP_CLARITY,               PROPN_PLAIN },
        { ARTP_RMSL,                  PROPN_PLAIN },
        { ARTP_SUSTAB,                PROPN_PLAIN },
    };

    // For randart jewellery, note the base jewellery type if it's not
    // covered by artefact_desc_properties()
    if (item.base_type == OBJ_JEWELLERY
        && (item_ident(item, ISFLAG_KNOW_TYPE)))
    {
        const char* type = _jewellery_base_ability_string(item.sub_type);
        if (*type)
            propnames.push_back(type);
    }
    else if (item_ident(item, ISFLAG_KNOW_TYPE)
             || is_artefact(item)
                && artefact_known_property(item, ARTP_BRAND))
    {
        string ego;
        if (item.base_type == OBJ_WEAPONS)
            ego = weapon_brand_name(item, true);
        else if (item.base_type == OBJ_ARMOUR)
            ego = armour_ego_name(item, true);
        if (!ego.empty())
        {
            // XXX: Ugly hack for adding a comma if needed.
            for (const property_annotators &ann : propanns)
                if (known_proprt(ann.prop) && ann.prop != ARTP_BRAND
                    && !no_comma)
                {
                    ego += ",";
                    break;
                }

            propnames.push_back(ego);
        }
    }

    if (is_unrandom_artefact(item))
    {
        const unrandart_entry *entry = get_unrand_entry(item.special);
        if (entry && entry->inscrip != nullptr)
            propnames.push_back(entry->inscrip);
     }

    for (const property_annotators &ann : propanns)
    {
        if (known_proprt(ann.prop))
        {
            const int val = proprt[ann.prop];

            // Don't show rF+/rC- for =Fire, or vice versa for =Ice.
            if (item.base_type == OBJ_JEWELLERY)
            {
                if (item.sub_type == RING_FIRE
                    && (ann.prop == ARTP_FIRE && val == 1
                        || ann.prop == ARTP_COLD && val == -1))
                {
                    continue;
                }
                if (item.sub_type == RING_ICE
                    && (ann.prop == ARTP_COLD && val == 1
                        || ann.prop == ARTP_FIRE && val == -1))
                {
                    continue;
                }
            }

            ostringstream work;
            switch (ann.spell_out)
            {
            case PROPN_NUMERAL: // e.g. AC+4
                work << showpos << artp_name(ann.prop) << val;
                break;
            case PROPN_SYMBOLIC: // e.g. F++
            {
                // XXX: actually handle absurd values instead of displaying
                // the wrong number of +s or -s
                const int sval = min(abs(val), 6);
                work << artp_name(ann.prop)
                     << string(sval, (val > 0 ? '+' : '-'));
                break;
            }
            case PROPN_PLAIN: // e.g. rPois or SInv
                if (ann.prop == ARTP_CURSED && val < 1)
                    continue;

                work << artp_name(ann.prop);
                break;
            }
            propnames.push_back(work.str());
        }
    }

    return propnames;
}

string artefact_inscription(const item_def& item)
{
    if (item.base_type == OBJ_BOOKS)
        return "";

    const vector<string> propnames = _randart_propnames(item);

    string insc = comma_separated_line(propnames.begin(), propnames.end(),
                                       " ", " ");
    if (!insc.empty() && insc[insc.length() - 1] == ',')
        insc.erase(insc.length() - 1);
    return sp2nbsp(insc);
}

void add_inscription(item_def &item, string inscrip)
{
    if (!item.inscription.empty())
    {
        if (ends_with(item.inscription, ","))
            item.inscription += " ";
        else
            item.inscription += ", ";
    }

    item.inscription += inscrip;
}

static const char* _jewellery_base_ability_description(int subtype)
{
    switch (subtype)
    {
    case RING_SUSTAIN_ABILITIES:
        return "It sustains your strength, intelligence and dexterity.";
    case RING_WIZARDRY:
        return "It improves your spell success rate.";
    case RING_FIRE:
        return "It enhances your fire magic, and weakens your ice magic.";
    case RING_ICE:
        return "It enhances your ice magic, and weakens your fire magic.";
    case RING_TELEPORTATION:
        return "It causes random teleportation, and can be evoked to teleport "
               "at will.";
    case RING_TELEPORT_CONTROL:
        return "It can be evoked for teleport control.";
    case AMU_CLARITY:
        return "It provides mental clarity.";
    case AMU_WARDING:
        return "It may prevent the melee attacks of summoned creatures.";
    case AMU_RESIST_CORROSION:
        return "It protects you from acid and corrosion.";
    case AMU_THE_GOURMAND:
        return "It allows you to eat raw meat even when not hungry.";
#if TAG_MAJOR_VERSION == 34
    case AMU_CONSERVATION:
        return "It protects your inventory from destruction.";
#endif
    case AMU_RESIST_MUTATION:
        return "It protects you from mutation.";
    case AMU_GUARDIAN_SPIRIT:
        return "It causes incoming damage to be split between your health and "
               "magic.";
    case AMU_FAITH:
        return "It allows you to gain divine favour quickly.";
    case AMU_STASIS:
        return "It prevents you from being teleported, slowed, hasted or "
               "paralysed.";
    case AMU_INACCURACY:
        return "It reduces the accuracy of all your attacks.";
    }
    return "";
}

struct property_descriptor
{
    artefact_prop_type property;
    const char* desc;           // If it contains %d, will be replaced by value.
    bool is_graded_resist;
};

static string _randart_base_type_string(const item_def &item)
{
    const string basename = base_type_string(item);
    return basename == "armour" ? "防具" :
           basename != "jewellery" ? basename :
           jewellery_is_amulet(item) ? "amulet" :
                                       "ring";
}

static string _randart_descrip(const item_def &item)
{
    string description;

    artefact_properties_t  proprt;
    artefact_known_props_t known;
    artefact_desc_properties(item, proprt, known);

    const property_descriptor propdescs[] =
    {
        { ARTP_AC, "It affects your AC (%d).", false },
        { ARTP_EVASION, "It affects your evasion (%d).", false},
        { ARTP_STRENGTH, "It affects your strength (%d).", false},
        { ARTP_INTELLIGENCE, "It affects your intelligence (%d).", false},
        { ARTP_DEXTERITY, "It affects your dexterity (%d).", false},
        { ARTP_SLAYING, "It affects your accuracy and damage with ranged "
                        "weapons and melee attacks (%d).", false},
        { ARTP_FIRE, "火", true},
        { ARTP_COLD, "冷気", true},
        { ARTP_ELECTRICITY, "It insulates you from electricity.", false},
        { ARTP_POISON, "毒", true},
        { ARTP_NEGATIVE_ENERGY, "負のエネルギー", true},
        { ARTP_SUSTAB, "It sustains your strength, intelligence and dexterity.", false},
        { ARTP_MAGIC, "It affects your resistance to hostile enchantments.", false},
        { ARTP_HP, "It affects your health (%d).", false},
        { ARTP_MAGICAL_POWER, "It affects your magic capacity (%d).", false},
        { ARTP_EYESIGHT, "It enhances your eyesight.", false},
        { ARTP_INVISIBLE, "It lets you turn invisible.", false},
        { ARTP_FLY, "It lets you fly.", false},
        { ARTP_BLINK, "It lets you blink.", false},
        { ARTP_BERSERK, "It lets you go berserk.", false},
        { ARTP_NOISES, "It may make noises in combat.", false},
        { ARTP_PREVENT_SPELLCASTING, "It prevents spellcasting.", false},
        { ARTP_CAUSE_TELEPORTATION, "It causes random teleportation.", false},
        { ARTP_PREVENT_TELEPORTATION, "It prevents most forms of teleportation.",
          false},
        { ARTP_ANGRY,  "It may make you go berserk in combat.", false},
        { ARTP_CURSED, "It may re-curse itself when equipped.", false},
        { ARTP_CLARITY, "It protects you against confusion.", false},
        { ARTP_MUTAGENIC, "It causes magical contamination when unequipped.", false},
        { ARTP_RMSL, "It protects you from missiles.", false},
        { ARTP_FOG, "It can be evoked to emit clouds of fog.", false},
        { ARTP_REGENERATION, "It increases your rate of regeneration.", false},
        { ARTP_RCORR, "It protects you from acid and corrosion.", false},
        { ARTP_RMUT, "It protects you from mutation.", false},
        { ARTP_TWISTER, "It can be evoked to create a twister.", false},
    };

    // Give a short description of the base type, for base types with no
    // corresponding ARTP.
    if (item.base_type == OBJ_JEWELLERY
        && (item_ident(item, ISFLAG_KNOW_TYPE)))
    {
        const char* type = _jewellery_base_ability_description(item.sub_type);
        if (*type)
        {
            description += "\n";
            description += make_stringf(jtransc(type),
                                        item.name(DESC_BASENAME).c_str());
        }
    }

    for (const property_descriptor &desc : propdescs)
    {
        if (known_proprt(desc.property))
        {
            // Only randarts with ARTP_CURSED > 0 may recurse themselves.
            if (desc.property == ARTP_CURSED && proprt[desc.property] < 1)
                continue;

            string sdesc = desc.desc;

            // FIXME Not the nicest hack.
            char buf[80];

            snprintf(buf, sizeof buf, "%+d", proprt[desc.property]);
            sdesc = replace_all(jtrans(sdesc), "%d", buf);

            if (desc.is_graded_resist)
            {
                int idx = proprt[desc.property] + 3;
                idx = min(idx, 6);
                idx = max(idx, 0);

                const char* prefixes[] =
                {
                    "It makes you extremely vulnerable to ",
                    "It makes you very vulnerable to ",
                    "It makes you vulnerable to ",
                    "Buggy descriptor!",
                    "It protects you from ",
                    "It greatly protects you from ",
                    "It renders you almost immune to "
                };
                sdesc = make_stringf(jtransc(prefixes[idx]),
                                     sdesc.c_str()) + "。";
            }

            sdesc = make_stringf(sdesc.c_str(),
                                 jtransc(_randart_base_type_string(item)));

            description += "\n";
            description += sdesc;
        }
    }

    if (known_proprt(ARTP_STEALTH))
    {
        const int stval = proprt[ARTP_STEALTH];
        char buf[80];
        snprintf(buf, sizeof buf, "\nIt makes you %s%s stealthy.",
                 (stval < -1 || stval > 1) ? "much " : "",
                 (stval < 0) ? "less" : "more");
        description += "\n" + make_stringf(jtransc(buf),
                                           jtransc(_randart_base_type_string(item)));
    }

    return description;
}
#undef known_proprt

static const char *trap_names[] =
{
#if TAG_MAJOR_VERSION == 34
    "dart",
#endif
    "arrow", "spear",
#if TAG_MAJOR_VERSION > 34
    "teleport",
#endif
    "permanent teleport",
    "alarm", "blade",
    "bolt", "net", "Zot", "needle",
    "shaft", "passage", "pressure plate", "web",
#if TAG_MAJOR_VERSION == 34
    "gas", "teleport",
#endif
     "shadow", "dormant shadow",
};

string trap_name(trap_type trap)
{
    COMPILE_CHECK(ARRAYSZ(trap_names) == NUM_TRAPS);

    if (trap >= 0 && trap < NUM_TRAPS)
        return trap_names[trap];
    return "";
}

string full_trap_name(trap_type trap)
{
    string basename = trap_name(trap);
    switch (trap)
    {
    case TRAP_GOLUBRIA:
        return basename + " of Golubria";
    case TRAP_PLATE:
    case TRAP_WEB:
    case TRAP_SHAFT:
        return basename;
    default:
        return basename + " trap";
    }
}

int str_to_trap(const string &s)
{
    // "Zot trap" is capitalised in trap_names[], but the other trap
    // names aren't.
    const string tspec = lowercase_string(s);

    // allow a couple of synonyms
    if (tspec == "random" || tspec == "any")
        return TRAP_RANDOM;

    for (int i = 0; i < NUM_TRAPS; ++i)
        if (tspec == lowercase_string(trap_names[i]))
            return i;

    return -1;
}

//---------------------------------------------------------------
//
// describe_demon
//
// Describes the random demons you find in Pandemonium.
//
//---------------------------------------------------------------
static string _describe_demon(const string& name, flight_type fly)
{
    const uint32_t seed = hash32(&name[0], name.size());
    #define HRANDOM_ELEMENT(arr, id) arr[hash_rand(ARRAYSZ(arr), seed, id)]

    const char* body_descs[] =
    {
        "armoured ",
        "vast, spindly ",
        "fat ",
        "obese ",
        "muscular ",
        "spiked ",
        "splotchy ",
        "slender ",
        "tentacled ",
        "emaciated ",
        "bug-like ",
        "skeletal ",
        "mantis ",
    };

    const char* wing_names[] =
    {
        " with small, bat-like wings",
        " with bony wings",
        " with sharp, metallic wings",
        " with the wings of a moth",
        " with thin, membranous wings",
        " with dragonfly wings",
        " with large, powerful wings",
        " with fluttering wings",
        " with great, sinister wings",
        " with hideous, tattered wings",
        " with sparrow-like wings",
        " with hooked wings",
        " with strange knobs attached",
    };

    const char* lev_names[] =
    {
        " which hovers in mid-air",
        " with sacs of gas hanging from its back",
    };

    const char* head_names[] =
    {
        " and a cubic structure in place of a head",
        " and a brain for a head",
        " and a hideous tangle of tentacles for a mouth",
        " and the head of an elephant",
        " and an eyeball for a head",
        " and wears a helmet over its head",
        " and a horn in place of a head",
        " and a thick, horned head",
        " and the head of a horse",
        " and a vicious glare",
        " and snakes for hair",
        " and the face of a baboon",
        " and the head of a mouse",
        " and a ram's head",
        " and the head of a rhino",
        " and eerily human features",
        " and a gigantic mouth",
        " and a mass of tentacles growing from its neck",
        " and a thin, worm-like head",
        " and huge, compound eyes",
        " and the head of a frog",
        " and an insectoid head",
        " and a great mass of hair",
        " and a skull for a head",
        " and a cow's skull for a head",
        " and the head of a bird",
        " and a large fungus growing from its neck",
    };

    const char* misc_descs[] =
    {
        " It seethes with hatred of the living.",
        " Tiny orange flames dance around it.",
        " Tiny purple flames dance around it.",
        " It is surrounded by a weird haze.",
        " It glows with a malevolent light.",
        " It looks incredibly angry.",
        " It oozes with slime.",
        " It dribbles constantly.",
        " Mould grows all over it.",
        " It looks diseased.",
        " It looks as frightened of you as you are of it.",
        " It moves in a series of hideous convulsions.",
        " It moves with an unearthly grace.",
        " It hungers for your soul!",
        " It leaves a glistening oily trail.",
        " It shimmers before your eyes.",
        " It is surrounded by a brilliant glow.",
        " It radiates an aura of extreme power.",
        " It seems utterly heartbroken.",
        " It seems filled with irrepressible glee.",
        " It constantly shivers and twitches.",
        " Blue sparks crawl across its body.",
        " It seems uncertain.",
        " A cloud of flies swarms around it.",
        " The air ripples with heat as it passes.",
        " It appears supremely confident.",
        " Its skin is covered in a network of cracks.",
        " Its skin has a disgusting oily sheen.",
        " It seems completely insane!",
        " It seems somehow familiar."
    };

    ostringstream description;
    description << jtrans("One of the many lords of Pandemonium, ") << name << "』は";

    const string a_body = HRANDOM_ELEMENT(body_descs, 2);
    description << jtrans(a_body) << "体";

    string head_desc = HRANDOM_ELEMENT(head_names, 1);

    switch (fly)
    {
    case FL_WINGED:
        description << "と";
        description << jtrans(HRANDOM_ELEMENT(wing_names, 3));

        if (jtrans(head_desc).find("。") != string::npos)
            description << "を持ち、";
        else
            description << "、そして";

        break;

    case FL_LEVITATE:
        description << "で";
        description << jtrans(HRANDOM_ELEMENT(lev_names, 3));
        break;

    default:
        description << "で、";
        break;
    }

    description << jtrans(head_desc);
    if (jtrans(head_desc).find("。") == string::npos)
        description << "を持っている。";

    if (hash_rand(40, seed, 4) < 3)
    {
        if (you.can_smell())
        {
            description << "\n";

            switch (hash_rand(4, seed, 5))
            {
            case 0:
                description << jtrans(" It stinks of brimstone.");
                break;
            case 1:
                description << jtrans(" It is surrounded by a sickening stench.");
                break;
            case 2:
                description << jtrans(" It smells delicious!");
                break;
            case 3:
                description << jtrans(string(" It smells like rotting flesh")
                             + (you.species == SP_GHOUL ? " - yum!"
                                                        : "."));
                break;
            }
        }
    }
    else if (hash_rand(2, seed, 6))
        description << "\n" << jtrans(HRANDOM_ELEMENT(misc_descs, 5));

    return description.str();
}

void append_weapon_stats(string &description, const item_def &item)
{
    description += "\n" + jtrans("Base accuracy: ") + " ";
    _append_value(description, property(item, PWPN_HIT), true);
    description += "　";

    const int base_dam = property(item, PWPN_DAMAGE);
    const int ammo_type = fires_ammo_type(item);
    const int ammo_dam = ammo_type == MI_NONE ? 0 :
                                                ammo_type_damage(ammo_type);
    description += jtrans("Base damage:") + " ";
    _append_value(description, base_dam + ammo_dam, false);
    description += "　";

    description += jtrans("Base attack delay:") + " ";
    _append_value(description, property(item, PWPN_SPEED) / 10.0f, false);
    description += "　";

    description += jtrans("Minimum delay:") + " ";
    _append_value(description, weapon_min_delay(item) / 10.0f, false);

    if (item_attack_skill(item) == SK_SLINGS)
    {
        description += "\n" + make_stringf(jtransc("\nFiring bullets:    Base damage: %d"),
                                           base_dam +
                                           ammo_type_damage(MI_SLING_BULLET));
    }

    description = sp2nbsp(description);
}

static string _handedness_string(const item_def &item)
{
    string description;

    switch (you.hands_reqd(item))
    {
    case HANDS_ONE:
        if (you.species == SP_FORMICID)
            description += "It is a weapon for one hand-pair.";
        else
            description += "It is a one handed weapon.";
        break;
    case HANDS_TWO:
        description += "It is a two handed weapon.";
        break;
    }

    return description;
}

//---------------------------------------------------------------
//
// describe_weapon
//
//---------------------------------------------------------------
static string _describe_weapon(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    description = "";

    if (verbose)
    {
        append_weapon_stats(description, item);
    }

    const int spec_ench = (is_artefact(item) || verbose)
                          ? get_weapon_brand(item) : SPWPN_NORMAL;
    const int damtype = get_vorpal_type(item);

    if (verbose)
    {
        switch (item_attack_skill(item))
        {
        case SK_POLEARMS:
            description += "\n\n" + jtrans("It can be evoked to extend its reach.");
            break;
        case SK_AXES:
            description += "\n\n" + jtrans("It can hit multiple enemies in an arc"
                                           " around the wielder.");
            break;
        case SK_SHORT_BLADES:
            {
                string adj = (item.sub_type == WPN_DAGGER) ? "extremely"
                                                           : "particularly";
                description += "\n\nこの武器は"
                            + make_stringf(jtransc("%s good for stabbing unaware enemies."),
                                           jtransc(adj));
            }
            break;
        default:
            break;
        }
    }

    // ident known & no brand but still glowing
    // TODO: deduplicate this with the code in itemname.cc
    const bool enchanted = get_equip_desc(item) && spec_ench == SPWPN_NORMAL
                           && !item_ident(item, ISFLAG_KNOW_PLUSES);

    // special weapon descrip
    if (item_type_known(item) && (spec_ench != SPWPN_NORMAL || enchanted))
    {
        description += "\n\n";

        switch (spec_ench)
        {
        case SPWPN_FLAMING:
            if (is_range_weapon(item))
            {
                description += jtrans("It causes projectiles fired from it to burn "
                    "those they strike,");
            }
            else
            {
                description += jtrans("It has been specially enchanted to burn "
                    "those struck by it,");
            }
            description += jtrans("causing extra injury flaming");
            if (!is_range_weapon(item) &&
                (damtype == DVORP_SLICING || damtype == DVORP_CHOPPING))
            {
                description += "\n" + jtrans(" Big, fiery blades are also staple "
                    "armaments of hydra-hunters.");
            }
            break;
        case SPWPN_FREEZING:
            if (is_range_weapon(item))
            {
                description += jtrans("It causes projectiles fired from it to freeze "
                    "those they strike,");
            }
            else
            {
                description += jtrans("It has been specially enchanted to freeze "
                    "those struck by it,");
            }
            description += jtrans("causing extra injury freezing");

            description += "\nその攻撃は" + jtrans(" can also slow down cold-blooded creatures.");
            break;
        case SPWPN_HOLY_WRATH:
            description += jtrans("It has been blessed by the Shining One");
            if (is_range_weapon(item))
            {
                description += "放たれた" + jtrans(ammo_name(item)) + "は";
            }
            description += jtrans(" cause great damage to the undead and demons.");
            break;
        case SPWPN_ELECTROCUTION:
            if (is_range_weapon(item))
            {
                description += jtrans("It charges the ammunition it shoots with "
                                      "electricity; occasionally upon a hit, such missiles "
                                      "may discharge and cause terrible harm.");
            }
            else
            {
                description += jtrans("Occasionally, upon striking a foe, it will "
                                      "discharge some electrical energy and cause terrible "
                                      "harm.");
            }
            break;
        case SPWPN_VENOM:
            if (is_range_weapon(item))
                description += jtrans("It poisons the ammo it fires.");
            else
                description += jtrans("It poisons the flesh of those it strikes.");
            break;
        case SPWPN_PROTECTION:
            description += jtrans("It protects the one who wields it against "
                "injury (+5 to AC).");
            break;
        case SPWPN_EVASION:
            description += jtrans("It affects your evasion (+5 to EV).");
            break;
        case SPWPN_DRAINING:
            description += jtrans("A truly terrible weapon, it drains the "
                "life of those it strikes.");
            break;
        case SPWPN_SPEED:
            description += jtrans("Attacks with this weapon are significantly faster.");
            break;
        case SPWPN_VORPAL:
            if (is_range_weapon(item))
            {
                description += "この武器から放たれた";
                description += jtrans(ammo_name(item));
                description += jtrans(" fired from it inflicts extra damage.");
            }
            else
            {
                description += jtrans("It inflicts extra damage upon your "
                    "enemies.");
            }
            break;
        case SPWPN_CHAOS:
            if (is_range_weapon(item))
            {
                description += jtrans("Each time it fires, it turns the "
                    "launched projectile into a different, random type "
                    "of bolt.");
            }
            else
            {
                description += jtrans("Each time it hits an enemy it has a "
                    "different, random effect.");
            }
            break;
        case SPWPN_VAMPIRISM:
            description += jtrans("It inflicts no extra harm, but heals its "
                "wielder somewhat when it strikes a living foe.");
            break;
        case SPWPN_PAIN:
            description += jtrans("In the hands of one skilled in necromantic "
                "magic, it inflicts extra damage on living creatures.");
            break;
        case SPWPN_DISTORTION:
            description += jtrans("It warps and distorts space around it. "
                "Unwielding it can cause banishment or high damage.");
            break;
        case SPWPN_PENETRATION:
            description += jtrans("Ammo fired by it will pass through the "
                "targets it hits, potentially hitting all targets in "
                "its path until it reaches maximum range.");
            break;
        case SPWPN_REAPING:
            description += jtrans("If a monster killed with it leaves a "
                "corpse in good enough shape, the corpse will be "
                "animated as a zombie friendly to the killer.");
            break;
        case SPWPN_ANTIMAGIC:
            description += jtrans("It disrupts the flow of magical energy around "
                    "spellcasters and certain magical creatures (including "
                    "the wielder).");
            break;
        case SPWPN_NORMAL:
            ASSERT(enchanted);
            description += jtrans("It has no special brand (it is not flaming, "
                    "freezing, etc), but is still enchanted in some way - "
                    "positive or negative.");
            break;
        }
    }

    if (you.duration[DUR_WEAPON_BRAND] && &item == you.weapon())
    {
        description += "\n" + jtrans("It is temporarily rebranded; it is actually a");
        if ((int) you.props[ORIGINAL_BRAND_KEY] == SPWPN_NORMAL)
            description += jtrans("n unbranded weapon.");
        else
        {
            description += jtrans("of " + ego_type_string(item, false, you.props[ORIGINAL_BRAND_KEY]))
                        + "武器だ。";
        }
    }

    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            if(!description.empty())
                description += "\n";
            description += rand_desc;
        }

        // XXX: Can't happen, right?
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES)
            && item_type_known(item))
        {
            description += "\n" + jtrans("\nThis weapon may have some hidden properties.");
        }
    }

    if (verbose)
    {
        description += "\n\nこの";
        if (is_unrandom_artefact(item))
            description += jtrans(get_artefact_base_name(item, true));
        else
            description += jtrans("weapon");
        description += "は";

        const skill_type skill = item_attack_skill(item);

        description +=
            make_stringf(jtransc(" '%s' category. "),
                         skill == SK_FIGHTING ? "buggy" : tagged_jtransc("[skill]", skill_name(skill)));

        description += jtrans(_handedness_string(item));

        if (!you.could_wield(item, true))
            description += "\n" + jtrans("\nIt is too large for you to wield.");
    }

    if (!is_artefact(item))
    {
        if (item_ident(item, ISFLAG_KNOW_PLUSES) && item.plus >= MAX_WPN_ENCHANT)
            description += "\n" + make_stringf(jtransc("\nIt cannot be enchanted further."),
                                               "武器");
        else
        {
            description += "\nこの武器は+";
            _append_value(description, MAX_WPN_ENCHANT, false);
            description += "まで強化できる。";
        }
    }

    return description;
}

//---------------------------------------------------------------
//
// describe_ammo
//
//---------------------------------------------------------------
static string _describe_ammo(const item_def &item)
{
    string description;

    description.reserve(64);

    const bool can_launch = has_launcher(item);
    const bool can_throw  = is_throwable(&you, item, true);

    if (item.special && item_type_known(item))
    {
        description += "\n";

        string basename = ammo_name(static_cast<missile_type>(item.sub_type));

        switch (item.special)
        {
        case SPMSL_FLAME:
            description += make_stringf(jtransc("spmsl flame desc"),
                                        jtransc(basename), jtransc(basename));
            break;
        case SPMSL_FROST:
            description += make_stringf(jtransc("spmsl frost desc"),
                                        jtransc(basename), jtransc(basename));
            break;
        case SPMSL_CHAOS:
            description += "この" + jtrans(basename) + "が";

            if (can_throw)
            {
                description += "投擲された";
                if (can_launch)
                    description += "、または";
                else
                    description += "とき、";
            }

            if (can_launch)
                description += jtrans("fired from an appropriate launcher, ");

            description += jtrans("it turns into a bolt of a random type.");
            break;
        case SPMSL_POISONED:
            description += make_stringf(jtransc("It is coated with poison."),
                                        jtransc(basename));
            break;
        case SPMSL_CURARE:
            description += jtrans("It is tipped with asphyxiating poison. Compared "
                                  "to other needles, it is twice as likely to be "
                                  "destroyed on impact");
            break;
        case SPMSL_PARALYSIS:
            description += jtrans("It is tipped with a paralysing substance.");
            break;
        case SPMSL_SLOW:
            description += jtrans("It is coated with a substance that causes slowness of the body.");
            break;
        case SPMSL_SLEEP:
            description += jtrans("It is coated with a fast-acting tranquilizer.");
            break;
        case SPMSL_CONFUSION:
            description += jtrans("It is tipped with a substance that causes confusion.");
            break;
#if TAG_MAJOR_VERSION == 34
        case SPMSL_SICKNESS:
            description += jtrans("It has been contaminated by something likely to cause disease.");
            break;
#endif
        case SPMSL_FRENZY:
            description += jtrans("It is tipped with a substance that causes a mindless "
                                  "rage, making people attack friend and foe alike.");
            break;
       case SPMSL_RETURNING:
            description += make_stringf(jtransc("A skilled user can throw it in such a way "
                                                "that it will return to its owner."),
                                        jtransc(basename));
            break;
        case SPMSL_PENETRATION:
            description += make_stringf(jtransc("It will pass through any targets it hits, "
                                                "potentially hitting all targets in its path until it "
                                                "reaches maximum range."),
                                        jtransc(basename));
            break;
        case SPMSL_DISPERSAL:
            description += make_stringf(jtransc("Any target it hits will blink, with a "
                                                "tendency towards blinking further away from the one "
                                                "who threw_or_fired it."),
                                        jtransc(basename));
            break;
        case SPMSL_EXPLODING:
            description += make_stringf(jtransc("It will explode into fragments upon "
                                                "hitting a target, hitting an obstruction, or reaching "
                                                "the end of its range."),
                                        jtransc(basename));
            break;
        case SPMSL_STEEL:
            description += make_stringf(jtransc("Compared to normal ammo, it does 30% more "
                                                "damage."),
                                        jtransc(basename));
            break;
        case SPMSL_SILVER:
            description += make_stringf(jtransc("spmsl silver desc"),
                                        jtransc(basename), jtransc(basename));
            break;
        }
    }

    append_missile_info(description, item);

    return description;
}

void append_armour_stats(string &description, const item_def &item)
{
    description += "\n" + jtrans("\nBase armour rating: ") + " ";
    _append_value(description, property(item, PARM_AC), false);
    description += "       ";

    const int evp = property(item, PARM_EVASION);
    description += jtrans("Encumbrance rating: ") + " ";
    _append_value(description, -evp / 10, false);

    // only display player-relevant info if the player exists
    if (crawl_state.need_save && get_armour_slot(item) == EQ_BODY_ARMOUR)
    {
        description += "\n" + make_stringf(jtransc("\nWearing mundane armour of this type "
                                                   "will give the following: %d AC"),
                                           you.base_ac_from(item));
    }

    description = sp2nbsp(description);
}

void append_shield_stats(string &description, const item_def &item)
{
    description += "\n" + jtrans("\nBase shield rating:") + " ";
    _append_value(description, property(item, PARM_AC), false);
    description += "       ";

    description += "\n" + jtrans("\nSkill to remove penalty:") + " ";
    _append_value(description, you.get_shield_skill_to_offset_penalty(item),
            false);

    description = sp2nbsp(description);
}

void append_missile_info(string &description, const item_def &item)
{
    const int dam = property(item, PWPN_DAMAGE);
    const string basename = ammo_name(static_cast<missile_type>(item.sub_type));

    if (dam)
        description += "\n" + make_stringf(jtranslnc("\nBase damage: %d\n"), dam);

    if (ammo_always_destroyed(item))
        description += "\n" + make_stringf(jtransc("\nIt will always be destroyed on impact."),
                                           jtransc(basename));
    else if (!ammo_never_destroyed(item))
        description += "\n" + make_stringf(jtransc("It may be destroyed on impact."),
                                           jtransc(basename));
}

//---------------------------------------------------------------
//
// describe_armour
//
//---------------------------------------------------------------
static string _describe_armour(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    if (verbose
        && item.sub_type != ARM_SHIELD
        && item.sub_type != ARM_BUCKLER
        && item.sub_type != ARM_LARGE_SHIELD)
    {
        append_armour_stats(description, item);
    }
    else if (verbose)
    {
        append_shield_stats(description, item);
    }

    const int ego = get_armour_ego_type(item);
    if (ego != SPARM_NORMAL && item_type_known(item) && verbose)
    {
        description += (description.empty() ? "\n" : "\n\n");

        switch (ego)
        {
        case SPARM_RUNNING:
            if (item.sub_type == ARM_NAGA_BARDING)
                description += jtrans("It allows its wearer to slither at a great speed.");
            else
                description += jtrans("It allows its wearer to run at a great speed.");
            break;
        case SPARM_FIRE_RESISTANCE:
            description += jtrans("It protects its wearer from heat.");
            break;
        case SPARM_COLD_RESISTANCE:
            description += jtrans("It protects its wearer from cold.");
            break;
        case SPARM_POISON_RESISTANCE:
            description += jtrans("It protects its wearer from poison.");
            break;
        case SPARM_SEE_INVISIBLE:
            description += jtrans("It allows its wearer to see invisible things.");
            break;
        case SPARM_INVISIBILITY:
            description += jtrans("When activated it hides its wearer from "
                                  "the sight of others, but also increases "
                                  "their metabolic rate by a large amount.");
            break;
        case SPARM_STRENGTH:
            description += jtrans("It increases the physical power of its wearer (+3 to strength).");
            break;
        case SPARM_DEXTERITY:
            description += jtrans("It increases the dexterity of its wearer (+3 to dexterity).");
            break;
        case SPARM_INTELLIGENCE:
            description += jtrans("It makes you more clever (+3 to intelligence).");
            break;
        case SPARM_PONDEROUSNESS:
            description += jtrans("It is very cumbersome, thus slowing your movement.");
            break;
        case SPARM_FLYING:
            description += jtrans("It can be activated to allow its wearer to "
                                  "fly indefinitely.");
            break;
        case SPARM_MAGIC_RESISTANCE:
            description += jtrans("It increases its wearer's resistance "
                                  "to enchantments.");
            break;
        case SPARM_PROTECTION:
            description += jtrans("It protects its wearer from harm (+3 to AC).");
            break;
        case SPARM_STEALTH:
            description += jtrans("It enhances the stealth of its wearer.");
            break;
        case SPARM_RESISTANCE:
            description += jtrans("It protects its wearer from the effects "
                                  "of both cold and heat.");
            break;

        // These two are only for robes.
        case SPARM_POSITIVE_ENERGY:
            description += jtrans("It protects its wearer from "
                                  "the effects of negative energy.");
            break;
        case SPARM_ARCHMAGI:
            description += jtrans("It increases the power of its wearer's "
                                  "magical spells.");
            break;
#if TAG_MAJOR_VERSION == 34
        case SPARM_PRESERVATION:
            description += "It does nothing special.";
            break;
#endif
        case SPARM_REFLECTION:
            description += jtrans("It reflects blocked things back in the "
                                  "direction they came from.");
            break;

        case SPARM_SPIRIT_SHIELD:
            description += jtrans("It shields its wearer from harm at the cost "
                                  "of magical power.");
            break;

        // This is only for gloves.
        case SPARM_ARCHERY:
            description += jtrans("It improves your effectiveness with ranged weaponry (Slay+4).");
            break;
        }
    }

    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            if(!description.empty())
                description += "\n";
            description += rand_desc;
        }

        // Can't happen, right? (XXX)
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES) && item_type_known(item))
            description += "\n" + jtrans("\nThis armour may have some hidden properties.");
    }

    if (!is_artefact(item))
    {
        const int max_ench = armour_max_enchant(item);
        if (armour_is_hide(item))
        {
            description += "\n" + jtrans("\nEnchanting it will turn it into a suit of "
                                         "magical armour.");
        }
        else if (item.plus < max_ench || !item_ident(item, ISFLAG_KNOW_PLUSES))
        {
            description += "\nこの防具は+";
            _append_value(description, max_ench, false);
            description += "まで強化できる。";
        }
        else
            description += "\n" + make_stringf(jtransc("\nIt cannot be enchanted further."),
                                               "防具");
    }

    return description;
}

//---------------------------------------------------------------
//
// describe_jewellery
//
//---------------------------------------------------------------
static string _describe_jewellery(const item_def &item, bool verbose)
{
    string description;

    description.reserve(200);

    if (verbose && !is_artefact(item)
        && item_ident(item, ISFLAG_KNOW_PLUSES))
    {
        // Explicit description of ring power.
        if (item.plus != 0)
        {
            switch (item.sub_type)
            {
            case RING_PROTECTION:
                description += "\n" + jtrans("\nIt affects your AC (") + " ";
                _append_value(description, item.plus, true);
                description += ")";
                break;

            case RING_EVASION:
                description += "\n" + jtrans("\nIt affects your evasion (") + " ";
                _append_value(description, item.plus, true);
                description += ")";
                break;

            case RING_STRENGTH:
                description += "\n" + jtrans("\nIt affects your strength (") + " ";
                _append_value(description, item.plus, true);
                description += ")";
                break;

            case RING_INTELLIGENCE:
                description += "\n" + jtrans("\nIt affects your intelligence (") + " ";
                _append_value(description, item.plus, true);
                description += ")";
                break;

            case RING_DEXTERITY:
                description += "\n" + jtrans("\nIt affects your dexterity (") + " ";
                _append_value(description, item.plus, true);
                description += ")";
                break;

            case RING_SLAYING:
                description += "\n" + jtrans("\nIt affects your accuracy and damage "
                               "with ranged weapons and melee attacks (") + " ";
                _append_value(description, item.plus, true);
                description += ")";
                break;

            default:
                break;
            }
        }
    }

    // Artefact properties.
    if (is_artefact(item))
    {
        string rand_desc = _randart_descrip(item);
        if (!rand_desc.empty())
        {
            if(!description.empty())
                description += "\n";
            description += rand_desc;
        }
        if (!item_ident(item, ISFLAG_KNOW_PROPERTIES) ||
            !item_ident(item, ISFLAG_KNOW_TYPE))
        {
            description += "\nこの";
            description += jtrans(jewellery_is_amulet(item) ? "amulet" : "ring");
            description += jtrans(" may have hidden properties.");
        }
    }

    return description;
}

static bool _compare_card_names(card_type a, card_type b)
{
    return string(card_name(a)) < string(card_name(b));
}

//---------------------------------------------------------------
//
// describe_misc_item
//
//---------------------------------------------------------------
static bool _check_buggy_deck(const item_def &deck, string &desc)
{
    if (!is_deck(deck))
    {
        desc += "This isn't a deck at all!\n";
        return true;
    }

    const CrawlHashTable &props = deck.props;

    if (!props.exists("cards")
        || props["cards"].get_type() != SV_VEC
        || props["cards"].get_vector().get_type() != SV_BYTE
        || cards_in_deck(deck) == 0)
    {
        return true;
    }

    return false;
}

static string _describe_deck(const item_def &item)
{
    string description;

    description.reserve(100);

    if (_check_buggy_deck(item, description))
        return "";

    if (item_type_known(item))
        description += deck_contents(item.sub_type) + "\n";

    description += "\n" + make_stringf(jtransc("\nMost decks begin with %d to %d cards."),
                                       MIN_STARTING_CARDS,
                                       MAX_STARTING_CARDS);

    auto card_stringify = [](card_type card){ return tagged_jtrans("[card]", card_name(card)); };

    const vector<card_type> drawn_cards = get_drawn_cards(item);
    if (!drawn_cards.empty())
    {
        description += "\n";
        description += jtrans("Drawn card(s): ") + " ";
        description += comma_separated_fn(drawn_cards.begin(),
                                          drawn_cards.end(),
                                          card_stringify,
                                          "、", "、") + "のカード";
    }

    const int num_cards = cards_in_deck(item);
    // The list of known cards, ending at the first one not known to be at the
    // top.
    vector<card_type> seen_top_cards;
    // Seen cards in the deck not necessarily contiguous with the start. (If
    // Nemelex wrath shuffled a deck that you stacked, for example.)
    vector<card_type> other_seen_cards;
    bool still_contiguous = true;
    for (int i = 0; i < num_cards; ++i)
    {
        uint8_t flags;
        const card_type card = get_card_and_flags(item, -i-1, flags);
        if (flags & CFLAG_SEEN)
        {
            if (still_contiguous)
                seen_top_cards.push_back(card);
            else
                other_seen_cards.push_back(card);
        }
        else
            still_contiguous = false;
    }

    if (!seen_top_cards.empty())
    {
        description += "\n";
        description += jtrans("Next card(s): ") + " ";
        description += comma_separated_fn(seen_top_cards.begin(),
                                          seen_top_cards.end(),
                                          card_stringify,
                                          "、", "、") + "のカード";
    }
    if (!other_seen_cards.empty())
    {
        description += "\n";
        sort(other_seen_cards.begin(), other_seen_cards.end(),
             _compare_card_names);

        description += jtrans("Seen card(s): ") + " ";
        description += comma_separated_fn(other_seen_cards.begin(),
                                          other_seen_cards.end(),
                                          card_stringify,
                                          "、", "、") + "のカード";
    }

    return description;
}

// ========================================================================
//      Public Functions
// ========================================================================

bool is_dumpable_artefact(const item_def &item)
{
    return is_known_artefact(item) && item_ident(item, ISFLAG_KNOW_PROPERTIES);
}

//---------------------------------------------------------------
//
// get_item_description
//
//---------------------------------------------------------------
string _spacer(const int length)
{
    return length < 0 ? "" : string(length, ' ');
}

string get_item_description(const item_def &item, bool verbose,
                            bool dump, bool noquote)
{
    if (dump)
        noquote = true;

    ostringstream description;

    if (!dump)
    {
        string name = item.name(DESC_INVENTORY_EQUIP);
        string name_en = (item.base_type == OBJ_BOOKS &&
                          is_artefact(item)) ? "" :
                          is_artefact(item) ? uppercase_first(item.name_en(DESC_THE))
                                            : uppercase_first(item.name_en(DESC_A));
        string spacer = _spacer(get_number_of_cols() - strwidth(name)
                                                     - strwidth(name_en) - 1);

        if (strwidth(name) + strwidth(name_en) + 5 > get_number_of_cols())
            name_en = "";

        string title = name + spacer + name_en;

        description << sp2nbsp(title);
    }

#ifdef DEBUG_DIAGNOSTICS
    if (!dump)
    {
        description << setfill('0');
        description << "\n\n"
                    << "base: " << static_cast<int>(item.base_type)
                    << " sub: " << static_cast<int>(item.sub_type)
                    << " plus: " << item.plus << " plus2: " << item.plus2
                    << " special: " << item.special
                    << "\n"
                    << "quant: " << item.quantity
                    << " rnd?: " << static_cast<int>(item.rnd)
                    << " flags: " << hex << setw(8) << item.flags
                    << dec << "\n"
                    << "x: " << item.pos.x << " y: " << item.pos.y
                    << " link: " << item.link
                    << " slot: " << item.slot
                    << " ident_type: "
                    << static_cast<int>(get_ident_type(item))
                    << "\nannotate: "
                    << stash_annotate_item(STASH_LUA_SEARCH_ANNOTATE, &item);
    }
#endif

    if (verbose || (item.base_type != OBJ_WEAPONS
                    && item.base_type != OBJ_ARMOUR
                    && item.base_type != OBJ_BOOKS))
    {
        description << "\n\n";

        bool need_base_desc = true;

        if (dump)
        {
            description << "["
                        << item.name(DESC_DBNAME, true, false, false)
                        << "]";
            need_base_desc = false;
        }
        else if (is_unrandom_artefact(item) && item_type_known(item))
        {
            const string desc = getLongDescription(get_artefact_name(item));
            if (!desc.empty())
            {
                description << desc;
                need_base_desc = false;
                description.seekp((streamoff)-1, ios_base::cur);
                description << " ";
            }
        }
        // Randart jewellery properties will be listed later,
        // just describe artefact status here.
        else if (is_artefact(item) && item_type_known(item)
                 && item.base_type == OBJ_JEWELLERY)
        {
            description << jtrans("It is an ancient artefact.");
            need_base_desc = false;
        }

        if (need_base_desc)
        {
            string db_name = item.name(DESC_DBNAME, true, false, false);
            string db_name_en = item.name_en(DESC_DBNAME, true, false, false);
            string db_desc = getLongDescription(db_name_en);

            if (db_desc.empty())
            {
                if (item_type_known(item))
                {
                    description << "[ERROR: no desc for item name '" << db_name
                                << "']\n";
                }
                else
                {
                    description << uppercase_first(item.name(DESC_A, true,
                                                             false, false));
                    description << ".\n";
                }
            }
            else
                description << db_desc;

            // Get rid of newline at end of description; in most cases we
            // will be adding "\n\n" immediately, and we want only one,
            // not two, blank lines.  This allow allows the "unpleasant"
            // message for chunks to appear on the same line.
            description.seekp((streamoff)-1, ios_base::cur);
            description << " ";
        }
    }

    string desc;
    switch (item.base_type)
    {
    // Weapons, armour, jewellery, books might be artefacts.
    case OBJ_WEAPONS:
        desc += _describe_weapon(item, verbose);
        break;

    case OBJ_ARMOUR:
        desc += _describe_armour(item, verbose);
        break;

    case OBJ_JEWELLERY:
        desc += _describe_jewellery(item, verbose);
        break;

    case OBJ_BOOKS:
        if (!player_can_memorise_from_spellbook(item))
        {
            desc += "\n" + jtrans("\nThis book is beyond your current level of "
                                          "understanding.");

            if (!item_type_known(item))
                break;
        }

        if (!verbose
            && (Options.dump_book_spells || is_random_artefact(item)))
        {
            desc += describe_item_spells(item);
        }
        break;

    case OBJ_MISSILES:
        desc += _describe_ammo(item);
        break;

    case OBJ_WANDS:
    {
        const bool known_empty = is_known_empty_wand(item);

        if (!item_ident(item, ISFLAG_KNOW_PLUSES) && !known_empty)
        {
            desc += "\n" + jtrans("\nIf evoked without being fully identified,"
                                  " several charges will be wasted out of"
                                  " unfamiliarity with the device.");
        }


        if (item_type_known(item))
        {
            const int max_charges = wand_max_charges(item.sub_type);
            if (item.charges < max_charges
                || !item_ident(item, ISFLAG_KNOW_PLUSES))
            {
                desc += "\n" + make_stringf(jtransc("\nIt can have at most %d charges."),
                                            max_charges);
            }
            else
                desc += "\n" + jtrans("\nIt is fully charged.");
        }

        if (known_empty)
            desc += "\n" + jtrans("\nUnfortunately, it has no charges left.");
        break;
    }

    case OBJ_CORPSES:
        if (item.sub_type == CORPSE_SKELETON)
            break;

        if (mons_class_leaves_hide(item.mon_type))
        {
            desc += "\n";
            if (item.props.exists(MANGLED_CORPSE_KEY))
            {
                desc += jtrans("This corpse is badly mangled; its hide is "
                               "beyond any hope of recovery.");
            }
            else
            {
                desc += jtrans("Butchering may allow you to recover this "
                                      "creature's hide, which can be enchanted into "
                                      "armour.");
            }
        }
        // intentional fall-through
    case OBJ_FOOD:
        if (item.base_type == OBJ_FOOD)
        {
            desc += "\n";

            const int turns = food_turns(item);
            ASSERT(turns > 0);
            if (turns > 1)
            {
                desc += jtrans(string("It is large enough that eating it takes ")
                      + ((turns > 2) ? "several" : "a couple of")
                      + " turns, during which time the eater is vulnerable"
                      + " to attack.");
            }
            else
                desc += jtrans("It is small enough that eating it takes "
                               "only one turn.");
        }
        if (item.base_type == OBJ_CORPSES || item.sub_type == FOOD_CHUNK)
        {
            switch (determine_chunk_effect(item, true))
            {
            case CE_POISONOUS:
                desc += "\n\n" + jtrans("\n\nThis meat is poisonous.");
                break;
            case CE_MUTAGEN:
                desc += "\n\n" + jtrans("\n\nEating this meat will cause random "
                               "mutations.");
                break;
            case CE_ROT:
                desc += "\n\n" + jtrans("\n\nEating this meat will cause rotting.");
                break;
            default:
                break;
            }
        }
        break;

    case OBJ_RODS:
        if (verbose)
        {
            desc += "\n" +
                jtrans("\nIt uses its own magic reservoir for casting spells, and "
                       "recharges automatically according to the recharging "
                       "rate.");

            const int max_charges = MAX_ROD_CHARGE;
            const int max_recharge_rate = MAX_WPN_ENCHANT;
            if (item_ident(item, ISFLAG_KNOW_PLUSES))
            {
                const int num_charges = item.charge_cap / ROD_CHARGE_MULT;
                if (max_charges > num_charges)
                {
                    desc += "\n" + make_stringf(jtransc("It can currently hold %d"
                                                        " charges. It can be magically "
                                                        "recharged to contain up to %d"
                                                        " charges."),
                                                num_charges, max_charges);
                }
                else
                    desc += "\n" + jtrans("\nIts capacity can be increased no further.");

                const int recharge_rate = item.special;
                if (recharge_rate < max_recharge_rate)
                {
                    desc += "\n" + make_stringf(jtransc("Its current recharge rate is %+d"
                                                        ". It can be magically "
                                                        "recharged up to +%d."),
                                                recharge_rate, max_recharge_rate);
                }
                else
                    desc += "\n" + jtrans("\nIts recharge rate is at maximum.");
            }
            else
            {
                desc += "\n" + make_stringf(jtransc("\nIt can have at most %d"
                                                    " charges and +%d"
                                                    " recharge rate."),
                                            max_charges, max_recharge_rate);
            }
        }
        else if (Options.dump_book_spells)
        {
            desc += describe_item_spells(item);
        }

        {
            string stats = "\n";
            append_weapon_stats(stats, item);
            desc += stats;
        }
        desc += "\n\n" + jtrans("\n\nIt falls into the 'Maces & Flails' category.");
        break;

    case OBJ_STAVES:
        {
            string stats = "\n";
            append_weapon_stats(stats, item);
            desc += stats;
        }
        desc += "\n\n" + jtrans("\n\nIt falls into the 'Staves' category. ");
        desc += jtrans(_handedness_string(item));
        break;

    case OBJ_MISCELLANY:
        if (is_deck(item))
            desc += _describe_deck(item);
        if (is_xp_evoker(item))
        {
            desc += "\n" + jtrans("\nOnce released, the spirits of this device will "
                                   "depart, leaving it ");

            if (!item_is_horn_of_geryon(item))
                desc += jtrans("and all other devices of its kind ");

            desc += jtrans("inert. However, more spirits will be attracted as "
                                  "its bearer grows in power and wisdom.");

            if (!evoker_is_charged(item))
            {
                mpr(jtrans("The device is presently inert. Gaining experience will "
                           "recharge it."));
            }
        }
        break;

    case OBJ_POTIONS:
#ifdef DEBUG_BLOOD_POTIONS
        // List content of timer vector for blood potions.
        if (!dump && is_blood_potion(item))
        {
            item_def stack = static_cast<item_def>(item);
            CrawlHashTable &props = stack.props;
            if (!props.exists("timer"))
                description << "\nTimers not yet initialized.";
            else
            {
                CrawlVector &timer = props["timer"].get_vector();
                ASSERT(!timer.empty());

                description << "\nQuantity: " << stack.quantity
                            << "        Timer size: " << (int) timer.size();
                description << "\nTimers:\n";
                for (int i = 0; i < timer.size(); ++i)
                    description << (timer[i].get_int()) << "  ";
            }
        }
#endif

    case OBJ_SCROLLS:
    case OBJ_ORBS:
    case OBJ_GOLD:
        // No extra processing needed for these item types.
        break;

    default:
        die("Bad item class");
    }
    if (!desc.empty())
        description << "\n" << desc;

    if (!verbose && item_known_cursed(item))
        description << "\n" << jtrans("\nIt has a curse placed upon it.");
    else
    {
        if (verbose)
        {
            if (item_known_cursed(item))
                description << "\n" << jtrans("\nIt has a curse placed upon it.");

            if (is_artefact(item))
            {
                if (item.base_type == OBJ_ARMOUR
                    || item.base_type == OBJ_WEAPONS)
                {
                    description << "\n" << jtrans("\nThis ancient artefact cannot be changed "
                                                  "by magic or mundane means.");
                }
                // Randart jewellery has already displayed this line.
                else if (item.base_type != OBJ_JEWELLERY
                         || (item_type_known(item) && is_unrandom_artefact(item)))
                {
                    description << "\n" << jtrans("It is an ancient artefact.");
                }
            }
        }
    }

    if (conduct_type ct = good_god_hates_item_handling(item))
    {
        description << "\n\n" << jtrans(god_name(you.religion))
                    << jtrans(" opposes the use of such an ");

        if (ct == DID_NECROMANCY)
            description << jtrans("evil");
        else
            description << jtrans("unholy");

        description << "アイテムを使用することにいい顔をしない。";
    }
    else if (god_hates_item_handling(item))
    {
        description << "\n\n" << jtrans(god_name(you.religion))
                    << jtrans(" disapproves of the use of such an item.");
    }

    if (verbose && origin_describable(item))
        description << "\n\n" << origin_desc(item, true);

    // This information is obscure and differs per-item, so looking it up in
    // a docs file you don't know to exist is tedious.  On the other hand,
    // it breaks the screen for people on very small terminals.
    if (verbose && get_number_of_lines() >= 28)
    {
        description << "\n\n" << jtrans("Stash search prefixes: ") + " "
                    << userdef_annotate_item(STASH_LUA_SEARCH_ANNOTATE, &item);
        string menu_prefix = item_prefix(item, false);
        if (!menu_prefix.empty())
            description << "\n" << jtrans("\nMenu/colouring prefixes: ") << " " << menu_prefix;
    }

    if (verbose && !noquote && (!item_type_known(item) || !is_random_artefact(item)))
    {
        const unsigned int lineWidth = get_number_of_cols();
        const          int height    = get_number_of_lines();
        string quote;
        if (is_unrandom_artefact(item) && item_type_known(item))
            quote = getQuoteString(get_artefact_name(item));
        else
            quote = getQuoteString(item.name_en(DESC_DBNAME, true, false, false));

        if (count_desc_lines(description.str(), lineWidth)
            + count_desc_lines(quote, lineWidth) < height)
        {
            if (!quote.empty())
                description << "\n\n" << quote;
        }
    }

    return description.str();
}

void get_feature_desc(const coord_def &pos, describe_info &inf)
{
    dungeon_feature_type feat = env.map_knowledge(pos).feat();

    string desc      = feature_description_at(pos, false, DESC_A, false);
    string desc_en   = feature_description_at_en(pos, false, DESC_A, false);
    string db_name   = feat == DNGN_ENTER_SHOP ? "a shop" : desc_en;
    string long_desc = getLongDescription(db_name);
    string spacer    = _spacer(get_number_of_cols() - strwidth(desc)
                                                    - strwidth(desc_en) - 1);

    inf.title = desc + spacer + (desc != desc_en ? desc_en : "");

    inf.title = sp2nbsp(inf.title);

    if (strwidth(desc) + strwidth(desc_en) + 5 > get_number_of_cols())
    {
        // e.g. "a wall of the weird stuff which makes up Pandemonium"
        inf.title = desc;
    }

    // If we couldn't find a description in the database then see if
    // the feature's base name is different.
    if (long_desc.empty())
    {
        desc_en   = feature_description_at_en(pos, false, DESC_A, false, true);
        long_desc = getLongDescription(desc_en);
    }

    const string marker_desc =
        env.markers.property_at(pos, MAT_ANY, "feature_description_long");

    // suppress this if the feature changed out of view
    if (!marker_desc.empty() && grd(pos) == feat)
        long_desc += sp2nbsp(marker_desc);

    // Display branch descriptions on the entries to those branches.
    if (feat_is_stair(feat))
    {
        for (branch_iterator it; it; ++it)
        {
            if (it->entry_stairs == feat)
            {
                long_desc += "\n";
                long_desc += getLongDescription(it->shortname);
                break;
            }
        }
    }

    // mention the ability to pray at altars
    if (feat_is_altar(feat))
        long_desc += "\n" + jtransln("(Pray here to learn more.)\n");

    inf.body << long_desc;

    if (const cloud_type cloud = env.map_knowledge(pos).cloud())
    {
        const string cl_name = cloud_type_name_j(cloud);
        const string cl_name_en = cloud_type_name(cloud);
        const string cl_desc = getLongDescription(cl_name_en + " cloud");
        inf.body << "\n" << jtrans(cl_name) << "が漂っている。"
                 << (cl_desc.empty() ? "" : "\n\n")
                 << cl_desc;
    }

    inf.quote = getQuoteString(db_name);
}

/// A message explaining how the player can toggle between quote &
static const string _toggle_message =
    "Press '<w>!</w>'"
#ifdef USE_TILE_LOCAL
    " or <w>Right-click</w>"
#endif
    " to toggle between the description and quote.";

/**
 * If the given description has an associated quote, print a message at the
 * bottom of the screen explaining how the player can toggle between viewing
 * that quote & the description, and then check whether the input corresponds
 * to such a toggle.
 *
 * @param inf[in]       The description in question.
 * @param key[in,out]   The input command. If zero, is set to getchm().
 * @return              Whether the description & quote should be toggled.
 */
static int _print_toggle_message(const describe_info &inf, int& key)
{
    mouse_control mc(MOUSE_MODE_MORE);

    if (inf.quote.empty())
    {
        if (!key)
            key = getchm();
        return false;
    }

    const int bottom_line = min(30, get_number_of_lines());
    cgotoxy(1, bottom_line);
    formatted_string::parse_string(jtrans(_toggle_message)).display();
    if (!key)
        key = getchm();

    if (key == '!' || key == CK_MOUSE_CMD)
        return true;

    return false;
}

void describe_feature_wide(const coord_def& pos, bool show_quote)
{
    describe_info inf;
    get_feature_desc(pos, inf);

#ifdef USE_TILE_WEB
    tiles_crt_control show_as_menu(CRT_MENU, "describe_feature");
#endif

    if (show_quote)
        _print_quote(inf);
    else
        print_description(inf);

    if (crawl_state.game_is_hints())
        hints_describe_pos(pos.x, pos.y);

    int key = 0;
    if (_print_toggle_message(inf, key))
        describe_feature_wide(pos, !show_quote);
}

void get_item_desc(const item_def &item, describe_info &inf)
{
    // Don't use verbose descriptions if the item contains spells,
    // so we can actually output these spells if space is scarce.
    const bool verbose = !item.has_spells();
    inf.body << get_item_description(item, verbose);
}

// Returns true if spells can be shown to player.
static bool _can_show_spells(const item_def &item)
{
    return item.has_spells()
           && (item.base_type != OBJ_BOOKS || item_type_known(item)
               || player_can_memorise_from_spellbook(item));
}

static void _show_item_description(const item_def &item)
{
    const unsigned int lineWidth = get_number_of_cols() - 1;
    const          int height    = get_number_of_lines();

    string desc = get_item_description(item, true, false,
                                       crawl_state.game_is_hints_tutorial());

    const int num_lines = count_desc_lines(desc, lineWidth) + 1;

    // XXX: hack: Leave room for "Inscribe item?" and the blank line above
    // it by removing item quote.  This should really be taken care of
    // by putting the quotes into a separate DB and treating them as
    // a suffix that can be ignored by print_description().
    if (height - num_lines <= 2)
        desc = get_item_description(item, true, false, true);

    print_description(desc);
    if (crawl_state.game_is_hints())
        hints_describe_item(item);

    if (_can_show_spells(item))
    {
        formatted_string fdesc;
        fdesc.cprintf("%s", desc.c_str());
        list_spellset(item_spellset(item), nullptr, &item, fdesc);
    }
}

static bool _can_memorise(item_def &item)
{
    return item.base_type == OBJ_BOOKS && in_inventory(item)
           && player_can_memorise_from_spellbook(item);
}

static void _update_inscription(item_def &item)
{
    if (item.base_type == OBJ_BOOKS && in_inventory(item)
        && !_can_memorise(item))
    {
        inscribe_book_highlevel(item);
    }
}

// it takes a key and a list of commands and it returns
// the command from the list which corresponds to the key
static command_type _get_action(int key, vector<command_type> actions)
{
    static bool act_key_init = true; // Does act_key needs to be initialise?
    static map<command_type, int> act_key;
    if (act_key_init)
    {
        act_key[CMD_WIELD_WEAPON]       = 'w';
        act_key[CMD_UNWIELD_WEAPON]     = 'u';
        act_key[CMD_QUIVER_ITEM]        = 'q';
        act_key[CMD_WEAR_ARMOUR]        = 'w';
        act_key[CMD_REMOVE_ARMOUR]      = 't';
        act_key[CMD_EVOKE]              = 'v';
        act_key[CMD_EAT]                = 'e';
        act_key[CMD_READ]               = 'r';
        act_key[CMD_WEAR_JEWELLERY]     = 'p';
        act_key[CMD_REMOVE_JEWELLERY]   = 'r';
        act_key[CMD_QUAFF]              = 'q';
        act_key[CMD_DROP]               = 'd';
        act_key[CMD_INSCRIBE_ITEM]      = 'i';
        act_key[CMD_ADJUST_INVENTORY]   = '=';
        act_key_init = false;
    }

    for (auto cmd : actions)
        if (key == act_key[cmd])
            return cmd;

    return CMD_NO_CMD;
}

//---------------------------------------------------------------
//
// _actions_prompt
//
// print a list of actions to be performed on the item
static bool _actions_prompt(item_def &item, bool allow_inscribe, bool do_prompt)
{
#ifdef USE_TILE_LOCAL
    PrecisionMenu menu;
    TextItem* tmp = nullptr;
    MenuFreeform* freeform = new MenuFreeform();
    menu.set_select_type(PrecisionMenu::PRECISION_SINGLESELECT);
    freeform->init(coord_def(1, 1),
                   coord_def(get_number_of_cols(), get_number_of_lines()),
                   "freeform");
    menu.attach_object(freeform);
    menu.set_active_object(freeform);

    BoxMenuHighlighter* highlighter = new BoxMenuHighlighter(&menu);
    highlighter->init(coord_def(0, 0), coord_def(0, 0), "highlighter");
    menu.attach_object(highlighter);
#endif
    string prompt_pre;
    string prompt;
    int keyin;
    vector<command_type> actions;

    if (is_artefact(item))
        prompt_pre = "この" + jtrans(get_artefact_base_name(item)) + "に対し、";
    else
        prompt_pre = "この" + item.name(DESC_BASENAME) + "に対し、";

    actions.push_back(CMD_ADJUST_INVENTORY);
    if (item_equip_slot(item) == EQ_WEAPON)
        actions.push_back(CMD_UNWIELD_WEAPON);
    switch (item.base_type)
    {
    case OBJ_WEAPONS:
    case OBJ_STAVES:
    case OBJ_RODS:
    case OBJ_MISCELLANY:
        if (!item_is_equipped(item))
        {
            if (item_is_wieldable(item))
                actions.push_back(CMD_WIELD_WEAPON);
            if (is_throwable(&you, item))
                actions.push_back(CMD_QUIVER_ITEM);
        }
        break;
    case OBJ_MISSILES:
        if (you.species != SP_FELID)
            actions.push_back(CMD_QUIVER_ITEM);
        break;
    case OBJ_ARMOUR:
        if (item_is_equipped(item))
            actions.push_back(CMD_REMOVE_ARMOUR);
        else
            actions.push_back(CMD_WEAR_ARMOUR);
        break;
    case OBJ_FOOD:
        if (can_eat(item, true, false))
            actions.push_back(CMD_EAT);
        break;
    case OBJ_SCROLLS:
    case OBJ_BOOKS: // only unknown ones
        if (item.sub_type != BOOK_MANUAL)
            actions.push_back(CMD_READ);
        break;
    case OBJ_JEWELLERY:
        if (item_is_equipped(item))
            actions.push_back(CMD_REMOVE_JEWELLERY);
        else
            actions.push_back(CMD_WEAR_JEWELLERY);
        break;
    case OBJ_POTIONS:
        if (you.undead_state() != US_UNDEAD) // mummies and lich form forbidden
            actions.push_back(CMD_QUAFF);
        break;
    default:
        ;
    }
#if defined(CLUA_BINDINGS)
    if (clua.callbooleanfn(false, "ch_item_wieldable", "i", &item))
        actions.push_back(CMD_WIELD_WEAPON);
#endif

    if (item_is_evokable(item))
        actions.push_back(CMD_EVOKE);

    actions.push_back(CMD_DROP);

    if (allow_inscribe)
        actions.push_back(CMD_INSCRIBE_ITEM);

    static bool act_str_init = true; // Does act_str needs to be initialised?
    static map<command_type, string> act_str;
    if (act_str_init)
    {
        act_str[CMD_WIELD_WEAPON]       = jtrans("(w)ield");
        act_str[CMD_UNWIELD_WEAPON]     = jtrans("(u)nwield");
        act_str[CMD_QUIVER_ITEM]        = jtrans("(q)uiver");
        act_str[CMD_WEAR_ARMOUR]        = jtrans("(w)ear");
        act_str[CMD_REMOVE_ARMOUR]      = jtrans("(t)ake off");
        act_str[CMD_EVOKE]              = jtrans("e(v)oke");
        act_str[CMD_EAT]                = jtrans("(e)at");
        act_str[CMD_READ]               = jtrans("(r)ead");
        act_str[CMD_WEAR_JEWELLERY]     = jtrans("(p)ut on");
        act_str[CMD_REMOVE_JEWELLERY]   = jtrans("(r)emove");
        act_str[CMD_QUAFF]              = jtrans("(q)uaff");
        act_str[CMD_DROP]               = jtrans("(d)rop");
        act_str[CMD_INSCRIBE_ITEM]      = jtrans("(i)nscribe");
        act_str[CMD_ADJUST_INVENTORY]   = jtrans("(=)adjust");
        act_str_init = false;
    }

    for (auto at = actions.begin(); at < actions.end(); ++at)
    {
#ifdef USE_TILE_LOCAL
        tmp = new TextItem();
        tmp->set_id(*at);
        tmp->set_text(act_str[*at]);
        tmp->set_fg_colour(CYAN);
        tmp->set_bg_colour(BLACK);
        tmp->set_highlight_colour(LIGHTGRAY);
        tmp->set_description_text(act_str[*at]);
        tmp->set_bounds(coord_def(prompt.size() + 1, wherey()),
                        coord_def(prompt.size() + act_str[*at].size() + 1,
                                  wherey() + 1));
        freeform->attach_item(tmp);
        tmp->set_visible(true);
#endif
        prompt += act_str[*at];
        if (at <= actions.end() - 2)
            prompt += "/";
    }
    prompt += "ことができる。";

    prompt_pre = "<cyan>" + prompt_pre;
    prompt += "</cyan>";
    if (do_prompt)
    {
        if(strwidth(prompt_pre) + strwidth(prompt) - 13 > get_number_of_cols()) // 13 for tags
            prompt_pre += "\n";

        formatted_string::parse_string(prompt_pre + prompt).display();
    }

#ifdef TOUCH_UI

    //draw menu over the top of the prompt text
    tiles.get_crt()->attach_menu(&menu);
    freeform->set_visible(true);
    highlighter->set_visible(true);
    menu.draw_menu();
#endif

    keyin = toalower(getch_ck());
#ifdef USE_TILE_LOCAL
    while (keyin == CK_REDRAW)
    {
        menu.draw_menu();
        keyin = toalower(getch_ck());
    }
#endif
    command_type action = _get_action(keyin, actions);

#ifdef TOUCH_UI
    if (menu.process_key(keyin))
    {
        vector<MenuItem*> selection = menu.get_selected_items();
        if (selection.size() == 1)
            action = (command_type) selection.at(0)->get_id();
    }
#endif

    const int slot = item.link;
    ASSERT_RANGE(slot, 0, ENDOFPACK);

    switch (action)
    {
    case CMD_WIELD_WEAPON:
        redraw_screen();
        wield_weapon(true, slot);
        return false;
    case CMD_UNWIELD_WEAPON:
        redraw_screen();
        wield_weapon(true, SLOT_BARE_HANDS);
        return false;
    case CMD_QUIVER_ITEM:
        redraw_screen();
        quiver_item(slot);
        return false;
    case CMD_WEAR_ARMOUR:
        redraw_screen();
        wear_armour(slot);
        return false;
    case CMD_REMOVE_ARMOUR:
        redraw_screen();
        takeoff_armour(slot);
        return false;
    case CMD_EVOKE:
        redraw_screen();
        evoke_item(slot);
        return false;
    case CMD_EAT:
        redraw_screen();
        eat_food(slot);
        return false;
    case CMD_READ:
    {
        const bool spellbook =
#if TAG_MAJOR_VERSION == 34
            item.sub_type != BOOK_BUGGY_DESTRUCTION &&
#endif
            item.base_type == OBJ_BOOKS;

        if (!spellbook)
            redraw_screen();
        read(slot);
        // In case of a book, stay in the inventory to see the content.
        return spellbook;
    }
    case CMD_WEAR_JEWELLERY:
        redraw_screen();
        puton_ring(slot);
        return false;
    case CMD_REMOVE_JEWELLERY:
        redraw_screen();
        remove_ring(slot, true);
        return false;
    case CMD_QUAFF:
        redraw_screen();
        drink(slot);
        return false;
    case CMD_DROP:
        redraw_screen();
        drop_item(slot, you.inv[slot].quantity);
        return false;
    case CMD_INSCRIBE_ITEM:
        inscribe_item(item, false);
        break;
    case CMD_ADJUST_INVENTORY:
        _adjust_item(item);
        return false;
    case CMD_NO_CMD:
    default:
        return true;
    }
    return true;
}

//---------------------------------------------------------------
//
// describe_item
//
// Describes all items in the game.
// Returns false if we should break out of the inventory loop.
//---------------------------------------------------------------
bool describe_item(item_def &item, bool allow_inscribe, bool shopping)
{
    if (!item.defined())
        return true;

#ifdef USE_TILE_WEB
    tiles_crt_control show_as_menu(CRT_MENU, "describe_item");
#endif

    // we might destroy the item, so save this first.
    const bool item_had_spells = _can_show_spells(item);
    _show_item_description(item);

    // spellbooks & rods have their own UIs, so we don't currently support the
    // inscribe/drop/etc prompt UI for them.
    // ...it would be nice if we did, though.
    if (item_had_spells)
    {
        // only continue the inventory loop if we didn't start memorizing a
        // spell & didn't destroy the item for amnesia.
        return !already_learning_spell() && item.is_valid();
    }

    _update_inscription(item);

    if (allow_inscribe && crawl_state.game_is_tutorial())
        allow_inscribe = false;

    // Don't ask if we're dead.
    if (in_inventory(item) && crawl_state.prev_cmd != CMD_RESISTS_SCREEN
        && !(you.dead || crawl_state.updating_scores))
    {
        // Don't draw the prompt if there aren't enough rows left.
        const bool do_prompt = wherey() <= get_number_of_lines() - 2;
        if (do_prompt)
            cgotoxy(1, wherey() + 2);
        return _actions_prompt(item, allow_inscribe, do_prompt);
    }
    else
        getchm();

    return true;
}

static void _safe_newline()
{
    if (wherey() == get_number_of_lines())
    {
        cgotoxy(1, wherey());
        formatted_string::parse_string(string(80, ' ')).display();
        cgotoxy(1, wherey());
    }
    else
        formatted_string::parse_string("\n").display();
}

// There are currently two ways to inscribe an item:
// * using the inscribe command ('{') -> msgwin = true
// * from the inventory when viewing an item -> msgwin = false
//
// msgwin also controls whether a hints mode explanation can be
// shown, or whether the pre- and post-inscription item names need to be
// printed.
void inscribe_item(item_def &item, bool msgwin)
{
    if (msgwin)
        mprf_nocap(MSGCH_EQUIPMENT, "%s", item.name(DESC_INVENTORY).c_str());

    const bool is_inscribed = !item.inscription.empty();
    string prompt = jtrans(is_inscribed ? "Replace inscription with what?"
                                        : "Inscribe with what?") + " ";

    char buf[79];
    int ret;
    if (msgwin)
    {
        ret = msgwin_get_line(prompt, buf, sizeof buf, nullptr,
                              item.inscription);
    }
    else
    {
        _safe_newline();
        prompt = "<cyan>" + prompt + "</cyan>";
        formatted_string::parse_string(prompt).display();
        ret = cancellable_get_line(buf, sizeof buf, nullptr, nullptr,
                                  item.inscription);
    }

    if (ret)
    {
        if (msgwin)
            canned_msg(MSG_OK);
        return;
    }

    // Strip spaces from the end.
    for (int i = strlen(buf) - 1; i >= 0; --i)
    {
        if (isspace(buf[i]))
            buf[i] = 0;
        else
            break;
    }

    if (item.inscription == buf)
    {
        if (msgwin)
            canned_msg(MSG_OK);
        return;
    }

    item.inscription = buf;

    if (msgwin)
    {
        mprf_nocap(MSGCH_EQUIPMENT, "%s", item.name(DESC_INVENTORY).c_str());
        you.wield_change  = true;
        you.redraw_quiver = true;
    }
}

static void _adjust_item(item_def &item)
{
    _safe_newline();
    string prompt = jtrans("<cyan>Adjust to which letter? </cyan>");
    formatted_string::parse_string(prompt).display();
    const int keyin = getch_ck();
    // TODO: CK_RESIZE?

    if (isaalpha(keyin))
    {
        const int a = letter_to_index(item.slot);
        const int b = letter_to_index(keyin);
        swap_inv_slots(a,b,true);
    }
}

/**
 * List the simple calculated stats of a given spell, when cast by the player
 * in their current condition.
 *
 * @param spell     The spell in question.
 * @param rod       Whether the spell is being cast from a rod (not a book).
 */
static string _player_spell_stats(const spell_type spell, bool rod)
{
    string description;
    description += "\n" + make_stringf(jtransc("\nLevel: %d"), spell_difficulty(spell));
    if (!rod)
    {
        const string schools = spell_schools_string(spell);
        description += "        " +
            make_stringf(jtransc("        School%s: %s"),
                         schools.c_str());

        if (!crawl_state.need_save
            || (get_spell_flags(spell) & SPFLAG_MONSTER))
        {
            return description; // all other info is player-dependent
        }

        const string failure = failure_rate_to_string(raw_spell_fail(spell));
        description += "        " +
            make_stringf(jtransc("        Fail: %s"), failure.c_str());
    }

    description += "\n\n魔法威力  : ";
    description += spell_power_string(spell, rod);
    description += "\n射程距離  : ";
    description += spell_range_string(spell, rod);
    description += "\n満腹度消費: ";
    description += spell_hunger_string(spell, rod);
    description += "\n騒音発生  : ";
    description += spell_noise_string(spell);
    description += "\n";

    return sp2nbsp(description);
}

string get_skill_description(skill_type skill, bool need_title)
{
    string lookup_en = skill_name(skill);
    string lookup = jtrans(lookup_en);
    string result = "";
    string spacer = _spacer(get_number_of_cols() - strwidth(lookup)
                                                 - strwidth(lookup_en) - 1);

    if (need_title)
    {
        result = lookup + spacer + (lookup != lookup_en ? lookup_en : "");
        result += "\n\n";
    }

    result += getLongDescription(lookup_en);

    switch (skill)
    {
        case SK_INVOCATIONS:
            if (you.species == SP_DEMIGOD)
            {
                result += "\n";
                result += jtrans("How on earth did you manage to pick this up?");
            }
            else if (you_worship(GOD_TROG))
            {
                result += "\n";
                result += jtrans("Note that Trog doesn't use Invocations, due to its "
                                 "close connection to magic.");
            }
            else if (you_worship(GOD_NEMELEX_XOBEH))
            {
                result += "\n";
                result += jtrans("Note that Nemelex uses Evocations rather than "
                                 "Invocations.");
            }
            break;

        case SK_EVOCATIONS:
            if (you_worship(GOD_NEMELEX_XOBEH))
            {
                result += "\n";
                result += jtrans("This is the skill all of Nemelex's abilities rely "
                                 "on.");
            }
            break;

        case SK_SPELLCASTING:
            if (you_worship(GOD_TROG))
            {
                result += "\n";
                result += jtrans("Keep in mind, though, that Trog will greatly "
                                 "disapprove of this.");
            }
            break;
        default:
            // No further information.
            break;
    }

    return sp2nbsp(result);
}

/**
 * What are the odds of the given spell, cast by a monster with the given
 * spell_hd, affecting the player?
 */
static int _hex_chance(const spell_type spell, const int hd)
{
    const int pow = mons_power_for_hd(spell, hd, false) / ENCH_POW_FACTOR;
    const int chance = hex_success_chance(you.res_magic(), pow, 100, true);
    if (spell == SPELL_STRIP_RESISTANCE)
        return chance + (100 - chance) / 3; // ignores mr 1/3rd of the time
    return chance;
}

/**
 * Describe mostly non-numeric player-specific information about a spell.
 *
 * (E.g., your god's opinion of it, whether it's in a high-level book that
 * you can't memorize from, whether it's currently useless for whatever
 * reason...)
 *
 * @param spell     The spell in question.
 * @param item      The object the spell is in; may be null.
 */
static string _player_spell_desc(spell_type spell, const item_def* item)
{
    if (!crawl_state.need_save || (get_spell_flags(spell) & SPFLAG_MONSTER))
        return ""; // all info is player-dependent

    string description;

    // Report summon cap
    const int limit = summons_limit(spell);
    if (limit)
    {
        description += make_stringf(jtranslnc("You can sustain at most %d"
                                              " creature%s"
                                              " summoned by this spell.\n"),
                                    limit);
    }

    const bool rod = item && item->base_type == OBJ_RODS;
    if (god_hates_spell(spell, you.religion, rod))
    {
        description += jtrans(god_name(you.religion))
                     + jtransln(" frowns upon the use of this spell.\n");
        if (god_loathes_spell(spell, you.religion))
            description += jtransln("You'd be excommunicated if you dared to cast it!\n");
    }
    else if (god_likes_spell(spell, you.religion))
    {
        description += jtrans(god_name(you.religion))
                     + jtransln(" supports the use of this spell.\n");
    }

    if (item && !player_can_memorise_from_spellbook(*item))
    {
        description += jtransln("The spell is scrawled in ancient runes that are "
                                "beyond your current level of understanding.\n");
    }

    if (spell_is_useless(spell, true, false, rod) && you_can_memorise(spell))
    {
        description += "\n" + jtrans("This spell will have no effect right now:") + " "
                     + jtrans(spell_uselessness_reason(spell, true, false, rod))
                     + "\n";
    }

    return sp2nbsp(description);
}


/**
 * Describe a spell, as cast by the player.
 *
 * @param spell     The spell in question.
 * @param item      The object the spell is in; may be null.
 * @return          Information about the spell; does not include the title or
 *                  db description, but does include level, range, etc.
 */
string player_spell_desc(spell_type spell, const item_def* item)
{
    const bool rod = item && item->base_type == OBJ_RODS;
    return _player_spell_stats(spell, rod)
           + _player_spell_desc(spell, item);
}

/**
 * Examine a given spell. Set the given string to its description, stats, &c.
 * If it's a book in a spell that the player is holding, mention the option to
 * memorize or forget it.
 *
 * @param spell         The spell in question.
 * @param mon_owner     If this spell is being examined from a monster's
 *                      description, 'spell' is that monster. Else, null.
 * @param description   Set to the description & details of the spell.
 * @param item          The item (book or rod) holding the spell, if any.
 * @return              BOOK_MEM if you can memorise the spell
 *                      BOOK_FORGET if you can forget it
 *                      BOOK_NEITHER if you can do neither.
 */
static int _get_spell_description(const spell_type spell,
                                  const monster_info *mon_owner,
                                  string &description,
                                  const item_def* item = nullptr)
{
    description.reserve(500);

    description = tagged_jtrans("[spell]", spell_title(spell));

    string spacer = _spacer(get_number_of_cols() - strwidth(description)
                                                 - strwidth(spell_title(spell)) - 1);

    description = description + spacer + spell_title(spell);
    description = sp2nbsp(description);
    description += "\n\n";
    const string long_descrip = getLongDescription(string(spell_title(spell))
                                                   + " spell");

    if (!long_descrip.empty())
        description += long_descrip;
    else
    {
        description += "This spell has no description. "
                       "Casting it may therefore be unwise. "
#ifdef DEBUG
                       "Instead, go fix it. ";
#else
                       "Please file a bug report.";
#endif
    }

    if (mon_owner)
    {
        // FIXME: this HD is wrong in some cases
        // (draining, malmutation, levelling up)
        const int hd = mons_class_hit_dice(mon_owner->type);
        const int range = mons_spell_range(spell, hd);
        description += "\n" + jtrans("Range :") + " "
                       + range_string(range, range, mons_char(mon_owner->type))
                       + "\n";

        // only display this if the player exists (not in the main menu)
        if (crawl_state.need_save && (get_spell_flags(spell) & SPFLAG_MR_CHECK))
        {
            description += make_stringf(jtranslnc("Chance to beat your MR: %d%%\n"),
                                        _hex_chance(spell, hd));
        }

    }
    else
        description += player_spell_desc(spell, item);

    // Don't allow memorization or amnesia after death.
    // (In the post-game inventory screen.)
    if (crawl_state.player_is_dead())
        return BOOK_NEITHER;

    const string quote = getQuoteString(string(spell_title(spell)) + " spell");
    if (!quote.empty())
        description += "\n" + quote;

    if (!mon_owner && !you_can_memorise(spell))
        description += "\n" + desc_cannot_memorise_reason(spell) + "\n";

    if (item && item->base_type == OBJ_BOOKS && in_inventory(*item))
    {
        if (you.has_spell(spell))
        {
            description += "\n" + jtransln("(F)orget this spell by destroying the book.\n");
            if (you_worship(GOD_SIF_MUNA))
                description += jtransln("Sif Muna frowns upon the destroying of books.\n");
            return BOOK_FORGET;
        }
        else if (player_can_memorise_from_spellbook(*item)
                 && you_can_memorise(spell))
        {
            description += "\n" + jtransln("(M)emorise this spell.\n");
            return BOOK_MEM;
        }
    }

    return BOOK_NEITHER;
}

/**
 * Provide the text description of a given spell.
 *
 * @param spell     The spell in question.
 * @param inf[out]  The spell's description is concatenated onto the end of
 *                  inf.body.
 */
void get_spell_desc(const spell_type spell, describe_info &inf)
{
    string desc;

    _get_spell_description(spell, nullptr, desc);
    inf.body << sp2nbsp(desc);
}


/**
 * Examine a given spell. List its description and details, and handle
 * memorizing or forgetting the spell in question, if the player is able to
 * do so & chooses to.
 *
 * @param spelled   The spell in question.
 * @param mon_owner If this spell is being examined from a monster's
 *                  description, 'mon_owner' is that monster. Else, null.
 * @param item      The item (book or rod) holding the spell, if any.
 */
void describe_spell(spell_type spelled, const monster_info *mon_owner,
                    const item_def* item)
{
#ifdef USE_TILE_WEB
    tiles_crt_control show_as_menu(CRT_MENU, "describe_spell");
#endif

    string desc;
    const int mem_or_forget = _get_spell_description(spelled, mon_owner, desc,
                                                     item);
    print_description(desc);

    mouse_control mc(MOUSE_MODE_MORE);
    char ch;
    if ((ch = getchm()) == 0)
        ch = getchm();

    if (mem_or_forget == BOOK_MEM && toupper(ch) == 'M')
    {
        redraw_screen();
        if (!learn_spell(spelled) || !you.turn_is_over)
            more();
        redraw_screen();
    }
    else if (mem_or_forget == BOOK_FORGET && toupper(ch) == 'F')
    {
        redraw_screen();
        if (!forget_spell_from_book(spelled, item) || !you.turn_is_over)
            more();
        redraw_screen();
    }
}

static string _describe_draconian(const monster_info& mi)
{
    string description;
    const int subsp = mi.draco_or_demonspawn_subspecies();

    if (subsp != mi.type)
    {
        description += "このモンスターは";

        switch (subsp)
        {
        case MONS_BLACK_DRACONIAN:      description += jtrans("black ");   break;
        case MONS_MOTTLED_DRACONIAN:    description += jtrans("mottled "); break;
        case MONS_YELLOW_DRACONIAN:     description += jtrans("yellow ");  break;
        case MONS_GREEN_DRACONIAN:      description += jtrans("green ");   break;
        case MONS_PURPLE_DRACONIAN:     description += jtrans("purple ");  break;
        case MONS_RED_DRACONIAN:        description += jtrans("red ");     break;
        case MONS_WHITE_DRACONIAN:      description += jtrans("white ");   break;
        case MONS_GREY_DRACONIAN:       description += jtrans("grey ");    break;
        case MONS_PALE_DRACONIAN:       description += jtrans("pale ");    break;
        default:
            break;
        }

        description += "鱗を持っている。\n";
    }

    switch (subsp)
    {
    case MONS_BLACK_DRACONIAN:
        description += jtrans("Sparks flare out of its mouth and nostrils.");
        break;
    case MONS_MOTTLED_DRACONIAN:
        description += jtrans("Liquid flames drip from its mouth.");
        break;
    case MONS_YELLOW_DRACONIAN:
        description += jtrans("Acidic fumes swirl around it.");
        break;
    case MONS_GREEN_DRACONIAN:
        description += jtrans("Venom drips from its jaws.");
        break;
    case MONS_PURPLE_DRACONIAN:
        description += jtrans("Its outline shimmers with magical energy.");
        break;
    case MONS_RED_DRACONIAN:
        description += jtrans("Smoke pours from its nostrils.");
        break;
    case MONS_WHITE_DRACONIAN:
        description += jtrans("Frost pours from its nostrils.");
        break;
    case MONS_GREY_DRACONIAN:
        description += jtrans("Its scales and tail are adapted to the water.");
        break;
    case MONS_PALE_DRACONIAN:
        description += jtrans("It is cloaked in a pall of superheated steam.");
        break;
    default:
        break;
    }

    return description;
}

static string _describe_chimera(const monster_info& mi)
{
    string description = "このモンスターは";

    description += apply_description_j(DESC_A, get_monster_data(mi.base_type)->name);

    monster_type part2 = get_chimera_part(&mi,2);
    description += "の頭と";
    if (part2 == mi.base_type)
    {
        description += "別の";
        description += apply_description_j(DESC_PLAIN,
                                           get_monster_data(part2)->name) + "の";
    }
    else
        description += apply_description_j(DESC_A, get_monster_data(part2)->name) + "の";

    monster_type part3 = get_chimera_part(&mi,3);
    description += "頭、そして";
    if (part3 == mi.base_type || part3 == part2)
    {
        if (part2 == mi.base_type)
            description += "さらに";
        description += "別の";
        description += apply_description_j(DESC_PLAIN,
                                           get_monster_data(part3)->name) + "の";
    }
    else
        description += apply_description_j(DESC_A, get_monster_data(part3)->name) + "の";

    description += "頭を持つ。\nこのモンスターは";
    description += apply_description_j(DESC_A,
                                       get_monster_data(mi.base_type)->name);
    description += "の体をして";

    const bool has_wings = mi.props.exists("chimera_batty")
                           || mi.props.exists("chimera_wings");

    if (mi.props.exists("chimera_legs") || has_wings)
        description += "おり、";

    if (mi.props.exists("chimera_legs"))
    {
        const monster_type leggy_part =
            get_chimera_part(&mi, mi.props["chimera_legs"].get_int());
        description += apply_description_j(DESC_A,
                                           get_monster_data(leggy_part)->name);

        description += "の肢";
        if (has_wings)
            description += "と";
        else
            description += "を持って";
    }

    if (has_wings)
    {
        monster_type wing_part = mi.props.exists("chimera_batty") ?
            get_chimera_part(&mi, mi.props["chimera_batty"].get_int())
            : get_chimera_part(&mi, mi.props["chimera_wings"].get_int());

        description += apply_description_j(DESC_A,
                                           get_monster_data(wing_part)->name);
        switch (mons_class_flies(wing_part))
        {
        case FL_WINGED:
            description += "の翼を持って";
            break;
        case FL_LEVITATE:
            description += "のように浮かんで";
            break;
        case FL_NONE:
            description += "のような動きをして"; // Unseen horrors
            break;
        }
    }
    description += "いる。";
    return description;
}

static string _describe_demonspawn_role(monster_type type)
{
    switch (type)
    {
    case MONS_BLOOD_SAINT:
        return jtrans("It weaves powerful and unpredictable spells of devastation.");
    case MONS_CHAOS_CHAMPION:
        return jtrans("It possesses chaotic, reality-warping powers.");
    case MONS_WARMONGER:
        return jtrans("It is devoted to combat, disrupting the magic of its foes as "
                      "it battles endlessly.");
    case MONS_CORRUPTER:
        return jtrans("It corrupts space around itself, and can twist even the very "
                      "flesh of its opponents.");
    case MONS_BLACK_SUN:
        return jtrans("It shines with an unholy radiance, and wields powers of "
                      "darkness from its devotion to the deities of death.");
    default:
        return "";
    }
}

static string _describe_demonspawn_base(int species)
{
    switch (species)
    {
    case MONS_MONSTROUS_DEMONSPAWN:
        return jtrans("It is more beast now than whatever species it is descended from.");
    case MONS_GELID_DEMONSPAWN:
        return jtrans("It is covered in icy armour.");
    case MONS_INFERNAL_DEMONSPAWN:
        return jtrans("It gives off an intense heat.");
    case MONS_PUTRID_DEMONSPAWN:
        return jtrans("It is surrounded by sickly fumes and gases.");
    case MONS_TORTUROUS_DEMONSPAWN:
        return jtrans("It menaces with bony spines.");
    }
    return "";
}

static string _describe_demonspawn(const monster_info& mi)
{
    string description;
    const int subsp = mi.draco_or_demonspawn_subspecies();

    description += _describe_demonspawn_base(subsp);

    if (subsp != mi.type)
    {
        const string demonspawn_role = _describe_demonspawn_role(mi.type);
        if (!demonspawn_role.empty())
            description += "\n" + demonspawn_role;
    }

    return description;
}

static const char* _get_resist_name(mon_resist_flags res_type)
{
    switch (res_type)
    {
    case MR_RES_ELEC:
        return "electricity";
    case MR_RES_POISON:
        return "poison";
    case MR_RES_FIRE:
        return "fire";
    case MR_RES_STEAM:
        return "steam";
    case MR_RES_COLD:
        return "cold";
    case MR_RES_ACID:
        return "acid";
    case MR_RES_ROTTING:
        return "rotting";
    case MR_RES_NEG:
        return "negative energy";
    default:
        return "buggy resistance";
    }
}

static const char* _get_threat_desc(mon_threat_level_type threat)
{
    switch (threat)
    {
    case MTHRT_TRIVIAL: return "harmless";
    case MTHRT_EASY:    return "easy";
    case MTHRT_TOUGH:   return "dangerous";
    case MTHRT_NASTY:   return "extremely dangerous";
    case MTHRT_UNDEF:
    default:            return "buggily threatening";
    }
}

static const char* _describe_attack_flavour(attack_flavour flavour)
{
    switch (flavour)
    {
    case AF_ACID:            return "deal extra acid damage";
    case AF_BLINK:           return "blink self";
    case AF_COLD:            return "deal extra cold damage";
    case AF_CONFUSE:         return "cause confusion";
    case AF_DRAIN_STR:       return "drain strength";
    case AF_DRAIN_INT:       return "drain intelligence";
    case AF_DRAIN_DEX:       return "drain dexterity";
    case AF_DRAIN_STAT:      return "drain strength, intelligence or dexterity";
    case AF_DRAIN_XP:        return "drain skills";
    case AF_ELEC:            return "cause electrocution";
    case AF_FIRE:            return "deal extra fire damage";
    case AF_HUNGER:          return "cause hungering";
    case AF_MUTATE:          return "cause mutations";
    case AF_PARALYSE:        return "cause paralysis";
    case AF_POISON:          return "cause poisoning";
    case AF_POISON_STRONG:   return "cause strong poisoning through resistance";
    case AF_ROT:             return "cause rotting";
    case AF_VAMPIRIC:        return "drain health";
    case AF_KLOWN:           return "cause random powerful effects";
    case AF_DISTORT:         return "cause wild translocation effects";
    case AF_RAGE:            return "cause berserking";
    case AF_STICKY_FLAME:    return "apply sticky flame";
    case AF_CHAOS:           return "cause unpredictable effects";
    case AF_STEAL:           return "steal items";
    case AF_CRUSH:           return "拘束してくる"; // 直接変更
    case AF_REACH:           return "deal damage from a distance";
    case AF_HOLY:            return "deal extra damage to undead and demons";
    case AF_ANTIMAGIC:       return "drain magic";
    case AF_PAIN:            return "cause pain to the living";
    case AF_ENSNARE:         return "ensnare with webbing";
    case AF_ENGULF:          return "engulf with water";
    case AF_PURE_FIRE:       return "deal pure fire damage";
    case AF_DRAIN_SPEED:     return "drain speed";
    case AF_VULN:            return "reduce resistance to hostile enchantments";
    case AF_WEAKNESS_POISON: return "cause poisoning and weakness";
    case AF_SHADOWSTAB:      return "deal extra damage from the shadows";
    case AF_DROWN:           return "deal drowning damage";
    case AF_FIREBRAND:       return "deal extra fire damage and surround the defender with flames";
    case AF_CORRODE:         return "cause corrosion";
    case AF_SCARAB:          return "drain speed and drain health";
    case AF_TRAMPLE:         return "knock back the defender";
    default:                 return "";
    }
}

static string _monster_attacks_description(const monster_info& mi)
{
    ostringstream result;
    set<attack_flavour> attack_flavours;
    vector<string> attack_descs;
    // Weird attack types that act like attack flavours.
    bool reach_sting = false;

    for (const auto &attack : mi.attack)
    {
        attack_flavour af = attack.flavour;
        if (!attack_flavours.count(af))
        {
            attack_flavours.insert(af);
            const char * const desc = jtransc(_describe_attack_flavour(af));
            if (desc[0]) // non-empty
                attack_descs.push_back(desc);
        }

        if (attack.type == AT_REACH_STING)
            reach_sting = true;
    }

    // Assumes nothing has both AT_REACH_STING and AF_REACH.
    if (reach_sting)
        attack_descs.emplace_back(jtrans(_describe_attack_flavour(AF_REACH)));

    if (!attack_descs.empty())
    {
        string pronoun = uppercase_first(mi.pronoun(PRONOUN_SUBJECTIVE));

        if (pronoun == "It" || pronoun == "それ")
            pronoun = "このモンスター";

        result << pronoun << "は" << to_separated_line(attack_descs.begin(), attack_descs.end(), true,
                                                       ("ことがある。\n" + pronoun + "は").c_str(),
                                                       ("ことがある。\n" + pronoun + "は").c_str());
        result << "ことがある。\n";
    }

    return result.str();
}

static string _monster_spells_description(const monster_info& mi)
{
    // Show a generic message for pan lords, since they're secret.
    if (mi.type == MONS_PANDEMONIUM_LORD)
        return jtransln("It may possess any of a vast number of diabolical powers.\n");

    // Ditto for (a)liches.
    if (mi.type == MONS_LICH || mi.type == MONS_ANCIENT_LICH)
        return jtransln("It has mastered any of a vast number of powerful spells.\n");

    // Show monster spells and spell-like abilities.
    if (!mi.has_spells())
        return "";

    formatted_string description;
    describe_spellset(monster_spellset(mi), nullptr, description);
    description.cprintf("\n");
    description.cprintf(jtranslnc("Select a spell to read its description.\n"));
    return description.tostring();
}

static const char *_speed_description(int speed)
{
    // These thresholds correspond to the player mutations for fast and slow.
    ASSERT(speed != 10);
    if (speed < 7)
        return "extremely slowly";
    else if (speed < 8)
        return "very slowly";
    else if (speed < 10)
        return "slowly";
    else if (speed > 15)
        return "extremely quickly";
    else if (speed > 13)
        return "very quickly";
    else if (speed > 10)
        return "quickly";

    return "buggily";
}

static void _add_energy_to_string(int speed, int energy, string what,
                                  vector<string> &fast, vector<string> &slow)
{
    if (energy == 10)
        return;

    const int act_speed = (speed * 10) / energy;
    if (act_speed > 10)
        fast.push_back(tagged_jtrans("[adj]", _speed_description(act_speed))
                       + jconj_verb(jtrans(what), JCONJ_PRES));
    if (act_speed < 10)
        slow.push_back(tagged_jtrans("[adj]", _speed_description(act_speed))
                       + jconj_verb(jtrans(what), JCONJ_PRES));
}


/**
 * Print a bar of +s and .s representing a given stat to a provided stream.
 *
 * @param value[in]         The current value represented by the bar.
 * @param max[in]           The max value that can be represented by the bar.
 * @param scale[in]         The value that each + and . represents.
 * @param name              The name of the bar.
 * @param result[in,out]    The stringstream to append to.
 * @param base_value[in]    The 'base' value represented by the bar. If
 *                          INT_MAX, is ignored.
 */
static void _print_bar(int value, int max, int scale,
                       string name, ostringstream &result,
                       int base_value = INT_MAX)
{
    if (base_value == INT_MAX)
        base_value = value;

    result << name << " ";

    const int cur_bars = value / scale;
    const int base_bars = base_value / scale;
    const int bars = cur_bars ? cur_bars : base_bars;
    const int max_bars = max / scale;

    const bool currently_disabled = !cur_bars && base_bars;

    if (currently_disabled)
        result << "(";

    for (int i = 0; i < min(bars, max_bars); i++)
        result << "+";

    if (currently_disabled)
        result << ")";

    for (int i = max_bars - 1; i >= bars; --i)
        result << ".";

#ifdef DEBUG_DIAGNOSTICS
    result << " (" << value << ")";
#endif

    if (currently_disabled)
    {
        result << " (通常時)";

#ifdef DEBUG_DIAGNOSTICS
        result << " (" << base_value << ")";
#endif
    }

    result << "\n";
}

/**
 * Append information about a given monster's AC to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_ac(const monster_info& mi, ostringstream &result)
{
    // max ac 40 (dispater)
    _print_bar(mi.ac, 40, 5, " AC ", result);
}

/**
 * Append information about a given monster's EV to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_ev(const monster_info& mi, ostringstream &result)
{
    // max ev 30 (eresh) (also to make space for parens)
    _print_bar(mi.ev, 30, 5, "回避", result, mi.base_ev);
}

/**
 * Append information about a given monster's MR to the provided stream.
 *
 * @param mi[in]            Player-visible info about the monster in question.
 * @param result[in,out]    The stringstream to append to.
 */
static void _describe_monster_mr(const monster_info& mi, ostringstream &result)
{
    if (mi.res_magic() == MAG_IMMUNE)
    {
        result << "魔防 ∞\n";
        return;
    }

    const int max_mr = 200; // export this? is this already exported?
    const int bar_scale = MR_PIP;
    _print_bar(mi.res_magic(), max_mr, bar_scale, "魔防", result);
}


// Describe a monster's (intrinsic) resistances, speed and a few other
// attributes.
static string _monster_stat_description(const monster_info& mi)
{
    ostringstream result;

    result << "\n";

    _describe_monster_ac(mi, result);
    _describe_monster_ev(mi, result);
    _describe_monster_mr(mi, result);

    result << "\n";

    resists_t resist = mi.resists();

    const mon_resist_flags resists[] =
    {
        MR_RES_ELEC,    MR_RES_POISON, MR_RES_FIRE,
        MR_RES_STEAM,   MR_RES_COLD,   MR_RES_ACID,
        MR_RES_ROTTING, MR_RES_NEG,
    };

    vector<string> extreme_resists;
    vector<string> high_resists;
    vector<string> base_resists;
    vector<string> suscept;

    for (mon_resist_flags rflags : resists)
    {
        int level = get_resist(resist, rflags);

        if (level != 0)
        {
            const char* attackname = _get_resist_name(rflags);
            level = max(level, -1);
            level = min(level,  3);
            switch (level)
            {
                case -1:
                    suscept.emplace_back(attackname);
                    break;
                case 1:
                    base_resists.emplace_back(attackname);
                    break;
                case 2:
                    high_resists.emplace_back(attackname);
                    break;
                case 3:
                    extreme_resists.emplace_back(attackname);
                    break;
            }
        }
    }

    vector<string> resist_descriptions;
    if (!extreme_resists.empty())
    {
        const string tmp =
            to_separated_line(extreme_resists.begin(),
                              extreme_resists.end(), true, "や", "、", "、")
            + jtrans("immune to");
        resist_descriptions.push_back(tmp);
    }
    if (!high_resists.empty())
    {
        const string tmp =
            to_separated_line(high_resists.begin(),
                              high_resists.end(), true, "や", "、", "、")
            + jtrans("very resistant to ");
        resist_descriptions.push_back(tmp);
    }
    if (!base_resists.empty())
    {
        const string tmp =
            to_separated_line(base_resists.begin(),
                              base_resists.end(), true, "や", "、", "、")
            + jtrans("resistant to ");
        resist_descriptions.push_back(tmp);
    }

    string pronoun = mi.pronoun(PRONOUN_SUBJECTIVE);

    if (pronoun == "It" || pronoun == "それ")
        pronoun = "このモンスター";

    if (mi.threat != MTHRT_UNDEF)
    {
        result << uppercase_first(pronoun) << "は"
               << jtrans(_get_threat_desc(mi.threat))
               << (mi.threat < MTHRT_TOUGH ? "。\n" : "だ。\n");
    }

    if (!resist_descriptions.empty())
    {
        result << uppercase_first(pronoun) << "は"
               << comma_separated_line(resist_descriptions.begin(),
                                       resist_descriptions.end(),
                                       "\n" + pronoun + "は",
                                       "\n" + pronoun + "は")
               << "\n";
    }

    // Is monster susceptible to anything? (On a new line.)
    if (!suscept.empty())
    {
        result << uppercase_first(pronoun) << "は"
               << to_separated_line(suscept.begin(), suscept.end(), true,  "や", "、", "、")
               << jtransln(" is susceptible to ");
    }

    if (mons_class_flag(mi.type, M_STATIONARY)
        && !mons_is_tentacle_or_tentacle_segment(mi.type))
    {
        result << uppercase_first(pronoun) << jtransln(" cannot move.\n");
    }

    // Monsters can glow from both light and radiation.
    if (mons_class_flag(mi.type, M_GLOWS_LIGHT))
        result << uppercase_first(pronoun) << jtransln(" is outlined in light.\n");
    if (mons_class_flag(mi.type, M_GLOWS_RADIATION))
        result << uppercase_first(pronoun) << jtransln(" is glowing with mutagenic radiation.\n");
    if (mons_class_flag(mi.type, M_SHADOW))
        result << uppercase_first(pronoun) << jtransln(" is wreathed in shadows.\n");

    // Seeing invisible.
    if (mi.can_see_invisible())
        result << uppercase_first(pronoun) << jtransln(" can see invisible.\n");

    // Echolocation, wolf noses, jellies, etc
    if (!mons_can_be_blinded(mi.type))
        result << uppercase_first(pronoun) << jtransln(" is immune to blinding.\n");
    // XXX: could mention "immune to dazzling" here, but that's spammy, since
    // it's true of such a huge number of monsters. (undead, statues, plants).
    // Might be better to have some place where players can see holiness &
    // information about holiness.......?


    // Unusual monster speed.
    const int speed = mi.base_speed();
    bool did_speed = false;
    if (speed != 10 && speed != 0)
    {
        did_speed = true;
        result << uppercase_first(pronoun) << jtrans(" is ") << jtrans(mi.speed_description())
               << "。\n";
    }
    const mon_energy_usage def = DEFAULT_ENERGY;
    if (!(mi.menergy == def))
    {
        const mon_energy_usage me = mi.menergy;
        vector<string> fast, slow;
        if (!did_speed)
            result << uppercase_first(pronoun) << "は";
        _add_energy_to_string(speed, me.move, "covers ground", fast, slow);
        // since MOVE_ENERGY also sets me.swim
        if (me.swim != me.move)
            _add_energy_to_string(speed, me.swim, "swims", fast, slow);
        _add_energy_to_string(speed, me.attack, "attacks", fast, slow);
        if (mons_class_itemuse(mi.type) >= MONUSE_STARTING_EQUIPMENT)
            _add_energy_to_string(speed, me.missile, "shoots", fast, slow);
        _add_energy_to_string(
            speed, me.spell,
            mi.is_actual_spellcaster() ? "casts spells" :
            mi.is_priest()             ? "uses invocations"
                                       : "uses natural abilities", fast, slow);
        _add_energy_to_string(speed, me.special, "uses special abilities",
                              fast, slow);
        if (mons_class_itemuse(mi.type) >= MONUSE_STARTING_EQUIPMENT)
            _add_energy_to_string(speed, me.item, "uses items", fast, slow);

        const string pronoun_is = pronoun + "は";
        const string nlpronoun_is = "。\n" + pronoun_is;

        if (speed >= 10)
        {
            if (did_speed && fast.size() == 1)
                result << pronoun_is<< fast[0];
            else if (!fast.empty())
            {
                if (did_speed)
                    result << "、";
                result << comma_separated_line(fast.begin(), fast.end(),
                                               nlpronoun_is,
                                               nlpronoun_is);
            }
            if (!slow.empty())
            {
                if (did_speed || !fast.empty())
                    result << "が、";
                result << comma_separated_line(slow.begin(), slow.end(),
                                               nlpronoun_is,
                                               nlpronoun_is);
            }
        }
        else if (speed < 10)
        {
            if (did_speed && slow.size() == 1)
                result << pronoun_is << slow[0];
            else if (!slow.empty())
            {
                if (did_speed)
                    result << "、";
                result << comma_separated_line(slow.begin(), slow.end(),
                                               nlpronoun_is,
                                               nlpronoun_is);
            }
            if (!fast.empty())
            {
                if (did_speed || !slow.empty())
                    result << "が、";
                result << comma_separated_line(fast.begin(), fast.end(),
                                               nlpronoun_is,
                                               nlpronoun_is);
            }
        }
        result << "。\n";
    }

    // Can the monster fly, and how?
    // This doesn't give anything away since no (very) ugly things can
    // fly, all ghosts can fly, and for demons it's already mentioned in
    // their flavour description.
    switch (mi.fly)
    {
    case FL_NONE:
        break;
    case FL_WINGED:
        result << uppercase_first(pronoun) << jtransln(" can fly.\n");
        break;
    case FL_LEVITATE:
        result << uppercase_first(pronoun) << jtransln(" can fly magically.\n");
        break;
    }

    // Unusual regeneration rates.
    if (!mi.can_regenerate())
        result << uppercase_first(pronoun) << jtransln(" cannot regenerate.\n");
    else if (mons_class_fast_regen(mi.type))
        result << uppercase_first(pronoun) << jtransln(" regenerates quickly.\n");

    // Size
    static const char * const sizes[] =
    {
        "極めて小さい",
        "とても小さい",
        "小さい",
        nullptr,     // don't display anything for 'medium'
        "大きい",
        "とても大きい",
        "巨大だ",
    };
    COMPILE_CHECK(ARRAYSZ(sizes) == NUM_SIZE_LEVELS);

    if (sizes[mi.body_size()])
    {
        result << uppercase_first(pronoun) << jtrans(" is ")
        << sizes[mi.body_size()] << "。\n";
    }

    result << _monster_attacks_description(mi);
    result << _monster_spells_description(mi);

    return result.str();
}

static string _serpent_of_hell_flavour(monster_type m)
{
    switch (m)
    {
    case MONS_SERPENT_OF_HELL_COCYTUS:
        return "cocytus";
    case MONS_SERPENT_OF_HELL_DIS:
        return "dis";
    case MONS_SERPENT_OF_HELL_TARTARUS:
        return "tartarus";
    default:
        return "gehenna";
    }
}

static string _get_unique_title(const string& key)
{
    return jtrans_has_key(key) ? jtrans(key) : "";
}

// Fetches the monster's database description and reads it into inf.
void get_monster_db_desc(const monster_info& mi, describe_info &inf,
                         bool &has_stat_desc, bool force_seen)
{
    string desc, desc_en;
    if (inf.title.empty())
    {
        desc = getMiscString(mi.common_name_en(DESC_DBNAME) + " title");
        desc_en = _get_unique_title(mi.common_name_en(DESC_DBNAME) + " title");
    }
    if (desc_en.empty())
    {
        desc = mi.full_name(DESC_A, true);
        desc_en = uppercase_first(mi.full_name_en(DESC_A, true));
    }

    if (mi.type == MONS_CHIMERA)
        desc_en = "A chimera";

    if (inf.title.empty())
    {
        string spacer = _spacer(get_number_of_cols() - strwidth(desc)
                                                     - strwidth(desc_en) - 1);

        inf.title = desc + spacer + (desc != desc_en ? desc_en : "");
    }
    inf.body << "\n";

    string db_name;

    if (mi.props.exists("dbname"))
        db_name = mi.props["dbname"].get_string();
    else if (mi.mname.empty())
        db_name = mi.db_name();
    else
        db_name = mi.full_name(DESC_PLAIN, true);

    if (mons_species(mi.type) == MONS_SERPENT_OF_HELL)
        db_name += " " + _serpent_of_hell_flavour(mi.type);

    // This is somewhat hackish, but it's a good way of over-riding monsters'
    // descriptions in Lua vaults by using MonPropsMarker. This is also the
    // method used by set_feature_desc_long, etc. {due}
    if (!mi.description.empty())
        inf.body << mi.description;
    // Don't get description for player ghosts.
    else if (mi.type != MONS_PLAYER_GHOST
             && mi.type != MONS_PLAYER_ILLUSION)
    {
        inf.body << getLongDescription(db_name);
    }

    // And quotes {due}
    if (!mi.quote.empty())
        inf.quote = mi.quote;
    else
        inf.quote = getQuoteString(db_name);

    string symbol;
    symbol += get_monster_data(mi.type)->basechar;
    if (isaupper(symbol[0]))
        symbol = "cap-" + symbol;

    string quote2;
    if (!mons_is_unique(mi.type))
    {
        string symbol_prefix = "__" + symbol + "_prefix";
        inf.prefix = getLongDescription(symbol_prefix);

        string symbol_suffix = "__" + symbol + "_suffix";
        quote2 = getQuoteString(symbol_suffix);
    }

    if (!inf.quote.empty() && !quote2.empty())
        inf.quote += "\n";
    inf.quote += quote2;

    switch (mi.type)
    {
    case MONS_VAMPIRE:
    case MONS_VAMPIRE_KNIGHT:
    case MONS_VAMPIRE_MAGE:
        if (you.undead_state() == US_ALIVE && mi.attitude == ATT_HOSTILE)
            inf.body << "\n" + jtransln("It wants to drink your blood!\n");
        break;

    case MONS_REAPER:
        if (you.undead_state(false) == US_ALIVE && mi.attitude == ATT_HOSTILE)
            inf.body <<  "\n" + jtransln("It has come for your soul!\n");
        break;

    case MONS_RED_DRACONIAN:
    case MONS_WHITE_DRACONIAN:
    case MONS_GREEN_DRACONIAN:
    case MONS_PALE_DRACONIAN:
    case MONS_MOTTLED_DRACONIAN:
    case MONS_BLACK_DRACONIAN:
    case MONS_YELLOW_DRACONIAN:
    case MONS_PURPLE_DRACONIAN:
    case MONS_GREY_DRACONIAN:
    case MONS_DRACONIAN_SHIFTER:
    case MONS_DRACONIAN_SCORCHER:
    case MONS_DRACONIAN_ZEALOT:
    case MONS_DRACONIAN_ANNIHILATOR:
    case MONS_DRACONIAN_CALLER:
    case MONS_DRACONIAN_MONK:
    case MONS_DRACONIAN_KNIGHT:
    {
        inf.body << "\n" << _describe_draconian(mi) << "\n";
        break;
    }

    case MONS_MONSTROUS_DEMONSPAWN:
    case MONS_GELID_DEMONSPAWN:
    case MONS_INFERNAL_DEMONSPAWN:
    case MONS_PUTRID_DEMONSPAWN:
    case MONS_TORTUROUS_DEMONSPAWN:
    case MONS_BLOOD_SAINT:
    case MONS_CHAOS_CHAMPION:
    case MONS_WARMONGER:
    case MONS_CORRUPTER:
    case MONS_BLACK_SUN:
    {
        inf.body << "\n" << _describe_demonspawn(mi) << "\n";
        break;
    }

    case MONS_PLAYER_GHOST:
        inf.body << get_ghost_description(mi) << jtransln("The apparition of ");
        break;

    case MONS_PLAYER_ILLUSION:
        inf.body << get_ghost_description(mi) << jtransln("An illusion of ");
        break;

    case MONS_PANDEMONIUM_LORD:
        inf.body << _describe_demon(mi.mname, mi.fly) << "\n";
        break;

    case MONS_CHIMERA:
        inf.body << "\n" << _describe_chimera(mi) << "\n";
        break;

    case MONS_PROGRAM_BUG:
        inf.body << "If this monster is a \"program bug\", then it's "
                "recommended that you save your game and reload.  Please report "
                "monsters who masquerade as program bugs or run around the "
                "dungeon without a proper description to the authorities.\n";
        break;

    default:
        break;
    }

    if (!mons_is_unique(mi.type))
    {
        string symbol_suffix = "__";
        symbol_suffix += symbol;
        symbol_suffix += "_suffix";

        string suffix = getLongDescription(symbol_suffix)
                      + getLongDescription(symbol_suffix + "_examine");

        if (!suffix.empty())
            inf.body << "\n" << suffix;
    }

    // Get information on resistances, speed, etc.
    string result = _monster_stat_description(mi);
    if (!result.empty())
    {
        inf.body << result;
        has_stat_desc = true;
    }

    string pronoun = uppercase_first(mi.pronoun(PRONOUN_SUBJECTIVE));
    if (pronoun == "It" || pronoun == "それ")
        pronoun = "このモンスター";

    bool stair_use = false;
    if (!mons_class_can_use_stairs(mi.type))
    {
        inf.body << "\n" << pronoun
                 << jtransln(" is incapable of using stairs.\n");
        stair_use = true;
    }

    if (mi.intel() <= I_PLANT)
    {
        inf.body << pronoun
                 << jtransln(" is mindless.\n");
    }
    else if (mi.intel() <= I_INSECT && you_worship(GOD_ELYVILON))
    {
        inf.body << pronoun
                 << jtransln(" is not intelligent enough to pacify.\n");
    }


    if (mi.is(MB_CHAOTIC))
    {
        inf.body << pronoun
                 << jtransln(" is vulnerable to silver and hated by Zin.\n");
    }

    if (in_good_standing(GOD_ZIN, 0))
    {
        const int check = mons_class_hit_dice(mi.type) - zin_recite_power();
        if (check >= 0)
        {
            inf.body << pronoun
                     << jtrans(" is too strong to be recited to.");
        }
        else if (check >= -5)
        {
            inf.body << pronoun
                     << jtrans(" may be too strong to be recited to.");
        }
        else
        {
            inf.body << pronoun
                     << jtrans(" is weak enough to be recited to.");
        }
        if (you.wizard)
        {
            inf.body << " (Recite power:" << zin_recite_power()
                     << ", Hit dice:" << mons_class_hit_dice(mi.type) << ")";
        }
        inf.body << "\n";
    }

    if (mi.is(MB_SUMMONED))
    {
        inf.body << "\n" << jtrans("This monster has been summoned, and is thus only "
                       "temporary. Killing it yields no experience, nutrition "
                       "or items");
        if (!stair_use)
            inf.body << jtrans(", and it is incapable of using stairs");
        inf.body << "ない。\n";
    }
    else if (mi.is(MB_PERM_SUMMON))
    {
        inf.body << "\n" << pronoun << jtransln("This monster has been summoned in a durable "
                       "way, and only partially exists. Killing it yields no "
                       "experience, nutrition or items. You cannot easily "
                       "abjure it, though.\n");
    }
    else if (mons_class_leaves_hide(mi.type))
    {
        inf.body << "\n" << pronoun << jtransln("mons class leaves hide");
        /*
        inf.body << "\nIf " << mi.pronoun(PRONOUN_SUBJECTIVE) << " is slain "
        "and butchered, it may be possible to recover "
        << mi.pronoun(PRONOUN_POSSESSIVE) << " hide, which can be "
        "enchanted into armour.\n";
        */
    }

    if (mi.is(MB_SUMMONED_CAPPED))
    {
        inf.body << "\n" << jtransln("You have summoned too many monsters of this kind "
                                     "to sustain them all, and thus this one will "
                                     "shortly expire.\n");
    }

#ifdef DEBUG_DIAGNOSTICS
    if (mi.pos.origin() || !monster_at(mi.pos))
        return; // not a real monster
    monster& mons = *monster_at(mi.pos);

    if (mons.has_originating_map())
    {
        inf.body << make_stringf("\nPlaced by map: %s",
                                 mons.originating_map().c_str());
    }

    inf.body << "\nMonster health: "
             << mons.hit_points << "/" << mons.max_hit_points << "\n";

    const actor *mfoe = mons.get_foe();
    inf.body << "Monster foe: "
             << (mfoe? mfoe->name(DESC_PLAIN, true)
                 : "(none)");

    vector<string> attitude;
    if (mons.friendly())
        attitude.emplace_back("friendly");
    if (mons.neutral())
        attitude.emplace_back("neutral");
    if (mons.good_neutral())
        attitude.emplace_back("good_neutral");
    if (mons.strict_neutral())
        attitude.emplace_back("strict_neutral");
    if (mons.pacified())
        attitude.emplace_back("pacified");
    if (mons.wont_attack())
        attitude.emplace_back("wont_attack");
    if (!attitude.empty())
    {
        string att = comma_separated_line(attitude.begin(), attitude.end(),
                                          "; ", "; ");
        if (mons.has_ench(ENCH_INSANE))
            inf.body << "; frenzied and insane (otherwise " << att << ")";
        else
            inf.body << "; " << att;
    }
    else if (mons.has_ench(ENCH_INSANE))
        inf.body << "; frenzied and insane";

    const monster_spells &hspell_pass = mons.spells;
    bool found_spell = false;

    for (unsigned int i = 0; i < hspell_pass.size(); ++i)
    {
        if (!found_spell)
        {
            inf.body << "\n\nMonster Spells:\n";
            found_spell = true;
        }

        inf.body << "    " << i << ": "
                 << spell_title(hspell_pass[i].spell)
                 << " (";
        if (hspell_pass[i].flags & MON_SPELL_EMERGENCY)
            inf.body << "emergency, ";
        if (hspell_pass[i].flags & MON_SPELL_NATURAL)
            inf.body << "natural, ";
        if (hspell_pass[i].flags & MON_SPELL_DEMONIC)
            inf.body << "demonic, ";
        if (hspell_pass[i].flags & MON_SPELL_MAGICAL)
            inf.body << "magical, ";
        if (hspell_pass[i].flags & MON_SPELL_WIZARD)
            inf.body << "wizard, ";
        if (hspell_pass[i].flags & MON_SPELL_PRIEST)
            inf.body << "priest, ";
        if (hspell_pass[i].flags & MON_SPELL_BREATH)
            inf.body << "breath, ";
        inf.body << (int) hspell_pass[i].freq << ")";
    }

    bool has_item = false;
    for (int i = 0; i < NUM_MONSTER_SLOTS; ++i)
    {
        if (mons.inv[i] != NON_ITEM)
        {
            if (!has_item)
            {
                inf.body << "\n\nMonster Inventory:\n";
                has_item = true;
            }
            inf.body << "    " << i << ": "
                     << mitm[mons.inv[i]].name(DESC_A, false, true);
        }
    }

    if (mons.props.exists("blame"))
    {
        inf.body << "\n\nMonster blame chain:\n";

        const CrawlVector& blame = mons.props["blame"].get_vector();

        for (const auto &entry : blame)
            inf.body << "    " << entry.get_string() << "\n";
    }
#endif
}

int describe_monsters(const monster_info &mi, bool force_seen,
                      const string &footer)
{
    describe_info inf;
    bool has_stat_desc = false;
    get_monster_db_desc(mi, inf, has_stat_desc, force_seen);

    if (!footer.empty())
    {
        if (inf.footer.empty())
            inf.footer = footer;
        else
            inf.footer += "\n" + footer;
    }

#ifdef USE_TILE_WEB
    tiles_crt_control show_as_menu(CRT_MENU, "describe_monster");
#endif

    spell_scroller fs(monster_spellset(mi), &mi, nullptr);
    fs.add_text(inf.title);
    fs.add_text(inf.body.str(), false, get_number_of_cols() - 1);
    if (crawl_state.game_is_hints())
        fs.add_text(hints_describe_monster(mi, has_stat_desc).c_str());

    formatted_scroller qs;

    if (!inf.quote.empty())
    {
        fs.add_item_formatted_string(
                formatted_string::parse_string("\n" + jtrans(_toggle_message)));

        qs.add_text(inf.title);
        qs.add_text("\n" + inf.quote, false, get_number_of_cols() - 1);
        qs.add_item_formatted_string(
                formatted_string::parse_string("\n" + jtrans(_toggle_message)));
    }

    fs.add_item_formatted_string(formatted_string::parse_string(inf.footer));

    bool show_quote = false;
    while (true)
    {
        if (show_quote)
            qs.show();
        else
            fs.show();

        int keyin = (show_quote ? qs : fs).get_lastch();
        // this is never actually displayed to the player
        // we just use it to check whether we should toggle.
        if (_print_toggle_message(inf, keyin))
            show_quote = !show_quote;
        else
            return keyin;
    }
}

static const char* xl_rank_names[] =
{
    "",
    "average",
    "experienced",
    "powerful",
    "mighty",
    "great",
    "awesomely powerful",
    "legendary"
};

static string _xl_rank_name(const int xl_rank)
{
    const string rank = xl_rank_names[xl_rank];

    return rank;
}

string short_ghost_description(const monster *mon, bool abbrev)
{
    ASSERT(mons_is_pghost(mon->type));

    const ghost_demon &ghost = *(mon->ghost);
    const char* rank = xl_rank_names[ghost_level_to_rank(ghost.xl)];

    string desc = make_stringf("%s%sの%s", jtransc(rank),
                               jtransc(species_name(ghost.species)),
                               jtransc(get_job_name(ghost.job)));
    return desc;
}

// Describes the current ghost's previous owner. The caller must
// prepend "The apparition of" or whatever and append any trailing
// punctuation that's wanted.
string get_ghost_description(const monster_info &mi, bool concise)
{
    ostringstream gstr;

    const species_type gspecies = mi.u.ghost.species;

    // We're fudging stats so that unarmed combat gets based off
    // of the ghost's species, not the player's stats... exact
    // stats aren't required anyway, all that matters is whether
    // dex >= str. -- bwr
    const int dex = 10;
    int str = 5;

    switch (gspecies)
    {
    case SP_DEEP_DWARF:
    case SP_TROLL:
    case SP_OGRE:
    case SP_MINOTAUR:
    case SP_HILL_ORC:
#if TAG_MAJOR_VERSION == 34
    case SP_LAVA_ORC:
#endif
    case SP_CENTAUR:
    case SP_NAGA:
    case SP_MUMMY:
    case SP_GHOUL:
    case SP_FORMICID:
    case SP_VINE_STALKER:
        str += 10;
        break;

    case SP_HUMAN:
    case SP_DEMIGOD:
    case SP_DEMONSPAWN:
        str += 5;
        break;

    default:
        break;
    }

    gstr << tagged_jtrans("[skill]",
                          skill_title_by_rank(mi.u.ghost.best_skill,
                                              mi.u.ghost.best_skill_rank,
                                              gspecies,
                                              str, dex, mi.u.ghost.religion))
         << "として名の知れた"
         << jtrans(_xl_rank_name(mi.u.ghost.xl_rank))
         << jtrans(species_name(gspecies))
         << "の"
         << jtrans(get_job_name(mi.u.ghost.job));

    if (mi.u.ghost.religion != GOD_NO_GOD)
    {
        gstr << "にして"
             << jtrans(god_name(mi.u.ghost.religion))
             << "の信徒である";
    }

    gstr << "『" << mi.mname << "』";

    return gstr.str();
}

void describe_skill(skill_type skill)
{
    ostringstream data;

#ifdef USE_TILE_WEB
    tiles_crt_control show_as_menu(CRT_MENU, "describe_skill");
#endif

    data << get_skill_description(skill, true);

    print_description(data.str());
    getchm();
}

#ifdef USE_TILE
string get_command_description(const command_type cmd, bool terse)
{
    string lookup = command_to_name(cmd);

    if (!terse)
        lookup += " verbose";

    string result = getLongDescription(lookup);
    if (result.empty())
    {
        if (!terse)
        {
            // Try for the terse description.
            result = get_command_description(cmd, true);
            if (!result.empty())
                return result + ".";
        }
        return command_to_name(cmd);
    }

    return result.substr(0, result.length() - 1);
}
#endif

void alt_desc_proc::nextline()
{
    ostr << "\n";
}

void alt_desc_proc::print(const string &str)
{
    ostr << str;
}

int alt_desc_proc::count_newlines(const string &str)
{
    return count(begin(str), end(str), '\n');
}

void alt_desc_proc::trim(string &str)
{
    int idx = str.size();
    while (--idx >= 0)
    {
        if (str[idx] != '\n')
            break;
    }
    str.resize(idx + 1);
}

bool alt_desc_proc::chop(string &str)
{
    int loc = -1;
    for (size_t i = 1; i < str.size(); i++)
        if (str[i] == '\n' && str[i-1] == '\n')
            loc = i;

    if (loc == -1)
        return false;

    str.resize(loc);
    return true;
}

void alt_desc_proc::get_string(string &str)
{
    str = replace_all(ostr.str(), "\n\n\n\n", "\n\n");
    str = replace_all(str, "\n\n\n", "\n\n");

    trim(str);
    while (count_newlines(str) > h)
    {
        if (!chop(str))
            break;
    }
}

/**
 * @file
 * @brief Notetaking stuff
**/

#include "AppHdr.h"

#include "notes.h"

#include <iomanip>
#include <sstream>
#include <vector>

#include "branch.h"
#include "database.h"
#include "english.h"
#include "hiscores.h"
#include "japanese.h"
#include "message.h"
#include "mutation.h"
#include "options.h"
#include "religion.h"
#include "skills.h"
#include "spl-util.h"
#include "state.h"
#include "stringutil.h"
#include "unicode.h"

#define NOTES_VERSION_NUMBER 1002

vector<Note> note_list;

// return the real number of the power (casting out nonexistent powers),
// starting from 0, or -1 if the power doesn't exist
static int _real_god_power(int religion, int idx)
{
    if (god_gain_power_messages[religion][idx][0] == 0)
        return -1;

    int count = 0;
    for (int j = 0; j < idx; ++j)
        if (god_gain_power_messages[religion][j][0])
            ++count;

    return count;
}

static bool _is_highest_skill(int skill)
{
    for (int i = 0; i < NUM_SKILLS; ++i)
    {
        if (i == skill)
            continue;
        if (you.skills[i] >= you.skills[skill])
            return false;
    }
    return true;
}

static bool _is_noteworthy_hp(int hp, int maxhp)
{
    return hp > 0 && Options.note_hp_percent
           && hp <= (maxhp * Options.note_hp_percent) / 100;
}

static int _dungeon_branch_depth(uint8_t branch)
{
    if (branch >= NUM_BRANCHES)
        return -1;
    return brdepth[branch];
}

static bool _is_noteworthy_dlevel(level_id place)
{
    branch_type branch = place.branch;
    int lev = place.depth;

    // Entering the Abyss is noted a different way, since we care mostly about
    // the cause.
    if (branch == BRANCH_ABYSS)
        return lev == _dungeon_branch_depth(branch);

    // These get their note in the .des files.
    if (branch == BRANCH_WIZLAB)
        return false;

    // Other portal levels are always interesting.
    if (!is_connected_branch(branch))
        return true;

    return lev == _dungeon_branch_depth(branch)
           || branch == BRANCH_DUNGEON && (lev % 5) == 0
           || branch != BRANCH_DUNGEON && lev == 1;
}

// Is a note worth taking?
// This function assumes that game state has not changed since
// the note was taken, e.g. you.* is valid.
static bool _is_noteworthy(const Note& note)
{
    // Always noteworthy.
    if (note.type == NOTE_XP_LEVEL_CHANGE
        || note.type == NOTE_LEARN_SPELL
        || note.type == NOTE_GET_GOD
        || note.type == NOTE_GOD_GIFT
        || note.type == NOTE_GET_MUTATION
        || note.type == NOTE_LOSE_MUTATION
        || note.type == NOTE_GET_ITEM
        || note.type == NOTE_ID_ITEM
        || note.type == NOTE_BUY_ITEM
        || note.type == NOTE_DONATE_MONEY
        || note.type == NOTE_SEEN_MONSTER
        || note.type == NOTE_DEFEAT_MONSTER
        || note.type == NOTE_POLY_MONSTER
        || note.type == NOTE_USER_NOTE
        || note.type == NOTE_MESSAGE
        || note.type == NOTE_LOSE_GOD
        || note.type == NOTE_PENANCE
        || note.type == NOTE_MOLLIFY_GOD
        || note.type == NOTE_DEATH
        || note.type == NOTE_XOM_REVIVAL
        || note.type == NOTE_SEEN_FEAT
        || note.type == NOTE_PARALYSIS
        || note.type == NOTE_NAMED_ALLY
        || note.type == NOTE_ALLY_DEATH
        || note.type == NOTE_FEAT_MIMIC
        || note.type == NOTE_OFFERED_SPELL
        || note.type == NOTE_FOCUS_CARD)
    {
        return true;
    }

    // Never noteworthy, hooked up for fun or future use.
    if (note.type == NOTE_MP_CHANGE
        || note.type == NOTE_MAXHP_CHANGE
        || note.type == NOTE_MAXMP_CHANGE)
    {
        return false;
    }

    // Xom effects are only noteworthy if the option is true.
    if (note.type == NOTE_XOM_EFFECT)
        return Options.note_xom_effects;

    // God powers might be noteworthy if it's an actual power.
    if (note.type == NOTE_GOD_POWER
        && _real_god_power(note.first, note.second) == -1)
    {
        return false;
    }

    // HP noteworthiness is handled in its own function.
    if (note.type == NOTE_HP_CHANGE
        && !_is_noteworthy_hp(note.first, note.second))
    {
        return false;
    }

    // Skills are noteworthy if in the skill value list or if
    // it's a new maximal skill (depending on options).
    if (note.type == NOTE_GAIN_SKILL || note.type == NOTE_LOSE_SKILL)
    {
        if (Options.note_all_skill_levels
            || note.second <= 27 && Options.note_skill_levels[note.second]
            || Options.note_skill_max && _is_highest_skill(note.first))
        {
            return true;
        }
        return false;
    }

    if (note.type == NOTE_DUNGEON_LEVEL_CHANGE)
        return _is_noteworthy_dlevel(note.place);

    for (const Note &oldnote : note_list)
    {
        if (oldnote.type != note.type)
            continue;

        const Note& rnote(oldnote);
        switch (note.type)
        {
        case NOTE_GOD_POWER:
            if (rnote.first == note.first && rnote.second == note.second)
                return false;
            break;

        case NOTE_HP_CHANGE:
            // Not if we have a recent warning
            // unless we've lost half our HP since then.
            if (note.turn - rnote.turn < 5
                && note.first * 2 >= rnote.first)
            {
                return false;
            }
            break;

        default:
            mpr("Buggy note passed: unknown note type");
            // Return now, rather than give a "Buggy note passed" message
            // for each note of the matching type in the note list.
            return true;
        }
    }
    return true;
}

static const char* _number_to_ordinal(int number)
{
    const char* ordinals[5] = { "first", "second", "third", "fourth", "fifth" };

    if (number < 1)
        return "[unknown ordinal (too small)]";
    if (number > 5)
        return "[unknown ordinal (too big)]";
    return ordinals[number-1];
}

string Note::describe(bool when, bool where, bool what) const
{
    ostringstream result;

    if (when)
        result << setw(6) << turn << " ";

    if (where)
    {
        result << "| "
               << chop_string(place.describe_j(), MAX_NOTE_PLACE_LEN)
               << " | ";
    }

    if (what)
    {
        switch (type)
        {
        case NOTE_HP_CHANGE:
            // [ds] Shortened HP change note from "Had X hitpoints" to
            // accommodate the cause for the loss of hitpoints.
            result << "HP: " << first << "/" << second
                   << " [" << name << "]";
            break;
        case NOTE_XOM_REVIVAL:
            result << jtrans("Xom revived you");
            break;
        case NOTE_MP_CHANGE:
            result << "MP: " << first << "/" << second;
            break;
        case NOTE_MAXHP_CHANGE:
            result << "最大HPが" << first << "になった";
            break;
        case NOTE_MAXMP_CHANGE:
            result << "最大MPが" << first << "になった";
            break;
        case NOTE_XP_LEVEL_CHANGE:
            result << "レベル" << first << "に到達した (" << name << ")";
            break;
        case NOTE_DUNGEON_LEVEL_CHANGE:
            if (!desc.empty())
                result << desc;
            else
                result << place.describe_j(true, true) << "に進んだ";
            break;
        case NOTE_LEARN_SPELL:
            result << "レベル"
                   << spell_difficulty(static_cast<spell_type>(first))
                   << "の呪文「"
                   << tagged_jtrans("[spell]", spell_title(static_cast<spell_type>(first)))
                   << "」を覚えた";
            break;
        case NOTE_GET_GOD:
            result << jtrans(god_name(static_cast<god_type>(first)))
                   << "の信徒になった";
            break;
        case NOTE_LOSE_GOD:
            result << jtrans(god_name(static_cast<god_type>(first))) << "への信仰を失った";
            break;
        case NOTE_PENANCE:
            result << jtrans(god_name(static_cast<god_type>(first))) << "への償いをしなければならなくなった";
            break;
        case NOTE_MOLLIFY_GOD:
            result << jtrans(god_name(static_cast<god_type>(first))) << "の赦しを得た";
            break;
        case NOTE_GOD_GIFT:
            result << jtrans(god_name(static_cast<god_type>(first)))
                   << "からの授かり物を得た";
            if (!name.empty())
                result << " (" << name << ")";
            break;
        case NOTE_ID_ITEM:
            result << name << "を識別した";
            if (!desc.empty())
                result << "\n" + string(25, ' ') + "(" << desc << ")";
            break;
        case NOTE_GET_ITEM:
            result << name << "を手にした";
            break;
        case NOTE_BUY_ITEM:
            result << name << "を金貨" << first << "枚で購入した";
            break;
        case NOTE_DONATE_MONEY:
            result << "ジンに金貨" << first << "枚を寄付した";
            break;
        case NOTE_GAIN_SKILL:
            result << tagged_jtransc("[skill]", skill_name(static_cast<skill_type>(first)))
                   << "スキルがレベル" << second << "に到達した";
            break;
        case NOTE_LOSE_SKILL:
            result << tagged_jtransc("[skill]", skill_name(static_cast<skill_type>(first)))
                   << "スキルがレベル" << second << "に減少した";
            break;
        case NOTE_SEEN_MONSTER:
            result << name << "に遭遇した";
            break;
        case NOTE_DEFEAT_MONSTER:
            if (second)
                result << "仲間の" << name << jconj_verb(desc, JCONJ_PASS);
            else
                result << name << desc;
            break;
        case NOTE_POLY_MONSTER:
            result << name << "が" << desc << "に変化した";
            break;
        case NOTE_GOD_POWER:
            result << jtrans(god_name(static_cast<god_type>(first)))
                   << "の"
                   << jtrans(_number_to_ordinal(_real_god_power(first, second)+1))
                   << "の能力を得た";
            break;
        case NOTE_GET_MUTATION:
            result << "突然変異が発現した: "
                   << jtrans(mutation_desc(static_cast<mutation_type>(first),
                                           second == 0 ? 1 : second));
            if (!name.empty())
                result << " [" << name << "]";
            break;
        case NOTE_LOSE_MUTATION:
            result << "突然変異を失った: "
                   << jtrans(mutation_desc(static_cast<mutation_type>(first),
                                           second == 3 ? 3 : second+1));
            if (!name.empty())
                result << " [" << name << "]";
            break;
        case NOTE_PERM_MUTATION:
            result << "突然変異が定着した: "
                   << jtrans(mutation_desc(static_cast<mutation_type>(first),
                                           second == 0 ? 1 : second));
            if (!name.empty())
                result << " [" << name << "]";
            break;
        case NOTE_DEATH:
            result << name;
            break;
        case NOTE_USER_NOTE:
            result << Options.user_note_prefix << name;
            break;
        case NOTE_MESSAGE:
            result << jtrans(name);
            break;
        case NOTE_SEEN_FEAT:
            result << name << "を見つけた";
            break;
        case NOTE_FEAT_MIMIC:
            result << name << "はミミックだった";
            break;
        case NOTE_XOM_EFFECT:
            result << "[ゾム] " << name;
#if defined(DEBUG_XOM) || defined(NOTE_DEBUG_XOM)
            // If debugging, also take note of piety and tension.
            result << " (piety: " << first;
            if (second >= 0)
                result << ", tension: " << second;
            result << ")";
#endif
            break;
        case NOTE_PARALYSIS:
            result << name << "に" << first << "ターン麻痺させられた";
            break;
        case NOTE_NAMED_ALLY:
            result << name << "が仲間になった";
            break;
        case NOTE_ALLY_DEATH:
            result << "仲間の" << name << "が死んだ";
            break;
        case NOTE_OFFERED_SPELL:
            result << "ヴェフメットが"
                   << tagged_jtrans("[spell]", spell_title(static_cast<spell_type>(first)))
                   << "の呪文の知識を授ける提案をした";
            break;
        case NOTE_FOCUS_CARD:
            result << tagged_jtrans("[card]", "Focus") << "のカードを引いた: "
                   << name << "が" << first << "に増加するかわりに"
                   << desc << "が" << second << "に減少した";
            break;
        default:
            result << "Buggy note description: unknown note type";
            break;
        }
    }

    return result.str();
}

void Note::check_milestone() const
{
    if (crawl_state.game_is_arena())
        return;

    if (type == NOTE_DUNGEON_LEVEL_CHANGE)
    {
        const int br = place.branch,
                 dep = place.depth;

        // Wizlabs report their milestones on their own.
        if (br != -1 && br != BRANCH_WIZLAB)
        {
            ASSERT_RANGE(br, 0, NUM_BRANCHES);
            string branch = place.describe_j(true, false);

            if (dep == 1)
            {
                mark_milestone(br == BRANCH_ZIGGURAT ? "zig.enter" : "br.enter",
                               branch + "に突入した", "parent");
            }
            else if (dep == _dungeon_branch_depth(br)
                     || br == BRANCH_ZIGGURAT)
            {
                string level = place.describe_j(true, true);

                mark_milestone(br == BRANCH_ZIGGURAT ? "zig" : "br.end",
                               level + "に到達した");
            }
        }
    }
}

void Note::save(writer& outf) const
{
    marshallInt(outf, type);
    marshallInt(outf, turn);
    place.save(outf);
    marshallInt(outf, first);
    marshallInt(outf, second);
    marshallString4(outf, name);
    marshallString4(outf, desc);
}

void Note::load(reader& inf)
{
    type = static_cast<NOTE_TYPES>(unmarshallInt(inf));
    turn = unmarshallInt(inf);
#if TAG_MAJOR_VERSION == 34
    if (inf.getMinorVersion() < TAG_MINOR_PLACE_UNPACK)
        place = level_id::from_packed_place(unmarshallShort(inf));
    else
#endif
    place.load(inf);
    first  = unmarshallInt(inf);
    second = unmarshallInt(inf);
    unmarshallString4(inf, name);
    unmarshallString4(inf, desc);
}

static bool notes_active = false;

bool notes_are_active()
{
    return notes_active;
}

void take_note(const Note& note, bool force)
{
    if (notes_active && (force || _is_noteworthy(note)))
    {
        note_list.push_back(note);
        note.check_milestone();
    }
}

void activate_notes(bool active)
{
    notes_active = active;
}

void save_notes(writer& outf)
{
    marshallInt(outf, NOTES_VERSION_NUMBER);
    marshallInt(outf, note_list.size());
    for (const Note &note : note_list)
        note.save(outf);
}

void load_notes(reader& inf)
{
    if (unmarshallInt(inf) != NOTES_VERSION_NUMBER)
        return;

    const int num_notes = unmarshallInt(inf);
    for (int i = 0; i < num_notes; ++i)
    {
        Note new_note;
        new_note.load(inf);
        note_list.push_back(new_note);
    }
}

void make_user_note()
{
    char buf[400];
    bool validline = !msgwin_get_line("Enter note: ", buf, sizeof(buf));
    if (!validline || (!*buf))
        return;
    Note unote(NOTE_USER_NOTE);
    unote.name = buf;
    take_note(unote);
}

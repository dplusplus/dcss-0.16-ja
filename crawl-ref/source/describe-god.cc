/**
 * @file
 * @brief Functions used to print information about gods.
 **/

#include "AppHdr.h"

#include "describe-god.h"

#include <iomanip>

#include "ability.h"
#include "branch.h"
#include "cio.h"
#include "database.h"
#include "describe.h"
#include "english.h"
#include "godabil.h"
#include "godpassive.h"
#include "godprayer.h"
#include "libutil.h"
#include "macro.h"
#include "menu.h"
#include "religion.h"
#include "skills.h"
#include "spl-util.h"
#include "stringutil.h"
#include "unicode.h"
#include "xom.h"

extern ability_type god_abilities[NUM_GODS][MAX_GOD_ABILITIES];

enum god_desc_type
{
    GDESC_OVERVIEW,
    GDESC_DETAILED,
    GDESC_WRATH,
    NUM_GDESCS
};

static bool _print_final_god_abil_desc(int god, const string &final_msg,
                                       const ability_type abil)
{
    // If no message then no power.
    if (final_msg.empty())
        return false;

    string buf = final_msg;

    // For ability slots that give more than one ability, display
    // "Various" instead of the cost of the first ability.
    const string cost =
    "(" +
    (abil == ABIL_YRED_RECALL_UNDEAD_SLAVES ? jtrans("Various")
                                            : make_cost_description(abil))
    + ")";

    if (cost != ("(" + jtrans("None") + ")"))
    {
        // XXX: Handle the display better when the description and cost
        // are too long for the screen.
        buf = chop_string(buf, get_number_of_cols() - 1 - strwidth(cost));
        buf += cost;
    }

    cprintf("%s\n", sp2nbspc(buf));

    return true;
}

static bool _print_god_abil_desc(int god, int numpower)
{
    const char* pmsg = god_gain_power_messages[god][numpower];

    // If no message then no power.
    if (!pmsg[0])
        return false;

    // Don't display ability upgrades here.
    string buf = jtrans(adjust_abil_message(pmsg, false));
    if (buf.empty())
        return false;

    if (!isupper(pmsg[0])) // Complete sentence given?
    {
        // ゴザーグの能力表示はget_gold内でも呼ばれるのでこちらで文を合わせる
        if (god == GOD_GOZAG) buf += "こと";

        buf = "あなたは" + buf + "ができる。";
    }

    // This might be ABIL_NON_ABILITY for passive abilities.
    const ability_type abil = god_abilities[god][numpower];
    _print_final_god_abil_desc(god, buf, abil);

    return true;
}

static int _piety_level(int piety)
{
    return (piety >= piety_breakpoint(5)) ? 7 :
           (piety >= piety_breakpoint(4)) ? 6 :
           (piety >= piety_breakpoint(3)) ? 5 :
           (piety >= piety_breakpoint(2)) ? 4 :
           (piety >= piety_breakpoint(1)) ? 3 :
           (piety >= piety_breakpoint(0)) ? 2 :
           (piety >                    0) ? 1
                                          : 0;
}

static int _gold_level()
{
    return (you.gold >= 50000) ? 7 :
           (you.gold >= 10000) ? 6 :
           (you.gold >=  5000) ? 5 :
           (you.gold >=  1000) ? 4 :
           (you.gold >=   500) ? 3 :
           (you.gold >=   100) ? 2
                               : 1;
}

static string _describe_favour(god_type which_god)
{
    if (player_under_penance())
    {
        const int penance = you.penance[which_god];
        return jtrans((penance >= 50) ? "Godly wrath is upon you!" :
                      (penance >= 20) ? "You've transgressed heavily! Be penitent!" :
                      (penance >=  5) ? "You are under penance."
                                      : "You should show more discipline.");
    }

    if (which_god == GOD_XOM)
        return jtrans(describe_xom_favour());

    const int rank = which_god == GOD_GOZAG ? _gold_level()
    : _piety_level(you.piety);

    const string godname = jtrans(god_name(which_god));
    switch (rank)
    {
        case 7:  return make_stringf(jtransc("A prized avatar of"), godname.c_str());
        case 6:  return make_stringf(jtransc("A favoured servant of"), godname.c_str());
        case 5:

            if (you_worship(GOD_DITHMENOS))
                return make_stringf(jtransc("A glorious shadow in the eyes of"), godname.c_str());
            else
                return make_stringf(jtransc("A shining star in the eyes of"), godname.c_str());

        case 4:

            if (you_worship(GOD_DITHMENOS))
                return make_stringf(jtransc("A rising shadow in the eyes of"), godname.c_str());
            else
                return make_stringf(jtransc("A rising star in the eyes of"), godname.c_str());

        case 3:  return godname + jtrans(" is most pleased with you.");
        case 2:  return godname + jtrans(" is pleased with you.");
        default: return godname + jtrans(" is noncommittal.");
    }
}

static string _religion_help(god_type god)
{
    string result = "";

    switch (god)
    {
        case GOD_ZIN:
            if (can_do_capstone_ability(god))
                result += jtransln("You can have all your mutations cured.\n");
            result += jtrans("You can pray at an altar to donate money.");
            break;

        case GOD_SHINING_ONE:
        {
            const int halo_size = you.halo_radius2();
            if (halo_size >= 0)
            {
                if (!result.empty())
                    result += " ";

                string msg;
                msg += "You radiate a ";

                if (halo_size > 37)
                    msg += "large ";
                else if (halo_size > 10)
                    msg += "";
                else
                    msg += "small ";

                msg += "righteous aura, and all beings within it are "
                    "easier to hit.";

                result += jtrans(msg);
            }
            if (can_do_capstone_ability(god))
            {
                if (!result.empty())
                    result += " ";

                result += jtrans("You can pray at an altar to have your weapon "
                                 "blessed, especially a demon weapon.");
            }
            break;
        }

        case GOD_LUGONU:
            if (can_do_capstone_ability(god))
            {
                result += jtrans("You can pray at an altar to have your weapon "
                                 "corrupted.");
            }
            break;

        case GOD_KIKUBAAQUDGHA:
            if (can_do_capstone_ability(god))
            {
                result += jtrans("You can pray at an altar to have your necromancy "
                                 "enhanced.");
            }
            break;

        case GOD_BEOGH:
            result += jtrans("You can pray to sacrifice all orcish remains on your "
                             "square.");
            break;

        case GOD_FEDHAS:
            if (you.piety >= piety_breakpoint(0))
            {
                result += jtrans("Evolving plants requires fruit, and evolving "
                                 "fungi requires piety.");
            }
            break;

        default:
            break;
    }

    if (god_likes_fresh_corpses(god))
    {
        if (!result.empty())
            result += " ";

        result += jtrans("You can pray to sacrifice all fresh corpses on your "
                         "square.");
    }

    return result;
}

// The various titles granted by the god of your choice.  Note that Xom
// doesn't use piety the same way as the other gods, so these are just
// placeholders.
static const char *divine_title[NUM_GODS][8] =
{
    // No god.
    {"Buglet",             "Firebug",               "Bogeybug",                 "Bugger",
        "Bugbear",            "Bugged One",            "Giant Bug",                "Lord of the Bugs"},

    // Zin.
    {"冒涜者",             "隠遁者"   ,             "弁証者",                   "敬虔者",
        "導士",               "正しき者",              "無垢清浄の者",             "秩序の代行者"},

    // The Shining One.
    {"不名誉な存在",       "侍祭",                  "高潔な者",                 "揺るがぬ者",
        "聖戦者",             "悪を祓う者",            "悪を滅する者",             "光の代行者"},

    // Kikubaaqudgha -- scholarly death.
    {"苦痛を受けし者",     "痛みを与える者",        "死を探求する者",           "苦痛の商人",
        "死の芸術家",         "絶望を振り撒く者",      "黒き太陽",                 "暗黒の領主"},

    // Yredelemnul -- zombie death.
    {"反逆者",             "堕落者",                "松明を携えし者",           "狂気の@Genus@",
        "黒の十字軍",         "死体を彩る者",          "死の体現者",               "永遠なる死の支配者"},

    // Xom.
    {"ゾムの玩具",         "ゾムの玩具",            "ゾムの玩具",               "ゾムの玩具",
        "ゾムの玩具",         "ゾムの玩具",            "ゾムの玩具",               "ゾムの玩具"},

    // Vehumet -- battle mage theme.
    {"敗北者",             "魔術師見習い",          "破壊を探求する者",         "破滅の詠唱者",
        "魔術師",             "戦闘魔術師",            "大魔術師",                 "破壊魔術の指導者"},

    // Okawaru -- battle theme.
    {"卑怯者",             "奮闘者",                "闘士",                     "武人",
        "騎士",               "戦争屋",                "司令官",                   "千の戦の支配者"},

    // Makhleb -- chaos theme.
    {"従卒",               "混沌の申し子",          "破壊者の門弟",             "虐殺の凱歌",
        "悪魔の化身",         "@Genus@の破壊者",       "修羅",                     "混沌の代行者"},

    // Sif Muna -- scholarly theme.
    {"愚か者",             "門弟",                  "研究者",                   "熟練者",
        "知識を残す者",       "魔法学者",              "賢者",                     "秘術の支配者"},

    // Trog -- anger theme.
    {"弱者",               "世捨て人",              "怒れる奇人",               "荒れ狂う者",
        "猛襲の@Genus@",      "猛威をふるう者",        "激怒する@Genus@",          "文明の破壊者"},

    // Nemelex Xobeh -- alluding to Tarot and cards.
    {"不運な@Genus@",      "アイテム収拾人",        "道化",                     "占い師",
        "予言者",             "カードの魔術師",        "イカサマ師",               "運命を手にする者"},

    // Elyvilon.
    {"罪人",               "開業医",                "慰める者",                 "癒す者",
        "修繕者",             "平和主義者",            "@Genus@の浄罪者",          "生命を司る者"},

    // Lugonu -- distortion theme.
    {"純粋な者",           "深淵の洗礼を受けた者",  "瓦解させる者",             "歪んだ@Genus@",
        "エントロピーの代行者", "乖離させる者",        "虚空の使者",               "次元の破壊者"},

    // Beogh -- messiah theme.
    {"背信者",             "伝令",                  "改宗者",                   "司祭",
        "宣教師",             "福音伝道者",            "使徒",                    "救世主"},

    // Jiyva -- slime and jelly theme.
    {"塵屑",               "咀嚼する者",            "ウーズ",                   "ジェリー",
        "スライム",           "溶解せる@Genus@",       "ブロブ",                  "ロイヤルジェリー"},

    // Fedhas Madash -- nature theme.
    {"@Walking@肥料",      "真菌",                  "緑の@Genus@",              "繁茂させる者",
        "実りをもたらす者",   "光合成する者",          "緑の殲滅者",               "自然の化身"},

    // Cheibriados -- slow theme
    {"せっかち",           "のろまな@Genus@",       "熟慮者",                   "ゆっくり",
        "瞑想者",             "時代を区切る者",        "時間超越者",               "永劫の@Adj@"},

    // Ashenzari -- divination theme
    {"薄幸の者",           "呪われし者",            "秘呪に通じる者",           "予言者",
        "千里眼",             "託宣者",                "啓示を受けし者",           "全知全能の賢者"},

    // Dithmenos -- darkness theme
    {"燃えさし",           "薄暗がり",              "暗転者",                   "消火者",
        "暗黒",               "漆黒",                  "影の手",                   "永劫の夜"},

    // Gozag -- entrepreneur theme
    {"放蕩者",             "貧乏人",                "起業家",                   "資本家",
        "裕福者",             "富裕者",                "大立者",                   "大富豪"},

    // Qazlal -- natural disaster theme
    {"傷つかざる者",       "@Adj@の災難",           "避雷針",                   "@Adj@の大災害",
        "台風の目",           "破局の@Adj@",           "大変動の@Adj@",            "紀元の終末者"},

    // Ru -- enlightenment theme
    {"不覚者",             "質問者",                "秘術の伝授者",             "真実の探求者",
        "真理の道の歩行者",   "ベールを上げる者",      "非現実を飲み干す者",       "卓越せる覚者"},
};

string god_title(god_type which_god, species_type which_species, int piety)
{
    string title;
    if (player_under_penance(which_god))
        title = divine_title[which_god][0];
    else if (which_god == GOD_GOZAG)
        title = divine_title[which_god][_gold_level()];
    else
        title = divine_title[which_god][_piety_level(piety)];

    //XXX: unify with stuff in skills.cc
    title = replace_all(title, "@Genus@", jtrans(species_name(which_species, true, false)));
    title = replace_all(title, "@Adj@", jtrans(species_name(which_species, false, true)));
    title = replace_all(title, "@Walking@", jtrans(species_walking_verb(which_species)));

    return title;
}

static string _describe_ash_skill_boost()
{
    if (!you.bondage_level)
    {
        return "Ashenzari won't support your skills until you bind yourself "
               "with cursed items.";
    }

    static const char* bondage_parts[NUM_ET] = { "Weapon hand", "Shield hand",
                                                 "Armour", "Jewellery" };
    static const char* bonus_level[3] = { "Low", "Medium", "High" };
    ostringstream desc;
    desc.setf(ios::left);
    desc << "<white>";
    desc << align_left(jtrans("Bound part"), 18);
    desc << align_left(jtrans("Boosted skills"), 30);
    desc << jtransln("Bonus\n");
    desc << "</white>";

    for (int i = ET_WEAPON; i < NUM_ET; i++)
    {
        if (you.bondage[i] <= 0 || i == ET_SHIELD && you.bondage[i] == 3)
            continue;

        if (i == ET_WEAPON && you.bondage[i] == 3)
            desc << align_left(jtrans("Hands"), 18);
        else
            desc << align_left(jtrans(bondage_parts[i]), 18);

        string skills;
        map<skill_type, int8_t> boosted_skills = ash_get_boosted_skills(eq_type(i));
        const int8_t bonus = boosted_skills.begin()->second;
        auto it = boosted_skills.begin();

        // First, we keep only one magic school skill (conjuration).
        // No need to list all of them since we boost all or none.
        while (it != boosted_skills.end())
        {
            if (it->first > SK_CONJURATIONS && it->first <= SK_LAST_MAGIC)
            {
                boosted_skills.erase(it++);
                it = boosted_skills.begin();
            }
            else
                ++it;
        }

        it = boosted_skills.begin();
        while (!boosted_skills.empty())
        {
            // For now, all the bonuses from the same bounded part have
            // the same level.
            ASSERT(bonus == it->second);
            if (it->first == SK_CONJURATIONS)
                skills += jtrans("Magic schools");
            else
                skills += tagged_jtrans("[skill]", skill_name(it->first));

            if (boosted_skills.size() > 2)
                skills += ", ";
            else if (boosted_skills.size() == 2)
                skills += "および";

            boosted_skills.erase(it++);
        }

        desc << align_left(skills + "スキル", 30);
        desc << jtransln(bonus_level[bonus -1]);
    }

    return desc.str();
}

// from dgn-overview.cc
extern map<branch_type, set<level_id> > stair_level;

// XXX: apply padding programmatically?
static const char* const bribe_susceptibility_adjectives[] =
{
    "不可         ",
    "かなり難しい ",
    "難しい       ",
    "普通         ",
    "簡単         ",
    "非常に簡単   ",
};

/**
 * Populate a provided vector with a list of bribable branches which are known
 * to the player.
 *
 * @param[out] targets      A list of bribable branches.
 */
static void _list_bribable_branches(vector<branch_type> &targets)
{
    for (branch_iterator it; it; ++it)
    {
        const branch_type br = it->id;
        if (!gozag_branch_bribable(br))
            continue;

        // If you don't know the branch exists, don't list it;
        // this mainly plugs info leaks about Lair branch structure.
        if (!stair_level.count(br) && is_random_subbranch(br))
            continue;

        targets.push_back(br);
    }
}

/**
 * Describe the current options for Gozag's bribe branch ability.
 *
 * @return      A description of branches' bribe status.
 */
static string _describe_branch_bribability()
{
    string ret = jtransln("You can bribe the following branches of the dungeon:");
    vector<branch_type>targets;
    _list_bribable_branches(targets);

    size_t width = 0;
    for (branch_type br : targets)
        width = max(width, (size_t)strwidth(tagged_jtrans("[branch]", branches[br].shortname)));

    for (branch_type br : targets)
    {
        string line = " ";
        line += tagged_jtrans("[branch]", branches[br].shortname);
        line += string(width + 2 - strwidth(line), ' ');
        // XXX: move this elsewhere?
        switch (br)
        {
            case BRANCH_ORC:
                line += "(オーク)              ";
                break;
            case BRANCH_ELF:
                line += "(エルフ)              ";
                break;
            case BRANCH_SNAKE:
                line += "(ナーガ/サラマンダー) ";
                break;
            case BRANCH_SHOALS:
                line += "(水棲の民)            ";
                break;
            case BRANCH_VAULTS:
                line += "(人間)                ";
                break;
            case BRANCH_ZOT:
                line += "(ドラコニアン)        ";
                break;
            case BRANCH_COCYTUS:
            case BRANCH_DIS:
            case BRANCH_GEHENNA:
            case BRANCH_TARTARUS:
                line += "(悪魔)                ";
                break;
            default:
                line += "(buggy)               ";
                break;
        }

        line += jtrans("Susceptibility:") + " ";
        const int suscept = gozag_branch_bribe_susceptibility(br);
        ASSERT(suscept >= 0
               && suscept < (int)ARRAYSZ(bribe_susceptibility_adjectives));
        line += bribe_susceptibility_adjectives[suscept];

        if (!branch_bribe[br])
            line += jtrans("not bribed");
        else
            line += make_stringf("$%d", branch_bribe[br]);

        ret += line + "\n";
    }

    return sp2nbsp(ret);
}

/**
 * Print a guide to cycling between description screens, and check if the
 * player does so.
 *
 * @return Whether the player chose to cycle to the next description screen.
 */
static bool _check_description_cycle(god_desc_type gdesc)
{
    // Another function may have left a dangling recolour.
    textcolour(LIGHTGREY);

    const int bottom_line = min(30, get_number_of_lines());

    cgotoxy(1, bottom_line);
    const char* place = nullptr;
    switch (gdesc)
    {
        case GDESC_OVERVIEW: place = "<w>Overview</w>|Powers|Wrath"; break;
        case GDESC_DETAILED: place = "Overview|<w>Powers</w>|Wrath"; break;
        case GDESC_WRATH:    place = "Overview|Powers|<w>Wrath</w>"; break;
        default: die("Unknown god description type!");
    }
    formatted_string::parse_string(make_stringf("[<w>!</w>/<w>^</w>"
#ifdef USE_TILE_LOCAL
                                   "|<w>Right-click</w>"
#endif
    "]: %s", jtransc(place))).display();

    mouse_control mc(MOUSE_MODE_MORE);

    const int keyin = getchm();
    return keyin == '!' || keyin == CK_MOUSE_CMD || keyin == '^';
}

/**
 * Linewrap & print a provided string, if non-empty.
 *
 * Also adds a pair of newlines, if the string is non-empty. (Ugly hack...)
 *
 * @param str       The string in question. (May be empty.)
 * @param width     The width to wrap to.
 */
static void _print_string_wrapped(string str, int width)
{
    if (!str.empty())
    {
        linebreak_string(str, width);
        display_tagged_block(str);
        cprintf("\n");
        cprintf("\n");
    }
}

/**
 * Turn a list of gods into a nice, comma-separated list of their names, with
 * an 'and' at the end if appropriate.
 *
 * XXX: this can almost certainly be templatized and put somewhere else; it
 * might already exist? (the dubiously named comma_separated_line?)
 *
 * @param gods[in]  The enums of the gods in question.
 * @return          A comma-separated list of the given gods' names.
 */

static string _comma_separate_gods(const vector<god_type> &gods)
{
    // ugly special case to prevent foo, and bar
    if (gods.size() == 2)
        return jtrans(god_name(gods[0])) + "と" + jtrans(god_name(gods[1]));

    string names = "";
    for (unsigned int i = 0; i < gods.size() - 1; i++)
        names += jtrans(god_name(gods[i])) + "、";
    if (gods.size() > 1)
        names += "そして";
    if (gods.size() > 0)
        names += jtrans(god_name(gods[gods.size()-1]));
    return names;
}

/**
 * Describe the causes of the given god's wrath.
 *
 * @param which_god     The god in question.
 * @return              A description of the actions that cause this god's
 *                      wrath.
 */
static string _describe_god_wrath_causes(god_type which_god)
{
    vector<god_type> evil_gods;
    vector<god_type> chaotic_gods;
    for (int i = 0; i < NUM_GODS; i++)
    {
        god_type god = (god_type)i;
        if (is_evil_god(god))
            evil_gods.push_back(god);
        else if (is_chaotic_god(god)) // intentionally not including evil!
            chaotic_gods.push_back(god);
        // XXX: refactor this if any god hates chaotic but not evil gods
    }

    switch (which_god)
    {
        case GOD_SHINING_ONE:
        case GOD_ELYVILON:
            return make_stringf(jtransc("TSO and Ely wrath cause"),
                                jtransc(god_name(which_god)),
                                _comma_separate_gods(evil_gods).c_str());
        case GOD_ZIN:
            return make_stringf(jtransc("Zin wrath cause"),
                                jtransc(god_name(which_god)),
                                _comma_separate_gods(evil_gods).c_str(),
                                _comma_separate_gods(chaotic_gods).c_str());
        case GOD_RU:
            return make_stringf(jtransc("Ru wrath cause"),
                                jtransc(god_name(which_god)));
        case GOD_XOM:
            return make_stringf(jtransc("Xom wrath cause"),
                                jtransc(god_name(which_god)),
                                jtransc(god_name(which_god)),
                                jtransc(god_name(which_god)));
        default:
            return make_stringf(jtransc("default wrath cause"),
                                jtransc(god_name(which_god)));
    }
}

/**
 * Print the standard top line of the god description screens.
 *
 * @param god       The god in question.
 * @param width     The width of the screen.
 */
static void _print_top_line(god_type which_god, int width)
{
    const string godname = jtrans(god_name(which_god, true));
    textcolour(god_colour(which_god));
    const int len = width - strwidth(godname);
    cprintf("%s%s\n", string(len / 2, ' ').c_str(), godname.c_str());
    textcolour(LIGHTGREY);
    cprintf("\n");
}

/**
 * Print a description of the given god's dislikes & wrath effects.
 *
 * @param which_god     The god in question.
 */
static void _god_wrath_description(god_type which_god)
{
    clrscr();

    const int width = min(80, get_number_of_cols()) - 1;

    _print_top_line(which_god, width);

    _print_string_wrapped(get_god_dislikes(which_god, true), width);
    _print_string_wrapped(_describe_god_wrath_causes(which_god), width);
    _print_string_wrapped(getLongDescription(god_name(which_god) + " wrath"),
                          width);
}

/**
 * Describe miscellaneous information about the given god.
 *
 * @param which_god     The god in question.
 * @return              Info about gods which isn't covered by their powers,
 *                      likes, or dislikes.
 */
static string _get_god_misc_info(god_type which_god)
{
    switch (which_god)
    {
        case GOD_ASHENZARI:
        case GOD_JIYVA:
        case GOD_TROG:
        {
            const string piety_only = jtrans(god_name(which_god)) + "は" +
                jtrans("does not demand training of the"
                       " Invocations skill. All abilities are"
                       " purely based on piety.") +
                jtrans("Note that");

            if (which_god == GOD_ASHENZARI
                && which_god == you.religion
                && piety_rank() > 1)
            {
                return piety_only + "\n\n" + _describe_ash_skill_boost();
            }

            return piety_only;
        }

        case GOD_KIKUBAAQUDGHA:
            return jtrans("The power of Kikubaaqudgha's abilities is governed by "
                          "Necromancy skill instead of Invocations.");

        case GOD_ELYVILON:
            return jtrans("elyvilon misc info");

        case GOD_NEMELEX_XOBEH:
            return jtrans("The power of Nemelex Xobeh's abilities and of the "
                          "cards' effects is governed by Evocations skill "
                          "instead of Invocations.");

        case GOD_GOZAG:
            return _describe_branch_bribability();

        default:
            return "";
    }
}

/**
 * Print a detailed description of the given god's likes and powers.
 *
 * @param god       The god in question.
 */
static void _detailed_god_description(god_type which_god)
{
    clrscr();

    const int width = min(80, get_number_of_cols()) - 1;

    _print_top_line(which_god, width);

    _print_string_wrapped(get_god_powers(which_god), width);

    _print_string_wrapped(get_god_likes(which_god, true), width);
    _print_string_wrapped(_get_god_misc_info(which_god), width);
}

/**
 * Describe the given god's level of irritation at the player.
 *
 * Player may or may not be currently under penance.
 *
 * @param which_god     The god in question.
 * @return              A description of the god's ire (or lack thereof).
 */
static string _god_penance_message(god_type which_god)
{
    int which_god_penance = you.penance[which_god];

    // Give more appropriate message for the good gods.
    // XXX: ^ this is a hack
    if (which_god_penance > 0 && is_good_god(which_god))
    {
        if (is_good_god(you.religion))
            which_god_penance = 0;
        else if (!god_hates_your_god(which_god) && which_god_penance >= 5)
            which_god_penance = 2; // == "Come back to the one true church!"
    }

    const string penance_message = jtrans(
        (which_god == GOD_NEMELEX_XOBEH
         && which_god_penance > 0 && which_god_penance <= 100)
            ? "%s doesn't play fair with you." :
        (which_god_penance >= 50)   ? "%s's wrath is upon you!" :
        (which_god_penance >= 20)   ? "%s is annoyed with you." :
        (which_god_penance >=  5)   ? "%s well remembers your sins." :
        (which_god_penance >   0)   ? "%s is ready to forgive your sins." :
        (you.worshipped[which_god]) ? "%s is ambivalent towards you."
                                    : "%s is neutral towards you.");

    return make_stringf(penance_message.c_str(),
                        jtransc(god_name(which_god)));
}

/**
 * Print a description of the powers & abilities currently granted to the
 * player by the given god.
 *
 * @param which_god     The god in question.
 */
static void _describe_god_powers(god_type which_god, int numcols)
{
    textcolour(LIGHTGREY);
    const char *header = "Granted powers:";
    const char *cost   = "(Cost)";
    string align = string(min(80, get_number_of_cols()) - 1
                          - strwidth(jtrans(header))
                          - strwidth(jtrans(cost)), ' ');
    cprintf("\n\n%s%s%s\n", jtransc(header),
            sp2nbspc(align),
            jtransc(cost));
    textcolour(god_colour(which_god));

    // mv: Some gods can protect you from harm.
    // The god isn't really protecting the player - only sometimes saving
    // his life.
    bool have_any = false;

    if (god_can_protect_from_harm(which_god))
    {
        have_any = true;

        int prot_chance = 10 + you.piety/10; // chance * 100
        const char *when = "";

        switch (elyvilon_lifesaving())
        {
            case 1:
                when = ", especially when called upon";
                prot_chance += 100 - 3000/you.piety;
                break;
            case 2:
                when = ", and always does so when called upon";
                prot_chance = 100;
        }

        const char *how = (prot_chance >= 85) ? "carefully" :
                          (prot_chance >= 55) ? "often" :
                          (prot_chance >= 25) ? "sometimes"
                                              : "occasionally";


        string buf = jtrans(god_name(which_god));
        buf += "は";
        buf += jtrans(how);
        buf += jtrans("watches over you");
        buf += jtrans(when);

        _print_final_god_abil_desc(which_god, buf, ABIL_NON_ABILITY);
    }

    if (which_god == GOD_ZIN)
    {
        have_any = true;
        const char *how =
        (you.piety >= piety_breakpoint(5)) ? "carefully" :
        (you.piety >= piety_breakpoint(3)) ? "often" :
        (you.piety >= piety_breakpoint(1)) ? "sometimes" :
                                             "occasionally";

        cprintf(jtranslnc("%s %s shields you from chaos."),
               jtransc(god_name(which_god)), jtransc(how));
    }
    else if (which_god == GOD_SHINING_ONE)
    {
        if (you.piety >= piety_breakpoint(1))
        {
            have_any = true;
            const char *how =
            (you.piety >= piety_breakpoint(5)) ? "completely" :
            (you.piety >= piety_breakpoint(3)) ? "mostly" :
                                                 "partially";

            cprintf(jtranslnc("%s %s shields you from negative energy."),
                    jtransc(god_name(which_god)), jtransc(how));
        }
    }
    else if (which_god == GOD_TROG)
    {
        have_any = true;
        string buf = make_stringf(jtransc("You can call upon %s to burn spellbooks in your surroundings."),
                                  jtransc(god_name(which_god)));
        _print_final_god_abil_desc(which_god, buf,
                                   ABIL_TROG_BURN_SPELLBOOKS);
    }
    else if (which_god == GOD_JIYVA)
    {
        if (you.piety >= piety_breakpoint(2))
        {
            have_any = true;
            cprintf(jtranslnc("%s shields you from corrosive effects."),
                    jtransc(god_name(which_god)));
        }
        if (you.piety >= piety_breakpoint(1))
        {
            have_any = true;
            string buf = "あなたは";
            buf += jtransln("when your fellow slimes consume items.");
            if (you.piety >= piety_breakpoint(4))
                buf += "体力と魔力、および";
            else if (you.piety >= piety_breakpoint(3))
                buf += "魔力および";
            buf += "栄養を得る。";
            _print_final_god_abil_desc(which_god, buf,
                                       ABIL_NON_ABILITY);
        }
    }
    else if (which_god == GOD_FEDHAS)
    {
        have_any = true;
        _print_final_god_abil_desc(which_god,
                                   jtrans("You can pray to speed up decomposition."),
                                   ABIL_NON_ABILITY);
        _print_final_god_abil_desc(which_god,
                                   jtrans("You can walk through plants and "
                                          "fire through allied plants."),
                                   ABIL_NON_ABILITY);
    }
    else if (which_god == GOD_ASHENZARI)
    {
        have_any = true;
        _print_final_god_abil_desc(which_god,
                                   jtrans("You are provided with a bounty of information."),
                                   ABIL_NON_ABILITY);
        _print_final_god_abil_desc(which_god,
                                   jtrans("You can pray to corrupt scrolls of remove curse on your square."),
                                   ABIL_NON_ABILITY);
    }
    else if (which_god == GOD_CHEIBRIADOS)
    {
        if (!player_under_penance())
        {
            have_any = true;
            cprintf(jtranslnc("%s supports your attributes (+%d)."),
                    jtransc(god_name(which_god)),
                    chei_stat_boost(you.piety));
            _print_final_god_abil_desc(which_god,
                                       jtrans("You can bend time to slow others."),
                                       ABIL_CHEIBRIADOS_TIME_BEND);
        }
    }
    else if (which_god == GOD_VEHUMET)
    {
        if (const int numoffers = you.vehumet_gifts.size())
        {
            have_any = true;

            string offer = numoffers == 1
                           ? tagged_jtrans("[spell]", spell_title(*you.vehumet_gifts.begin()))
                           : jtrans("some of Vehumet's most lethal spells");
            if (numoffers == 1)
                offer += "の呪文";

            _print_final_god_abil_desc(which_god,
                                       "あなたは" + offer + "を覚えることができる。",
                                       ABIL_NON_ABILITY);
        }
    }
    else if (which_god == GOD_GOZAG)
    {
        have_any = true;
        _print_final_god_abil_desc(which_god,
                                   jtrans("You passively detect gold."),
                                   ABIL_NON_ABILITY);
        _print_final_god_abil_desc(which_god,
                                   jtrans(god_name(which_god))
                                   + jtrans(" turns your defeated foes' bodies"
                                            " to gold."),
                                   ABIL_NON_ABILITY);
        _print_final_god_abil_desc(which_god,
                                   jtrans("Your enemies may become distracted by "
                                          "glittering piles of gold."),
                                   ABIL_NON_ABILITY);
    }
    else if (which_god == GOD_QAZLAL)
    {
        have_any = true;
        _print_final_god_abil_desc(which_god,
                                   jtrans("You are immune to your own clouds."),
                                   ABIL_NON_ABILITY);
    }

    // mv: No abilities (except divine protection) under penance
    if (!player_under_penance())
    {
        vector<ability_type> abilities = get_god_abilities(true, true);
        for (int i = 0; i < MAX_GOD_ABILITIES; ++i)
            if ((you_worship(GOD_GOZAG)
                 && you.gold >= get_gold_cost(abilities[i])
                 || !you_worship(GOD_GOZAG)
                 && you.piety >= piety_breakpoint(i))
                && _print_god_abil_desc(which_god, i))
            {
                have_any = true;
            }
    }

    string extra = get_linebreak_string(_religion_help(which_god),
                                        numcols).c_str();
    if (!extra.empty())
    {
        have_any = true;
        _print_final_god_abil_desc(which_god, extra, ABIL_NON_ABILITY);
    }

    if (!have_any)
        cprintf(jtranslnc("None."));
}

static void _god_overview_description(god_type which_god, bool give_title)
{
    clrscr();

    const int numcols = min(80, get_number_of_cols()) - 1;
    if (give_title)
    {
        textcolour(WHITE);
        cprintf(jtransc("Religion"));
        textcolour(LIGHTGREY);
    }
    // Center top line even if it already contains "Religion" (len = 8)
    //                                In Japanese, "信仰" (width = 4)
    _print_top_line(which_god, numcols - (give_title ? 4 : 0));

    // Print god's description.
    string god_desc = getLongDescription(god_name(which_god));
    cprintf("%s\n", get_linebreak_string(god_desc.c_str(), numcols).c_str());

    // Title only shown for our own god.
    if (you_worship(which_god))
    {
        // Print title based on piety.
        cprintf(sp2nbspc("\n" + jtrans("Title  -") + " "));
        textcolour(god_colour(which_god));

        string title = jtrans(god_title(which_god, you.species, you.piety));
        cprintf("%s", title.c_str());
    }

    // mv: Now let's print favour as Brent suggested.
    // I know these messages aren't perfect so if you can think up
    // something better, do it.

    textcolour(LIGHTGREY);
    cprintf(sp2nbspc("\n" + jtrans("Favour -") + " "));
    textcolour(god_colour(which_god));

    //mv: Player is praying at altar without appropriate religion.
    // It means player isn't checking his own religion and so we only
    // display favour and go out.
    if (!you_worship(which_god))
    {
        textcolour(god_colour(which_god));
        cprintf(_god_penance_message(which_god).c_str());
    }
    else
    {
        cprintf(_describe_favour(which_god).c_str());
        if (which_god == GOD_ASHENZARI)
            cprintf("\n%s", ash_describe_bondage(ETF_ALL, true).c_str());

        _describe_god_powers(which_god, numcols);
    }
}

static god_desc_type _describe_god_by_type(god_type which_god, bool give_title,
                                           god_desc_type gdesc)
{
    switch (gdesc)
    {
    case GDESC_OVERVIEW:
        _god_overview_description(which_god, give_title);
        break;
    case GDESC_DETAILED:
        _detailed_god_description(which_god);
        break;
    case GDESC_WRATH:
        _god_wrath_description(which_god);
        break;
    default:
        die("Unknown god description type!");
    }

    if (_check_description_cycle(gdesc))
        return static_cast<god_desc_type>((gdesc + 1) % NUM_GDESCS);
    else
        return NUM_GDESCS;
}

void describe_god(god_type which_god, bool give_title)
{
    if (which_god == GOD_NO_GOD) //mv: No god -> say it and go away.
    {
        mpr(jtrans("You are not religious."));
        return;
    }

    god_desc_type gdesc = GDESC_OVERVIEW;
    while ((gdesc = _describe_god_by_type(which_god, give_title, gdesc))
            != NUM_GDESCS);
}

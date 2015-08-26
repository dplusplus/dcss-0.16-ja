/**
 * @file
 * @brief Functions and data structures dealing with the syntax,
 *        morphology, and orthography of the Japanese language.
**/

#include "AppHdr.h"

#include "database.h"
#include "japanese.h"

/**
 * アイテム種別に応じた助数詞を返す
 */
const char * counter_suffix(const item_def &item)
{
    switch (item.base_type)
    {
    case OBJ_WEAPONS:
    case OBJ_ARMOUR:
    case OBJ_WANDS:
    case OBJ_JEWELLERY:
    case OBJ_MISCELLANY:
    case OBJ_BOOKS:
    case OBJ_RODS:
    case OBJ_STAVES:
    case OBJ_ORBS:
    case OBJ_CORPSES:
        // スタックしないものは無視
        return "(no stack)";
    case OBJ_POTIONS:
        return "本";
    case OBJ_FOOD:
        return "個";
    case OBJ_SCROLLS:
        return "巻";
    case OBJ_GOLD:
        return "枚";
    case OBJ_MISSILES:
        switch(item.sub_type)
        {
        case MI_NEEDLE:
        case MI_ARROW:
        case MI_BOLT:
        case MI_JAVELIN:
        case MI_TOMAHAWK:
            return "本";
        case MI_STONE:
        case MI_LARGE_ROCK:
        case MI_SLING_BULLET:
            return "個";
        case MI_THROWING_NET:
            return "枚";
        default:
            return "(buggy)";
        }
        break;
    default:
        return "(buggy)";
    }
}

/*
 * 個数のみによって決まる汎用の助数詞を返す
 * size <= 9 までは 1つ～9つ
 * size > 10 は10個～
 * アイテムその他の種類によっては無理が出るが、10以上いっぺんに出る状況は限られるので放置
 */
const char * general_counter_suffix(const int size)
{
    if (size <= 9)
        return "つ";
    else
        return "個";
}

string jpluralise(const string &name, const char *prefix, const char *suffix)
{
    return prefix + jtrans(name) + suffix;
}

static const char * const _pronoun_declension_j[][NUM_PRONOUN_CASES] =
{
    // subj     poss        refl          obj
    { "それ",   "その",     "それ自身",   "それを"   }, // neuter
    { "彼",     "彼の",     "彼自身",     "彼を"     }, // masculine
    { "彼女",   "彼女の",   "彼女自身",   "彼女を"   }, // feminine
    { "あなた", "あなたの", "あなた自身", "あなたを" }, // 2nd person
};

const char *decline_pronoun_j(gender_type gender, pronoun_type variant)
{
    COMPILE_CHECK(ARRAYSZ(_pronoun_declension_j) == NUM_GENDERS);
    ASSERT_RANGE(gender, 0, NUM_GENDERS);
    ASSERT_RANGE(variant, 0, NUM_PRONOUN_CASES);
    return _pronoun_declension_j[gender][variant];
}

/*
 * english.cc/thing_do_grammar()の代替
 * 英語処理が必要な箇所もあるためthing_do_grammarは残す
 */
string thing_do_grammar_j(description_level_type dtype, bool add_stop,
                        bool force_article, string desc)
{
    if (dtype == DESC_PLAIN || !force_article)
        return desc;

    switch (dtype)
    {
    case DESC_THE:
    case DESC_A:
        return desc;
    case DESC_NONE:
        return "";
    default:
        return desc;
    }
}

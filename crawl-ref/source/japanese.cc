/**
 * @file
 * @brief Functions and data structures dealing with the syntax,
 *        morphology, and orthography of the Japanese language.
**/

#include "AppHdr.h"

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

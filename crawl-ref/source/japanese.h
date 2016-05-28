/**
 * @file
 * @brief Functions and data structures dealing with the syntax,
 *        morphology, and orthography of the Japanese language.
**/

#ifndef JAPANESE_H
#define JAPANESE_H

#include "enum.h"

enum jconj
{
    JCONJ_IRRE, // 未然形
    JCONJ_CONT, // 連用形
    JCONJ_TERM, // 終止形
    JCONJ_ATTR, // 連体形
    JCONJ_HYPO, // 仮定形
    JCONJ_IMPR, // 命令形
    JCONJ_PERF, // 完了形
    JCONJ_PASS, // 受動態
};

const char * counter_suffix_weapon(const item_def& item);
const char * counter_suffix_armour(const item_def& item);
const char * counter_suffix_misc(const item_def& item);
const char * counter_suffix_missile(const item_def& item);
const char * counter_suffix(const item_def &item);
const char * general_counter_suffix(const int size);
string jpluralise(const string &name, const char *prefix, const char *suffix = "");
const char *decline_pronoun_j(gender_type gender, pronoun_type variant);
string apply_description_j(description_level_type desc, const string &name,
                         int quantity = 1, bool num_in_words = false);
string thing_do_grammar_j(description_level_type dtype, bool add_stop,
                          bool force_article, string desc);
string get_desc_quantity_j(const int quant, const int total,
                           string whose);
string jconj_verb(const string& verb, jconj conj);

#endif

/**
 * @file
 * @brief Functions and data structures dealing with the syntax,
 *        morphology, and orthography of the Japanese language.
**/

#ifndef JAPANESE_H
#define JAPANESE_H

#include "enum.h"

const char * counter_suffix(const item_def &item);
const char * general_counter_suffix(const int size);
string jpluralise(const string &name, const char *prefix, const char *suffix = "");
const char *decline_pronoun_j(gender_type gender, pronoun_type variant);
string thing_do_grammar_j(description_level_type dtype, bool add_stop,
                          bool force_article, string desc);
#endif
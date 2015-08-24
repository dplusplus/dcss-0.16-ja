/**
 * @file
 * @brief Functions and data structures dealing with the syntax,
 *        morphology, and orthography of the Japanese language.
**/

#ifndef JAPANESE_H
#define JAPANESE_H

const char * counter_suffix(const item_def &item);
const char * general_counter_suffix(const int size);
string jpluralise(const string &name, const char *prefix, const char *suffix = "");

#endif

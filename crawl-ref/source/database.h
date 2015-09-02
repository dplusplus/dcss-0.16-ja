/**
 * @file
 * database.h
**/

#ifndef DATABASE_H
#define DATABASE_H

#include <list>

#ifdef DB_NDBM
extern "C" {
#   include <ndbm.h>
}
#elif defined(DB_DBH)
extern "C" {
#   define DB_DBM_HSEARCH 1
#   include <db.h>
}
#elif defined(USE_SQLITE_DBM)
#   include "sqldbm.h"
#else
#   error DBM interfaces unavailable!
#endif

#define DPTR_COERCE char *

void databaseSystemInit();
void databaseSystemShutdown();

typedef bool (*db_find_filter)(string key, string body);

string getQuoteString(const string &key);
string getLongDescription(const string &key);

vector<string> getLongDescKeysByRegex(const string &regex,
                                      db_find_filter filter = nullptr);
vector<string> getLongDescBodiesByRegex(const string &regex,
                                        db_find_filter filter = nullptr);

string getGameStartDescription(const string &key);

string getShoutString(const string &monst, const string &suffix = "");
string getSpeakString(const string &key);
string getRandNameString(const string &itemtype, const string &suffix = "");
string getHelpString(const string &topic);
string getMiscString(const string &misc, const string &suffix = "");
string getHintString(const string &key);

string jtrans(const char* key, const bool linefeed = false);
string jtrans(const string &key, const bool linefeed = false);
string jtrans_make_stringf(const string &msg, const string &subject, const string &verb, const string &object);
string jtrans_make_stringf(const string &msg, const string &verb, const string &object);
#define jtransln(x) (jtrans(x, true))
#define jtransc(x) (jtrans(x).c_str())
#define jtranslnc(x) (jtrans(x, true).c_str())
string rune_of_zot_name(const string &name);
bool jtrans_has_key(const string &key);

template<typename C1, typename C2>
void append_container_jtrans(C1& container_base, const C2& container_append)
{
    for (auto val : container_append)
    {
        container_base.push_back(jtrans(val));
    }
}

/*
 * 渡されたシーケンスの各要素を変換しながら適切に繋ぐ
 *
 * [a, b]       -> "aとb"
 * [a, b, c]    -> "aとb、そしてc"
 * [a, b, c, d] -> "aとb、c、そしてd"
 */
template <typename Z, typename F>
string to_separated_fn(Z start, Z end, F stringify,
                          const string &first = "と",
                          const string &second = "、",
                          const string &fin = "、そして")
{
    string text;
    for (Z i = start; i != end; ++i)
    {
        if (i != start)
        {
            if (prev(i) == start)
                text += first;
            else if (next(i) != end)
                text += second;
            else
                text += fin;
        }

        text += stringify(*i);
    }
    return text;
}

template<typename Z>
string to_separated_line(Z start, Z end, bool to_j = true,
                         const string &first = "と",
                         const string &second = "、",
                         const string &fin = "、そして")
{
    auto stringify = to_j ? [] (const string &s) { return jtrans(s); }
                          : [] (const string &s) { return s; };

    return to_separated_fn(start, end, stringify, first, second, fin);
}

vector<string> getAllFAQKeys();
string getFAQ_Question(const string &key);
string getFAQ_Answer(const string &question);
#endif

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

string jtrans(const string &key, const bool linefeed = false);
string jtrans_make_stringf(const string &msg, const string &subject, const string &verb, const string &object);
string jtrans_make_stringf(const string &msg, const string &verb, const string &object);
#define jtransln(x) (jtrans(x, true))

vector<string> getAllFAQKeys();
string getFAQ_Question(const string &key);
string getFAQ_Answer(const string &question);
#endif

#ifndef _ALIAS_H_
#define _ALIAS_H_
#include "dynarray.h"
#include "parse.h"

struct aliasentry {
    char *name;
    char *value;
};

void updateEntry(DynArray_T pTable, const char *name, const char *value);
struct aliasentry *findEntry(DynArray_T pTable, const char *name);
void updateToken(DynArray_T pTable, DynArray_T oTokens, int start);
void printentry(DynArray_T pTable);
void cleanentry(DynArray_T pTable);
#endif

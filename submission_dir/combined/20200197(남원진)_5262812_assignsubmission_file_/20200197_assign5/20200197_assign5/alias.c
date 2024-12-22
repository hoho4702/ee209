#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "token.h"
#include "lexsyn.h"
#include "alias.h"

int entryCompare(const void *a, const void *b) {
    struct aliasentry *A = (struct aliasentry *) a;
    struct aliasentry *B = (struct aliasentry *) b;
    return strcmp(A->name, B->name);
}

void updateEntry(DynArray_T pTable, const char *name, const char *value) {
    struct aliasentry *ptr;
    if ((ptr = findEntry(pTable, name))) {
        //found. replace value.
        free(ptr -> value);
        ptr -> value = strdup(value);
    } else {
        //new entry.
        struct aliasentry *entry = malloc(sizeof(struct aliasentry));
        entry -> name = strdup(name);
        entry -> value = strdup(value);

        DynArray_add(pTable, entry);
    }
}

void printentry(DynArray_T pTable) {
    int len = DynArray_getLength(pTable);
    const char *name, *value;
    struct aliasentry *pEntry;
    for (int i=0; i<len; i++) {
        pEntry = DynArray_get(pTable, i);
        name = pEntry -> name;
        value = pEntry -> value;
        printf("alias %s=\'%s\'\n", name, value);
    }
}

struct aliasentry *findEntry(DynArray_T pTable, const char *name) {
    struct aliasentry target = {(char *)name, NULL};
    int ind;
    if ((ind=DynArray_search(pTable, &target, entryCompare)) == -1) {
        //not found.
        return NULL;
    } else {
        return DynArray_get(pTable, ind);
    }
}

void updateToken(DynArray_T pTable, DynArray_T oTokens, int start) {
    // everytime command encounters, search cmd[0] to alias table.
    // if found, replace tokens.
    char *target;
    char *replace;
    struct aliasentry *ptr;
    int sublen;
    DynArray_T substitute = DynArray_new(0);
    
    target = ((struct Token *) DynArray_get(oTokens, start)) -> pcValue;
    if ((ptr = findEntry(pTable, target))) {
        // entry found.
        replace = ptr -> value;
        // construct substitute tokens.
        lexLine(replace, substitute);
        sublen = DynArray_getLength(substitute);
        // delete original command from oTokens.
        DynArray_removeAt(oTokens, start);
        // shove substitute tokens.
        for (int i=0; i<sublen; i++) {
            struct Token *pTok = DynArray_get(substitute, i);
            DynArray_addAt(oTokens, start++, pTok);
        }
    }
    DynArray_free(substitute);
}

void cleanentry(DynArray_T pTable) {
    int len = DynArray_getLength(pTable);
    struct aliasentry *pEntry;
    for (int i=0; i<len; i++) {
        pEntry = DynArray_get(pTable, i);
        free(pEntry -> name);
        free(pEntry -> value);
        free(pEntry);
    }
    DynArray_free(pTable);
}

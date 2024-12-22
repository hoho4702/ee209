/*--------------------------------------------------------------------*/
/* util.h - 20230523 Yeonjun Lee                                      */
/* This is header file to use utility functions in util.c             */
/*--------------------------------------------------------------------*/

#ifndef _UTIL_H_
#define _UTIL_H_

#include "token.h"
#include "dynarray.h"

enum {FALSE, TRUE};

enum BuiltinType {NORMAL, B_EXIT, B_SETENV, B_USETENV,
    B_CD, B_ALIAS, B_FG};
enum PrintMode {SETUP, PERROR, FPRINTF, ALIAS};

void errorPrint(char *input, enum PrintMode mode);
enum BuiltinType checkBuiltin(struct Token *t);
int countPipe(DynArray_T oTokens);
int checkBG(DynArray_T oTokens);
void dumpLex(DynArray_T oTokens);

#endif /* _UTIL_H_ */

#ifndef _UTIL_H_
#define _UTIL_H_

#include "token.h"
#include "dynarray.h"

#define MAX_PATH_LEN 100

enum {FALSE, TRUE};

enum BuiltinType {NORMAL, B_EXIT, B_SETENV, B_USETENV, B_CD, B_ALIAS, B_FG};
enum PrintMode {SETUP, PERROR, FPRINTF, ALIAS};

void errorPrint(char *input, enum PrintMode mode);
enum BuiltinType checkBuiltin(struct Token *t);
int countPipe(DynArray_T oTokens);
int checkBG(DynArray_T oTokens);
void dumpLex(DynArray_T oTokens);

void redirect(DynArray_T oTokens);
char* get_abspath(char* path, char* abspath);
#endif /* _UTIL_H_ */

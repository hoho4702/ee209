#ifndef _UTIL_H_
#define _UTIL_H_

#include "token.h"
#include "dynarray.h"

enum {FALSE, TRUE};

enum BuiltinType {NORMAL, B_EXIT, B_SETENV, B_USETENV, B_UNSETENV, B_CD, B_ALIAS, B_FG};
enum PrintMode {SETUP, PERROR, FPRINTF, ALIAS};

void executeExternal(DynArray_T oTokens);
void errorPrint(char *input, enum PrintMode mode);
enum BuiltinType checkBuiltin(struct Token *t);
int countPipe(DynArray_T oTokens);
int checkBG(DynArray_T oTokens);
void dumpLex(DynArray_T oTokens);
void executePipedCommands(char *cmd1[], char *cmd2[]);

#endif /* _UTIL_H_ */

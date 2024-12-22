#ifndef _EXE_H
#define _EXE_H


#include "token.h"
#include "util.h"
#include "lexsyn.h"

void executeBuiltin(DynArray_T oTokens, enum BuiltinType btype);
void executeExternal(DynArray_T oTokens);

#endif
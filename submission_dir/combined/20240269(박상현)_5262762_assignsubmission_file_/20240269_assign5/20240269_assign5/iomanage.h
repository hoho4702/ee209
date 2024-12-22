#ifndef _IOMANAGE_H_
#define _IOMANAGE_H_

#include <stdio.h>
#include "lexsyn.h"
#include "token.h"

void redirect_pipe(int state);
int redirect(DynArray_T oTokens);

#endif

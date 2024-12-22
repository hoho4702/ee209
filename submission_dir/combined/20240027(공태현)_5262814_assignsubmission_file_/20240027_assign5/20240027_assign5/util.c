#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include "dynarray.h"
#include "util.h"

void
errorPrint(char *input, enum PrintMode mode) {
  static char *ishname = NULL;

  if (mode == SETUP)
    ishname = input;
  else {
    if (ishname == NULL)
      fprintf(stderr, "[WARN] Shell name is not set. Please fix this bug in main function\n");
    if (mode == PERROR) {
      if (input == NULL)
        fprintf(stderr, "%s: %s\n", ishname, strerror(errno));
      else
        fprintf(stderr, "%s: %s\n", input, strerror(errno));
    }
    else if (mode == FPRINTF)
      fprintf(stderr, "%s: %s\n", ishname, input);
    else if( mode == ALIAS)
      fprintf(stderr, "%s: alias: %s: not found\n", ishname, input);
    else if (mode == USER)
      fprintf(stderr, "%s: %s\n", ishname, input);
    else
      fprintf(stderr, "mode %d not supported in errorPrint\n", mode);
    }
}

enum BuiltinType
checkBuiltin(char* pc) {
  /* Check null input before using string functions  */
  assert(pc);

  if (strncmp(pc, "cd", 2) == 0 && strlen(pc) == 2)
    return B_CD;
  if (strncmp(pc, "fg", 2) == 0 && strlen(pc) == 2)
    return B_FG;
  if (strncmp(pc, "exit", 4) == 0 && strlen(pc) == 4)
    return B_EXIT;
  else if (strncmp(pc, "setenv", 6) == 0 && strlen(pc) == 6)
    return B_SETENV;
  else if (strncmp(pc, "unsetenv", 8) == 0 && strlen(pc) == 8)
    return B_USETENV;
  else if (strncmp(pc, "alias" , 5) == 0 && strlen(pc) == 5) 
    return B_ALIAS;
  else
    return NORMAL;
}

int
countPipe(DynArray_T oTokens) {
  int cnt = 0, i;
  struct Token *t;

  for (i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE)
      cnt++;
  }

  return cnt;
}

/* Check background Command */
int
checkBG(DynArray_T oTokens) {
  int i;
  struct Token *t;

  for (i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_BG)
      return 1;
  }
  return 0;
}

const char* specialTokenToStr(struct Token* psToken) {
  switch(psToken->eType) {
    case TOKEN_PIPE:
      return "TOKEN_PIPE(|)";
      break;
    case TOKEN_REDIN:
      return "TOKEN_REDIRECTION_IN(<)";
      break;
    case TOKEN_REDOUT:
      return "TOKEN_REDIRECTION_OUT(>)";
      break;
    case TOKEN_BG:
      return "TOKEN_BACKGROUND(&)";
      break;
    case TOKEN_WORD:
      /* This should not be called with TOKEN_WORD */
    default:
      assert(0 && "Unreachable");
      return NULL;
  }
}

void
dumpLex(DynArray_T oTokens) {
  if (getenv("DEBUG") != NULL) {
    int i;
    struct Token *t;

    for (i = 0; i < DynArray_getLength(oTokens); i++) {
      t = DynArray_get(oTokens, i);
      if (t->pcValue == NULL)
        fprintf(stderr, "[%d] %s\n", i, specialTokenToStr(t));
      else
        fprintf(stderr, "[%d] TOKEN_WORD(\"%s\")\n", i, t->pcValue);
    }
  }
}

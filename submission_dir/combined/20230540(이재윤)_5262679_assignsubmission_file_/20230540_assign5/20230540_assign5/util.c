/* 20230540 Lee Jaeyun
This file contains utility functions for a simple shell implementation.
It includes functions for error handling,  built-in command execution,
token parsing, and functions to count special tokens.
It uses a dynamic array for managing tokens. */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

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
    else
      fprintf(stderr, "mode %d not supported in errorPrint\n", mode);
    }
}

enum BuiltinType
checkBuiltin(struct Token *t) {
  /* Check null input before using string functions  */
  assert(t);
  assert(t->pcValue);

  if (strncmp(t->pcValue, "cd", 2) == 0 && strlen(t->pcValue) == 2)
    return B_CD;
  if (strncmp(t->pcValue, "fg", 2) == 0 && strlen(t->pcValue) == 2)
    return B_FG;
  if (strncmp(t->pcValue, "exit", 4) == 0 && strlen(t->pcValue) == 4)
    return B_EXIT;
  else if (strncmp(t->pcValue, "setenv", 6) == 0 && strlen(t->pcValue) == 6)
    return B_SETENV;
  else if (strncmp(t->pcValue, "unsetenv", 8) == 0 && strlen(t->pcValue) == 8)
    return B_USETENV;
  else if (strncmp(t->pcValue, "alias" , 5) == 0 && strlen(t->pcValue) == 5) 
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

void executeCd(DynArray_T oTokens) {
  int argc = DynArray_getLength(oTokens);
  if (argc >2) {
    errorPrint("cd takes one parameter", FPRINTF);
  }
  else if (argc == 1) {
    const char *homeDir = getenv("HOME");
    if (chdir(homeDir) == -1) errorPrint(NULL, PERROR);
  }
  else {
    const char *dir = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
    if (chdir(dir) == -1) errorPrint(NULL, PERROR);
  }
}

void executeSetenv(DynArray_T oTokens) {
  int argc = DynArray_getLength(oTokens);
  if (argc > 3 || argc == 1) {
    errorPrint("setenv takes one or two parameter", FPRINTF);
  }
  else if (argc == 2) {
    const char *dir = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
    setenv(dir, "", 1);
  }
  else {
    const char *dir = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
    const char *value = ((struct Token *)DynArray_get(oTokens, 2))->pcValue;
    setenv(dir, value, 1);
  }
}

void executeExit(DynArray_T oTokens) {
  int argc = DynArray_getLength(oTokens);
  if (argc > 1) {
    errorPrint("exit takes no parameter", FPRINTF);
    return;
  }
  printf("\n");
  DynArray_free(oTokens);
  exit(EXIT_SUCCESS);
}

void executeUnsetenv(DynArray_T oTokens) {
  int argc = DynArray_getLength(oTokens);
  if (argc != 2) {
    errorPrint("unsetenv takes one parameter", FPRINTF);
    return;
  }
  const char *dir = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
  if (getenv(dir) != NULL) {
    unsetenv(dir);
  }
}

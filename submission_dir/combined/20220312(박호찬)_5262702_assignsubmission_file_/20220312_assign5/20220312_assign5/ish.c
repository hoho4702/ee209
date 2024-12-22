#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include "lexsyn.h"
#include "util.h"

#define MAX_LINE_SIZE 1024

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static void
shellHelper(const char *inLine) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }


  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0)
        return;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        if (btype == B_EXIT) {
          exit(0);
        } else if (btype == B_CD) {
          struct Token *path = DynArray_get(oTokens, 1);
          if (path != NULL && chdir(path->pcValue) == -1) {
            perror("cd");
          }
        } else {
          char command[256];
          memset(command, 0, sizeof(command));

          struct Token *cmd = DynArray_get(oTokens, 0);
          strcpy(command, cmd->pcValue);

          for (int i = 1; i < DynArray_getLength(oTokens); i++) {
              struct Token *arg = DynArray_get(oTokens, i);
              strcat(command, " ");
              strcat(command, arg->pcValue);
          }

          int status = system(command);
          if (status == -1) {
              errorPrint("Failed to execute command", FPRINTF);
          }
        }
      }

      /* syntax error cases */
      else if (syncheck == SYN_FAIL_NOCMD)
        errorPrint("Missing command name", FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDOUT)
        errorPrint("Multiple redirection of standard out", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTOUT)
        errorPrint("Standard output redirection without file name", FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDIN)
        errorPrint("Multiple redirection of standard input", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTIN)
        errorPrint("Standard input redirection without file name", FPRINTF);
      else if (syncheck == SYN_FAIL_INVALIDBG)
        errorPrint("Invalid use of background", FPRINTF);
      break;

    case LEX_QERROR:
      errorPrint("Unmatched quote", FPRINTF);
      break;

    case LEX_NOMEM:
      errorPrint("Cannot allocate memory", FPRINTF);
      break;

    case LEX_LONG:
      errorPrint("Command is too large", FPRINTF);
      break;

    default:
      errorPrint("lexLine needs to be fixed", FPRINTF);
      exit(EXIT_FAILURE);
  }
}

void initialize_shell() {
  char *home_dir = getenv("HOME");
  if (home_dir == NULL) return;
  char ishrc_path[256];
  snprintf(ishrc_path, sizeof(ishrc_path), "%s/.ishrc", home_dir);
  FILE *fp = fopen(ishrc_path, "r");
  if (fp == NULL) return;
  char line[1024];
  while (fgets(line, sizeof(line), fp) != NULL) {
    shellHelper(line);
  }
  fclose(fp);
}

int main() {
  /* TODO */
  initialize_shell();
  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
    printf("%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
}

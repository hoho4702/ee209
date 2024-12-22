#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>   // chdir, fork, execvp
#include <string.h>   // strcmp, strncmp
#include <sys/wait.h> // wait
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

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }


  lexcheck = lexLine(inLine, oTokens);
  syncheck = syntaxCheck(oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0)
          return;

      // dump lex result when DEBUG is set
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
          char *command = DynArray_get(oTokens, 0);
          if (strcmp(command, "exit") == 0) {
              exit(EXIT_SUCCESS);
          } else if (strcmp(command, "cd") == 0) {
              if (DynArray_getLength(oTokens) < 2) {
                  fprintf(stderr, "./ish: cd requires a directory argument\n");
              } else {
                  if (chdir(DynArray_get(oTokens, 1)) != 0) {
                      perror("./ish");
                  } else {
                      char cwd[1024];
                      if (getcwd(cwd, sizeof(cwd)) != NULL) {
                          printf("%s\n", cwd);
                      }
                  }
              }
          } else {
              // External command execution
              pid_t pid = fork();
              if (pid == 0) {  // Child process
                  char *argv[DynArray_getLength(oTokens) + 1];
                  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
                      argv[i] = DynArray_get(oTokens, i);
                  }
                  argv[DynArray_getLength(oTokens)] = NULL;
                  if (execvp(command, argv) == -1) {
                      perror("./ish");
                      exit(EXIT_FAILURE);
                  }
              } else if (pid > 0) {  // Parent process
                  wait(NULL);  // Wait for child process
              } else {
                  perror("./ish: fork failed");
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
    DynArray_free(oTokens);
}

int main() {
    char acLine[MAX_LINE_SIZE + 2];
    while (1) {
        fprintf(stdout, "%% ");
        fflush(stdout);
        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }
        if (strncmp(acLine, "exit", 4) == 0 && (acLine[4] == '\0' || acLine[4] == '\n')) {
            exit(EXIT_SUCCESS);
        }
        shellHelper(acLine);
    }
}


#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include "lexsyn.h"
#include "util.h"
#include "token.h"
#include "iomanage.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static void exitexit(int iSig) {
  exit(EXIT_SUCCESS);
}

static void waitSec(int iSig) {
  void (*pfRet) (int);
  printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  alarm(5);
  pfRet = signal(SIGQUIT, exitexit);
  assert(pfRet != SIG_ERR);
}

static void noInput(int iSig) {
  void (*pfRet) (int);
  pfRet = signal(SIGQUIT, waitSec);
  assert(pfRet != SIG_ERR);
}

//Concastinate the commands in oTokens, return offset where the special char appears
void makeCommand(DynArray_T oTokens, char *arg_buf[MAX_ARGS_CNT][MAX_ARGS_CNT]) {
  assert(oTokens);
  assert(arg_buf);
  struct Token *t;
  int j = 0, k = 0;
  
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_WORD) {
      arg_buf[k][j++] = t->pcValue;
    } else if (t->eType == TOKEN_PIPE) {
      k++;
      j = 0;
    } else i++;
  }
  return;
}

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
        char *arg_buf[MAX_ARGS_CNT][MAX_ARGS_CNT] = {{0}};
        makeCommand(oTokens, arg_buf);

        for (int i = 0; arg_buf[i][0] != NULL; i++) {
          btype = checkBuiltin(arg_buf[i][0]);

          switch (btype) {
            case B_CD:
              if (arg_buf[i][2]) {
                errorPrint("cd takes one parameter", USER);
                return;
              }
              if (!arg_buf[i][1]) {
                char* home = getenv("HOME");
                if (chdir(home) != 0) errorPrint(NULL, PERROR);
              } else if (chdir(arg_buf[i][1]) != 0) errorPrint(NULL, PERROR);
              break;

            case B_SETENV:
              if (setenv(arg_buf[i][1], arg_buf[i][2], 1) != 0) errorPrint(NULL, PERROR);
              break;

            case B_USETENV:
              if (unsetenv(arg_buf[i][1]) != 0) errorPrint(NULL, PERROR);
              break;
            
            case B_EXIT:
              exit(EXIT_SUCCESS);

            default:
              fflush(NULL);
              pid_t pid = fork();
              if (pid == 0) {
                if (i != 0 || arg_buf[i+1][0] != NULL) {
                  int j = i;
                  if (arg_buf[i+1][0] == NULL) j = -1;
                  redirect_pipe(j);
                }
                if (redirect(oTokens) != 0) {
                  errorPrint("No such file or directory", USER);
                  return;
                }

                void (*pfRet) (int);
                pfRet = signal(SIGINT, SIG_DFL);
                assert(pfRet != SIG_ERR);
                pfRet = signal(SIGQUIT, SIG_DFL);
                assert(pfRet != SIG_ERR);
                pfRet = signal(SIGALRM, SIG_DFL);
                assert(pfRet != SIG_ERR);
                if (execvp(arg_buf[i][0], arg_buf[i]) == -1) errorPrint(arg_buf[i][0], PERROR);
                exit(EXIT_FAILURE);
              }
              waitpid(pid, NULL, 0);
          }
        }
        remove("temp.txt");
        remove("temp_in.txt");
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

int main(int argc, char* argv[]) {
  errorPrint(argv[0], SETUP);
  char acLine[MAX_LINE_SIZE + 2];
  void (*pfRet) (int);
  sigset_t sSet;

  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);
  pfRet = signal(SIGINT, SIG_IGN);
  assert(pfRet != SIG_ERR);
  pfRet = signal(SIGQUIT, waitSec);
  assert(pfRet != SIG_ERR);
  pfRet = signal(SIGALRM, noInput);
  assert(pfRet != SIG_ERR);
  
  char filepath[512];
  const char *home = getenv("HOME");
  if (home == NULL) {
    fprintf(stderr, "Can't find ""Home"" variable\n");
    return 1;
  }
  
  snprintf(filepath, sizeof(filepath), "%s/.ishrc", home);
  FILE *file = fopen(filepath, "r");
  
  if (file) {
    while (fgets(acLine, sizeof(acLine), file)) {
      if (acLine[strlen(acLine)-1] == '\n') acLine[strlen(acLine)-1] = 0;
      printf("%% %s\n", acLine);
      shellHelper(acLine);
    }
    fclose(file);
  }

  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
}

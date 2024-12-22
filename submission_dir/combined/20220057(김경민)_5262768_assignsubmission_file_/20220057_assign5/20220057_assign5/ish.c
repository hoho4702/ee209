#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

int sigquitCount = 0;

//  signal handler for SIGALRM signal
static void sigalrmHandler() {
  sigquitCount = 0;
}

//  signal handler for SIGQUIT signal
static void sigquitHandler() {
  if(sigquitCount == 0) {
    sigquitCount = 1;
    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5);
  }
  else {
    exit(EXIT_SUCCESS);
  }
}
/*  Processes a line of input, perform lexical 
 *  analysis and syntax check. Executes built-in
 *  commands or external programs based on the input.
 */

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
        if(btype == B_CD) { // cd
          if(DynArray_getLength(oTokens) == 1) { // only cd
            if(chdir(getenv("HOME")) != 0)
              errorPrint(strerror(errno), FPRINTF);
          }
          else if(DynArray_getLength(oTokens) == 2) { // a parameter
            if(chdir(tokenToStr(DynArray_get(oTokens, 1))) != 0)
              errorPrint(strerror(errno), FPRINTF);
          }
          else
            errorPrint("cd takes one parameter", FPRINTF);
        }
        else if(btype == B_EXIT) { // exit
          DynArray_free(oTokens);
          exit(EXIT_SUCCESS);
        }
        else if(btype == B_SETENV) { // setenv
          if(DynArray_getLength(oTokens) == 2 && 
          checkWordToken(DynArray_get(oTokens, 1))) {
            if(setenv(tokenToStr(DynArray_get(oTokens, 1)), "", 1) != 0)
              errorPrint(strerror(errno), FPRINTF);
          }
          else if(DynArray_getLength(oTokens) == 3 && 
          checkWordToken(DynArray_get(oTokens, 1)) && 
          checkWordToken(DynArray_get(oTokens, 2))) {
            if(setenv(tokenToStr(DynArray_get(oTokens, 1)), 
            tokenToStr(DynArray_get(oTokens, 2)), 1) != 0)
              errorPrint(strerror(errno), FPRINTF);
            }
          else
            errorPrint("setenv takes one or two parameters", FPRINTF);
        }
        else if(btype == B_USETENV) { // unsetenv
          if(DynArray_getLength(oTokens) == 2) {
            if(unsetenv(tokenToStr(DynArray_get(oTokens, 1))) != 0)
              errorPrint(strerror(errno), FPRINTF);
          }
          else
            errorPrint("unsetenv takes one parameter", FPRINTF);
        }
        else { // normal
          // implement pipe - issue : 'sed'
          /*
          int pipeCount = 0, i = 0;
          while(i < DynArray_getLength(oTokens)) {
            struct Token *token = DynArray_get(oTokens, i);
            if(token->eType == TOKEN_PIPE)
              pipeCount++;
            i++;
          }
          if(pipeCount > 0) {
            int pipefd[2 * pipeCount + 2];

            i = 0;
            while(i <= pipeCount) {
              if(pipe(pipefd + i * 2) == -1) {
                errorPrint(strerror(errno), FPRINTF);
                exit(EXIT_FAILURE);
              }
              i++;
            }
            
            pid_t pid;
            i = 0;
            int j = 0;
            while(i < pipeCount + 1) {
              int argc = 0;
              while(j + argc < DynArray_getLength(oTokens) && 
              checkWordToken(DynArray_get(oTokens, j + argc)))
                argc++;
              const char *argv[argc + 1];
              int m = 0;
              while(m < argc) {
                argv[m] = 
                  strdup(tokenToStr(DynArray_get(oTokens, j + m)));
                m++;
              }
              argv[argc] = NULL;

              pid = fork();
              if(pid == 0) {    // child process
                signal(SIGINT, SIG_DFL); // reset signal handler
                signal(SIGQUIT, SIG_DFL);
                char *programName = strdup(argv[0]);
                errorPrint(programName, SETUP);
                free(programName);

                if(i > 0) { // non-first pipe
                  close(pipefd[(i - 1) * 2 + 1]);
                  dup2(pipefd[(i - 1) * 2], STDIN_FILENO);
                  close(pipefd[(i - 1) * 2]);
                }
                if(i < pipeCount) { // non-last pipe
                  close(pipefd[i * 2]);
                  dup2(pipefd[i * 2 + 1], STDOUT_FILENO);
                  close(pipefd[i * 2 + 1]);
                }

                if(i == pipeCount) {
                  close(pipefd[i * 2 + 1]);
                }

                int k = 0;
                while(k < 2 * pipeCount + 2) {
                  close(pipefd[k]);
                  k++;
                }
                
                if(execvp(argv[0], argv) == -1) {
                  errorPrint(strerror(errno), FPRINTF);
                  exit(EXIT_FAILURE);
                }
                exit(EXIT_FAILURE);
              }
              int status;
              waitpid(pid, &status, 0);
              i++;
              j += argc + 1;
            }

          }
          */
          
          pid_t pid = fork();
          if(pid == 0) {  // child process
            signal(SIGINT, SIG_DFL); // reset signal handler
            signal(SIGQUIT, SIG_DFL);
            char *programName = strdup(tokenToStr(DynArray_get(oTokens, 0)));
            errorPrint(programName, SETUP);
            free(programName);
            
            int argc = 0;
            while(argc < DynArray_getLength(oTokens) && 
            checkWordToken(DynArray_get(oTokens, argc)))
              argc++;
            
            char *argv[argc + 1];
            int i = 0;
            while(i < argc) {
              argv[i] = strdup(tokenToStr(DynArray_get(oTokens, i)));
              i++;
            }
            argv[argc] = NULL;

            while(i < DynArray_getLength(oTokens)) { // redirection
              struct Token *token = DynArray_get(oTokens, i);
              if(token->eType == TOKEN_REDIN) {
                if(access(tokenToStr(DynArray_get(oTokens, i + 1)), 
                F_OK) == 0) {
                  int fd = 
                    open(tokenToStr(DynArray_get(oTokens, i + 1)), 
                    O_RDONLY);
                  dup2(fd, STDIN_FILENO);
                  i++;
                }
                else {
                  errorPrint("No such file or directory", FPRINTF);
                  exit(0);
                }
              }
              else if(token->eType == TOKEN_REDOUT) {
                int fd = open(tokenToStr(DynArray_get(oTokens, i + 1)),
                  O_WRONLY | O_TRUNC | O_CREAT, 0600);
                dup2(fd, STDOUT_FILENO);
                i++;
              }
              i++;
            }
            
            if(execvp(argv[0], argv) == -1) {
              errorPrint(strerror(errno), FPRINTF);
              exit(EXIT_FAILURE);
            }
          }
          else {          // parent process
            int status;
            waitpid(pid, &status, 0);
          }
        }
      }


      /* syntax error cases */
      else if (syncheck == SYN_FAIL_NOCMD)
        errorPrint("Missing command name", FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDOUT)
        errorPrint("Multiple redirection of standard out", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTOUT)
        errorPrint("Standard output redirection without file name",
          FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDIN)
        errorPrint("Multiple redirection of standard input", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTIN)
        errorPrint("Standard input redirection without file name",
          FPRINTF);
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


/*  the main function install signal handlers, 
 *  read .ishrc file, and read inputs.
 */
int main(int argc __attribute__((unused)), char *argv[]) {
  sigset_t sSet;
  char acLine[MAX_LINE_SIZE + 2];
  char file_path[MAX_LINE_SIZE + 2] = "";

  // install signal handler
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, sigquitHandler);
  signal(SIGALRM, sigalrmHandler);
  errorPrint(argv[0], SETUP);

  // read .ishrc at first
  strcat(file_path, getenv("HOME"));
  if(file_path[strlen(file_path) - 1] != '/')
    strcat(file_path, "/.ishrc");
  else
    strcat(file_path, ".ishrc");

  if(access(file_path, R_OK) == 0) {
    FILE *fp = fopen(file_path, "r");
    while(fgets(acLine, MAX_LINE_SIZE, fp) != NULL) {
      fprintf(stdout, "%% %s", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
  }

  while(1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if(fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
  return 0;
}


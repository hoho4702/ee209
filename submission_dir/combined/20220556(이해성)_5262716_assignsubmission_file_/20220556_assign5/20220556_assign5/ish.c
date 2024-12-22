#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>

#include "lexsyn.h"
#include "util.h"
#include "token.h"
#include "dynarray.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

/*20220556 이해성 EE209 Assignment 5: A Unix Shell*/

/*ctrl-\ 처리를 위한 global variable*/
int quitcheck = 0; 

/*ctrl-\, alarm을 위한 handler*/

static void handler_quit(int iSig) {
  if (quitcheck == 1)
    exit(EXIT_SUCCESS);

  else {
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    alarm(5);
    quitcheck = 1;
  }
}
static void handler_alarm(int iSig) {
  quitcheck = 0;
}

/*한 줄의 입력에 대한 처리를 하는 shellhelper 함수*/
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
        
        pid_t pid;
        int i;
        int fd;
        char* somefile;
        int check_in = 0;
        int check_out = 0;
        int original_in = dup(0);
        int original_out = dup(1);

        for (i = 0; i < DynArray_getLength(oTokens); i++) {
          if (((struct Token*)DynArray_get(oTokens, i)) -> eType == TOKEN_REDIN)
            check_in = i;
          else if (((struct Token*)DynArray_get(oTokens, i)) -> eType == TOKEN_REDOUT)
            check_out = i;
        }

        switch (btype)
        {
          case B_CD :
            if (DynArray_getLength(oTokens) == 2) {
              if (chdir(((struct Token*)DynArray_get(oTokens, 1))-> pcValue) == -1)
                fprintf(stderr, "./ish: No such file or directory\n");
            }

            else if (DynArray_getLength(oTokens) == 1) {
              if (chdir(getenv("HOME")) == -1)
                fprintf(stderr, "./ish: No such file or directory\n");
            }

            else
              fprintf(stderr, "./ish: cd takes one parameter\n");

            break;

          case B_EXIT :
            if (DynArray_getLength(oTokens) == 1){
              DynArray_free(oTokens);
              exit(0);
            }
            else 
              fprintf(stderr, "./ish: exit does not take any parameters\n");

            break;

          case B_SETENV :
            if (DynArray_getLength(oTokens) == 2) 
              setenv(((struct Token*)DynArray_get(oTokens, 1)) -> pcValue, "", 1);

            if (DynArray_getLength(oTokens) == 3) 
              setenv(((struct Token*)DynArray_get(oTokens, 1)) -> pcValue, ((struct Token*)DynArray_get(oTokens, 2)) -> pcValue, 1);

            else 
              fprintf(stderr, "./ish: setenv takes one or two parameters\n");

            break;

          case B_USETENV :
            if (DynArray_getLength(oTokens) == 2) 
              unsetenv(((struct Token*)DynArray_get(oTokens, 1))-> pcValue);

            else
              fprintf(stderr, "./ish: unsetenv takes one parameter\n");

            break;

          case NORMAL :
            pid = fork();
            if (pid == 0){
              signal(SIGINT, SIG_DFL);
              signal(SIGQUIT, SIG_DFL);

              char *argv[DynArray_getLength(oTokens) + 1];
              for (i = 0; i < DynArray_getLength(oTokens); i++) {
                argv[i] = ((struct Token*)DynArray_get(oTokens, i)) -> pcValue;
              }
              argv[DynArray_getLength(oTokens)] = NULL;

              if (check_out > 0) {
                somefile = ((struct Token*)DynArray_get(oTokens, check_out + 1)) -> pcValue;
                fd = creat(somefile, 0600);
                close(1);
                dup2(fd, 1);
                close(fd);
              }

              if (check_in > 0) {
                somefile = ((struct Token*)DynArray_get(oTokens, check_in + 1)) -> pcValue;
                fd = open(somefile, 0600);

                if (fd == -1) {
                  fprintf(stderr,"./ish: No such file or directory\n");
                  exit(EXIT_FAILURE);
                }

                close(0);
                dup2(fd, 0);
                close(fd);
              }

              if(execvp(argv[0], argv) == -1) 
                fprintf(stderr, "%s: No such file or directory\n", argv[0]);
              
              exit(EXIT_FAILURE);
            }

            pid = wait(NULL);
            dup2(original_in, 0);
            dup2(original_out, 1);
            close(original_in);
            close(original_out);

            break;
          
          default:

            break;
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


int main() {
  /* TODO */
  char acLine[MAX_LINE_SIZE + 2];
  errorPrint("./ish", SETUP);

  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);  

  void (*pfRet)(int);
  pfRet = signal(SIGINT, SIG_IGN);
  assert(pfRet != SIG_ERR);
  pfRet = signal(SIGQUIT, handler_quit);
  assert(pfRet != SIG_ERR);
  pfRet = signal(SIGALRM, handler_alarm);
  assert(pfRet != SIG_ERR);
  
  char *homedir = getenv("HOME");
  char *filedir = "/.ishrc";
  char *filepath = calloc(strlen(homedir) + strlen(filedir) + 1, 1);
  strcpy(filepath, homedir);
  strcat(filepath, filedir);
  FILE *openfile = fopen(filepath, "r");
  free(filepath);

  if (openfile != NULL) {
    while (fgets(acLine, MAX_LINE_SIZE, openfile) != NULL) {

      if (strlen(acLine) > 0 && acLine[strlen(acLine)-1] == '\n')
        acLine[strlen(acLine)-1] = '\0';

      printf("%% %s\n", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
    fclose(openfile);
  }

  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      fflush(stdout);
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
}


/*
EE209 Assignment 5: A Unix Shell
Submitted by SeungEon Lee, 20210854
This file implements the main function of ish.
*/


#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include "lexsyn.h"
#include "util.h"
#include "executor.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

volatile sig_atomic_t sigquit_count = 0;

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

  /* Temporary code for printing token list. */
  // int i;
  // struct Token *t;
  // for (i = 0; i < DynArray_getLength(oTokens); i++) {
  //   t = (struct Token *) DynArray_get(oTokens, i);
  //   if (t->eType == TOKEN_WORD) {
  //     fprintf(stdout, "Token %d: %s\n", i+1, t->pcValue);
  //   }
  //   else {
  //     fprintf(stdout, "Token %d: %s\n", i+1, (char *)specialTokenToStr(t));
  //   }
  // }

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
        int numPipes = 0;
        // if (checkRedirectionIn(oTokens)) redirection_in(oTokens);
        // if (checkRedirectionOut(oTokens)) redirection_out(oTokens);
        switch (btype) {
          case B_EXIT:            /* Built-in exit */
            execute_exit(oTokens);
            break;
          case B_SETENV:          /* Built-in setenv */
            execute_setenv(oTokens);
            break;
          case B_USETENV:         /* Built-in usetenv */
            execute_unsetenv(oTokens);
            break;
          case B_CD:              /* Built-in cd */
            execute_cd(oTokens);
            break;
          case NORMAL:
            numPipes = countPipe(oTokens);
            if (numPipes > 0) {
              execute_pipe(oTokens, numPipes);
            }
            else {
              execute_normal(oTokens);
            }
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

void sigquit_handler (int sig) {  
  (void) sig;
  sigset_t oldmask, newmask;
  
  /* Block SIGALRM and SIGQUIT while handling SIGQUIT. */
  sigemptyset(&newmask);
  sigaddset(&newmask, SIGALRM);
  sigaddset(&newmask, SIGQUIT);
  sigprocmask(SIG_BLOCK, &newmask, &oldmask);

  /* Exit only when SIGQUIT is recieved within 5 seconds interval. */
  if (sigquit_count == 0) {   /* If recieved first time (not within 5 seconds)*/
    sigquit_count = 1;
    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5);
  }
  else {                      /* If recieved second time (within 5 seconds)*/
    exit(0);
  }

  /* Restore previous signal mask. */
  sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

void sigalarm_handler (int sig) {
  (void) sig;
  sigset_t oldmask, newmask;

  /* Block SIGALRM and SIGQUIT while handling SIGQUIT. */
  sigemptyset(&newmask);
  sigaddset(&newmask, SIGALRM);
  sigaddset(&newmask, SIGQUIT);
  sigprocmask(SIG_BLOCK, &newmask, &oldmask);

  /* If SIGALRM is recieved, reset sigquit_count to zero. */
  sigquit_count = 0;

  /* Restore previous signal mask. */
  sigprocmask(SIG_SETMASK, &oldmask, NULL);
}

int main(int argc, char *argv[]) {
  /* TODO */

  /* Installing signal handlers for SIGINT, SIGQUIT, (and SIGALRM) */
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, sigquit_handler);
  signal(SIGALRM, sigalarm_handler);

  /* Initialization */
  /* 1. Setting up ishname for errorPrint as the executable name */
  errorPrint(argv[0],SETUP);

  /* 2. Reading and processing the .ishrc file in the HOME directory. */
  const char *homeDir = getenv("HOME");
  char ishrcPath[MAX_LINE_SIZE + 2];          /* Set up path for .ishrc */
  if(homeDir != NULL) {
    snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", homeDir);
  }
  if (access(ishrcPath, F_OK) == 0) {         /* Read only when .ishrc exits.*/
    FILE *ishrc = fopen(ishrcPath, "r");      /* Open .ishrc */
    char ishrcLine[MAX_LINE_SIZE + 2];        /* Set up array to read from .ishrc */
    if (ishrc != NULL) {
      while (fgets(ishrcLine, MAX_LINE_SIZE, ishrc) != NULL) {
        fprintf(stdout, "%% %s", ishrcLine);  /* Print line by line with % in front.*/
        fflush(stdout);
        shellHelper(ishrcLine);               /* Run command using shellHelper. */
      }
    }
    fclose(ishrc);
  }

  /* Interactive Operation */
  char acLine[MAX_LINE_SIZE + 2];
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

/*--------------------------------------------------------------------*/
/* 20220674 주영주                                                     */
/* Assignment5                                                        */
/* ish.c                                                              */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <string.h>
#include <assert.h>
#include <signal.h>
#include <unistd.h>

#include "lexsyn.h"
#include "util.h"
#include "execute.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

char *programname;     // name of program(ish)
static int qattempt = 0;     // number of SIGQUIT signal(for 5 seconds)

/*-----handler for SIGQUIT signal-----*/
void sigquit_handler(int sig){
  /*-----for the first SIGQUIT signal-----*/
  if(qattempt == 0){
    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    alarm(5);
    qattempt++;
  }

  /*-----for the second SIGQUIT signal(befor ringing alarm)-----*/
  else {
    fprintf(stdout, "\n");
    exit(EXIT_SUCCESS);
  }
}

/*-----handler for SIGALRM signal-----*/
void sigalrm_handler(int sig){
  qattempt = 0;
}

/*-----free dynamic array and tokens-----*/
void Free_token_dynarray(DynArray_T oTokens){
  int dynlen = DynArray_getLength(oTokens);
  int i;
  struct Token *cToken;
  for(i = 0; i < dynlen; i++){
    cToken = (struct Token *)DynArray_get(oTokens, i);
    if(cToken != NULL) freeToken(cToken, NULL);
  }
  DynArray_free(oTokens);
}

/*-----shell helper function-----*/
/*-----for input line,
  1) make token dynamic array
  2) check syntactical correctness
  3) execute---------------------*/
static void
shellHelper(const char *inLine) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  /*enum BuiltinType btype;*/

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }

  /*-----make the line to lexline-----*/
  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0)
        return;

      /*-----dump lex result when DEBUG is set-----*/
      dumpLex(oTokens);

      /*-----syntax check for lexline-----*/
      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) execute(oTokens);

      /*-----syntax error cases-----*/
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

    /*-----lexline error cases-----*/
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
      Free_token_dynarray(oTokens);     // free dynamic array and tokens
      exit(EXIT_FAILURE);
  }
  Free_token_dynarray(oTokens);     // free dynamic array and tokens
}

/*-----main function-----*/
int main(int argc, char *argv[]) {
  programname = argv[0];
  errorPrint(programname, SETUP);
  
  char acLine[MAX_LINE_SIZE + 2];     // save each line which will be executed
  char ishrcdir[PATH_MAX];     // save the path of .ishrc
  FILE* ishrc_fp;     // file pointer for .ishrc file

/*-----signal handling part-----*/
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, sigquit_handler);
  signal(SIGALRM, sigalrm_handler);

/*-----save the path of .ishrc file-----*/
  const char* homedir = getenv("HOME");
  assert(homedir != NULL);

  strcpy(ishrcdir, homedir);
  strcat(ishrcdir, "/.ishrc");

/*-----first, call shellhelper for .ishrc file-----*/
  ishrc_fp = fopen(ishrcdir, "r");

  if(ishrc_fp != NULL){
    while(fgets(acLine, MAX_LINE_SIZE, ishrc_fp) != NULL){
      acLine[strcspn(acLine, "\n")] = '\0';     // for print with correct form
      fprintf(stdout, "%% %s\n", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
    fclose(ishrc_fp);
  }

/*-----next, call shellhelper for stdin input-----*/
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);     // for null input, exit the process
    }
    shellHelper(acLine);
  }
}


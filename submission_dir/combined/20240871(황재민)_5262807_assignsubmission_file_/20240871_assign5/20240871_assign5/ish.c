// 20240871, hwang jaemin
// ish program
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <signal.h>
#include <fcntl.h>
#include "lexsyn.h"
#include "util.h"
#include "signal.h"
/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static void
exec_ish(enum BuiltinType btype, DynArray_T oTokens){
  
  switch(btype){
    case NORMAL:{
      pid_t pid;
      if((pid=fork())==0){
        redirect(oTokens);// redirect

        //make oTokens to char* argv[] format
        struct Token *argv_token[MAX_ARGS_CNT];
        char *argv[MAX_ARGS_CNT];
        DynArray_toArray(oTokens,(void*)argv_token);
        for(int i=0;i<DynArray_getLength(oTokens);i++)
          argv[i]=argv_token[i]->pcValue;
        argv[DynArray_getLength(oTokens)]=NULL;
        // set default signal
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        // execute
        execvp(argv[0],argv);
        // failed to execute
        printf("%s: No such file or directory\n",argv[0]);
        exit(0);
      }
      waitpid(-1,NULL,0);
      break;}
    case B_EXIT:
      exit(0);
    case B_SETENV:
      setenv(((struct Token*)DynArray_get(oTokens,1))->pcValue,((struct Token*)DynArray_get(oTokens,2))->pcValue,0);
      break;
    case B_USETENV:
      unsetenv(((struct Token*)DynArray_get(oTokens,1))->pcValue);
      break;
    case B_CD:{
      char abspath[MAX_PATH_LEN+1];

      if(DynArray_getLength(oTokens)<2) strcpy(abspath,getenv("HOME"));
      else get_abspath(((struct Token*)DynArray_get(oTokens,1))->pcValue,abspath);

      if(chdir(abspath))printf("./ish: No such file or directory\n");

      break;}
    case B_ALIAS:
      break;
    case B_FG:
      break;
  }
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
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        exec_ish(btype, oTokens);
        /* TODO */
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

static void 
init_ish(){
  
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT,sigquit_handler);
  signal(SIGALRM,sigalarm_handler);
  
  // execute ~/.ishrc
  char acLine[MAX_LINE_SIZE + 2];

  FILE *ishrc;
  char* home=getenv("HOME");
  sprintf(acLine,"%s%s",home,"/.ishrc");

  if((ishrc=fopen(acLine,"r"))==NULL)return;//failed to open

  while(fgets(acLine,MAX_LINE_SIZE,ishrc)){
    fprintf(stdout,"%% %s",acLine); // print line
    shellHelper(acLine); // execute
  }
  fclose(ishrc);
}

int main() {
  /* TODO */
  init_ish();

  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    //printf("%s",acLine);
    shellHelper(acLine);
  }
}

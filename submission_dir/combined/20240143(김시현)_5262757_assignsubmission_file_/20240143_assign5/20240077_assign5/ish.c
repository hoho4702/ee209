#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

// function for command 'cd'.
void com_cd(DynArray_T oTokens){
  const int nParam = DynArray_getLength(oTokens);
  assert(nParam>0);
  if(nParam==1){
    if(chdir(getenv("HOME"))==-1)
      errorPrint(strerror(errno), FPRINTF);
  } else if(nParam==2){
    if(chdir(((struct Token *)DynArray_get(oTokens, 1))->pcValue)==-1)
      errorPrint(strerror(errno), FPRINTF);
  } else {
    errorPrint("cd takes one parameter\n", FPRINTF);
  }
}
// function for command 'exit'.
void com_exit(DynArray_T oTokens){
  exit(0);
}
// function for command 'setenv'.
void com_setenv(DynArray_T oTokens){
  const int nParam = DynArray_getLength(oTokens);
  assert(nParam>0);
  if(nParam==2){ // setenv ~~
    if(setenv(((struct Token*)DynArray_get(oTokens, 1))->pcValue, "", 1)==-1){
      // fail to setenv
      errorPrint(strerror(errno), FPRINTF);
    }
  } else if(nParam==3){
    // <, >
    if(((struct Token*)DynArray_get(oTokens, 1))->eType == TOKEN_REDIN)
      errorPrint("Standard input redirection", FPRINTF);
    else if(((struct Token*)DynArray_get(oTokens, 1))->eType == TOKEN_REDOUT)
      errorPrint("Standard output redirection", FPRINTF);
    else {
      if(setenv(((struct Token*)DynArray_get(oTokens, 1))->pcValue, 
          ((struct Token*)DynArray_get(oTokens, 2))->pcValue, 1)==-1) {
        errorPrint(strerror(errno), FPRINTF);
      }
    }
  } else {
    errorPrint("setenv takes one or two parameter", FPRINTF);
  }
}
// function for command 'unsetenv'.
void com_unsetenv(DynArray_T oTokens){
  const int nParam = DynArray_getLength(oTokens);
  assert(nParam>0);
  if(nParam==2) {
    if(unsetenv(((struct Token *) (DynArray_get(oTokens, 1)))->pcValue)==-1)
      errorPrint(strerror(errno), FPRINTF);
  }
  else{
    errorPrint("unsetenv takes one parameter", FPRINTF);
  }
}

void com_NORMAL(DynArray_T oTokens){
  const int nParam = DynArray_getLength(oTokens);
  assert(nParam>0);
  fflush(NULL);
  pid_t pid = fork();
  if(pid==0){
    int i;
    for (i=0; i<nParam; i++) {
      if(((struct Token *)DynArray_get(oTokens, i))->eType==TOKEN_REDIN) {
        int fd=open(((struct Token *) (DynArray_get(oTokens, i+1)))->pcValue, O_RDONLY, 0600);
        if(fd==-1){
          errorPrint(strerrer(errno), FPRINTF);
        } else {
          dup2(fd, 0);
          close(fd);
        }
      }
      if(((struct Token *)DynArray_get(oTokens, i))->eType==TOKEN_REDOUT) {
        int fd=open(((struct Token *) (DynArray_get(oTokens, i+1)))->pcValue, O_WRONLY|O_CREAT|O_TRUNC, 0600);
        dup2(fd, 1);
        close(fd);
      } 
    }

    char **new_argv = (char **)malloc(sizeof(char*)*(nParam+1));
    if(new_argv==NULL){
      errorPrint("Cannot allocate memory", FPRINTF);
      exit(0);
    }
    for(int i=0; i<nParam; i++) 
      new_argv[i] = ((struct Token*)DynArray_get(oTokens, i))->pcValue;
    new_argv[nParam] = NULL;
    
    if(execvp(new_argv[0], new_argv)==-1){
      errorPrint(new_argv[0], PERROR);
      fflush(stderr);
    }
    free(new_argv);
    exit(0);
  } else {

  }

  wait(NULL);
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
        /* TODO */
        switch(btype){
          case NORMAL:
            com_NORMAL(oTokens);
            break;
          case B_EXIT:
            com_exit(oTokens);
            break;
          case B_SETENV:
            com_setenv(oTokens);
            break;
          case B_USETENV:
            com_unsetenv(oTokens);
            break;
          case B_CD:
            com_cd(oTokens);
            break;
          default:
            assert(FALSE);
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

// Signal Handlers
int quitMode = 0;
void waitQuit(int signal){
  if(quitMode==1) exit(0);
  else quitMode = 1;
  fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
  fflush(stdout);
  alarm(5);
}

void backQuit(int signal){
  quitMode = 0;
}

int main() {
  /* TODO */
  char acLine[MAX_LINE_SIZE + 2];
  errorPrint("./ish", SETUP);
  
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, waitQuit);
  signal(SIGALRM, backQuit);
  
  // read .ishrc file
  char ishrc_path[1024]={0};
  // puts(getenv("HOME"));
  strcat(ishrc_path, getenv("HOME"));
  strcat(ishrc_path, "/.ishrc");
  FILE *fp = fopen(ishrc_path, "r");

  if(fp!=NULL){
    while(TRUE){
      if(fgets(acLine, MAX_LINE_SIZE, fp)==NULL) break;
      else{
        fprintf(stdout, "%% %s", acLine);
        fflush(stdout);
        shellHelper(acLine);
      }
    }
  }
  fclose(fp);

  // puts("----------------------");

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


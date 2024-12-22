#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>

#include "lexsyn.h"
#include "util.h"
#include "token.h"

#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/


// function declaration
static void all_free(DynArray_T);
static void default_func(DynArray_T,char *);
static void redirection_input(DynArray_T, char *, int);
static void redirection_output(DynArray_T , char *, int);
// static void redirection_both(DynArray_T, char *, int, int);
/* static void pipe_func(DynArray_T, char *, int, int, int); */
static void sigquitHandler(int iSig);
static void sigalarmHandler(int iSig);

/*--------------------------------------------------------------------*/

static void all_free(DynArray_T oTokens){
  // function for free memory allocated by tokens & DynArray
  int len = DynArray_getLength(oTokens);
  for(int i=0; i<len; i++){
    void *pvItem = DynArray_get(oTokens,i);
    freeToken(pvItem,NULL); // free token
  }
  DynArray_free(oTokens); // free DynArray
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
    all_free(oTokens);
    exit(EXIT_FAILURE);
  }


  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0){
        all_free(oTokens);
        return;
      }

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        /* command setenv */
        if(btype==B_SETENV){
          int token_len = DynArray_getLength(oTokens);
          // try setenv without parameter
          if(token_len==1){
          errorPrint("setenv takes one or two parameters", FPRINTF);
          all_free(oTokens);
          return;
          }
          /* setenv envname */
          else if(token_len==2){
            char* envName =\
            ((struct Token*)DynArray_get(oTokens,1))->pcValue;
            if(setenv(envName, "",1)==-1){ // envVal as ""
              char *programName =\
              ((struct Token*)DynArray_get(oTokens,0))->pcValue;
              errorPrint(programName, PERROR);
            }
          }
          /* setenv envname envval */
          else if(token_len==3){
            char* envName =\
            ((struct Token*)DynArray_get(oTokens,1))->pcValue;
             char* envVal =\
            ((struct Token*)DynArray_get(oTokens,2))->pcValue;
            if(setenv(envName, envVal,1)==-1){
              char *programName =\
              ((struct Token*)DynArray_get(oTokens,0))->pcValue;
              errorPrint(programName, PERROR);
            }
          }
          // try unsetenv with more than two parameter
          else
            errorPrint("setenv takes one or two parameters",FPRINTF);
          all_free(oTokens);
          return;
        }
        /* command unsetenv */
        else if(btype==B_USETENV){
          int token_len = DynArray_getLength(oTokens);
          // try unsetenv without parameter
          if(token_len==1){
          errorPrint("unsetenv takes one parameter", FPRINTF);
          all_free(oTokens);
          return;
          }
          /* setenv envname */
          else if(token_len==2){
            char* envName =\
            ((struct Token*)DynArray_get(oTokens,1))->pcValue;
            if(unsetenv(envName)==-1){
              char *programName =\
              ((struct Token*)DynArray_get(oTokens,0))->pcValue;
              errorPrint(programName, PERROR);
            }
          }
          // try unsetenv with more than one parameter
          else
            errorPrint("unsetenv takes one parameters",FPRINTF);
          all_free(oTokens);
          return;
        }
        else if(btype==B_CD){
          /* command : cd */
          int token_len = DynArray_getLength(oTokens);
          if(token_len==1)  //cd
            chdir(getenv("HOME"));
          else if(token_len==2){ //cd dir
            char *dirName=\
              ((struct Token*)DynArray_get(oTokens,1))->pcValue;
            if(chdir(dirName)==-1)
              errorPrint("No such file or directory",FPRINTF);
          }
          // try cd with more than one parameter
          else
            errorPrint("cd takes one parameter", FPRINTF);
          all_free(oTokens);
          return;
        }
          /* exit */
        else if(btype==B_EXIT){
          int token_len = DynArray_getLength(oTokens);
          if(token_len==1){
            all_free(oTokens); //free
            exit(0);
          }
          else
            errorPrint("exit does not take any parameters", FPRINTF);
          all_free(oTokens); //free
          return;
        }
        /* NOT BUILT IN FUNCTION */
        else if(btype==NORMAL){
          // normal_func(oTokens);   

    int redInInd=0; // save position of '<'
    int redOutInd=0; // save position of '>'
    struct Token *t;
    int argv_len = DynArray_getLength(oTokens); // count token

    char * prgName =\
    ((struct Token *)DynArray_get(oTokens,0))->pcValue;

  for(int i=0;i<argv_len; i++){
    t = DynArray_get(oTokens,i);
        switch(t->eType){
          case TOKEN_REDIN: // '<' 
          redInInd=i;
            break;
        case TOKEN_REDOUT: // '>'
          redOutInd=i;
            break;
        /* case TOKEN_PIPE: // '|' */
          default:
            break;
          }
        }
      if ((redInInd==0) && (redOutInd==0))
        default_func(oTokens,prgName);
      else if(redOutInd==0)
        redirection_input(oTokens,prgName,redInInd);
      else if(redInInd==0)
        redirection_output(oTokens,prgName,redOutInd);
      else{
        redirection_input(oTokens,prgName,redInInd);
        redirection_output(oTokens,prgName,redOutInd);
      }

          all_free(oTokens); //free
          return;
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
      all_free(oTokens);
      exit(EXIT_FAILURE);
  }

  all_free(oTokens);
  return;
}

/* function for no special tokens */
static void default_func(DynArray_T oTokens, char* programName){
  int argv_len = DynArray_getLength(oTokens); // count token
  char * argv[argv_len+1];

  for (int i=0; i<argv_len; i++)
    argv[i]=((struct Token *)DynArray_get(oTokens,i))->pcValue;
  argv[argv_len]=NULL;

  int status; // for store wait status
  pid_t pid = fork();
  if (pid==-1){
    /* if fork failed */
    errorPrint(programName, PERROR);
    return;
  }
  else if (pid==0){
    /* in child */
    signal(SIGQUIT,SIG_DFL); //remove signal handler
    signal(SIGINT,SIG_DFL); //remove signal handler
    execvp(programName, argv); // execute program
    /* if not executed */
    errorPrint(programName, PERROR);
    all_free(oTokens);
    exit(EXIT_FAILURE);
  }
  wait(&status);
  return;
}

static void redirection_input(DynArray_T oTokens, \
char * programName, int redInInd){
  int argv_len = DynArray_getLength(oTokens);
  char * argv[argv_len-1]; // make array for exev

  /* make argv of (before '<')+(after '<') */
  for (int i=0;i<redInInd;i++)
    argv[i]=((struct Token *)DynArray_get(oTokens,i))->pcValue;
  for(int i=redInInd+2;i<argv_len;i++)
    argv[i-2]=((struct Token *)DynArray_get(oTokens,i))->pcValue;
  argv[argv_len-2]=NULL;
  
  int status;
  pid_t pid = fork();
  if (pid==-1){ //fork failed
    errorPrint(programName, PERROR);
    return;
  }
  else if(pid==0){
    // in child process
    signal(SIGQUIT,SIG_DFL); //remove signal handler
    signal(SIGINT,SIG_DFL); //remove signal handler
    
    char * file_name= 
          ((struct Token *)DynArray_get(oTokens,redInInd+1))->pcValue;
    int fd;
    /* open file & save file descriptor */
    if((fd = open(file_name,O_RDONLY,0600))==-1){
        // if file open failed
      errorPrint("No such file or directory", FPRINTF);
      all_free(oTokens);
      exit(EXIT_FAILURE);
    }
    /* redirection input */
    close(0);
    dup(fd);
    close(fd);
    execvp(programName, argv);
    /* if execute failed */
    errorPrint(programName, PERROR);
    all_free(oTokens); //free
    exit(EXIT_FAILURE);
  }
  wait(&status);
  return;  
}

static void redirection_output(DynArray_T oTokens, \
char * programName, int redOutInd){
  int argv_len = DynArray_getLength(oTokens);
  char * argv[argv_len-1]; // make array for exev

  /* make argv of (before '>') + (after '>') */
  for (int i=0;i<redOutInd;i++)
    argv[i]=((struct Token *)DynArray_get(oTokens,i))->pcValue;
  for(int i=redOutInd+2;i<argv_len;i++)
    argv[i-2]=((struct Token *)DynArray_get(oTokens,i))->pcValue;
  argv[argv_len-2]=NULL;

  int status;
  pid_t pid = fork();
  if (pid==-1){ //fork failed
    errorPrint(programName, PERROR);
    return;
  }
  else if(pid==0){
    // in child process
    signal(SIGQUIT,SIG_DFL); //remove signal handler
    signal(SIGINT,SIG_DFL); //remove signal handler
    char * file_name= 
          ((struct Token *)DynArray_get(oTokens,redOutInd+1))->pcValue;
    int fd;
    /* Create file */
    if((fd = creat(file_name,0600))==-1){
        // if file open failed
      errorPrint("No such file or directory", FPRINTF);
      all_free(oTokens);
      exit(EXIT_FAILURE);
    }
    /* redirection output */
    close(1);
    dup(fd);
    close(fd);
    execvp(programName, argv);
    /* if execute failed */
    errorPrint(programName, PERROR);
    all_free(oTokens); //free
    exit(EXIT_FAILURE);
  }
  wait(&status);
  return;  
}

/*
static void redirection_both(DynArray_T oTokens, \
char * programName, int redInInd,int redOutInd){
}
*/

static void sigalarmHandler(int iSig){
    /* reset SIGQUIT count to 1 */
    signal(SIGQUIT, sigquitHandler);
}

static void sigquitHandler(int iSig){
  /* exit when SIGQUIT entered twice in 5 sec */
  printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  signal(SIGQUIT,exit); // one more SIGQUIT : exit
  signal(SIGALRM,sigalarmHandler);
  alarm(5);
}

int main() {
  /* TODO */
  /* signal handler : SIGINT(ctrl+c), SIGQUIT(ctrl+\) */
  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  signal(SIGINT, SIG_IGN);
  sigaddset(&sSet, SIGQUIT);
  signal(SIGQUIT, sigquitHandler);
  sigaddset(&sSet, SIGALRM);
  /* unblock signal */
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  chdir(getenv("HOME"));
  /* open .ishrc */
  FILE * ishrc=fopen(".ishrc","r");

  /* storing command */
  char acLine[MAX_LINE_SIZE + 2];

  while (1) {
    if(ishrc==NULL){
      /* .ishrc do not exist/unreable/contents==NULL */
      fprintf(stdout, "%% ");
      fflush(stdout);
      if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) { // get command
        printf("\n");
        exit(EXIT_SUCCESS);
      }
      shellHelper(acLine);
      fflush(stdin);
    }
    else{
      /* .ishrc in HOME print the command */
      if (fgets(acLine, MAX_LINE_SIZE, ishrc) == NULL) {
        ishrc=NULL; 
        continue;
      }
      fprintf(stdout, "%% ");
      fflush(stdout);
      if(acLine[strlen(acLine)-1]=='\n')
        acLine[strlen(acLine)-1]='\0';
      fprintf(stdout,"%s\n",acLine);
      fflush(stdout);
      shellHelper(acLine);
      fflush(ishrc);
    }
  }
  return 0;
}


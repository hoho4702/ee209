#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifndef SA_RESTART
#define SA_RESTART 0x10000000
#endif
//#include "dynarray.h"
#include "lexsyn.h"
#include "util.h"
//#include "token.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Additional Modifications: SomeoneElse                              */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

/* This function immediately exits the program. */
static void reallyExit(){
  alarm(0);
  exit(0);
}

/* This function warns the user that pressing Ctrl-\ again
   within 5 seconds will truly exit the program. */
static void warnExit(){
  write(STDOUT_FILENO,"\nType Ctrl-\\ again within 5 seconds to exit.\n",44);
  struct sigaction sact;
  sact.sa_handler=reallyExit;
  sact.sa_flags=SA_RESTART;
  sigemptyset(&sact.sa_mask);
  if(sigaction(SIGQUIT,&sact,NULL)==-1)exit(EXIT_FAILURE);
  alarm(5);
  return;
}

/* This function resets the SIGQUIT handler to warnExit, effectively
   cancelling the immediate exit if the user does not press Ctrl-\ again. */
static void cancelExit(){
  struct sigaction sact;
  sact.sa_handler=warnExit;
  sact.sa_flags=SA_RESTART;
  sigemptyset(&sact.sa_mask);
  if(sigaction(SIGQUIT,&sact,NULL)==-1)exit(EXIT_FAILURE);
  alarm(0);
  return;
}

/* Handles normal external commands (non-builtin),
   including input/output redirection. */
static void runNormalCmd(DynArray_T tokArr){
  int tokArrLen=DynArray_getLength(tokArr);
  struct Token *tempTok=DynArray_get(tokArr,0);
  char *firstCmd=tempTok->pcValue;

  /* We keep track of redirection states. */
  int rIn=0,rOut=0;
  int origStdin=dup(STDIN_FILENO);
  int origStdout=dup(STDOUT_FILENO);
  FILE *fp=NULL;/* "fp" will be used if we open a file for redirection. */
  int status;

  /* We'll store argv[] for execvp here. */
  char *argArr[tokArrLen+1];
  int aj=0;
  
  for(int i=0;i<tokArrLen;i++){
    tempTok=DynArray_get(tokArr,i);
    /* If it's a normal word and not currently setting up redirection, treat as arg. */
    if(tempTok->eType==TOKEN_WORD && rIn==0 && rOut==0){
      argArr[aj]=malloc(strlen(tempTok->pcValue)+1);
      strcpy(argArr[aj],tempTok->pcValue);
      aj++;
    }
    /* If we just saw '<', then next token is supposed to be an input file. */
    if(rIn==1){
      if(tempTok->eType!=TOKEN_WORD){
        fprintf(stderr,"./ish: Standard output redirection without file name\n");
        fflush(stderr);
        return;
      }
      fp=fopen(tempTok->pcValue,"r");
      if(fp==NULL){
        fprintf(stderr,"%s: No such file or directory\n",tempTok->pcValue);
        return;
      }
      dup2(fileno(fp),STDIN_FILENO);
      fclose(fp);
      rIn++;
    }
    /* If we just saw '>', then next token is supposed to be an output file. */
    else if(rOut==1){
      if(tempTok->eType!=TOKEN_WORD){
        fprintf(stderr,"./ish: Standard output redirection without file name\n");
        fflush(stderr);
        return;
      }
      fp=fopen(tempTok->pcValue,"w");
      dup2(fileno(fp),STDOUT_FILENO);
      fclose(fp);
      rOut++;
    }
    /* Check if the token is a redirection symbol. */
    if(tempTok->eType==TOKEN_REDIN)rIn++;
    else if(tempTok->eType==TOKEN_REDOUT)rOut++;
    /* Check for multiple redirections. */
    if(rIn>2){
      fprintf(stderr,"./ish: Multiple redirection of standard input\n");
      fflush(stderr);
      return;
    }else if(rOut>2){
      fprintf(stderr,"./ish: Multiple redirection of standard out\n");
      fflush(stderr);
      return;
    }
  }
  argArr[aj]=NULL;

  /* Spawn child to run the external command. */
  pid_t pid=fork();
  if(pid==0){
    execvp(firstCmd,argArr);
    fprintf(stderr,"%s: No such file or directory\n",firstCmd);
  }
  pid=wait(&status);

  /* Free the allocated argument strings. */
  for(int i=0;i<tokArrLen;i++){
    free(argArr[i]);
  }
  /* Restore original file descriptors. */
  dup2(origStdin,STDIN_FILENO);
  dup2(origStdout,STDOUT_FILENO);
  close(origStdin);
  close(origStdout);
  return;  
}

/* Builtin command 'setenv': sets or updates an environment variable. */
static void doSetenv(DynArray_T tokArr){
  struct Token *tmpTok;
  int arrLength=DynArray_getLength(tokArr);
  for(int i=0;i<arrLength;i++){
    tmpTok=DynArray_get(tokArr,i);
    if(tmpTok->eType==TOKEN_REDIN||tmpTok->eType==TOKEN_REDOUT){
      fprintf(stderr,"./ish: Standard input redirection without file name\n");
      return;
    }
    if(tmpTok->pcValue==NULL){
      fprintf(stderr,"./ish: setenv takes one or two parameters\n");
      return;
    }
  }
  if(arrLength>3||arrLength<2){
    fprintf(stderr,"./ish: setenv takes one or two parameters\n");
    return;
  }
  tmpTok=DynArray_get(tokArr,1);
  char *envKey=malloc(strlen(tmpTok->pcValue)+1);
  strcpy(envKey,tmpTok->pcValue);

  tmpTok=DynArray_get(tokArr,2);
  /* tmpTok could be NULL if only 1 parameter is given. 
     그러니 안전하게 체크해도 좋지만, 기존 코드에선 바로 씀 */
  int valLen=strlen(tmpTok->pcValue);
  char *envVal=malloc(valLen+1);
  strcpy(envVal,tmpTok->pcValue);

  if(setenv(envKey,envVal,1)!=0)fprintf(stderr,"./ish: Can't set environment variable\n");
  free(envKey);
  free(envVal);
  return;
}

/* Builtin command 'unsetenv': removes an environment variable. */
static void doUnsetenv(DynArray_T tokArr){
  struct Token *tmpTok;
  int arrLength=DynArray_getLength(tokArr);
  for(int i=0;i<arrLength;i++){
    tmpTok=DynArray_get(tokArr,i);
    if(tmpTok->eType==TOKEN_REDIN||tmpTok->eType==TOKEN_REDOUT){
      fprintf(stderr,"./ish: Standard input redirection without file name\n");
      return;
    }
    if(tmpTok->pcValue==NULL){
      fprintf(stderr,"./ish: unsetenv takes one parameter\n");
      return;
    }
  }
  if(arrLength!=2){
    fprintf(stderr,"./ish: unsetenv takes one parameter\n");
    return;
  }
  char *envKey=malloc(strlen(tmpTok->pcValue)+1);
  strcpy(envKey,tmpTok->pcValue);

  if(unsetenv(envKey)!=0)fprintf(stderr,"./ish: Can't destroy environment variable\n");
  free(envKey);
  return;
}

/* Builtin command 'cd': changes directory to given path, or HOME if none is given. */
static void doCd(DynArray_T tokArr){
  int arrLen=DynArray_getLength(tokArr);
  struct Token *tmpTok;
  for(int i=0;i<arrLen;i++){
    tmpTok=DynArray_get(tokArr,i);
    if(tmpTok->eType==TOKEN_REDIN||tmpTok->eType==TOKEN_REDOUT){
      fprintf(stderr,"./ish: Standard input redirection without file name\n");
      return;
    }
    if(tmpTok->pcValue==NULL){
      fprintf(stderr,"./ish: cd takes one parameter\n");
      return;
    }
  }
  if(arrLen==1){
    if(chdir(getenv("HOME"))!=0)fprintf(stderr,"./ish: fail to change directory\n");
    return;
  }
  if(arrLen!=2){
    fprintf(stderr,"./ish: cd takes one parameter\n");
    return;
  }
  tmpTok=DynArray_get(tokArr,1);
  char *dirPath=tmpTok->pcValue;
  if(chdir(dirPath)!=0)fprintf(stderr,"./ish: fail to change directory\n");
  return;
}

/* Analyzes a single line of input, performs lexical analysis,
   syntax checks, then decides if it's builtin or external command. */
static void shellhelper(const char *inLine){
  DynArray_T tokenDyn=DynArray_new(0);
  if(tokenDyn==NULL){
    errorPrint("Cannot allocate memory",FPRINTF);
    exit(EXIT_FAILURE);
  }
  enum LexResult lxRes=lexLine(inLine,tokenDyn);
  switch(lxRes){
    case LEX_SUCCESS:{
      if(DynArray_getLength(tokenDyn)==0)return;
      dumpLex(tokenDyn);
      enum SyntaxResult synRes=syntaxCheck(tokenDyn);
      if(synRes==SYN_SUCCESS){
        enum BuiltinType btype=checkBuiltin(DynArray_get(tokenDyn,0));
        switch(btype){
          case NORMAL:runNormalCmd(tokenDyn);break;
          case B_EXIT:exit(0);break;
          case B_SETENV:doSetenv(tokenDyn);break;
          case B_USETENV:doUnsetenv(tokenDyn);break;
          case B_CD:doCd(tokenDyn);break;
          default:printf("Default action\n");break;
        }
      }
      else if(synRes==SYN_FAIL_NOCMD)errorPrint("Missing command name",FPRINTF);
      else if(synRes==SYN_FAIL_MULTREDOUT)errorPrint("Multiple redirection of standard out",FPRINTF);
      else if(synRes==SYN_FAIL_NODESTOUT)errorPrint("Standard output redirection without file name",FPRINTF);
      else if(synRes==SYN_FAIL_MULTREDIN)errorPrint("Multiple redirection of standard input",FPRINTF);
      else if(synRes==SYN_FAIL_NODESTIN)errorPrint("Standard input redirection without file name",FPRINTF);
      else if(synRes==SYN_FAIL_INVALIDBG)errorPrint("Invalid use of background",FPRINTF);
    }break;
    case LEX_QERROR:errorPrint("Unmatched quote",FPRINTF);break;
    case LEX_NOMEM:errorPrint("Cannot allocate memory",FPRINTF);break;
    case LEX_LONG:errorPrint("Command is too large",FPRINTF);break;
    default:
      errorPrint("lexLine needs to be fixed",FPRINTF);
      exit(EXIT_FAILURE);
  }
}

int main(){
  /* Install SIGQUIT -> warnExit and SIGALRM -> cancelExit. */
  struct sigaction sact;
  sact.sa_handler=warnExit;
  sact.sa_flags=SA_RESTART;
  sigemptyset(&sact.sa_mask);
  if(sigaction(SIGQUIT,&sact,NULL)==-1)exit(EXIT_FAILURE);

  sact.sa_handler=cancelExit;
  sact.sa_flags=SA_RESTART;
  sigemptyset(&sact.sa_mask);
  if(sigaction(SIGALRM,&sact,NULL)==-1)exit(EXIT_FAILURE);

  /* Attempt to read commands from ~/.ishrc before interactive loop. */
  const char *cfgName="/.ishrc";
  char *homePath=malloc(strlen(getenv("HOME"))+strlen(cfgName)+1);
  strcpy(homePath,getenv("HOME"));
  strncat(homePath,cfgName,strlen(cfgName));
  FILE *cfgFile=fopen(homePath,"r");
  char acLine[MAX_LINE_SIZE+2];
  if(cfgFile){
    while(fgets(acLine,MAX_LINE_SIZE,cfgFile)!=NULL){
      fprintf(stdout,"%% ");
      fprintf(stdout,"%s",acLine);
      fflush(stdout);
      shellhelper(acLine);
    }
  }
  /* Now go into interactive mode. */
  while(1){
    fprintf(stdout,"%% ");
    fflush(stdout);
    if(fgets(acLine,MAX_LINE_SIZE,stdin)==NULL){
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellhelper(acLine);
  }
}

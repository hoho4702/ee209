#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include "lexsyn.h"
#include "util.h"
#include "dynarray.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/
volatile  sig_atomic_t sigquit_received= 0;
void sigint_handler(int signum){

}
void sigquit_handler(int signum){
  if(sigquit_received==0){
    printf("Type Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5);
    sigquit_received=1;
  }
  else{
    exit(0);
  }
}
void sigalrm_handler(int signum){
  sigquit_received=0;
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
  switch (lexcheck)   {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0)
        return;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        switch (btype)
        {
        case B_SETENV:
        {
          if(DynArray_getLength(oTokens)<2){
            errorPrint("setenv takes one or two parameters", FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          char *varName = DynArray_get(oTokens,1);
          if(varName==NULL || strlen(varName)==0){
            return;
          }
          char *value=NULL;
          if(DynArray_getLength(oTokens)>2){
            value=DynArray_get(oTokens, 2);
          }
          else{
            value="";
          }
          if(setenv(varName,value,1)!=0){
            errorPrint(NULL,PERROR);
            DynArray_free(oTokens);
            return;
          }
          else{
            errorPrint("Invalid parameters for sentev", FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          break;
        }
        case B_USETENV:
        {
          if(DynArray_getLength(oTokens)!=2){
            errorPrint("unsetenv takes one parameter", FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          char *varToUnset = DynArray_get(oTokens, 1);
          if(varToUnset ==NULL || strlen(varToUnset)==0){
            errorPrint("Invalid parameter for unsetenv", FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          if(unsetenv(varToUnset)!=0){
            errorPrint(NULL,PERROR);
            DynArray_free(oTokens);
            return;
          }
          DynArray_free(oTokens);
          return;
        }
        case B_CD:
        {
          if(DynArray_getLength(oTokens)>2 || DynArray_getLength(oTokens)==0){
            errorPrint("cd takes one parameter", FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          char *targetDir;
          if(DynArray_getLength(oTokens)==1){
            targetDir=getenv("HOME");
            if(targetDir==NULL|| chdir(targetDir)!=0){
              errorPrint(NULL, PERROR);
            }
            DynArray_free(oTokens);
            return;
            
          }
          else{
            targetDir=DynArray_get(oTokens, 1);
            if(targetDir==NULL || chdir(targetDir)!=0){
              errorPrint(NULL,PERROR);
            }
            DynArray_free(oTokens);
            return;
          }
          break;
        }
        
        case B_EXIT:
        {
          if(DynArray_getLength(oTokens)>1){
            errorPrint("exit takes no parameter", FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          DynArray_free(oTokens);
          exit(EXIT_SUCCESS);
          break;
        }
        
        default:
        {
          size_t i=0;
          pid_t pid=fork();
          if(pid==-1){
            errorPrint("Fork failed", PERROR);
            DynArray_free(oTokens);
            return;
          }

          if(pid==0){
            int inputFd=-1,outputFd=-1;
          
          for(i=0; i<DynArray_getLength(oTokens);i++){
            char *token=DynArray_get(oTokens,i);
            if(strcmp(token,"<")==0){
              if(inputFd!=-1){
                errorPrint("Multiple redirection of standard input", FPRINTF);
                exit(EXIT_FAILURE);
              }
              if(i+1>=DynArray_getLength(oTokens)){
                errorPrint("Standard input redirection without file name",FPRINTF);
                exit(EXIT_FAILURE);
              }
              inputFd=open(DynArray_get(oTokens,i+1),O_RDONLY);
              dup2(inputFd,STDIN_FILENO);
              close(inputFd);
              DynArray_set(oTokens, i, NULL);
              DynArray_set(oTokens, i+1, NULL);
            }
            else if(strcmp(token, ">")==0){
              if(outputFd!=-1){
                errorPrint("Multiple redirection of standard output", FPRINTF);
                exit(EXIT_FAILURE);
              }
              if(i+1>=DynArray_getLength(oTokens)){
                errorPrint("Standard output redirecion without file name", FPRINTF);
                exit(EXIT_FAILURE);
              }
              outputFd=open(DynArray_get(oTokens,i+1),O_WRONLY|O_CREAT|O_TRUNC,0600);
              if(outputFd=-1){
                errorPrint(NULL,PERROR);
                exit(EXIT_FAILURE);
              }
              dup2(outputFd,STDOUT_FILENO);
              close(outputFd);
              DynArray_set(oTokens,i,NULL);
              DynArray_set(oTokens, i+1, NULL);
            }
            }
            DynArray_T cleanTokens =DynArray_new(0);
            for(i=0; i<DynArray_getLength(oTokens);i++){
              char *token =DynArray_get(oTokens,i);
              if(token!=NULL){
                DynArray_add(cleanTokens, token);
              }
            }
            char **argv=malloc((DynArray_getLength(cleanTokens)+1)*sizeof(char*));
            if(argv==NULL){
              errorPrint("Cannot allocate memory",FPRINTF);
              DynArray_free(cleanTokens);
              exit(EXIT_FAILURE);
            }
            for(i=0;i<DynArray_getLength(cleanTokens); i++){
              argv[i]=(char*)DynArray_get(cleanTokens,i);
            }
            execvp(argv[0],argv);
            errorPrint(NULL,PERROR);
            DynArray_free(cleanTokens);
            free(argv);
            exit(EXIT_FAILURE);
          }
          if(pid>0){
            int status;
            waitpid(pid,&status,0);
            DynArray_free(oTokens);
            return;
          }
          break;
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
}

int main() {
  /* TODO */
  sigset_t group;
  sigemptyset(&group);
  sigaddset(&group,SIGINT);
  sigaddset(&group,SIGQUIT);
  sigaddset(&group,SIGALRM);
  sigprocmask(SIG_UNBLOCK,&group,NULL);
  signal(SIGINT,sigint_handler);
  signal(SIGQUIT,sigquit_handler);
  signal(SIGALRM,sigalrm_handler);
  signal(SIGINT,SIG_IGN);

  const char *home_dir =getenv("HOME");
  if(home_dir==NULL){
    perror("Error retrieving HOME environment variable");
    exit(EXIT_FAILURE);
  }
  char ishrc_path[PATH_MAX];
  snprintf(ishrc_path, sizeof(ishrc_path),"%s/.ishrc", home_dir);
  char ishname[MAX_LINE_SIZE]="./ish";
  errorPrint(ishname,SETUP);
  FILE *ishrc_file =fopen(ishrc_path, "r");
  if(ishrc_file){
    char acLine[MAX_LINE_SIZE+2];
    while (fgets(acLine,sizeof(acLine),ishrc_file)){
    fprintf(stdout,"%% %s", acLine);
    fflush(stdout);
    shellHelper(acLine);
  }
  fclose(ishrc_file);
  }
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

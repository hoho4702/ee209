#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <ctype.h>
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
/*takes the signal of SIGINT
it will take the pid and send the
SIGINT signal to all the process related to the 
pid including child process and parent process*/
void handler(int sig){
  int pid=getpid();
  if(pid<0){
    errorPrint("Fail to retrieve pid",FPRINTF);
    exit(-1);
  }
  kill(-pid,sig);
  exit(0);
}
/*sigquit is a indication of whether there was a previous 
SIGQUIT signal received*/
int sigquit=1;
/*this is a handler in case SIGQUIT singal was triggered.*/
void quit_handler(int sig){
  /*check if the SIGQUIT was received for the first time
  if that is the case, print a message indicating to wait for 
  5seconds and set alarm 5
  then set sigquit as 0 to indicate that SIGQUIT was received*/
  if(sigquit){
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    sigquit=0;
    alarm(5);
  /*if it is the second time of receiving the second time, 
  exit the process*/
  }else{
    exit(0);
  }
}
/*after the first SIGQUIT signal is received, the alarm is set 5
if the alarm ends, SIGALRM is recieved. This is the signal handler
for SIGALRM which will set back sigquit to 1 which indicates
SIGQUIT has reached its end of time thus, being reset*/
void alarm_handler(int sig){
  sigquit=1;
}
/*builtin function takes cases where function is not included in 
execvp. it takes four command and act depending on the command*/
void builtin(char** argv){
  /*when the command starts with setenv, it will run setenv function*/
  if(strcmp(argv[0],"setenv")==0){
    if(setenv(argv[1],argv[2],0)){
      errorPrint("Fail to setenv",FPRINTF);
      exit(-1);
    }
  }
  /*if the command starts with unsetenv, it will run unsetenv function*/
  else if(strcmp(argv[0],"unsetenv")==0){
    if(unsetenv(argv[1])){
      errorPrint("Fail to unsetenv",FPRINTF);
      exit(-1);
    }
  }
  /*when the command starts with cd, it will change directory*/
  else if(strcmp(argv[0],"cd")==0){
    if(chdir(argv[1])!=0){
      errorPrint("Fail to change directory",FPRINTF);
      exit(-1);
    }
  }
  /*if the command is exit, it will exit the process and shell*/
  else if(strcmp(argv[0],"exit")==0){
    free(argv);
    exit(0);
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
    errorPrint("Cannot allocate memory\n", FPRINTF);
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
        /*argv takes each token from oTokens and store in each element in argv*/
        char **argv=(char**)malloc((MAX_LINE_SIZE+2)*sizeof(char*));
        /*take the number of oTokens*/
        int size=DynArray_getLength(oTokens);
        /*check if there is a redirection of input*/
        int redir_in=0;
        /*check if there is a redirction of output*/
        int redir_out=0;
        /*get the file name for redirction of input*/
        char* inf=NULL;
        /*get the file name for redirction of output*/
        char* outf=NULL;
        /*j indicates index of argv that will take command string from oTokens
        and store in its element*/
        int j=0;
        /*take each element from oTokens and store it in argv
        if the command is about redirection, do not store in argv
        and set conditions for redirection*/
        for(int i=0;i<size;i++){
          /*token stores token from each element of oTokens*/
          struct Token* token;
          token=((struct Token*)DynArray_get(oTokens,i));
          /*if the token is a redirection of input
          set redir_in indicating command for redirection input
          and take the next oToken element and store the file name in inf*/
          if((token->eType)==TOKEN_REDIN){
            redir_in=1;
            i++;
            inf=((struct Token*)DynArray_get(oTokens,i))->pcValue;
          }
          /*if the token is a redirection of output
          set redir_out indicating the command for redirection output
          and take the next oToken element and store the file name in outf*/
          else if((token->eType)==TOKEN_REDOUT){
            redir_out=1;
            i++;
            outf=((struct Token*)DynArray_get(oTokens,i))->pcValue;
          }
          /*if not the special case, take the token's pcValue and store the 
          command line in argv*/
          else{
            argv[j]=token->pcValue;
            j++;
          }
        }
        /*if the command is not a builtin command continue
        running the command from argv*/
        if(!btype){
          /*fflush before running the command*/
          fflush(NULL);
          /*create the child process that will execute the command*/
          int pid=fork();
          /*if it is the child process*/
          if(pid==0){
            /*take the signals */
            signal(SIGINT,SIG_DFL);
            signal(SIGQUIT,SIG_DFL);
            signal(SIGALRM,SIG_DFL);
            /*if there is a redireciton input, then 
            redirect the stdin*/
            if(redir_in){
              /*set up the file name*/
              errorPrint(inf,SETUP);
              int fd;
              /*open the file*/
              if((fd=open(inf,O_RDONLY))<0){
                errorPrint("failed to open the file",FPRINTF);
                exit(-1);
              }
              /*close the stdin directory*/
              close(0);
              /*duplicate the file descriptor*/
              dup2(fd,0);
            }
            /*same case as redirecting stdin 
            but instead redirecting stdout*/
            if(redir_out){
              errorPrint(outf,SETUP);
              int fd;
              if((fd=open(outf,O_WRONLY|O_CREAT,0600))<0){
                errorPrint("failed to open file",FPRINTF);
                exit(-1);
              }
              close(1);
              dup2(fd,1);
            }
            /*execute the commands from argv*/
            if(execvp(argv[0],argv)==-1){
              /*in case the command has error*/
              fprintf(stderr,"%s: No such file or directory\n",argv[0]);
              free(argv);
              exit(0);
            }
          }
          /*parent process that will wait until the child process terminates*/
          else{
            signal(SIGINT,SIG_IGN);
            int status;
            if(waitpid(pid,&status,0)==-1){
              errorPrint("Fail to wait",FPRINTF);
              exit(-1);
            }
            free(argv);
          }
        }
        /*if the command is a builtin command
        run according to builtin command*/
        else{
          builtin(argv);
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
  /*set up to make sure that signals are not blocked*/
  sigset_t new_set;
  sigset_t old_set;
  sigemptyset(&new_set);
  sigaddset(&new_set,SIGINT);
  sigaddset(&new_set,SIGQUIT);
  sigaddset(&new_set,SIGALRM);
/*signals that will be executing handlers whenever the signal
is triggered*/
  signal(SIGQUIT,quit_handler);
  signal(SIGALRM,alarm_handler);
  signal(SIGINT,SIG_IGN);
  /*unblock the signals so that the processes can receive signals and 
  act accordingly*/
  if(sigprocmask(SIG_UNBLOCK,&new_set,&old_set)<0){
    errorPrint("Fail to unblock signals\n",FPRINTF);
    return 0;
  }
  /*get the home directory*/
  char* home=getenv("HOME");
  /*make an string that will take directory
  concatenated with home directory and ishrc*/
  char path[MAX_LINE_SIZE];
  /*copy the home directory path to path and add /.ishrc at the end*/
  if((home!=NULL)){
    strcpy(path,home);
    strcat(path,"/.ishrc");
  }
  /*open the directory to ishrc path
  and read each line of command from the file
  and when stored, go to shellHelper to execute the command*/
  FILE *fp=fopen(path,"r");
  if(fp!=NULL){
    while(fgets(acLine,MAX_LINE_SIZE,fp)!=NULL){
      fprintf(stdout,"%% %s",acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
    fclose(fp);
  }
  /*execute the line that was received from standard input*/
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    /*set the file name as ./ish before running the command*/
    errorPrint("./ish",SETUP);
    shellHelper(acLine);
  }
}



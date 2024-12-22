
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif



#include <stdio.h>
#include <stdlib.h>

#include "lexsyn.h"
#include "util.h"
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>



/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/


void SIGQUIT_handl(int sig){
  sigset_t newset;
  sigemptyset(&newset);

  sigaddset(&newset, SIGINT);
  
  
  if(sigprocmask(SIG_BLOCK, &newset, NULL)){
    errorPrint("Interrupt signal could not be blocked", FPRINTF);
    exit(1);
  }


  int time = alarm(5);
  if(time>0){
    exit(1);
  }
  else{
    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
  }
  sigprocmask(SIG_UNBLOCK, &newset, NULL );
}


void normal(int argc, char **argv, DynArray_T oTokens){

  int redin_index = -1;
  int redout_index = -1;
  for (int i = 0; i<argc; i++){
    if(strcmp(argv[i], "<") == 0){
      if (i == argc){
        errorPrint("No such file or directory", FPRINTF);
        exit(1);
      }
      if(redin_index == -1){
         redin_index = i +1 ;
      }
      else{
        errorPrint("<: only one redirection is possible", FPRINTF);
        return;
      }
    }
    if (strcmp(argv[i], ">") == 0){
      if (i == argc){
        errorPrint("No such file ", FPRINTF);
        exit(1);
      }
      if (redout_index == -1 ){
        redout_index = i+1;
      }
      else{
        errorPrint(">: only one redirection is possible", FPRINTF);
        return;
      }
    }
  }

  

  
  pid_t pid;
  if((pid = fork()) == 0){
    if(redin_index != -1){
      int fdIn = open(argv[redin_index], O_RDONLY);
      if(fdIn == -1){
        errorPrint("No such file or directory", FPRINTF);
        exit(1);
      } 
      close(0);
      dup(fdIn);
      close(fdIn);
      DynArray_removeAt(oTokens, redin_index);


    }

    if(redout_index != -1){
      int fdOut = open(argv[redout_index], O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
      if(fdOut == -1){
        errorPrint("No such file or directory", FPRINTF);
        exit(1);
      } 
      close(1);
      dup(fdOut);
      close(fdOut);
      DynArray_removeAt(oTokens, redout_index);

    }
    

    int argc_2_new = DynArray_getLength(oTokens);

    char* argv_2_new[argc_2_new+1];

    for (int i =0; i<argc_2_new; i++){
      struct Token * argument =(struct Token *)DynArray_get(oTokens,i);
      argv_2_new[i] = argument->pcValue;
    }

    argv_2_new[argc_2_new] = NULL;
    int result_exe = execvp(argv_2_new[0], argv_2_new);
    if(result_exe == -1){
      errorPrint(argv[0], SETUP);
      errorPrint("No such file or directory", FPRINTF);
      exit(1);
    }
  }
  waitpid(pid, NULL, 0);
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


        int argc_2 = DynArray_getLength(oTokens);
        if (argc_2 == 0) return;
        char* argv_2[argc_2+1];

        for (int i =0; i<argc_2; i++){
          struct Token * argument =(struct Token *)DynArray_get(oTokens,i);
          argv_2[i] = argument->pcValue;
        }

        argv_2[argc_2] = NULL;

        /* TODO */
        switch(btype){
          case B_CD:
            if(argc_2 == 2){
              int result = chdir(argv_2[1]);
              if (result == -1){
              if (errno == ENOENT)
                errorPrint("No such file or directory", FPRINTF);
              if (errno == EACCES)
                errorPrint("Permission denied", FPRINTF);
              }

            }
            else if (argc_2 ==1){
              char* home = getenv("HOME");
              assert(home);
              int result = chdir(home);
              if (result == -1){
                if (errno == ENOENT) 
                  errorPrint("No such file or directory", FPRINTF);
                if (errno == EACCES)
                  errorPrint("Permission denied", FPRINTF);
              }
            }

            else{
              errorPrint("Cd: Only one or no parameter is accepted", FPRINTF);
              return;
            }

            return;
            break;
          case B_EXIT:
            if (argc_2 == 1){
              exit(0);
              break; 
            }
            errorPrint("exit does not take any parameters", FPRINTF);
            return;
            break;
          case B_USETENV:
            if(argc_2 == 2){
              int result = unsetenv(argv_2[1]);
              assert(!result);
              return;
              break;
            }
            else{
              errorPrint("unsetenv: only one parameter is accepted", FPRINTF);
              return;
            }
          case B_SETENV:
            if (argc_2 == 3) {
              //for changing
              setenv(argv_2[1], argv_2[2], 1);
              return;
              break;
            }
            else if(argc_2 == 2){
              setenv(argv_2[1], "", 1);
              return;
              break;
            }
           
            else{
              errorPrint("setenv: only one or two parameter is accepted", FPRINTF);
              return;
            }
          case NORMAL:
            normal(argc_2, argv_2, oTokens);
            return;
            break;

          default:
            break;
        }
        fflush(stdout);
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
        errorPrint("", FPRINTF);
      else if (syncheck == SYN_FAIL_INVALIDBG);
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



void reading_ishrc(){
  
  int result = chdir(getenv("HOME"));
  assert(!result);
  char acLine[MAX_LINE_SIZE+2];
  FILE * fp = fopen(".ishrc", "r");


  if(fp){
    while(fgets(acLine, MAX_LINE_SIZE, fp) != NULL){
      fprintf(stdout, "%% %s", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
  }
}

int main(int argc, char** argv) {
  /* TODO */


  sigset_t signalSet;

  sigemptyset(&signalSet);

  sigaddset(&signalSet, SIGQUIT);
  
  sigaddset(&signalSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &signalSet, NULL );

  sigset_t signalINT;
  sigemptyset(&signalINT);
  sigaddset(&signalINT, SIGINT);
  sigprocmask(SIG_BLOCK, &signalINT, NULL);

  signal(SIGQUIT, SIGQUIT_handl);


  errorPrint(argv[0], SETUP);
  const char* home = getenv("HOME");
  assert(home);



  //for saving the current directory before 
  //changing to home directory

  char* dir;
  dir = get_current_dir_name();
  reading_ishrc();
  assert(dir);
  fflush(stdout);
  


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


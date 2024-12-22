// 20210869 SEJOON JUN, implemented a simple UNIX shell
// using provided source code files and headers, dynarray,
// lexsyn, token and util.
/*--------------------------------------------------------------------*/
// include neccessary header files to implement the UNIZ shell
// define GNU and DEFAULT source using ifndef
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
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
void CD(DynArray_T oTokens, int length);
void EXIT(DynArray_T oTokens, int length);
void SETENV(DynArray_T oTokens, int length);
void UNSETENV(DynArray_T oTokens, int length);
void NOT_BUILTIN(DynArray_T oTokens, int length);
void myhandler_sigquit(int sig);
void myhandler_sigalarm(int sig);

static void shellHelper(const char *inLine) {
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
        // implementing in DFA states to resolve this
        int length = DynArray_getLength(oTokens);
        switch (btype){
          // other than four built-in functions are considered
          // to be the name of files. This state is set as default
          default:
            NOT_BUILTIN(oTokens, length);
            break;
          case B_CD:
              CD(oTokens, length);
              break;
          case B_EXIT:
              EXIT(oTokens, length);
              break;
          case B_SETENV:
              SETENV(oTokens, length);
              break;
          case B_USETENV:
              UNSETENV(oTokens, length);
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

// implemented cd command for changing directories
void CD(DynArray_T oTokens, int length){
  // if cd command contains invalid number of arguments
  // which are more than two, then print the error messsage.
  if (length>2){
    errorPrint("cd takes one parameter", FPRINTF);
  }
  // if no directory has been written, then change directory to home.
  else if (length == 1){
    char* home_address = getenv("HOME");
    int result = chdir(home_address);
    // to check if chdir is successful
    if(result == -1){
      errorPrint(NULL, PERROR);
    }
  }
  // if a directory has been written, then change the directory
  // to the one.
  else if (length == 2){
    char* directory_address = 
    (((struct Token*)DynArray_get(oTokens,1))-> pcValue);
    int result = chdir(directory_address);
    if(result == -1){
      errorPrint("No such file or directory", FPRINTF);
    }
  }
}

// implemented exit command
void EXIT(DynArray_T oTokens, int length){
  // if command contains more than one argv then 
  // prints out an error message
  if (length>1){
    errorPrint("exit does not take any parameters", FPRINTF);
  }
  // executes exit(0)
  else if (length == 1){
    // free each token by iterating the array
    void *Token = DynArray_get(oTokens,length-1);
    // free token using freeToken in the token.c
    freeToken(Token, NULL);
    DynArray_free(oTokens);
    exit(0);
  }
}

// implemented sentenv command
void SETENV(DynArray_T oTokens, int length){
  // if it does not contain the valid number of parameters then
  // prints out an error message
  if (length<2){
    errorPrint("setenv takes one or two parameters", FPRINTF);
  }
  // if a value is not given then let value be an
  // empty string
  else if (length == 2){
    char* env_value= "";
    char* env_name=(((struct Token*)DynArray_get(oTokens,1))->pcValue);
    int result = setenv(env_name,env_value,1);
    if (result == -1){
      errorPrint(NULL, PERROR);
    }
  }
  else if (length == 3){
    // retrieve both name and value of a token from the array
    char* env_name=(((struct Token*)DynArray_get(oTokens,1))->pcValue);
    char* env_value=(((struct Token*)DynArray_get(oTokens,2))->pcValue);
    int result = setenv(env_name, env_value, 1);
    if (result == -1){
      errorPrint(NULL, PERROR);
    }
  }
}

// implemented unsentenv command
void UNSETENV(DynArray_T oTokens, int length){
  // if it does not contain the valid number of parameters then
  // prints out an error message
  if (length!=2){
    errorPrint("unsetenv takes one or two parameters", FPRINTF);
  }
  // if the length is 2 then acquire the name of a token from the array
  else{
    char* env_name=(((struct Token*)DynArray_get(oTokens,1))->pcValue);
    int result = unsetenv(env_name);
    if (result == -1){
      errorPrint(NULL, PERROR);
    }
  }
}

// implemented not_builtin function if the input commands are not
// specified built-in commands
void NOT_BUILTIN(DynArray_T oTokens, int length){
  // create an array of command arguments 
  // which has the length of argc + 1
  char** command_array = malloc((length+1)*sizeof(char*));
  // check if malloc is successfull
  if (command_array == NULL){
    errorPrint(NULL, PERROR);
  }
  pid_t pid = fork();
  // check if fork is successfull
  if(pid<0){
    errorPrint(NULL, PERROR);
  }
  // for the parent process
  if(pid>0){
    wait(NULL);
    free(command_array);
  }
  // for the child process
  if(pid==0){
    // restore the SIGINT and SIGQUIT to their default behviour
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    // iterate the array to insert tokens into the command_array
    for (int i=0; i<length; i++){
      // casting into struct Token* to use their values
      struct Token* t = (struct Token*)DynArray_get(oTokens,i);
      command_array[i]= t->pcValue;
      if((t->eType) == TOKEN_REDOUT){
        char* output_file = 
        (((struct Token*)DynArray_get(oTokens, i + 1))-> pcValue);
        // obtain output_file file descritopr using open function,
        // if it does not exist then create the file
        // if the file already exists then destory the orginal contents
        // to be rewritten
        int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        // redirct the standard output to file descriptor of the output_file
        int result = dup2(fd, 1);
        if (result<0){
          close(fd);
          errorPrint(NULL, PERROR);
        }
        close(fd); 
      }
      if((t->eType) == TOKEN_REDIN){
        char* input_file = 
        (((struct Token*)DynArray_get(oTokens, i + 1))-> pcValue);
        int fd = open(input_file, O_RDONLY);
        if (fd == -1){
          errorPrint("No such file or directory", FPRINTF);
          return;
        }
        // redirct the standard input to file descriptor of the input_file
        int result = dup2(fd, 0);
        if (result<0){
          close(fd);
          errorPrint(NULL, PERROR);
        }
        close(fd); 
      }
    }
    // null terminate for execvp call
    command_array[length] = NULL;
    execvp(command_array[0], command_array);
    errorPrint(command_array[0], SETUP);    
    errorPrint("No such file or directory", FPRINTF);
    free(command_array);
    exit(EXIT_FAILURE);
  }
}
// flag_quit is a variable to verifiy whether the second SIGQUIT
// happened withing five seconds
static int flag_quit = 1;
// implement a signal handler for SIGALRM
// it would set the flag_quit to be zero
// if the alarm is set off
void myhandler_sigalarm(int sig){
  flag_quit = 1;
}
// implement a signal handler for SIGQUIT
void myhandler_sigquit(int sig){
  // implement sigalarm handler to set the flag_quit value
  signal(SIGALRM, myhandler_sigalarm);
  if(flag_quit == 1){
    printf("\nType Ctrl-\\ again within 5 seconds to exit\n");
    fflush(stdout);
    alarm(5);
    flag_quit = 0;
  }
  else{
    exit(0);
  }
}
int main(int argc, char*argv[]) {
  /* TODO */
  // using signal masks for SIGINT, SIGQUIT, and SIGALRM not
  // to be blocked in the beginning of main function
  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);
  // SIGINT is ignored in the parent process
  signal(SIGINT, SIG_IGN);
  // install signal handler for SIGQUIT and verify 
  // that it is successful using assert functio
  static void (*pfRet) (int);
  pfRet = signal(SIGQUIT, myhandler_sigquit);
  assert(pfRet != SIG_ERR);
  errorPrint(argv[0], SETUP);
  char* home_address = getenv("HOME");
  char home_buffer[1000];
  char acLine[MAX_LINE_SIZE + 2];
  if (home_address != NULL){
    strcpy(home_buffer, home_address);
    strcat(home_buffer, "/.ishrc");
    FILE *fp = fopen(home_buffer, "r");
    if(fp!=NULL){
      while(fgets(acLine, MAX_LINE_SIZE, fp)){   
        printf("%% %s", acLine);   
        fflush(stdout);
        shellHelper(acLine);        
      }
      fclose(fp);
    }
  }
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


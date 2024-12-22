#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
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
/* Name: Park Seongmin, ID: 20210231
Description: This code implements basic shell with 4 built-in command,
cd, setenv, unsetenv, and exit. Others are used through execvp. First it
reads .ishrc that is in the HOME directory and do shell process repeatedly*/
/* Global variable definition*/

pid_t pid = 10; // to use it in signal handler
void (*pfRet)(int);
void *pFree; 
int  sigalarm = 0;

/*Redirection handler for NORMAL cases: if there exists 
< or > in token, redirect it into stdin or stdout depending on
the token*/

void RedirectionHandler (DynArray_T oTokens){
  int i;
  struct Token *pToken;
  struct Token *pNextToken; // might be a file name
  int Num_parameter = DynArray_getLength(oTokens);
  int flag_in = 0;
  int flag_out = 0;
  int fd_in = 0;
  int fd_out = 0;

  for(i = 0; i < Num_parameter; i++ ){
    pToken = DynArray_get(oTokens, i);
    
    if(pToken -> eType == TOKEN_REDIN){ // '<'
      if(i == 0){
        errorPrint("Missing command name", FPRINTF);
        return;
      }
      else if(i+1 == Num_parameter){
        errorPrint("Standard input redirection without file name", FPRINTF);
        return;
      }
      else if(flag_in == 1){
        errorPrint("Multiple redirection of standard input", FPRINTF);
        return;
      }
      else if(((pNextToken = DynArray_get(oTokens, i+1))->pcValue) == NULL){
        errorPrint("Standard input redirection of standard input", FPRINTF);
        return;
      }
      flag_in = 1;
      fd_in = open(pNextToken->pcValue, O_RDONLY, 0600);
      if (fd_in == -1){ // No such file exists
        errorPrint("No such file or directory", PERROR);
        return;
      }
      // redirection
      close(0);
      dup(fd_in);
      close(fd_in);
      i++;
    }

    else if (pToken->eType == TOKEN_REDOUT){ // '>'
      if(i == 0){
        errorPrint("Missing command name", FPRINTF);
        return;
      }
      else if(i+1 == Num_parameter){
        errorPrint("Standard output redirection without file name", FPRINTF);
        return;
      }
      else if(flag_out == 1){
        errorPrint("Multiple redirection of standard out", FPRINTF);
        return;
      }
      else if(((pNextToken = DynArray_get(oTokens, i+1))->pcValue) == NULL){
        errorPrint("Standard output redirection without file name", FPRINTF);
        return;
      }
      flag_out = 1;
      fd_out = open(pNextToken->pcValue, O_CREAT | O_WRONLY , 0600);
      if (fd_out == -1){ // cannot make file
        errorPrint("No such file or directory", PERROR);
        return;
      }
      //redirection
      close(1);
      dup(fd_out);
      close(fd_out);
      i++;
    }
  }
}

/*shellHelper gets line from stdin and parse it into Token.
Depending on the builtintype, it either process builtin function
or replace child process with other process */

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
  int Num_parameter = DynArray_getLength(oTokens);

  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0)
        return;

      // dump lex result when DEBUG is set //
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        //array for token values
        char ** new_token_array = (char**) calloc(Num_parameter, sizeof(char*));
        int i;

        for(i = 0; i < Num_parameter; i++){
          new_token_array[i] = (char*)(((struct Token *)DynArray_get(oTokens, i))->pcValue);
        }

        switch (btype){
          case B_CD:
            if(Num_parameter == 1){
                const char* Home = getenv("HOME");
                if (Home == NULL){
                  errorPrint("HOME environment variable is not set\n", FPRINTF); 
                  free(new_token_array);
                  freeToken(oTokens, pFree);
                  return;
                }
                if (chdir(Home) == -1){
                  errorPrint("Failed to change to home directory\n", PERROR);}

                  free(new_token_array);
                  freeToken(oTokens, pFree);
                  return;
              }
            else if(Num_parameter == 2){
                if(chdir(new_token_array[1]) != 0){
                  errorPrint(new_token_array[1], PERROR);
                  }
                  free(new_token_array);
                  freeToken(oTokens, pFree);
                  return;
                }
            else{
              errorPrint("cd takes one parameter", FPRINTF);
              free(new_token_array);
              freeToken(oTokens, pFree);
              return;
             }
          
          case B_SETENV:
            if(Num_parameter == 2){
              if((setenv(new_token_array[1], "", 1) != 0)){
                errorPrint("HOME environment variable is not set\n", FPRINTF); 
              }
                free(new_token_array);
                freeToken(oTokens, pFree);
                return;
            }
            else if(Num_parameter == 3){
              if((setenv(new_token_array[1], new_token_array[2], 1) != 0)){
                errorPrint("HOME environment variable is not set\n", FPRINTF); }
                free(new_token_array);
                freeToken(oTokens, pFree);
                return;
              }
            
            else{
              free(new_token_array);
              freeToken(oTokens,pFree);
              errorPrint("setenv takes one or two parameters", FPRINTF);
              return;
            }

          case B_USETENV:
            if(Num_parameter == 2){
              if((unsetenv(new_token_array[1]) != 0)){
                free(new_token_array);
                freeToken(oTokens, pFree);
                return;
              }
            }
            else{
              errorPrint("unsetenv takes one parameter", FPRINTF);
              free(new_token_array);
              freeToken(oTokens, pFree);
              return;
            }
          
          case B_EXIT:
            if(Num_parameter != 1){
              errorPrint("exit does not take any parameters",FPRINTF);
              free(new_token_array);
              freeToken(oTokens, pFree);
              return;
            }
            else{
              free(new_token_array);
              freeToken(oTokens,pFree);
              exit(0);
              return;
            }
          
          case NORMAL:
            pid = fork();
            if (pid == -1){
              errorPrint("Failed to fork", PERROR);
            }
            else if (pid == 0){//Child Process
              RedirectionHandler(oTokens);
              /* Make new array with handling redirection*/
              int i = 0; //i = iterations, argument = actual tokens except redirection
              char **argv = (char**) calloc(DynArray_getLength(oTokens)+1, sizeof(char*));
              if(argv == NULL){
                errorPrint("Fail to allocate memory", PERROR);
                return;
              }

              int argument = 0;
              for (i = 0; i < Num_parameter; i++ ){
                struct Token *pToken = DynArray_get(oTokens, i);
                if(pToken -> eType == TOKEN_WORD){
                  argv[argument++] = pToken->pcValue;
                }else if(pToken -> eType == TOKEN_REDIN || pToken -> eType == TOKEN_REDOUT)
                {
                  i++;
                }
              }
              argv[argument] = NULL; // set final argv NULL

              execvp(argv[0], argv); // replace Process
              errorPrint(argv[0], PERROR);
              free(argv);
              free(new_token_array);
              freeToken(oTokens, pFree);
              return;
            }
            else{
              free(new_token_array);
              freeToken(oTokens, pFree);
              wait(NULL);
            }
            return;

          default:
            assert(0); //shouldn't get here
          

        }

      }

      //syntax error cases //
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

/*SIGINT_handler : if parent, ignore SIGINT.
if child, terminate
SIGQUIT_handler: parent and already got signal ctrl + \, terminate
if not, get signal and wait for the same signal again
if child, terminate*/

static void SIGINT_handler(int iSig){
  if(pid){
    pfRet = signal(SIGINT, SIG_IGN);
    assert(pfRet != SIG_ERR);
  }
  else{
    pfRet = signal(SIGINT, SIG_DFL);
    assert(pfRet != SIG_ERR);
    raise(SIGINT);
  }
}

static void SIGQUIT_handler(int iSig){
  if(pid && sigalarm){
    exit(0);
  }
  else if(pid){
    fprintf(stdout, "Type Ctrl-\\ again within 5 seconds to exit\n");
    fflush(stdout);
    sigalarm = 1;
    alarm(5);
  }
  else{
    pfRet = signal(SIGQUIT, SIG_DFL);
    raise(SIGQUIT);
  }
}

static void SIGALRM_handler(int iSig){
  sigalarm = 0;
}

/*Simple shell: first get .ishrc from HOME path and execute it
line by line. Afterwards, work as a shell. */

int main(int argc, char* argv[]) {

  char * Shell_name = (argc > 0 && argv[0] != NULL) ? argv[0]: "ish"; //shell name
  errorPrint(Shell_name, SETUP);
  
  //signal handling

  sigset_t signalSet;

  sigemptyset(&signalSet);
  sigaddset(&signalSet, SIGINT);
  sigaddset(&signalSet, SIGQUIT);
  sigaddset(&signalSet, SIGQUIT);
  sigprocmask(SIG_UNBLOCK, &signalSet, NULL);

  signal(SIGINT, SIGINT_handler);
  signal(SIGQUIT, SIGQUIT_handler);
  signal(SIGALRM, SIGALRM_handler);


  // .ishrc handling
  char cwd[MAX_LINE_SIZE];
  const char *Home_directory = getenv("HOME");
  char *Now_directory = getcwd(cwd, sizeof(cwd));

  if(Home_directory == NULL){
    errorPrint("HOME environment variable is not set", FPRINTF); 
    return 1;
  }
  
  size_t path_length = strlen(Home_directory) + strlen("/.ishrc") + 1;
  char *ishrc_path = (char *) malloc(path_length);

  if(ishrc_path == NULL){
    errorPrint("Allocation failed", FPRINTF);
    return 1;
  }

  strcpy(ishrc_path, Home_directory);
  strcat(ishrc_path, "/.ishrc");

  if(chdir(Home_directory) != 0){
    errorPrint("Failed to change directory", FPRINTF);
    free(ishrc_path);
    return 1;
  }

  FILE *file_ishrc = fopen(ishrc_path, "r");

  //.ishrc processing
  if(file_ishrc != NULL){
    char ishrc_content[MAX_LINE_SIZE + 2];
    while(fgets(ishrc_content, sizeof(ishrc_content), file_ishrc) != NULL){
      fprintf(stdout, "%% ");
      printf("%s", ishrc_content);
      fflush(stdout);
      shellHelper(ishrc_content);
    }
    fclose(file_ishrc);
  }
  free(ishrc_path);

  chdir(Now_directory);

  char acLine[MAX_LINE_SIZE + 2];
  while (1) { // Shell process repeated
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
}


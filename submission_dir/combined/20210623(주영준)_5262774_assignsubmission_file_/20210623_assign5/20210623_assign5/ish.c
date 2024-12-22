/*
  Name
  - Yeongjun Joo

  Student ID
  - 20210623

  Description of the file
  - A simple Unix shell implementation that supports command execution,
    piping, input/output redirection, and built-in commands like cd, setenv, and exit.
    It includes basic syntax and lexical analysis using a deterministic finite state automaton (DFA).
*/

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
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

static int first_quit = 1;

/*
  Handles the SIGQUIT signal. On the first Ctrl-\ input,
  it warns the user to repeat the action within 5 seconds to exit the program.
  If the signal is sent again within 5 seconds, the program exits.
*/
static void quit_handler(){
  if(first_quit){
    first_quit = 0;
    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5);
  }
  else
    exit(0);
}
/*
  Handles the SIGALRM signal. Resets the state to allow the user
  to trigger the quit process again after the 5-second timeout has elapsed.
*/
static void alrm_handler(){
  first_quit = 1;
}

/*
  Executes commands with support for input/output redirection and piping.
  Processes tokens, handles redirection (TOKEN_REDIN, TOKEN_REDOUT),
  pipes (TOKEN_PIPE), and recursively executes piped commands using execvp.
  Prints errors for invalid files or commands.
*/
static void
exe_fun(DynArray_T oTokens, char **Token_pcValue, int num){
  assert(oTokens && Token_pcValue);

  struct Token *psToken;
  enum TokenType eType;
  int len_oTokens = DynArray_getLength(oTokens);
  int fd;

  char *real_Token_pcValue[MAX_LINE_SIZE] = {NULL};
  int index_real = 0;

  while(1){
    if(num >= len_oTokens){
      if(index_real < 1) return;
      if(execvp(real_Token_pcValue[0], real_Token_pcValue) < 0){
        errorPrint(real_Token_pcValue[0], SETUP);
        errorPrint("No such file or directory", FPRINTF);
        exit(1);
      }
      exit(0);
    }
    psToken = DynArray_get(oTokens, num);
    eType = psToken -> eType;

    switch(eType){
      case TOKEN_REDIN:
        num++;
        fd = open(Token_pcValue[num], O_RDONLY);
        if(fd < 0){
          errorPrint("No such file or directory", FPRINTF);
          exit(1);
        }
        close(0);
        dup(fd);
        close(fd);
        break;
      
      case TOKEN_REDOUT:
        num++;
        fd = creat(Token_pcValue[num], 0600);
        close(1);
        dup(fd);
        close(fd);
        break;
      
      case TOKEN_PIPE:
        if(num==0 || num==len_oTokens-1){
          errorPrint("Missing command name", FPRINTF);
          exit(1);
        }
        num++;
        psToken = DynArray_get(oTokens, num);
        if(psToken->eType == TOKEN_PIPE){
          errorPrint("Missing command name", FPRINTF);
          exit(1);
        }

        int fd_pipe[2];
        if(pipe(fd_pipe) < 0)
          assert(0);

        pid_t pid = fork();
        if(pid == 0){    //child
          dup2(fd_pipe[1], 1);
          close(fd_pipe[0]);
          close(fd_pipe[1]);
          real_Token_pcValue[index_real] = NULL;
          if(execvp(real_Token_pcValue[0], real_Token_pcValue) < 0){
            errorPrint(real_Token_pcValue[0], SETUP);
            errorPrint("No such file or directory", FPRINTF);
            exit(1);
          }
        }
        else{    //parent
          dup2(fd_pipe[0], 0);
          close(fd_pipe[0]);
          close(fd_pipe[1]);
          exe_fun(oTokens, Token_pcValue, num);
          waitpid(pid, NULL, 0);
        }
        break;
      
      default:
        real_Token_pcValue[index_real] = Token_pcValue[num];
        index_real++;
        break;
    }
    num++;
  }
}

/*
  Parses user input, validates syntax, and executes commands.
  Handles built-in commands like cd and exit, forks child processes
  for normal commands, and reports errors for invalid input or syntax issues.
*/
static void
shellHelper(const char *inLine) {
  assert(inLine);

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
        
        int len_oTokens = DynArray_getLength(oTokens);
        struct Token *psToken;
        char *Token_pcValue[len_oTokens + 1];
        assert(Token_pcValue);
        int i;
        for(i=0 ; i<len_oTokens ; i++){
          psToken = DynArray_get(oTokens, i);
          Token_pcValue[i] = psToken -> pcValue; 
        }
        Token_pcValue[len_oTokens] = NULL;

        pid_t pid;
        switch(btype){
          case NORMAL:
            pid = fork();
            if(pid == 0){    //child process
              signal(SIGINT, SIG_DFL);
              signal(SIGQUIT, SIG_DFL);
              exe_fun(oTokens, Token_pcValue, 0);
              exit(0);
            }
            else    //parent process
              waitpid(pid, NULL, 0);
            break;
          case B_EXIT:
            if(len_oTokens == 1)
              exit(0);
            else errorPrint("exit does not take any parameters", FPRINTF);
            break;

          case B_SETENV:
            if(len_oTokens==2 || len_oTokens==3) setenv(Token_pcValue[1], Token_pcValue[2], 1);
            else errorPrint("setenv takes one or two parameters", FPRINTF);
            break;

          case B_USETENV:
            if(len_oTokens == 2) unsetenv(Token_pcValue[1]);
            else errorPrint("unsetenv takes one paremeter", FPRINTF);
            break;

          case B_CD:
            if(len_oTokens == 1){
              const char *home_dir = getenv("HOME");
              assert(home_dir);
              chdir(home_dir);
            }
            else if(len_oTokens == 2){
              if(chdir(Token_pcValue[1]) < 0)
                errorPrint("No such file or directory", FPRINTF);
            }
            else
              errorPrint("cd takes one parameter", FPRINTF);
            break;

          default:
            assert(0);  
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
  DynArray_free(oTokens);
}

int main() {
  /* Unblock signals */
  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  signal(SIGINT, SIG_IGN);
  signal(SIGALRM, alrm_handler);
  signal(SIGQUIT, quit_handler);

  errorPrint("./ish", SETUP);

  char acLine[MAX_LINE_SIZE + 2];

  char *ishrc_dir = getenv("HOME");
  assert(ishrc_dir);
  FILE *ishrc = fopen(strcat(ishrc_dir, "/.ishrc"), "r");
  if(ishrc != NULL){
    while(fgets(acLine, MAX_LINE_SIZE, ishrc) != NULL){
      fprintf(stdout, "%% %s", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
    fclose(ishrc);
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

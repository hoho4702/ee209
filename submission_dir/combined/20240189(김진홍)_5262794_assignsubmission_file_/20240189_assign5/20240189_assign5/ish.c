/*
Name: Jinhong Kim
Student ID: 20240189
File Name: ish.c
Description: UNIX shell program that reads user input
*/
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syscall.h>
#include <wait.h>
#include <sysexits.h>
#include <syslog.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <assert.h>
#include "lexsyn.h"
#include "util.h"

/* Function: pipeLine 
This function implements pipe features*/
int pipeLine(DynArray_T oTokens) {
    int totNum = 0, currNum = 0, i, j;
    char* comm[MAX_ARGS_CNT][MAX_ARGS_CNT] = {NULL};
    struct Token *t;
    pid_t pid;

    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        t = DynArray_get(oTokens, i);
        if (t->eType != TOKEN_PIPE) {
            comm[totNum][currNum++] = t->pcValue;
        } else {
            totNum++;
            currNum = 0;
        }
    }
    totNum++;

    int pipelist[totNum - 1][2];
    for (i = 0; i < totNum - 1; i++) {
        if (pipe(pipelist[i]) == -1) {
            perror("pipe");
            exit(1);
        }
    }

    for (i = 0; i < totNum; i++) {
        pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            signal(SIGINT, SIG_DFL);

            if (i > 0) {
                close(0);
                dup2(pipelist[i - 1][0], 0);
            }
            if (i < totNum - 1) {
                close(1);
                dup2(pipelist[i][1], 1);
            }
            for (j = 0; j < totNum - 1; j++) {
                close(pipelist[j][0]);
                close(pipelist[j][1]);
            }

            if (execvp(comm[i][0], comm[i]) == -1) {
                perror("execvp");
                exit(1);
            }
        }
    }

    for (i = 0; i < totNum - 1; i++) {
        close(pipelist[i][0]);
        close(pipelist[i][1]);
    }
    for (i = 0; i < totNum; i++) {
        wait(NULL);
    }
    return 1;
}

/* Function: realquit 
exits shell
*/
static void realquit() {
    exit(0);
}

/* Function: quit_handler 
Executes when SIGQUIT is received. This function, when first called, it sets 5 second alarm and sets SIGQUIT to realquit.
*/
static void quit_handler() {
    void (*sp)(int);
    sp = signal(SIGQUIT, realquit);
    assert(sp != SIG_ERR);
    printf("\nType Ctrl -\\ again within 5 seconds to exit.\n");
    alarm(5);
}

/* Function: alarm_handler 
When alarm rings, this function restores SIGQUIT from realquit to quit_handler
*/
static void alarm_handler() {
    void(*sp)(int);
    sp = signal(SIGQUIT, quit_handler);
    assert(sp!=SIG_ERR);
}

/* Function: int_handler 
Executed when ctrl+C is pressed.
Upon receiving SIGINT, this ignore the signal. 
*/
static void int_handler() {
    void(*sp)(int);
    signal(SIGINT, SIG_IGN);
    assert(sp!=SIG_ERR);
}

/* Function: shellHelper
this function reads input line, does the lexical analysis in order to divide input string into tokens.
tokens are stored in dynamic array, then syntax analysis is performed appropriate functions are called.
 */
void shellHelper(const char *inLine) {
    DynArray_T oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }

    enum LexResult lexcheck = lexLine(inLine, oTokens);
    switch (lexcheck) {
        case LEX_SUCCESS:
            if (DynArray_getLength(oTokens) == 0) return;

            dumpLex(oTokens);
            enum SyntaxResult syncheck = syntaxCheck(oTokens);
            if (syncheck == SYN_SUCCESS) {
                enum BuiltinType btype = checkBuiltin(DynArray_get(oTokens, 0));
                if (btype == B_CD) changeDir(oTokens);
                if (btype == B_SETENV) setEnv(oTokens);
                if (btype == B_USETENV) unsetEnv(oTokens);
                if (btype == B_EXIT) exit_handler(oTokens);
                if (btype == NORMAL) normal(oTokens);
            } else {
                char *errorMsg = NULL;
                switch (syncheck) {
                    case SYN_FAIL_NOCMD: errorMsg = "Missing command name"; break;
                    case SYN_FAIL_MULTREDOUT: errorMsg = "Multiple redirection of standard out"; break;
                    case SYN_FAIL_NODESTOUT: errorMsg = "Standard output redirection without file name"; break;
                    case SYN_FAIL_MULTREDIN: errorMsg = "Multiple redirection of standard input"; break;
                    case SYN_FAIL_NODESTIN: errorMsg = "Standard input redirection without file name"; break;
                    case SYN_FAIL_INVALIDBG: errorMsg = "Invalid use of background"; break;
                    default: break;
                }
                if (errorMsg) errorPrint(errorMsg, FPRINTF);
            }
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

/* Function: changeDir 
This function gets input address token and changes directory to the input address.
*/
int changeDir(DynArray_T oTokens) {
    assert(oTokens != NULL);
    int length = DynArray_getLength(oTokens);
    if (length == 1) {
        const char* homeDir = getenv("HOME");
        if (homeDir) {
            chdir(homeDir);
        } else {
            perror("chdir");
        }
    } else if (length == 2) {
        const char* dir = ((struct Token*)DynArray_get(oTokens, 1))->pcValue;
        if (chdir(dir) < 0) {
            errorPrint("No such file or directory", FPRINTF);
            fflush(stderr);
        }
    } else {
        errorPrint("cd takes one parameter", FPRINTF);
        fflush(stderr);
    }
    return 1;
}

/* Function: setEnv 
This function sets value for the environment variable designated by the user 
*/
int setEnv(DynArray_T oTokens) {
    if (DynArray_getLength(oTokens) == 2) {
        const char *key = ((struct Token*)DynArray_get(oTokens, 1))->pcValue;
        if (setenv(key, "", 1) < 0) perror("setenv");
    } else if (DynArray_getLength(oTokens) == 3) {
        struct Token *key = DynArray_get(oTokens, 1);
        struct Token *value = DynArray_get(oTokens, 2);
        if (setenv(key->pcValue, value->pcValue, 1) < 0) perror("setenv");
    } else {
        errorPrint("setenv takes one or two parameters", FPRINTF);
        fflush(stderr);
    }
    return 1;
}

/* Function: unsetEnv 
This function unsets the assigned value for the designated environment variable
*/
int unsetEnv(DynArray_T oTokens) {
    assert(oTokens != NULL);
    if (DynArray_getLength(oTokens) == 2) {
        const char *key = ((struct Token*)DynArray_get(oTokens, 1))->pcValue;
        if (unsetenv(key) < 0) perror("unsetenv");
    } else {
        errorPrint("unsetenv takes one parameter", FPRINTF);
        fflush(stderr);
    }
    return 1;
}

/* Function: exit_handler 
Executes exit of shell
*/
int exit_handler(DynArray_T oTokens) {
    assert(oTokens != NULL);
    if (DynArray_getLength(oTokens) == 1) {
        fflush(stdout);
        DynArray_free(oTokens);
        exit(EXIT_SUCCESS);
    } else {
        errorPrint("exit does not take any parameters", FPRINTF);
    }
    return 1;
}

/* Function: normal 
This function executes when normal input is received(not exit, set/unset,..etc). Especially handles file redirections
*/
int normal(DynArray_T oTokens) {
  assert(oTokens != NULL);

  if (countPipe(oTokens) != 0) {
    pipeLine(oTokens);
  }
  else {
    char* args[1024];
    struct Token *t;
    pid_t pid;

    pid = fork();

    if (pid < 0) { // error 
      errorPrint("fork error", FPRINTF);
      exit(0);
    } else if (pid == 0) { // child process
      int length = DynArray_getLength(oTokens);
      char *nIn, *nOut;
      int fdIn, fdOut;
      int countIn = 0;
      int countOut = 0;
      int i;
      
      void (*sp)(int);
      sp = signal(SIGINT, SIG_DFL);
      assert(sp != SIG_ERR);

      for (i = 0; i<DynArray_getLength(oTokens); i++) { // error handling
        t = DynArray_get(oTokens, i);
        if (t -> eType == TOKEN_REDIN){ // if token is redirection in
          countIn += 1;
          if (i<DynArray_getLength(oTokens)-1){
            t = DynArray_get(oTokens, i+1);
            if ((t->eType == TOKEN_REDIN) || t->eType == TOKEN_REDOUT){
              errorPrint("Standard input redirection without file name\n", FPRINTF);
              fflush(stderr);
            }
          } else if (i == DynArray_getLength(oTokens)-1) {
            errorPrint("Standard input redirection without file name\n", FPRINTF);
            fflush(stderr);
          }
          if (countIn > 1) {
            errorPrint("Multiple redirection of standard input\n", FPRINTF);
            fflush(stderr);
          }
        }
        if (t->eType == TOKEN_REDOUT) { // if token is rediirection out
          countOut += 1;
          if (i < length -1){
            t = DynArray_get(oTokens, i+1);
            if ((t->eType == TOKEN_REDIN) || t->eType == TOKEN_REDOUT) {
              errorPrint("Standard output redirection without file name\n", FPRINTF);
              fflush(stderr);
            }
          } else if (i == DynArray_getLength(oTokens)-1){
            errorPrint("Standard output redirection without file name\n", FPRINTF);
            fflush(stderr);
          }
          if (countOut > 1) {
            errorPrint("Multiple redirection of standard input\n", FPRINTF);
            fflush(stderr);
          }
        }  
      } 
      for (i = 0; i < length; i++) {
        t = DynArray_get(oTokens, i);
        if  (t->eType == TOKEN_REDIN) { // handles redirection in
          nIn = ((struct Token*)DynArray_get(oTokens, i+1))->pcValue;
          fdIn = open(nIn, O_RDONLY);
          if (fdIn < 0) {
            errorPrint("No such file of directory", FPRINTF);
            exit(0);
          }
          close(0);
          dup2(fdIn, 0);
          close(fdIn);
        }
        if (t->eType == TOKEN_REDOUT) { // handles redirection out
          nOut = ((struct Token*)DynArray_get(oTokens, i+1))->pcValue;
          fdOut = open(nOut, O_WRONLY|O_CREAT|O_TRUNC, 0600);
          if (fdOut < 0) {
            errorPrint("No such file of directory", FPRINTF);
            exit(0);
          }
          close(1);
          dup2(fdOut, 1);
          close(fdOut);
        }

      }
      for (i = 0; i < length; i++){
        t = DynArray_get(oTokens, i);
        args[i]=t->pcValue;
      }
      args[length] = NULL;
      if(execvp(args[0], args)<0){
      errorPrint(args[0], SETUP);
      errorPrint("No such file or directory", FPRINTF);
      exit(0);
      }
      exit(0);
    }
    
    else { //Wait for child process
      wait(NULL);
    }
  }
}
/*Function: main
This function handles signals, reads .ishrc file in home directory, and executes the command, then reads and executes input string commands. 
*/
int main(int argc, char* argv[]) {
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGQUIT);
  sigaddset(&sigset, SIGALRM);

  sigprocmask(SIG_UNBLOCK, &sigset, NULL);

  void (*sp)(int);
  //signal handling
  sp = signal(SIGINT, int_handler);
  assert(sp!=SIG_ERR);

  sp = signal(SIGQUIT, quit_handler);
  assert(sp!=SIG_ERR);

  sp = signal(SIGALRM, alarm_handler);
  assert(sp!=SIG_ERR);

  errorPrint(argv[0], SETUP);
  /* TODO */
  char acLine[MAX_LINE_SIZE + 2];
  char* home_dir = getenv("HOME");
  char* filePath;
  FILE* f;

  // reading .ishrc file and executing the command written in that file
  if (home_dir == NULL) {
    errorPrint("HOME environment variable is not set", FPRINTF);
  }
  else {
    filePath = strcat(home_dir, "/.ishrc");
    f= fopen(filePath, "r");
  }
  if (f != NULL) {
    while(fgets(acLine, MAX_LINE_SIZE, f) != NULL) {
      if (acLine[strlen(acLine)-1] == '\n') {
        acLine[strlen(acLine)-1] = '\0';
      }
      fprintf(stdout, "%% %s\n", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
    fclose(f);
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

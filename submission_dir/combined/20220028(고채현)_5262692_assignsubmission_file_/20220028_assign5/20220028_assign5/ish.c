/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Author: [Kyuwon Lee, 20230478] [ChaeHyeon Go, 20220028]                              */
/*                                                                    */
/* This file implements a simple Unix shell as described in the       */
/* assignment specification. It supports initialization from .ishrc,  */
/* built-in commands, environment variable handling, directory        */
/* changes, command execution via fork/execvp, I/O redirection,       */
/* error handling, and signal handling (SIGINT and SIGQUIT).          */
/* Optionally, it handles pipes for extra credit.                     */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <time.h>

#include "lexsyn.h"
#include "util.h"
#include "dynarray.h"
#include "token.h"

static char *program_name = NULL;
static sig_atomic_t quitCount=0;
static time_t lastQuitTime=0;

// Signal Handlers                                                    

/* SIGINT handler: parent ignores SIGINT by doing nothing. */
static void handleSIGINT(int signum) {
    (void)signum; 
}

/* SIGQUIT handling variables */

/* SIGQUIT handler: 
   Print a message if first time. If triggered again within 5 seconds, exit. */
static void handleSIGQUIT(int signum) {
    (void)signum;
    time_t currentTime = time(NULL);
    if (quitCount == 0){
        printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        quitCount = 1;
        lastQuitTime = currentTime;
        alarm(5);
    }else{
        if (difftime(currentTime, lastQuitTime) <= 5){
            exit(0);
        }else{
            printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
            fflush(stdout);
            quitCount = 1;
            lastQuitTime = currentTime;
            alarm(5);
        }
    }
}

/* SIGALRM handler: reset quitCount */
static void handleSIGALRM(int signum) {
    (void)signum;
    assert(signal(SIGQUIT, handleSIGQUIT)!=SIG_ERR);
}

/* Install the required signal handlers for the parent shell process. */
static void installSignalHandlers(void) {
    sigset_t signal_SET;
    sigemptyset(&signal_SET);
    sigaddset(&signal_SET, SIGINT);
    sigaddset(&signal_SET, SIGQUIT);
    sigaddset(&signal_SET, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &signal_SET, NULL);

    if (signal(SIGINT, handleSIGINT) == SIG_ERR) {
        exit(EXIT_FAILURE);
    }
    if (signal(SIGQUIT, handleSIGQUIT) == SIG_ERR) {
        exit(EXIT_FAILURE);
    }
    if (signal(SIGALRM, handleSIGALRM) == SIG_ERR) {
        exit(EXIT_FAILURE);
    }
}

//Helper Functions                                    
static void freeTokens(DynArray_T oTokens) {
    DynArray_map(oTokens, freeToken, NULL);
    DynArray_free(oTokens);
}

//Extract arguments and perform redirections
static char** parseCommand(DynArray_T oTokens, char **pInFile, char **pOutFile) {

    int length = DynArray_getLength(oTokens);
    char **argv = calloc(length + 1, sizeof(char*));
    int argCount = 0;
    *pInFile = NULL;
    *pOutFile = NULL;

    if (argv == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        return NULL;
    }

    for (int i = 0; i < length; i++) {
        struct Token *t = DynArray_get(oTokens, i);

        if (t->eType == TOKEN_WORD) {
            argv[argCount++] = t->pcValue;
        }
        else{
            if (t->eType == TOKEN_REDIN) {
                //check whether next token is word
                if (i+1 == length) {
                    errorPrint("Standard input redirection without file name",FPRINTF);
                    free(argv);
                    return NULL;
                }
                struct Token *next = DynArray_get(oTokens, i+1);
                *pInFile = next->pcValue;
                i++;
            }
            else if (t->eType == TOKEN_REDOUT){
                //check whether next token is a word
                if (i+1 == length){
                    errorPrint("Standard output redirection without file name",FPRINTF);
                    free(argv);
                    return NULL;
                }
                struct Token *next = DynArray_get(oTokens, i+1);
                *pOutFile = next->pcValue;
                i++;
            }
        }
    }

    argv[argCount] = NULL;
    return argv;
}

static void executeSingleCommand(DynArray_T oTokens) {
      // Execute a single command with no pipes. If built-in, handle internally.
   // if external, fork and execvp.
    enum BuiltinType btype = checkBuiltin(DynArray_get(oTokens, 0));
    int arg_num = DynArray_getLength(oTokens);

    for (int i = 1; i < DynArray_getLength(oTokens); i++) {
         // check for redirection in builtins
        struct Token *t = DynArray_get(oTokens, i);

      if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
                // Builtins except alias and fg must not have redirection
                if(btype == B_CD){
                    errorPrint("cd takes one parameter",FPRINTF);
                    return;
                }
                else if(btype ==B_SETENV){
                    errorPrint("setenv takes one or two parameters",FPRINTF);
                    return;
                }
                else if(btype ==B_USETENV){
                    errorPrint("unsetenv takes one parameter",FPRINTF);
                    return;
                }
                else if(btype == B_EXIT){
                    errorPrint("exit does not take any parameters",FPRINTF);
                    return;
                }               
                }
            }
   
    // check errors in built-in including number of parameters error
    if (btype == B_EXIT) {
        if (arg_num>1){
            errorPrint("exit does not take any parameters", FPRINTF);
            return;
        }
        DynArray_free(oTokens);
        exit(0);
    }

    else if (btype == B_CD) {
        char *dir = NULL;
        if (arg_num > 2){
            errorPrint("cd takes one parameter", FPRINTF);
            return;
        }else if (arg_num==1){
            // no given dir -> HOME
            dir = getenv("HOME");
        }else{
            struct Token *t=DynArray_get(oTokens, 1);
            dir= t->pcValue;
        }
            
        if (dir == NULL||chdir(dir) ==-1) {
            errorPrint(NULL, PERROR);
        }
        return;
    }

    else if (btype == B_SETENV){
        if (arg_num == 2){

            struct Token *t = DynArray_get(oTokens, 1);
            if (setenv(t->pcValue, "", 1) == -1) {
                errorPrint(NULL, PERROR);
            }
        } 
        else if (arg_num < 2||arg_num>3){
            errorPrint("setenv takes one or two parameters",FPRINTF);
        }
        else{
            struct Token *tvar = DynArray_get(oTokens, 1);
            struct Token *tval = DynArray_get(oTokens, 2);
            if (setenv(tvar->pcValue, tval->pcValue, 1) == -1) {
                errorPrint(NULL, PERROR);
            }
        } 
        return;
    }

    else if (btype == B_USETENV) {
        if (arg_num == 2) {
            struct Token *t = DynArray_get(oTokens, 1);
            if (getenv(t->pcValue)!=NULL){
                unsetenv(t->pcValue);
            }
        } else {
            errorPrint("unsetenv takes one parameter",FPRINTF);
        }
        return;
    }

    else if (btype == B_ALIAS || btype == B_FG) {
        //Not required to handle 
        return;
    }

    //External command

    char *inFile = NULL;
  char *outFile = NULL;

    char **argv = parseCommand(oTokens, &inFile, &outFile);

    if (argv == NULL) {
        return; 
        }
    if (argv[0] == NULL) {
        //no command name
        free(argv);
        return; 
        }

    //fork and exec 

    fflush(NULL);
    pid_t pid = fork();

    if (pid < 0) {
        errorPrint(NULL, PERROR);
        free(argv);
        return;
    }
    else if (pid == 0) {

        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGALRM, SIG_DFL);

        //redirections
        if (inFile) {
            int fd = open(inFile, O_RDONLY);
            if (fd < 0) {
                errorPrint(NULL, PERROR);
                
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (outFile) {
            int fd = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0) {
                errorPrint(NULL, PERROR);
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(argv[0], argv);
        // If execvp returns, it's error
        errorPrint(argv[0], PERROR);
        exit(1);
    }
    else {
        //parent
        int status;
        waitpid(pid, &status, 0);
    }
    free(argv);
}

static void executePipeline(DynArray_T oTokens) {

    int numPipes = countPipe(oTokens);

    //Split tokens by pipe into commands
    //Create an array of DynArray_T for each command.

    int length = DynArray_getLength(oTokens);

    DynArray_T *commands = malloc((numPipes+1)*sizeof(DynArray_T));
    
    if (commands == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        return;
    }

    int start = 0;
    int cmdnumber = 0;

    for (int i = 0; i < length; i++) {

        struct Token *t = DynArray_get(oTokens, i);

        if (t->eType == TOKEN_PIPE) {
            // command ends before this and next starts after this
            
         commands[cmdnumber] = DynArray_new(0);

            for (int j = start; j < i; j++) {
                DynArray_add(commands[cmdnumber], DynArray_get(oTokens,j));
            }
            cmdnumber++;
            start = i+1;
        }
    }
    commands[cmdnumber] = DynArray_new(0);

    for (int j = start; j < length; j++) {
        DynArray_add(commands[cmdnumber], DynArray_get(oTokens,j));
    }

    int cmdSum = numPipes + 1;
    int connect_pipe[numPipes*2];

    for (int i = 0; i < numPipes; i++) {
        if (pipe((i*2) + connect_pipe) < 0) {
          errorPrint(NULL, PERROR);

            //cleanup
            for (int i = 0; i < cmdSum; i++){
                DynArray_free(commands[i]);
            }
             free(commands);
             return;
        }
    }

    fflush(NULL);

    for (int i = 0; i < cmdSum; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            errorPrint(NULL, PERROR);
            //cleanup
            for (int i = 0; i < cmdSum; i++){
                DynArray_free(commands[i]);
            }
             free(commands);
             return;
        }

        else if (pid == 0) {
            //Child process 
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGALRM, SIG_DFL);

            // Set up 
            if (i > 0) {
                dup2(connect_pipe[2*(i-1)], STDIN_FILENO);
            }
            
            if (i < cmdSum - 1) {
                dup2(connect_pipe[2*i + 1], STDOUT_FILENO);
            }

            //close all pipes in child
            for (int k = 0; k < 2*numPipes; k++) {
                close(connect_pipe[k]);
            }

            //Parse redirection for this command (no multiple pipes inside a single segment)
            char *inFile = NULL, *outFile = NULL;
            char **argv = parseCommand(commands[i], &inFile, &outFile);
            if (argv == NULL || argv[0] == NULL) {
                exit(1);
            }

            if (inFile) {
                int fd = open(inFile, O_RDONLY);

                if (fd < 0) {
                    errorPrint(inFile, PERROR);
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }
            if (outFile) {
                int fd = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (fd < 0) {
                    errorPrint(outFile, PERROR);
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            execvp(argv[0], argv);
            errorPrint(argv[0], PERROR);
            exit(1);
        }
        // Parent continues to next 
    }

    // Parent closes pipes 
    for (int i = 0; i < 2*numPipes; i++) {
        close(connect_pipe[i]);
    }

    // Wait for all children
    for (int i = 0; i < cmdSum; i++) {
        wait(NULL);
    }

    //free memory after all children process terminated
    for (int i=0; i<cmdSum; i++){
        DynArray_free(commands[i]);
    }
    free(commands);

}
static void shellHelper(const char *inLine);

// Read and execute lines of .ishrc 
static void readIshrc() {
    const char *home = getenv("HOME");
    if (home == NULL) return; 

    char path[MAX_LINE_SIZE];
    snprintf(path, sizeof(path), "%s/.ishrc", home);
    FILE *ishrc = fopen(path, "r");

    if (ishrc == NULL)
        return; 

    char acLine[MAX_LINE_SIZE+2];

    while (fgets(acLine, MAX_LINE_SIZE, ishrc) != NULL) {
        int command_last=strlen(acLine);
         if (command_last > 0 && acLine[command_last-1]!='\n') {
            if (command_last < MAX_LINE_SIZE - 1) {
                acLine[command_last] = '\n';
                acLine[command_last+1] = '\0';
            } 
        }
        fprintf(stdout, "%% %s", acLine);
        shellHelper(acLine);
    }
    fclose(ishrc);
}

static void shellHelper(const char *inLine) {
  DynArray_T oTokens;
  enum LexResult lexcheck;
  enum SyntaxResult syncheck;

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }

  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0) {
        DynArray_free(oTokens);
        return;
      }

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        //check whether it contains a pipeline 
        int npipes = countPipe(oTokens);
        if (npipes > 0) {
            executePipeline(oTokens);
        }
        else {
            executeSingleCommand(oTokens);
        }
      }
      else if (syncheck == SYN_FAIL_NOCMD)
        errorPrint("Missing command name", FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDOUT)
        errorPrint("Multiple redirection of standard out", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTOUT)
        errorPrint("Standard output redirection without file name",FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDIN)
        errorPrint("Multiple redirection of standard input",FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTIN)
        errorPrint("Standard input redirection without file name",FPRINTF);
      else if (syncheck == SYN_FAIL_INVALIDBG)
        errorPrint("Invalid use of background",FPRINTF);

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

  freeTokens(oTokens);
}

int main(int argc, char *argv[]) {
    if(argc > 0){
        program_name = argv[0];
    }
    else{
        program_name = "ish";
    }
    
    errorPrint(program_name, SETUP);

    installSignalHandlers();
    readIshrc();

    char acLine[MAX_LINE_SIZE+2];
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

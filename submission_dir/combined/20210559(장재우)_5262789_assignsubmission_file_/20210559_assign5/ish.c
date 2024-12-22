#define _GNU_SOURCE
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

void setEnv(DynArray_T tokens) {
    size_t len = DynArray_getLength(tokens);
    // check if setenv is called with valid number of parameters
    if (len > 3 || len < 2) {
        errorPrint("setenv takes one or two parameters", FPRINTF);
        fflush(stderr);
        return;
    }
    char *var = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
    char *value = "";
    if (len == 3) {
        value = ((struct Token *)DynArray_get(tokens, 2))->pcValue;
    }
    if (setenv(var, value, 1) < 0) {
        perror("setenv");
    }
}

void unsetEnv(DynArray_T tokens) {
    size_t len = DynArray_getLength(tokens);
    // check if unsetenv is called with valid number of parameters
    if (len != 2) {
        errorPrint("unsetenv takes one parameter", FPRINTF);
        fflush(stderr);
        return;
    }
    char *var = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
    if (unsetenv(var) < 0) {
        perror("unsetenv");
    }
}

void cd(DynArray_T tokens) {
    size_t len = DynArray_getLength(tokens);
    // check if cd is called with valid number of parameters
    if (len > 3 || len < 1) {
        errorPrint("cd takes one parameter.", FPRINTF);
        fflush(stderr);
        return;
    }
    // set default destination to HOME (no parameter)
    char *dest = getenv("HOME");
    if (len == 2) {
        dest = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
    }
    // change directory
    if (chdir(dest) == -1) {
        errorPrint("No such file or directory", FPRINTF);
        fflush(stderr);
    }
}

void execute(DynArray_T tokens) {
    int i, n = 0, cnt = 0;

    int pipeLen = countPipe(tokens);
    size_t len = DynArray_getLength(tokens);
    // file descriptor for pipe
    int fd[pipeLen][2];
    // arguments for execvp
    char *args[pipeLen+1][1024];
    // redirection src and dest
    char *redSrc = NULL;
    char *redDest = NULL;

    // set arguments and redirections
    for (i = 0; i < len; i++) {
        struct Token *t = DynArray_get(tokens, i);
        if (cnt == 0 && t->eType != TOKEN_WORD) {
            errorPrint("Missing command name", FPRINTF);
            fflush(stderr);
            return;
        }
        if (t->eType == TOKEN_PIPE) {
            n++;
            cnt = 0;
            continue;
        } else if (t->eType == TOKEN_REDIN) {
            redSrc = ((struct Token *)DynArray_get(tokens, i+1))->pcValue;
        } else if (t->eType == TOKEN_REDOUT) {
            redDest = ((struct Token *)DynArray_get(tokens, i+1))->pcValue;
        }
        args[n][cnt++] = t->pcValue;
        
    }

    assert(n == pipeLen);

    // set pipe for redirection
    for (i = 0; i < pipeLen; i++) {
        if (pipe(fd[i]) < 0) {
            errorPrint("pipe error", FPRINTF);
            fflush(stderr);
            return;
        }
    }

    pid_t pid[pipeLen + 1];

    for (i = 0; i < pipeLen + 1; i++) {
        fflush(NULL);
        pid[i] = fork();
        if (pid[i] < 0) {
            errorPrint("fork error", FPRINTF);
            fflush(stderr);
            // wait all child processes before closing
            for (n = 0; n < i; n++) {
                close(fd[n][0]);
                close(fd[n][1]);
            }
            for (n = 0; n < i; n++) wait(NULL);
            return;
        } else if (pid[i] == 0) {
            // child process
            // reset signal handling 
            assert(signal(SIGINT, SIG_DFL) != SIG_ERR);
            assert(signal(SIGQUIT, SIG_DFL) != SIG_ERR);
            // setup pipe
            if (i != 0) {
                dup2(fd[i-1][0], STDIN_FILENO);
            }
            if (i != pipeLen) {
                dup2(fd[i][1], STDOUT_FILENO);
            }
            for (n = 0; n < pipeLen; n++) {
                close(fd[n][0]);
                close(fd[n][1]);
            }
            // redirection
            // input redirection MUST be in the first command of pipe
            if (i == 0 && redSrc) {
                int inFd = open(redSrc, O_RDONLY);
                if (inFd < 0) {
                    errorPrint(redSrc, SETUP);
                    errorPrint("No such file or directory", FPRINTF);
                    fflush(stderr);
                    exit(0);
                }
                dup2(inFd, STDIN_FILENO);
                close(inFd);
            }
            // output redirection MUST be in the last command of pipe
            if (i == pipeLen && redDest) {
                int outFd = open(redDest, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (outFd < 0) {
                    errorPrint(redDest, SETUP);
                    errorPrint("Cannot open file for writing", FPRINTF);
                    fflush(stderr);
                    exit(0);
                }
                dup2(outFd, STDOUT_FILENO);
                close(outFd);
            }

            if (getenv("DEBUG") != NULL) {
                fprintf(stderr, "[DEBUG] %s: Executing execvp in child %d\n", args[i][0], i);
            }
            if (execvp(args[i][0], args[i]) < 0) {
                errorPrint(args[i][0], SETUP);
                errorPrint("No such file or directory", FPRINTF);
                fflush(stderr);
                exit(0);
            }
            // cannot be reached
            assert(0);
        }
    }
    // parent process
    // close all file descriptors
    for (i = 0; i < pipeLen; i++) {
        close(fd[i][0]);
        close(fd[i][1]);
    }
    // wait for child processes
    if (getenv("DEBUG") != NULL) {
        fprintf(stderr, "[DEBUG] Waiting for child processes\n");
    }
    for (i = 0; i <= pipeLen; i++) {
        wait(NULL);
        if (getenv("DEBUG") != NULL) {
            fprintf(stderr, "[DEBUG] Child process %d has terminated\n", i);
        }
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
            switch(btype) {
            case B_SETENV:
                setEnv(oTokens);
                break;
            case B_USETENV:
                unsetEnv(oTokens);
                break;
            case B_CD:
                cd(oTokens);
                break;
            case B_EXIT:
                // check if exit is called with no parameters
                if (DynArray_getLength(oTokens) != 1) {
                    errorPrint("exit does not take any parameters", FPRINTF);
                } else {
                    DynArray_free(oTokens);
                    exit(EXIT_SUCCESS);
                }
                break;
            case B_ALIAS:
            case B_FG:
                break;
            case NORMAL:
                execute(oTokens);
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


void quitImmediately(int sig) {
    exit(EXIT_SUCCESS);
}

void handleQuit(int sig) {
    assert(signal(SIGQUIT, quitImmediately) != SIG_ERR);
    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5);
}

void resetQuit(int sig) {
    assert(signal(SIGQUIT, handleQuit) != SIG_ERR);
}

int main(int argc, char *argv[]) {
  /* TODO */

    // unblock signals
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    // set signal handlers
    assert(signal(SIGINT, SIG_IGN) != SIG_ERR);
    assert(signal(SIGQUIT, handleQuit) != SIG_ERR);
    assert(signal(SIGALRM, resetQuit) != SIG_ERR);

    // setup ishname in errorPrint
    errorPrint(argv[0], SETUP);

    char *home = getenv("HOME");
    if (home == NULL) {
        errorPrint("HOME environment variable is not set.", FPRINTF);
    } else {
        // read .ishrc file
        char path[MAX_LINE_SIZE];
        strcpy(path, home);
        strcat(path, "/.ishrc");
        FILE *file = fopen(path, "r");
        // read and execute each line in file if it exists
        if (file) {
            char line[MAX_LINE_SIZE + 2];
            while (fgets(line, MAX_LINE_SIZE, file) != NULL) {
                line[strlen(line)-1] = '\0';
                fprintf(stdout, "%% %s\n", line);
                fflush(stdout);
                shellHelper(line);
            }
            fclose(file);
        }
    }

    // work as an interpreter
    char acLine[MAX_LINE_SIZE + 2];
    while (1) {
        // fprintf(stderr, "[DEBUG] Printing prompt\n");
        fprintf(stdout, "%% ");
        fflush(stdout);
        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }
        // fprintf(stderr, "[DEBUG] Executing shellHelper\n");
        shellHelper(acLine);
        // fprintf(stderr, "[DEBUG] Done with shellHelper\n");
    }
}


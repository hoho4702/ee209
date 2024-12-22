
/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Name: 박준서                                                       */
/* StudentID: 20200261                                                */
/*                                                                    */
/* This file implements a minimal Unix shell named 'ish'. It supports */
/* reading initialization commands from ~/.ishrc, executing built-in  */
/* commands (setenv, unsetenv, cd, exit), handling signals (SIGINT,   */
/* SIGQUIT), simple input/output redirection, and pipelines.          */
/*                                                                    */
/* It uses lexical and syntax analysis routines from lexsyn.h, and    */
/* utility functions from util.h. The shell repeatedly reads a line   */
/* of user input, parses it, and executes the corresponding command.   */
/*                                                                    */
/*--------------------------------------------------------------------*/
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include "lexsyn.h"
#include "util.h"

static int quitSignalReceived = 0;

/* Forward declaration */
/*--------------------------------------------------------------------*/
/* shellHelper                                                        */
/* Given a line of input (inLine), tokenize it, perform syntax check, */
/* and execute the command (either builtin or external).              */
/*                                                                    */
/* Parameters:                                                        */
/*  inLine: a string containing the user input line                   */
/* Reads from standard input if the command specifies redirection,    */
/* writes to standard output/error accordingly.                       */
/* Modifies global quitSignalReceived for SIGQUIT handling.           */
/*--------------------------------------------------------------------*/
static void shellHelper(const char *inLine);

/* Signal handlers */
/*--------------------------------------------------------------------*/
/* handleSIGINT                                                       */
/* Handles SIGINT by ignoring it in the parent process.               */
/* Child processes are not protected by this handler.                 */
/*                                                                    */
/* Parameters: sig - the signal number (SIGINT)                       */
/* No return value, no global variables modified (except via signal). */
/*--------------------------------------------------------------------*/
static void handleSIGINT(int sig) {
    (void)sig; /* unused */
    signal(SIGINT, SIG_IGN);  /* explicitly ignore SIGINT */
}

/*--------------------------------------------------------------------*/
/* handleSIGALRM                                                      */
/* Resets the quitSignalReceived flag after 5 seconds if user did not */
/* press Ctrl-\ again.                                                */
/*                                                                    */
/* Parameters: sig - the signal number (SIGALRM)                      */
/*--------------------------------------------------------------------*/
static void handleSIGALRM(int sig) {
    (void)sig; /* unused */
    quitSignalReceived = 0;
    signal(SIGALRM, handleSIGALRM);
}

/*--------------------------------------------------------------------*/
/* handleSIGQUIT                                                      */
/* On first SIGQUIT, print message and start a 5-second timer. If     */
/* SIGQUIT is received again within 5 seconds, exit.                  */
/*                                                                    */
/* Parameters: sig - the signal number (SIGQUIT)                      */
/*--------------------------------------------------------------------*/
static void handleSIGQUIT(int sig) {
    (void)sig; /* unused */
    if (quitSignalReceived) {
        exit(EXIT_SUCCESS);
    } else {
        printf("Type Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        quitSignalReceived = 1;
        alarm(5);  /* set a 5-second timer */
    }
    signal(SIGQUIT, handleSIGQUIT);
}

/* Built-in command handling */
/*--------------------------------------------------------------------*/
/* executeBuiltin                                                     */
/* Executes a built-in command (exit, cd, setenv, unsetenv).          */
/* Checks for redirection attempts on built-in commands and reports   */
/* error if present.                                                  */
/*                                                                    */
/* Parameters:                                                        */
/*  oTokens: the DynArray of tokens representing the command          */
/*  btype: the type of built-in command                               */
/* Reads/Writes: standard error for error messages.                   */
/*--------------------------------------------------------------------*/
static void executeBuiltin(DynArray_T oTokens, enum BuiltinType btype) 
{
    assert(oTokens != NULL);

    /* Check for any redirection in builtin commands */
    int i;
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
            errorPrint("Built-in commands cannot be redirected", 
            FPRINTF);
            return;
        }
    }

    switch (btype) {
        case B_EXIT:
            exit(EXIT_SUCCESS);
            break;
            
        case B_CD: {
            char *dir = NULL;
            if (DynArray_getLength(oTokens) > 1) {
                struct Token *dirToken = DynArray_get(oTokens, 1);
                dir = dirToken->pcValue;
            } else {
                dir = getenv("HOME");
            }
            
            if (chdir(dir) != 0) {
                errorPrint("Cannot change directory", PERROR);
            }
            break;
        }
            
        case B_SETENV: {
            if (DynArray_getLength(oTokens) < 2) {
                errorPrint("Too few arguments", FPRINTF);
                return;
            }
            struct Token *varToken = DynArray_get(oTokens, 1);
            char *value = "";
            if (DynArray_getLength(oTokens) > 2) {
                struct Token *valToken = DynArray_get(oTokens, 2);
                value = valToken->pcValue;
            }
            if (setenv(varToken->pcValue, value, 1) != 0) {
                errorPrint("Cannot set environment variable", PERROR);
            }
            break;
        }
            
        case B_USETENV: {
            if (DynArray_getLength(oTokens) < 2) {
                errorPrint("Too few arguments", FPRINTF);
                return;
            }
            struct Token *varToken = DynArray_get(oTokens, 1);
            unsetenv(varToken->pcValue);
            break;
        }
        
        case B_ALIAS:
        case B_FG:
        case NORMAL:
            /* Not implemented built-ins */
            errorPrint("Command not implemented", FPRINTF);
            break;
    }
}

/* External command execution */
/*--------------------------------------------------------------------*/
/* executeCommand                                                     */
/* Executes an external command (non-builtin) possibly with           */
/* redirections. Forks a child process to run execvp().               */
/*                                                                    */
/* Parameters:                                                        */
/*  oTokens: DynArray of tokens representing the command and args     */
/* Returns: none                                                      */
/* Reads/Writes: standard I/O, possibly redirected                    */
/* Global variables: none                                             */
/*--------------------------------------------------------------------*/
static void executeCommand(DynArray_T oTokens) {
    assert(oTokens != NULL);

    pid_t pid;
    int status;
    int hasRedirIn = 0, hasRedirOut = 0;
    char *inFile = NULL, *outFile = NULL;
    int i;
    
    /* Find any redirections */
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == 
                  TOKEN_REDIN && i + 1 < DynArray_getLength(oTokens)) {
            hasRedirIn = 1;
            struct Token *fileToken = DynArray_get(oTokens, i + 1);
            inFile = fileToken->pcValue;
        }
        if (t->eType == 
                  TOKEN_REDOUT && i + 1 < DynArray_getLength(oTokens)) {
            hasRedirOut = 1;
            struct Token *fileToken = DynArray_get(oTokens, i + 1);
            outFile = fileToken->pcValue;
        }
    }
    
    /* Create argument array */
    char *args[MAX_ARGS_CNT];
    int argIndex = 0;
    
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_WORD) {
            if (argIndex < MAX_ARGS_CNT - 1) {
                args[argIndex++] = t->pcValue;
            }
        } else if (t->eType == TOKEN_REDIN || 
                    t->eType == TOKEN_REDOUT) {
            i++; /* Skip the filename token */
        }
    }
    args[argIndex] = NULL;
    
    /* Fork and execute */
    fflush(NULL);  /* Clear all I/O buffers before fork */
    pid = fork();
    if (pid == -1) {
        errorPrint("Cannot create child process", PERROR);
        return;
    }
    
    if (pid == 0) { /* Child process */
        /* Handle redirections */
        if (hasRedirIn) {
            int fd = open(inFile, O_RDONLY);
            if (fd == -1) {
                errorPrint("Cannot open input file", PERROR);
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        
        if (hasRedirOut) {
            int fd = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd == -1) {
                errorPrint("Cannot open output file", PERROR);
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
        
        execvp(args[0], args);
        errorPrint(args[0], PERROR);
        exit(EXIT_FAILURE);
    }
    
    /* Parent process */
    waitpid(pid, &status, 0);
}

/* Pipe command checks and pipeline execution */
#define PIPE_CHECK_SUCCESS 1
#define PIPE_CHECK_ERROR   0

/*--------------------------------------------------------------------*/
/* checkPipeCommand                                                   */
/* Checks if a command in a pipeline is syntactically correct         */
/* regarding input/output redirections depending on its position in   */
/* the pipeline.                                                      */
/*                                                                    */
/* Parameters:                                                        */
/*  cmdTokens: DynArray of tokens for one command in the pipeline     */
/*  isFirst: 1 if this command is the first in the pipeline           */
/*  isLast:  1 if this command is the last in the pipeline            */
/*                                                                    */
/* Returns: PIPE_CHECK_SUCCESS or PIPE_CHECK_ERROR                    */
/*--------------------------------------------------------------------*/
static int checkPipeCommand(DynArray_T cmdTokens, 
                              int isFirst, int isLast) {
    assert(cmdTokens != NULL);
    int i;
    for (i = 0; i < DynArray_getLength(cmdTokens); i++) {
        struct Token *t = DynArray_get(cmdTokens, i);
        /* 파이프 왼쪽에서 stdout redirect 금지 */
        if (!isLast && t->eType == TOKEN_REDOUT) {
            errorPrint("Multiple redirection of standard out", FPRINTF);
            return PIPE_CHECK_ERROR;
        }
        /* 파이프 오른쪽에서 stdin redirect 금지 */
        if (!isFirst && t->eType == TOKEN_REDIN) {
            errorPrint("Multiple redirection of standard input", 
                                                              FPRINTF);
            return PIPE_CHECK_ERROR;
        }
    }
    return PIPE_CHECK_SUCCESS;
}

/*--------------------------------------------------------------------*/
/* executePipeline                                                    */
/* Executes a sequence of commands connected by pipes. If there are   */
/* no pipes, it just executes a single command. If there are pipes,   */
/* it creates pipes and forks processes for each command, redirecting */
/* input/output through pipes as needed.                              */
/*                                                                    */
/* Parameters:                                                        */
/*  oTokens: DynArray of tokens representing the entire pipeline      */
/* Returns: none                                                      */
/*--------------------------------------------------------------------*/
static void executePipeline(DynArray_T oTokens) {
    assert(oTokens != NULL);
    int i, j;
    int pipeCount = countPipe(oTokens);
    if (pipeCount == 0) {
        executeCommand(oTokens);
        return;
    }

    int pipes[2][2];
    int curPipe = 0;
    int start = 0;
    
    for (i = 0; i <= pipeCount; i++) {
        DynArray_T cmdTokens = DynArray_new(0);
        if (cmdTokens == NULL) {
            errorPrint("Cannot allocate memory", FPRINTF);
            return;
        }
        
        /* Collect tokens for current command */
        for (j = start; j < DynArray_getLength(oTokens); j++) {
            struct Token *t = DynArray_get(oTokens, j);
            if (t->eType == TOKEN_PIPE) {
                start = j + 1;
                break;
            }
            DynArray_add(cmdTokens, t);
        }
        
        /* Check pipeline syntax */
        if (checkPipeCommand(cmdTokens, i == 0, i == pipeCount)
            == PIPE_CHECK_ERROR) {
            DynArray_free(cmdTokens);
            return;
        }
        
        /* Create pipe if not last command */
        if (i < pipeCount) {
            if (pipe(pipes[curPipe]) == -1) {
                errorPrint("Cannot create pipe", PERROR);
                DynArray_free(cmdTokens);
                return;
            }
        }

        fflush(NULL);  /* Clear all I/O buffers before fork */
        pid_t pid = fork();
        if (pid == -1) {
            errorPrint("Cannot create process", PERROR);
            DynArray_free(cmdTokens);
            return;
        }

        if (pid == 0) {  /* Child process */
            /* input from previous pipe if not first command */
            if (i > 0) {
                dup2(pipes[1-curPipe][0], STDIN_FILENO);
                close(pipes[1-curPipe][0]);
                close(pipes[1-curPipe][1]);
            }
            
            /* output to next pipe if not last command */
            if (i < pipeCount) {
                dup2(pipes[curPipe][1], STDOUT_FILENO);
                close(pipes[curPipe][0]);
                close(pipes[curPipe][1]);
            }

            executeCommand(cmdTokens);
            exit(EXIT_SUCCESS);
        }
        
        /* Parent process */
        if (i > 0) {
            close(pipes[1-curPipe][0]);
            close(pipes[1-curPipe][1]);
        }
        
        curPipe = 1 - curPipe;
        DynArray_free(cmdTokens);
    }

    /* Wait for all children */
    for (i = 0; i <= pipeCount; i++) {
        wait(NULL);
    }
}

/*--------------------------------------------------------------------*/
/* processRCFile                                                      */
/* Reads and executes commands from ~/.ishrc if it exists and is      */
/* readable. Prints each line read from .ishrc with a "% " prefix.    */
/* If .ishrc does not exist or is not readable, it silently does      */
/* nothing.                                                           */
/*                                                                    */
/* Parameters: none                                                   */
/* Returns: none                                                      */
/*--------------------------------------------------------------------*/
static void processRCFile(void) {
    char *home = getenv("HOME");
    if (!home) return;
    
    char rcPath[MAX_LINE_SIZE];
    snprintf(rcPath, sizeof(rcPath), "%s/.ishrc", home);
    
    FILE *rcFile = fopen(rcPath, "r");
    if (!rcFile) return;
    
    char line[MAX_LINE_SIZE];
    while (fgets(line, sizeof(line), rcFile)) {
        if (ferror(rcFile)) {
            errorPrint("Error reading .ishrc", PERROR);
            clearerr(rcFile);
            break;
        }
        printf("%% %s", line);
        fflush(stdout);
        shellHelper(line);
    }
    
    if (ferror(rcFile)) {
        errorPrint("Error reading .ishrc", PERROR);
    }
    
    fclose(rcFile);
}

/*--------------------------------------------------------------------*/
/* shellHelper                                                        */
/* Tokenizes and parses a single line of input, checks for syntax     */
/* errors, and executes commands (builtin or external, possibly with  */
/* pipes).                                                            */
/*                                                                    */
/* Parameters:                                                        */
/*  inLine: the input line to process                                 */
/* Reads/Writes: may read from files if redirection specified, writes */
/* error messages to stderr, commands output to stdout (or redirected)*/
/* Global variables: may affect quitSignalReceived if signals occur   */
/*--------------------------------------------------------------------*/
static void shellHelper(const char *inLine) {
    assert(inLine != NULL);

    DynArray_T oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }
    
    enum LexResult lexcheck = lexLine(inLine, oTokens);
    switch (lexcheck) {
        case LEX_SUCCESS:
            if (DynArray_getLength(oTokens) == 0) {
                DynArray_free(oTokens);
                return;
            }
            
            /* dump lex result when DEBUG is set */
            dumpLex(oTokens);
            
            {
                enum SyntaxResult syncheck = syntaxCheck(oTokens);
                if (syncheck == SYN_SUCCESS) {
                    enum BuiltinType btype = 
                        checkBuiltin(DynArray_get(oTokens, 0));
                    if (btype != NORMAL) {
                        executeBuiltin(oTokens, btype);
                    } else {
                        executePipeline(oTokens);
                    }
                }
                else if (syncheck == SYN_FAIL_NOCMD)
                    errorPrint("Missing command name", FPRINTF);
                else if (syncheck == SYN_FAIL_MULTREDOUT)
                    errorPrint("Multiple redirection of standard out",
                                                              FPRINTF);
                else if (syncheck == SYN_FAIL_NODESTOUT)
                errorPrint("Standard output redirection without file name",
                               FPRINTF);
                else if (syncheck == SYN_FAIL_MULTREDIN)
                    errorPrint("Multiple redirection of standard input",
                               FPRINTF);
                else if (syncheck == SYN_FAIL_NODESTIN)
                errorPrint("Standard input redirection without file name",
                               FPRINTF);
                else if (syncheck == SYN_FAIL_INVALIDBG)
                    errorPrint("Invalid use of background", FPRINTF);
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
    
    DynArray_map(oTokens, freeToken, NULL);
    DynArray_free(oTokens);
}

/*--------------------------------------------------------------------*/
/* main                                                               */
/* Entry point of the ish program. Sets up signal handlers, unblocks  */
/* signals, processes .ishrc, and then enters the main command loop.  */
/*                                                                    */
/* Parameters:                                                        */
/*  argc, argv: standard arguments                                    */
/* Returns: 0 on normal termination, may exit early in signals/exit   */
/*--------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
    /* Set up signal handlers */
    signal(SIGINT, handleSIGINT);
    signal(SIGQUIT, handleSIGQUIT);
    signal(SIGALRM, handleSIGALRM);
    
    /* Unblock signals */
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    
    /* Set program name for error messages */
    errorPrint(argv[0], SETUP);
    
    /* Process .ishrc file */
    processRCFile();
    
    /* Main command loop */
    char acLine[MAX_LINE_SIZE + 2];
    while (1) {
        printf("%% ");
        fflush(stdout);
        
        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }
        
        shellHelper(acLine);
    }
    
    return 0;
}

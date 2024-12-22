#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include "dynarray.h"
#include "token.h"
#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by: Park Ilwoo (and further modified by 20240580 이태윤)    */
/*                                                                    */
/* This file implements the "ish" shell. It supports reading commands  */
/* from .ishrc and from stdin, tokenizing them, parsing pipelines and  */
/* redirections, running built-ins or external commands, and handling  */
/* signals.                                                           */
/* It uses a deterministic finite state automaton (DFA) for lexical    */
/* analysis.                                                          */
/*--------------------------------------------------------------------*/

#define MAXARGS 128
#define MAX_LINE_SIZE 1023

/* Global variable for handling SIGQUIT twice */
static int g_iQuitCount = 0;

/* A structure for each command stage in a pipeline */
typedef struct commandStage {
    char *argv[MAXARGS];
    int argc;
    char *inFile;
    char *outFile;
} commandStage;

/*--------------------------------------------------------------------*/
/* handleSIGQUIT
   Handle SIGQUIT signals. On first SIGQUIT, prompt user to press again 
   within 5 seconds to exit. If pressed again, exit the shell.
   Parameters:
     sig - the received signal number
   Returns: none
   I/O:
     Writes a message to stdout on first SIGQUIT.
   Globals:
     g_iQuitCount is modified.
*/
static void handleSIGQUIT(int sig) {
    (void)sig;
    if (g_iQuitCount == 0) {
        fprintf(stdout, "Type Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        alarm(5);
        g_iQuitCount = 1;
    }
    else {
        exit(0);
    }
}

/*--------------------------------------------------------------------*/
/* handleSIGALRM
   Handle SIGALRM: reset g_iQuitCount after 5 seconds.
   Parameters:
     sig - the signal number
   Returns: none
   Globals:
     g_iQuitCount is reset to 0.
*/
static void handleSIGALRM(int sig) {
    (void)sig;
    g_iQuitCount = 0;
}

/*--------------------------------------------------------------------*/
/* handleSIGINT
   Parent ignores SIGINT. Does nothing here.
   Parameters:
     sig - the signal number
   Returns: none
   Globals: none
*/
static void handleSIGINT(int sig) {
    (void)sig;
}

/*--------------------------------------------------------------------*/
/* doCD
   Implement 'cd' built-in.
   Parameters:
     oTokens - DynArray of tokens.
   Returns: none
   I/O:
     Writes error message to stderr if chdir fails.
   Globals: none
*/
static void doCD(DynArray_T oTokens) {
    int length = DynArray_getLength(oTokens);
    char *dir = NULL;
    assert(oTokens != NULL);
    assert(length >= 1);

    if (length == 1) {
        dir = getenv("HOME");
        if (dir == NULL)
            dir = "/";
    } else {
        struct Token *t = DynArray_get(oTokens, 1);
        assert(t != NULL);
        dir = t->pcValue;
    }

    if (chdir(dir) == -1) {
        errorPrint(dir, PERROR);
    }
}

/*--------------------------------------------------------------------*/
/* doSetenv
   Implement 'setenv' built-in: set an environment variable.
   Format: setenv VAR [VALUE]
   Parameters:
     oTokens - DynArray of tokens.
   Returns: none
   I/O:
     Writes error message if setenv fails.
   Globals: none
*/
static void doSetenv(DynArray_T oTokens) {
    int length = DynArray_getLength(oTokens);
    assert(oTokens != NULL);

    if (length == 1) {
        return;
    }

    struct Token *tVar = DynArray_get(oTokens, 1);
    assert(tVar != NULL);
    char *var = tVar->pcValue;
    char *val = "";
    if (length > 2) {
        struct Token *tVal = DynArray_get(oTokens, 2);
        assert(tVal != NULL);
        val = tVal->pcValue;
    }

    if (setenv(var, val, 1) == -1) {
        errorPrint("setenv", PERROR);
    }
}

/*--------------------------------------------------------------------*/
/* doUnsetenv
   Implement 'unsetenv' built-in.
   Format: unsetenv VAR
   Parameters:
     oTokens - DynArray of tokens.
   Returns: none
   I/O:
     Writes error message if unsetenv fails.
   Globals: none
*/
static void doUnsetenv(DynArray_T oTokens) {
    int length = DynArray_getLength(oTokens);
    assert(oTokens != NULL);

    if (length < 2) {
        return;
    }

    struct Token *tVar = DynArray_get(oTokens, 1);
    assert(tVar != NULL);
    char *var = tVar->pcValue;

    if (unsetenv(var) == -1) {
        errorPrint("unsetenv", PERROR);
    }
}

/*--------------------------------------------------------------------*/
/* tryBuiltin
   Check if command is a built-in and execute it if so.
   Also verify no redirections are used with built-ins.
   Parameters:
     oTokens - DynArray of tokens for the command.
   Returns:
     1 if built-in executed, 0 otherwise.
   I/O:
     Error messages to stderr if needed.
   Globals: none
*/
static int tryBuiltin(DynArray_T oTokens) {
    struct Token *t = DynArray_get(oTokens, 0);
    assert(t != NULL);
    enum BuiltinType btype = checkBuiltin(t);
    if (btype == NORMAL)
        return 0;

    int length = DynArray_getLength(oTokens);
    for (int i = 1; i < length; i++) {
        struct Token *tk = DynArray_get(oTokens, i);
        if (tk->eType == TOKEN_REDIN || tk->eType == TOKEN_REDOUT) {
            errorPrint("redirection not permitted for built-in commands",
                       FPRINTF);
            return 1;
        }
    }

    switch (btype) {
        case B_CD:
            doCD(oTokens);
            break;
        case B_SETENV:
            doSetenv(oTokens);
            break;
        case B_USETENV:
            doUnsetenv(oTokens);
            break;
        case B_EXIT:
            exit(0);
            break;
        case B_FG:
        case B_ALIAS:
        default:
            break;
    }
    return 1;
}

/*--------------------------------------------------------------------*/
/* parsePipelines
   Parse tokens into command stages separated by '|'.
   Also parse input/output redirections.
   Parameters:
     oTokens - DynArray of tokens.
     stagesPtr - pointer to array of commandStage
     stageCount - pointer to int for number of stages
   Returns:
     1 on success, 0 on failure.
   I/O:
     Error messages to stderr if parsing fails.
   Globals: none
*/
static int parsePipelines(DynArray_T oTokens, commandStage **stagesPtr,
                          int *stageCount) {
    int length = DynArray_getLength(oTokens);
    assert(oTokens != NULL);

    int pipeCount = countPipe(oTokens);
    *stageCount = pipeCount + 1;
    commandStage *stages = calloc(*stageCount, sizeof(commandStage));
    if (stages == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        return 0;
    }

    for (int i = 0; i < *stageCount; i++) {
        stages[i].argc = 0;
        stages[i].inFile = NULL;
        stages[i].outFile = NULL;
    }

    int stageIndex = 0;
    commandStage *current = &stages[0];

    for (int j = 0; j < length; j++) {
        struct Token *t = DynArray_get(oTokens, j);
        assert(t != NULL);
        if (t->eType == TOKEN_WORD) {
            if (current->argc < MAXARGS - 1) {
                current->argv[current->argc++] = t->pcValue;
            } else {
                errorPrint("Command is too large", FPRINTF);
                free(stages);
                return 0;
            }
        }
        else if (t->eType == TOKEN_PIPE) {
            stageIndex++;
            if (stageIndex >= *stageCount) {
                errorPrint("Missing command name", FPRINTF);
                free(stages);
                return 0;
            }
            current = &stages[stageIndex];
        }
        else if (t->eType == TOKEN_REDIN) {
            if (j+1 >= length) {
                errorPrint("Standard input redirection without file name",
                           FPRINTF);
                free(stages);
                return 0;
            }
            struct Token *f = DynArray_get(oTokens, j+1);
            if (f->eType != TOKEN_WORD) {
                errorPrint("Standard input redirection without file name",
                           FPRINTF);
                free(stages);
                return 0;
            }
            if (current->inFile != NULL) {
                errorPrint("Multiple redirection of standard input", FPRINTF);
                free(stages);
                return 0;
            }
            current->inFile = f->pcValue;
            j++;
        }
        else if (t->eType == TOKEN_REDOUT) {
            if (j+1 >= length) {
                errorPrint("Standard output redirection without file name",
                           FPRINTF);
                free(stages);
                return 0;
            }
            struct Token *f = DynArray_get(oTokens, j+1);
            if (f->eType != TOKEN_WORD) {
                errorPrint("Standard output redirection without file name",
                           FPRINTF);
                free(stages);
                return 0;
            }
            if (current->outFile != NULL) {
                errorPrint("Multiple redirection of standard out", FPRINTF);
                free(stages);
                return 0;
            }
            current->outFile = f->pcValue;
            j++;
        }
        else if (t->eType == TOKEN_BG) {
            /* Background command not implemented */
        }
    }

    for (int i = 0; i < *stageCount; i++) {
        current = &stages[i];
        current->argv[current->argc] = NULL;
        if (current->argc == 0) {
            errorPrint("Missing command name", FPRINTF);
            free(stages);
            return 0;
        }
    }

    *stagesPtr = stages;
    return 1;
}

/*--------------------------------------------------------------------*/
/* runStage
   Fork and exec a single command stage.
   Parameters:
     stg - commandStage
     inFd, outFd - file descriptors for input/output (if no file given)
   Returns:
     pid of child or -1 on error.
   I/O:
     May print error messages if exec or open fails.
   Globals: none
*/
static pid_t runStage(commandStage *stg, int inFd, int outFd) {
    assert(stg != NULL);
    fflush(NULL);
    pid_t pid = fork();
    if (pid == -1) {
        errorPrint("fork", PERROR);
        return -1;
    }
    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        if (stg->inFile != NULL) {
            int fd = open(stg->inFile, O_RDONLY);
            if (fd == -1) {
                errorPrint(stg->inFile, PERROR);
                exit(1);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        } else if (inFd != -1) {
            dup2(inFd, STDIN_FILENO);
        }

        if (stg->outFile != NULL) {
            int fd = open(stg->outFile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd == -1) {
                errorPrint(stg->outFile, PERROR);
                exit(1);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        } else if (outFd != -1) {
            dup2(outFd, STDOUT_FILENO);
        }

        if (inFd != -1) close(inFd);
        if (outFd != -1) close(outFd);

        execvp(stg->argv[0], stg->argv);
        errorPrint(stg->argv[0], PERROR);
        exit(1);
    }
    return pid;
}

/*--------------------------------------------------------------------*/
/* executePipeline
   Execute a pipeline of command stages.
   Parameters:
     stages - array of commandStage
     stageCount - number of stages
   Returns: none
   I/O:
     May print error messages.
   Globals: none
*/
static void executePipeline(commandStage *stages, int stageCount) {
    assert(stages != NULL);
    int prevFd = -1;
    pid_t *pids = calloc(stageCount, sizeof(pid_t));
    if (pids == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        return;
    }

    for (int i = 0; i < stageCount; i++) {
        int pipefd[2];
        int outFd = -1;
        if (i < stageCount - 1) {
            if (pipe(pipefd) == -1) {
                errorPrint("pipe", PERROR);
                free(pids);
                return;
            }
            outFd = pipefd[1];
        }

        pid_t pid = runStage(&stages[i], prevFd, outFd);
        if (pid == -1) {
            free(pids);
            return;
        }
        pids[i] = pid;

        if (prevFd != -1) close(prevFd);
        if (outFd != -1) close(outFd);

        if (i < stageCount - 1) {
            prevFd = pipefd[0];
        }
    }

    for (int i = 0; i < stageCount; i++) {
        waitpid(pids[i], NULL, 0);
    }
    free(pids);
}

/*--------------------------------------------------------------------*/
/* executeExternal
   Execute an external command (with optional pipes and redirection).
   Parameters:
     oTokens - DynArray of tokens
   Returns: none
   I/O:
     May print errors if failures occur.
   Globals: none
*/
static void executeExternal(DynArray_T oTokens) {
    commandStage *stages = NULL;
    int stageCount = 0;

    if (!parsePipelines(oTokens, &stages, &stageCount)) {
        return;
    }

    if (stageCount == 1) {
        fflush(NULL);
        pid_t pid = fork();
        if (pid == -1) {
            errorPrint("fork", PERROR);
            free(stages);
            return;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            if (stages[0].inFile != NULL) {
                int fd = open(stages[0].inFile, O_RDONLY);
                if (fd == -1) {
                    errorPrint(stages[0].inFile, PERROR);
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (stages[0].outFile != NULL) {
                int fd = open(stages[0].outFile,
                              O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (fd == -1) {
                    errorPrint(stages[0].outFile, PERROR);
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            execvp(stages[0].argv[0], stages[0].argv);
            errorPrint(stages[0].argv[0], PERROR);
            exit(1);
        } else {
            wait(NULL);
        }
    } else {
        executePipeline(stages, stageCount);
    }

    free(stages);
}

/*--------------------------------------------------------------------*/
/* runIshrc
   Execute commands from $HOME/.ishrc if it exists and is readable.
   Parameters: none
   Returns: none
   I/O:
     Prints each line read and may print error messages.
   Globals: none
*/
static void runIshrc() {
    char *home = getenv("HOME");
    if (home == NULL) return;
    char path[1024];
    snprintf(path, 1024, "%s/.ishrc", home);

    FILE *fp = fopen(path, "r");
    if (fp == NULL)
        return;

    char line[MAX_LINE_SIZE+2];
    while (fgets(line, MAX_LINE_SIZE+2, fp) != NULL) {
        fprintf(stdout, "%% %s", line);
        fflush(stdout);

        DynArray_T oTokens = DynArray_new(0);
        if (oTokens == NULL) {
            errorPrint("Cannot allocate memory", FPRINTF);
            continue;
        }

        enum LexResult lexcheck = lexLine(line, oTokens);
        if (lexcheck == LEX_SUCCESS) {
            if (DynArray_getLength(oTokens) > 0) {
                enum SyntaxResult syncheck = syntaxCheck(oTokens);
                if (syncheck == SYN_SUCCESS) {
                    if (!tryBuiltin(oTokens)) {
                        executeExternal(oTokens);
                    }
                } else {
                    if (syncheck == SYN_FAIL_NOCMD)
                        errorPrint("Missing command name", FPRINTF);
                    else if (syncheck == SYN_FAIL_MULTREDOUT)
                        errorPrint("Multiple redirection of standard out",
                                   FPRINTF);
                    else if (syncheck == SYN_FAIL_NODESTOUT)
                        errorPrint("Standard output redirection without "
                                   "file name", FPRINTF);
                    else if (syncheck == SYN_FAIL_MULTREDIN)
                        errorPrint("Multiple redirection of standard input",
                                   FPRINTF);
                    else if (syncheck == SYN_FAIL_NODESTIN)
                        errorPrint("Standard input redirection without "
                                   "file name", FPRINTF);
                    else if (syncheck == SYN_FAIL_INVALIDBG)
                        errorPrint("Invalid use of background", FPRINTF);
                }
            }
        } else {
            if (lexcheck == LEX_QERROR)
                errorPrint("Unmatched quote", FPRINTF);
            else if (lexcheck == LEX_NOMEM)
                errorPrint("Cannot allocate memory", FPRINTF);
            else if (lexcheck == LEX_LONG)
                errorPrint("Command is too large", FPRINTF);
            else {
                errorPrint("lexLine needs to be fixed", FPRINTF);
            }
        }

        DynArray_map(oTokens, freeToken, NULL);
        DynArray_free(oTokens);
    }

    fclose(fp);
}

/*--------------------------------------------------------------------*/
/* shellHelper
   Process a single input line: tokenize, parse, run built-in or external.
   Parameters:
     inLine - input command line
   Returns: none
   I/O:
     Prints errors if needed. Executes the command.
   Globals: none
*/
static void shellHelper(const char *inLine) {
    DynArray_T oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        return;
    }

    enum LexResult lexcheck = lexLine(inLine, oTokens);
    switch (lexcheck) {
        case LEX_SUCCESS:
            if (DynArray_getLength(oTokens) == 0) {
                DynArray_free(oTokens);
                return;
            }

            dumpLex(oTokens);
            {
                enum SyntaxResult syncheck = syntaxCheck(oTokens);
                if (syncheck == SYN_SUCCESS) {
                    if (!tryBuiltin(oTokens)) {
                        executeExternal(oTokens);
                    }
                } else {
                    if (syncheck == SYN_FAIL_NOCMD)
                        errorPrint("Missing command name", FPRINTF);
                    else if (syncheck == SYN_FAIL_MULTREDOUT)
                        errorPrint("Multiple redirection of standard out",
                                   FPRINTF);
                    else if (syncheck == SYN_FAIL_NODESTOUT)
                        errorPrint("Standard output redirection without "
                                   "file name", FPRINTF);
                    else if (syncheck == SYN_FAIL_MULTREDIN)
                        errorPrint("Multiple redirection of standard input",
                                   FPRINTF);
                    else if (syncheck == SYN_FAIL_NODESTIN)
                        errorPrint("Standard input redirection without "
                                   "file name", FPRINTF);
                    else if (syncheck == SYN_FAIL_INVALIDBG)
                        errorPrint("Invalid use of background", FPRINTF);
                }
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
/* main
   Entry point: set up signals, run .ishrc, then enter interactive loop.
   Print "% " prompt, read line, process it via shellHelper.
   On EOF or 'exit', terminate.
   Parameters:
     argc, argv - standard arguments
   Returns:
     0 on success
   I/O:
     Prints prompt, reads user input, may print errors.
   Globals:
     g_iQuitCount is modified by signal handlers.
*/
int main(int argc, char *argv[]) {
    (void)argc;
    errorPrint(argv[0], SETUP);

    sigprocmask(SIG_UNBLOCK, NULL, NULL);
    signal(SIGINT, handleSIGINT);
    signal(SIGQUIT, handleSIGQUIT);
    signal(SIGALRM, handleSIGALRM);

    runIshrc();

    char acLine[MAX_LINE_SIZE + 2];
    while (1) {
        fprintf(stdout, "%% ");
        fflush(stdout);
        if (fgets(acLine, MAX_LINE_SIZE+2, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }
        shellHelper(acLine);
    }

    return 0;
}

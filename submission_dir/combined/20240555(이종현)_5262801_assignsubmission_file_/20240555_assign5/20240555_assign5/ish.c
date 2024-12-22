/**
 *********************************************************************
 * @file    ish.c
 * @brief   EE209 Assignment 5: A Unix Shell
 * @author  Jonghyeon Lee (20240555)
 *********************************************************************
 * @details
 *
 * ish - interaactive unix shell
 * Original Author: Bob Dondero
 * Modified by : Park Ilwoo
 * Illustrate lexical analysis using a deterministic finite state
 * automaton (DFA)
 *
 *********************************************************************
 **/

/* Includes --------------------------------------------------------*/

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lexsyn.h"
#include "util.h"

/* Global Variables ------------------------------------------------*/

int childPID, FLAG_QUIT_REQUEST, FLAG_STARTUP;

/* Private Function Prototypes -------------------------------------*/

void freeTokenDynArray(DynArray_T array);
static void shellHelper(const char *inLine);

/* Signal Handlers -------------------------------------------------*/

void sigINTHandler() {
    if (childPID != 0) kill(childPID, SIGINT);
}

void sigQuitHandler() {
    if (childPID != 0) kill(childPID, SIGQUIT);
    if (FLAG_QUIT_REQUEST) {
        exit(0);
    } else {
        FLAG_QUIT_REQUEST = 1;
        fprintf(stdout,
                "\nType Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        alarm(5);
    }
}

void sigALRMHandler() { FLAG_QUIT_REQUEST = 0; }

/* Built-in Command Handlers ---------------------------------------*/

/**
 * @brief 'cd' command handler
 * @retval None
 **/
void cmdCDhandler(DynArray_T oTokens) {
    assert(oTokens);

    char *cwd;
    if (DynArray_getLength(oTokens) > 2) {
        errorPrint("cd takes one parameter", FPRINTF);
        freeTokenDynArray(oTokens);
        return;
    } else if (DynArray_getLength(oTokens) == 1) {
        /* No param, cd to home */
        cwd = getenv("HOME");
    } else {
        cwd = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
    }
    if (access(cwd, R_OK) == 0) {
        if (chdir(cwd) == -1) errorPrint(NULL, PERROR);
    } else {
        errorPrint(NULL, PERROR);
    }
    freeTokenDynArray(oTokens);
}

/**
 * @brief 'setenv' command handler
 * @retval None
 **/
void cmdSETENVhandler(DynArray_T oTokens) {
    assert(oTokens);

    if (DynArray_getLength(oTokens) > 3) {
        errorPrint("setenv takes one or two parameters", FPRINTF);
    } else if (DynArray_getLength(oTokens) == 3) {
        if (setenv(
                ((struct Token *)DynArray_get(oTokens, 1))->pcValue,
                ((struct Token *)DynArray_get(oTokens, 2))->pcValue,
                1) == -1)
            errorPrint(NULL, PERROR);
    } else if (DynArray_getLength(oTokens) == 2) {
        if (setenv(
                ((struct Token *)DynArray_get(oTokens, 1))->pcValue,
                "", 1) == -1)
            errorPrint(NULL, PERROR);
    } else if (DynArray_getLength(oTokens) == 1) {
        errorPrint("setenv takes one or two parameters", FPRINTF);
    }
    freeTokenDynArray(oTokens);
}

void cmdUNSETENVhandler(DynArray_T oTokens) {
    assert(oTokens);

    if (DynArray_getLength(oTokens) > 2) {
        errorPrint("unsetenv takes one parameter", FPRINTF);
    } else if (DynArray_getLength(oTokens) == 2) {
        if (unsetenv(((struct Token *)DynArray_get(oTokens, 1))
                         ->pcValue) == -1)
            errorPrint(NULL, PERROR);
    } else if (DynArray_getLength(oTokens) == 1) {
        errorPrint("unsetenv takes one parameter", FPRINTF);
    }
    freeTokenDynArray(oTokens);
}

void cmdEXIThandler(DynArray_T oTokens) {
    assert(oTokens);

    if (DynArray_getLength(oTokens) > 1) {
        errorPrint("exit does not take any parameters", FPRINTF);
        freeTokenDynArray(oTokens);
        return;
    } else {
        freeTokenDynArray(oTokens);
        exit(EXIT_SUCCESS);
    }
}

/* General Command Handler -----------------------------------------*/

/**
 * @brief convert tokens to argv array / extract STDIN/STDOUT path
 * @retval DynArray_T
 * @return converted argv array
 **/
DynArray_T parseTokens(DynArray_T oTokens, int *REDIN, int *REDOUT,
                       char **PIN, char **POUT) {
    assert(oTokens);

    DynArray_T argv = DynArray_new(0);
    if (argv == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        freeTokenDynArray(oTokens);
        exit(EXIT_FAILURE);
    }
    DynArray_add(argv,
                 ((struct Token *)DynArray_get(oTokens, 0))->pcValue);
    for (int i = 1; i < DynArray_getLength(oTokens); i++) {
        if (((struct Token *)DynArray_get(oTokens, i))->eType ==
            TOKEN_REDIN) {
            *REDIN = 1;
            continue;
        } else if (((struct Token *)DynArray_get(oTokens, i))
                       ->eType == TOKEN_REDOUT) {
            *REDOUT = 1;
            continue;
        } else if (*REDIN && *PIN == NULL) {
            *PIN =
                ((struct Token *)DynArray_get(oTokens, i))->pcValue;
            continue;
        } else if (*REDOUT && *POUT == NULL) {
            *POUT =
                ((struct Token *)DynArray_get(oTokens, i))->pcValue;
            continue;
        }
        if (((struct Token *)DynArray_get(oTokens, i))->eType ==
            TOKEN_WORD) {
            DynArray_add(
                argv,
                ((struct Token *)DynArray_get(oTokens, i))->pcValue);
        } else if (((struct Token *)DynArray_get(oTokens, i))
                       ->eType == TOKEN_PIPE) {
            DynArray_add(argv, NULL);
        }
    }
    return argv;
}

/**
 * @brief general command handler
 * @retval None
 **/
void commandhandler(DynArray_T oTokens) {
    assert(oTokens);

    /* parse stdin & stdout redirection */
    int REDIRECT_STDIN = 0, REDIRECT_STDOUT = 0;
    char *P_STDIN = NULL, *P_STDOUT = NULL;
    DynArray_T argv =
        parseTokens(oTokens, &REDIRECT_STDIN, &REDIRECT_STDOUT,
                    &P_STDIN, &P_STDOUT);

    /* parse (count, split) piped processes*/
    int PIPE_COUNT = 1;
    char **cp_argv = (char **)malloc(sizeof(char *) *
                                     (DynArray_getLength(argv) + 1));
    if (cp_argv == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        freeTokenDynArray(oTokens);
        exit(EXIT_FAILURE);
    }
    DynArray_toArray(argv, (void **)cp_argv);
    cp_argv[DynArray_getLength(argv)] = NULL;
    for (int i = 0, l = DynArray_getLength(argv); i < l; i++) {
        if (cp_argv[i] == NULL) PIPE_COUNT++;
    }
    int *PIPE_SIDX = (int *)malloc(sizeof(int) * PIPE_COUNT);
    if (PIPE_SIDX == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        freeTokenDynArray(oTokens);
        exit(EXIT_FAILURE);
    }
    for (int SIDX_PEND = 1, k = 0, i = 0,
             l = DynArray_getLength(argv);
         i < l; i++) {
        if (SIDX_PEND) {
            PIPE_SIDX[k++] = i;
            SIDX_PEND = 0;
        }
        if (cp_argv[i] == NULL) SIDX_PEND = 1;
    }

    /* execute */
    int p[2], q[2];
    for (int i = 0; i < PIPE_COUNT; i++) {
        if (i < PIPE_COUNT - 1) {
            if (pipe(q) == -1) {
                exit(EXIT_FAILURE);
            }
        }
        fflush(NULL);
        pid_t pid = fork();
        if (pid == 0) {
            /* child process */
            if (FLAG_STARTUP) close(STDERR_FILENO + 1);
            int fi, fo;
            if (i == 0) {
                if (REDIRECT_STDIN) {
                    fi = open(P_STDIN, O_RDONLY);
                    if (fi == -1) {
                        errorPrint(NULL, PERROR);
                        exit(EXIT_FAILURE);
                    }
                    dup2(fi, STDIN_FILENO);
                    close(fi);
                }
            } else {
                // pipe p -> stdin
                dup2(p[0], STDIN_FILENO);
                close(p[0]);
            }
            if (i == PIPE_COUNT - 1) {
                if (REDIRECT_STDOUT) {
                    fo = creat(P_STDOUT, 0600);
                    if (fo == -1) {
                        errorPrint(NULL, PERROR);
                        exit(EXIT_FAILURE);
                    }
                    dup2(fo, STDOUT_FILENO);
                    close(fo);
                }
            } else {
                // stdout -> pipe q
                dup2(q[1], STDOUT_FILENO);
                close(q[0]);
                close(q[1]);
            }
            execvp(cp_argv[PIPE_SIDX[i]], &cp_argv[PIPE_SIDX[i]]);
            errorPrint(cp_argv[PIPE_SIDX[i]], PERROR);
            exit(EXIT_FAILURE);
        } else {
            /* parent process */
            childPID = (int)pid;
            int status;
            wait(&status);
            if (i > 0) {
                close(p[0]);
            }
            if (i < PIPE_COUNT - 1) {
                p[0] = q[0];
                p[1] = q[1];
                close(p[1]);
            }
        }
    }

    /* cleanup & return */
    childPID = 0;
    free(cp_argv);
    free(PIPE_SIDX);
    DynArray_free(argv);
    freeTokenDynArray(oTokens);
    return;
}

/* Helper Functions ------------------------------------------------*/

/**
 * @brief free token array
 * @retval None
 **/
void freeTokenDynArray(DynArray_T array) {
    assert(array);
    for (int i = 0; i < DynArray_getLength(array); i++) {
        freeToken(DynArray_get(array, i), NULL);
    }
    DynArray_free(array);
}

/**
 * @brief command processing helper
 * @retval None
 **/
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
            if (DynArray_getLength(oTokens) == 0) {
                freeTokenDynArray(oTokens);
                return;
            }

            /* no BG Token this time :( */
            for (int i = 0, l = DynArray_getLength(oTokens); i < l; i++) {
                if (((struct Token *)DynArray_get(oTokens, i))
                       ->eType == TOKEN_BG) {
                    struct Token *nTkn;
                    if ((nTkn = makeToken(TOKEN_WORD, "&")) == NULL) {
                        errorPrint("Cannot allocate memory", FPRINTF);
                        freeTokenDynArray(oTokens);
                        exit(EXIT_FAILURE);
                    }
                    freeToken(DynArray_get(oTokens, i), NULL);
                    DynArray_set(oTokens, i, nTkn);
                }
            }

            /* dump lex result when DEBUG is set */
            dumpLex(oTokens);

            syncheck = syntaxCheck(oTokens);

            if (syncheck == SYN_SUCCESS) {
                btype = checkBuiltin(DynArray_get(oTokens, 0));
                switch (btype) {
                    case B_CD:  // 'cd' command
                        cmdCDhandler(oTokens);
                        break;
                    case B_EXIT:  // 'exit' command
                        cmdEXIThandler(oTokens);
                        break;
                    case B_SETENV:  // 'setenv' command
                        cmdSETENVhandler(oTokens);
                        break;
                    case B_USETENV:  // 'unsetenv' command
                        cmdUNSETENVhandler(oTokens);
                        break;
                    case B_FG:
                    case B_ALIAS:
                    case NORMAL:
                        commandhandler(oTokens);
                        break;
                    default:
                        errorPrint("syntaxCheck needs to be fixed",
                                   FPRINTF);
                        freeTokenDynArray(oTokens);
                        exit(EXIT_FAILURE);
                }
                break;
            }
            /* syntax error cases */
            else if (syncheck == SYN_FAIL_NOCMD)
                errorPrint("Missing command name", FPRINTF);
            else if (syncheck == SYN_FAIL_MULTREDOUT)
                errorPrint("Multiple redirection of standard out",
                           FPRINTF);
            else if (syncheck == SYN_FAIL_NODESTOUT)
                errorPrint(
                    "Standard output redirection without file name",
                    FPRINTF);
            else if (syncheck == SYN_FAIL_MULTREDIN)
                errorPrint("Multiple redirection of standard input",
                           FPRINTF);
            else if (syncheck == SYN_FAIL_NODESTIN)
                errorPrint(
                    "Standard input redirection without file name",
                    FPRINTF);
            else if (syncheck == SYN_FAIL_INVALIDBG)
                errorPrint("Invalid use of background", FPRINTF);
            freeTokenDynArray(oTokens);
            break;

        case LEX_QERROR:
            errorPrint("Unmatched quote", FPRINTF);
            freeTokenDynArray(oTokens);
            break;

        case LEX_NOMEM:
            errorPrint("Cannot allocate memory", FPRINTF);
            freeTokenDynArray(oTokens);
            break;

        case LEX_LONG:
            errorPrint("Command is too large", FPRINTF);
            freeTokenDynArray(oTokens);
            break;

        default:
            errorPrint("lexLine needs to be fixed", FPRINTF);
            freeTokenDynArray(oTokens);
            exit(EXIT_FAILURE);
    }
}

/**
 * @brief Application Entry Point
 * @retval int
 * @return 0 on successful exit
 **/
int main(int argc, char *argv[]) {
    /* SETUP */
    errorPrint(argv[0], SETUP);
    childPID = 0;
    FLAG_QUIT_REQUEST = 0;
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &mask, NULL);
    signal(SIGINT, sigINTHandler);
    signal(SIGQUIT, sigQuitHandler);
    signal(SIGALRM, sigALRMHandler);
    char acLine[MAX_LINE_SIZE + 2];

    /* .ishrc execution */
    FLAG_STARTUP = 1;
    char *ishrc =
        (char *)malloc(strlen(getenv("HOME")) + 10);  // ishrc path
    if (ishrc == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }
    ishrc[0] = '\0';
    strcat(ishrc, getenv("HOME"));
    strcat(ishrc, "/.ishrc");
    int fi = open(ishrc, O_RDONLY);
    if (fi != -1) {
        FILE *fp = fdopen(fi, "r");
        if (fp != NULL) {
            while (1) {
                if (fgets(acLine, MAX_LINE_SIZE, fp) == NULL) {
                    break;
                }
                fprintf(stdout, "%% %s", acLine);
                if (acLine[strlen(acLine) - 1] != '\n')
                    fprintf(stdout, "\n");
                fflush(stdout);
                shellHelper(acLine);
            }
            fclose(fp);
        }
        close(fi);
        fflush(NULL);
        clearerr(stdin);
    }
    free(ishrc);
    FLAG_STARTUP = 0;

    /* infinite loop */
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

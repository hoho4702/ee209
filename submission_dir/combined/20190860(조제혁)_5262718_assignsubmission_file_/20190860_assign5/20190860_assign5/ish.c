#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

#include "lexsyn.h"
#include "util.h"
#include "execute.h"

static void shellHelper(const char *inLine);

/* global signal handling var */
static int quitSignalCount = 0;
static int alarmTriggered = 0;

/* singal handlers */
static void handleSIGQUIT(int sig) {
    (void)sig;  /* suppressing warning */
    
    if (quitSignalCount == 0) {
        printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        quitSignalCount = 1;
        alarm(5);
        signal(SIGQUIT, handleSIGQUIT);  /* handler again */
    } else {
        printf("\n");
        exit(EXIT_SUCCESS);
    }
}

static void handleSIGALRM(int sig) {
    (void)sig;  /* suppressing warning */
    alarmTriggered = 1;
    quitSignalCount = 0;
    signal(SIGALRM, handleSIGALRM);  /* handler again */
}

static void setupSignalHandlers(void) {
    /* preparing sigprocmask */
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &mask, NULL);

    /* signal handlers */
    signal(SIGINT, SIG_IGN);  /* parent ignores SIGINT */
    signal(SIGQUIT, handleSIGQUIT);
    signal(SIGALRM, handleSIGALRM);
}

static void processIshrc(void) {
    char acLine[MAX_LINE_SIZE + 2];
    char *home;
    char *ishrcPath;
    FILE *ishrc;
    size_t pathLen;

    /* get HOME directory */
    home = getenv("HOME");
    if (!home) return;  /* if HOME not set skip .ishrc processing */

    pathLen = strlen(home) + 8;  /* +8 for "/.ishrc\0" */
    ishrcPath = malloc(pathLen);
    if (!ishrcPath) return;

    /* safely combine path with size limit */
    snprintf(ishrcPath, pathLen, "%s/.ishrc", home);

    ishrc = fopen(ishrcPath, "r");
    free(ishrcPath);
    
    if (!ishrc) return;  /* ishrc doesn't exist/nonreadable */

    while (fgets(acLine, MAX_LINE_SIZE, ishrc) != NULL) {
        printf("%% %s", acLine);
        fflush(stdout);
        shellHelper(acLine);
    }

    fclose(ishrc);
}

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

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
            if (DynArray_getLength(oTokens) == 0) {
                DynArray_map(oTokens, freeToken, NULL);
                DynArray_free(oTokens);
                return;
            }

            /* dump lex result when DEBUG is set */
            dumpLex(oTokens);

            syncheck = syntaxCheck(oTokens);
            if (syncheck == SYN_SUCCESS) {
                btype = checkBuiltin(DynArray_get(oTokens, 0));
                switch (btype) {
                    case B_EXIT:
                        DynArray_map(oTokens, freeToken, NULL);
                        DynArray_free(oTokens);
                        exit(EXIT_SUCCESS);
                        break;
                    case B_SETENV:
                    case B_USETENV:
                    case B_CD:
                    case NORMAL:
                        {
                            enum ExecResult execResult;
                            /* check for pipes */
                            if (countPipe(oTokens) > 0) {
                                execResult = executePipeline(oTokens);
                            } else {
                                execResult = executeCommand(oTokens);
                            }
                            switch (execResult) {
                                case EXEC_SUCCESS:
                                    break;
                                case EXEC_FAIL_PERMISSION:
                                    errorPrint("Permission denied", FPRINTF);
                                    break;
                                case EXEC_FAIL_NOT_FOUND:
                                    errorPrint("Command not found", FPRINTF);
                                    break;
                                case EXEC_FAIL_NO_MEM:
                                    errorPrint("Cannot allocate memory", FPRINTF);
                                    break;
                                case EXEC_FAIL_FORK:
                                    errorPrint("Cannot create child process", FPRINTF);
                                    break;
                                case EXEC_FAIL_PIPE:
                                    errorPrint("Pipe error", FPRINTF);
                                    break;
                                case EXEC_FAIL_DUP:
                                    errorPrint("Redirection error", FPRINTF);
                                    break;
                                case EXEC_FAIL_INVALID_ARGS:
                                    break;
                                default:
                                    break;
                            }
                        }
                        break;
                    default:
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

    /* clean up */
    DynArray_map(oTokens, freeToken, NULL);
    DynArray_free(oTokens);
}

int main(int argc, char *argv[]) {
    char acLine[MAX_LINE_SIZE + 2];
    
    (void)argc;  /* suppressing warning */
    
    /* set up program name for error messages */
    errorPrint(argv[0], SETUP);

    /* set up signal handlers */
    setupSignalHandlers();

    /* process .ishrc if it exists */
    processIshrc();

    while (1) {
        char *result;

        /* reset signal */
        if (alarmTriggered) {
            quitSignalCount = 0;
            alarmTriggered = 0;
            continue;
        }
        
        /* print prompt if not handling a signal */
        if (!quitSignalCount) {
            printf("%% ");
            fflush(stdout);
        }
        
        result = fgets(acLine, MAX_LINE_SIZE, stdin);

        if (result == NULL) {
            if (feof(stdin)) {
                printf("\n");
                exit(EXIT_SUCCESS);
            }
            if (errno == EINTR) {
                /* input interrupted */
                continue;
            }
            errorPrint("Error reading command", FPRINTF);
            exit(EXIT_FAILURE);
        }

        shellHelper(acLine);
    }
}

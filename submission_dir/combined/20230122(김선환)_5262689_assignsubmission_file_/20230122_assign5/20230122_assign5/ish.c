
#include <stdio.h>
#include <stdlib.h>

#include "lexsyn.h"
#include "util.h"
#include "dynarray.h"
#include "token.h"

#define _GNU_SOURCE
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>




/* 0이면 첫 번째 Ctrl-\ 대기 /// 1이면 두 번째 Ctrl-\ 대기 */
static int gPendingQuitSignal = 0;

static void setupSignals(void);
static void sigHandlerAlarm(int sig);
static void sigHandlerQuit(int sig);
static void ignoreParentSIGINT(void);

static void handleSingleCommandLine(const char *inputLine, const char *progName);
static void handleBuiltinCd(DynArray_T tokens, const char *progName);
static void handleBuiltinSetenv(DynArray_T tokens, const char *progName);
static void handleBuiltinUnsetenv(DynArray_T tokens, const char *progName);

static void invokeSingleCommandWithRedirection(DynArray_T cmdTokens, const char *progName);
static void processPipedCommands(DynArray_T tokenList, const char *progName);

static void initializeFromIshrc(const char *progName);


static void sigHandlerAlarm(int sig) {
    (void)sig;
    gPendingQuitSignal = 0;
}

static void sigHandlerQuit(int sig) {
    (void)sig;
    if (gPendingQuitSignal == 0) {
        fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        alarm(5);
        gPendingQuitSignal = 1;
    } else {
        exit(EXIT_SUCCESS);
    }
}

/* 부모 프로세스에서는 무시----------------------*/
static void ignoreParentSIGINT(void) {
    signal(SIGINT, SIG_IGN);
}

/* SIGINT, SIGQUIT, SIGALRM 핸들러--------------------- */
static void setupSignals(void) {
    sigset_t sSet;
    sigemptyset(&sSet);
    sigaddset(&sSet, SIGALRM);
    sigaddset(&sSet, SIGINT);
    sigaddset(&sSet, SIGQUIT);
    sigprocmask(SIG_UNBLOCK, &sSet, NULL);

    signal(SIGALRM, sigHandlerAlarm);
    signal(SIGQUIT, sigHandlerQuit);
    ignoreParentSIGINT();
}


static void handleBuiltinCd(DynArray_T tokens, const char *progName) {
    assert(tokens != NULL);
    int length = DynArray_getLength(tokens);
    if (length > 2) {
        fprintf(stderr, "%s: cd takes one parameter\n", progName);
        return;
    }

    const char *dest = NULL;
    if (length == 1) {
        dest = getenv("HOME");
        if (dest == NULL) {
            fprintf(stderr, "%s: cd: HOME not set\n", progName);
            return;
        }
    } else {
        struct Token *argToken = DynArray_get(tokens, 1);
        dest = argToken->pcValue;
    }

    if (chdir(dest) != 0) {
        fprintf(stderr, "%s: %s: %s\n", progName, dest, strerror(errno));
    }
}

static void handleBuiltinSetenv(DynArray_T tokens, const char *progName) {
    assert(tokens != NULL);
    int len = DynArray_getLength(tokens);
    if (len < 2 || len > 3) {
        fprintf(stderr, "%s: setenv takes one or two parameters\n", progName);
        return;
    }

    struct Token *varTk = DynArray_get(tokens, 1);
    const char *val = (len == 3) ? ((struct Token*)DynArray_get(tokens, 2))->pcValue : "";

    if (setenv(varTk->pcValue, val, 1) != 0) {
        fprintf(stderr, "%s: setenv: %s\n", progName, strerror(errno));
    }
}

static void handleBuiltinUnsetenv(DynArray_T tokens, const char *progName) {
    assert(tokens != NULL);
    if (DynArray_getLength(tokens) != 2) {
        fprintf(stderr, "%s: unsetenv takes one parameter\n", progName);
        return;
    }

    struct Token *vToken = DynArray_get(tokens, 1);
    if (unsetenv(vToken->pcValue) != 0) {
        fprintf(stderr, "%s: unsetenv: %s\n", progName, strerror(errno));
    }
}

static void invokeSingleCommandWithRedirection(DynArray_T cmdTokens, const char *progName) {
    assert(cmdTokens != NULL);

    struct Token **tempArr = calloc((size_t)DynArray_getLength(cmdTokens), sizeof(struct Token*));
    if (tempArr == NULL) {
        fprintf(stderr, "%s: Cannot allocate memory\n", progName);
        exit(EXIT_FAILURE);
    }
    DynArray_toArray(cmdTokens, (void**)tempArr);

    char *argv[DynArray_getLength(cmdTokens) + 1];
    for (int i = 0; i < DynArray_getLength(cmdTokens); i++)
        argv[i] = tempArr[i]->pcValue;
    argv[DynArray_getLength(cmdTokens)] = NULL;

    int fdIn = -1, fdOut = -1;
    for (int i = 0; i < DynArray_getLength(cmdTokens); i++) {
        struct Token *t = DynArray_get(cmdTokens, i);
        if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
            if (t->eType == TOKEN_REDIN) {
                if (i == DynArray_getLength(cmdTokens)-1) {
                    fprintf(stderr, "%s: Standard input redirection without file name\n", progName);
                    exit(EXIT_FAILURE);
                }
                struct Token *inF = DynArray_get(cmdTokens, i+1);
                DynArray_removeAt(cmdTokens, i);
                DynArray_removeAt(cmdTokens, i);
                fdIn = open(inF->pcValue, O_RDONLY);
                if (fdIn == -1) {
                    fprintf(stderr, "%s: %s: %s\n", progName, inF->pcValue, strerror(errno));
                    exit(EXIT_FAILURE);
                }
                close(0);
                dup(fdIn);
                close(fdIn);
                i--;
            } else {
                if (i == DynArray_getLength(cmdTokens)-1) {
                    fprintf(stderr, "%s: Standard output redirection without file name\n", progName);
                    exit(EXIT_FAILURE);
                }
                struct Token *outF = DynArray_get(cmdTokens, i+1);
                DynArray_removeAt(cmdTokens, i);
                DynArray_removeAt(cmdTokens, i);
                fdOut = creat(outF->pcValue, 0600);
                if (fdOut == -1) {
                    fprintf(stderr, "%s: %s: %s\n", progName, outF->pcValue, strerror(errno));
                    exit(EXIT_FAILURE);
                }
                close(1);
                dup(fdOut);
                close(fdOut);
                i--;
            }
        }
    }

    free(tempArr);

    struct Token **rebuildArr = calloc((size_t)DynArray_getLength(cmdTokens), sizeof(struct Token*));
    if (!rebuildArr) {
        fprintf(stderr, "%s: Cannot allocate memory\n", progName);
        exit(EXIT_FAILURE);
    }
    DynArray_toArray(cmdTokens, (void**)rebuildArr);
    for (int j = 0; j < DynArray_getLength(cmdTokens); j++)
        argv[j] = rebuildArr[j]->pcValue;
    argv[DynArray_getLength(cmdTokens)] = NULL;

    execvp(argv[0], argv);
    fprintf(stderr, "%s: %s: %s\n", progName, argv[0], strerror(errno));
    free(rebuildArr);
    exit(EXIT_FAILURE);
}

/*파이프 일때-----------------------------------------------------*/
static void processPipedCommands(DynArray_T tokenList, const char *progName) {
    assert(tokenList != NULL);
    int pipeCount = 0;
    for (int i = 0; i < DynArray_getLength(tokenList); i++) {
        struct Token *tk = DynArray_get(tokenList, i);
        if (tk->eType == TOKEN_PIPE) pipeCount++;
    }

    int cmdCount = pipeCount + 1;
    DynArray_T cmdArray[cmdCount];
    {
        int startPos = 0;
        int cIndex = 0;
        for (int i = 0; i < cmdCount; i++) {
            cmdArray[i] = DynArray_new(0);
            assert(cmdArray[i] != NULL);
        }

        for (int i = 0; i < DynArray_getLength(tokenList); i++) {
            struct Token *cT = DynArray_get(tokenList, i);
            if (cT->eType == TOKEN_PIPE) {
                for (int j = startPos; j < i; j++) {
                    DynArray_add(cmdArray[cIndex], DynArray_get(tokenList, j));
                }
                cIndex++;
                startPos = i + 1;
            }
        }
        for (int k = startPos; k < DynArray_getLength(tokenList); k++) {
            DynArray_add(cmdArray[cIndex], DynArray_get(tokenList, k));
        }
    }

    int pipes[pipeCount][2];
    for (int p = 0; p < pipeCount; p++) {
        if (pipe(pipes[p]) == -1) {
            fprintf(stderr, "%s: pipe: %s\n", progName, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    for (int c = 0; c < cmdCount; c++) {
        fflush(NULL);
        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            if (c > 0) {
                close(pipes[c-1][1]);
                if (dup2(pipes[c-1][0], 0) == -1) exit(EXIT_FAILURE);
                close(pipes[c-1][0]);
            }
            if (c < pipeCount) {
                close(pipes[c][0]);
                if (dup2(pipes[c][1], 1) == -1) exit(EXIT_FAILURE);
                close(pipes[c][1]);
            }

            for (int x = 0; x < pipeCount; x++) {
                close(pipes[x][0]);
                close(pipes[x][1]);
            }

            invokeSingleCommandWithRedirection(cmdArray[c], progName);
        }
    }

    for (int x = 0; x < pipeCount; x++) {
        close(pipes[x][0]);
        close(pipes[x][1]);
    }

    for (int w = 0; w < cmdCount; w++) {
        wait(NULL);
    }

    for (int cc = 0; cc < cmdCount; cc++) {
        DynArray_free(cmdArray[cc]);
    }
}

/*한 줄 일때------------------------------------------------------------*/
static void handleSingleCommandLine(const char *inputLine, const char *progName) {
    assert(inputLine != NULL);
    assert(progName != NULL);

    DynArray_T tokenList = DynArray_new(0);
    if (tokenList == NULL) {
        fprintf(stderr, "%s: Cannot allocate memory\n", progName);
        exit(EXIT_FAILURE);
    }

    enum LexResult lexRes = lexLine(inputLine, tokenList);
    if (lexRes == LEX_SUCCESS) {
        if (DynArray_getLength(tokenList) == 0) {
            DynArray_free(tokenList);
            return;
        }

        enum SyntaxResult synRes = syntaxCheck(tokenList);
        if (synRes == SYN_SUCCESS) {
            enum BuiltinType cmdType = checkBuiltin((struct Token*)DynArray_get(tokenList,0));
            if (cmdType == B_CD) {
                handleBuiltinCd(tokenList, progName);
            } else if (cmdType == B_SETENV) {
                handleBuiltinSetenv(tokenList, progName);
            } else if (cmdType == B_USETENV) {
                handleBuiltinUnsetenv(tokenList, progName);
            } else if (cmdType == B_EXIT) {
                DynArray_free(tokenList);
                exit(EXIT_SUCCESS);
            } else {
                processPipedCommands(tokenList, progName);
            }
        } else {
            fprintf(stderr, "%s: ", progName);
            switch (synRes) {
                case SYN_FAIL_NOCMD:
                    fprintf(stderr, "Missing command name\n");
                    break;
                case SYN_FAIL_MULTREDIN:
                    fprintf(stderr, "Multiple redirection of standard input\n");
                    break;
                case SYN_FAIL_NODESTIN:
                    fprintf(stderr, "Standard input redirection without file name\n");
                    break;
                case SYN_FAIL_MULTREDOUT:
                    fprintf(stderr, "Multiple redirection of standard out\n");
                    break;
                case SYN_FAIL_NODESTOUT:
                    fprintf(stderr, "Standard output redirection without file name\n");
                    break;
                case SYN_FAIL_INVALIDBG:
                    fprintf(stderr, "Invalid use of background\n");
                    break;
                default:
                    fprintf(stderr, "Unknown syntax error\n");
                    break;
            }
        }

        DynArray_free(tokenList);
    } else {
        fprintf(stderr, "%s: ", progName);
        switch (lexRes) {
            case LEX_QERROR:
                fprintf(stderr, "Unmatched quote\n");
                break;
            case LEX_NOMEM:
                fprintf(stderr, "Cannot allocate memory\n");
                break;
            case LEX_LONG:
                fprintf(stderr, "Command is too large\n");
                break;
            default:
                fprintf(stderr, "lexLine error\n");
                break;
        }
        DynArray_free(tokenList);
    }
}

/*ishrc 파일일 때------------------------------------------------------------*/
static void initializeFromIshrc(const char *progName) {
    assert(progName != NULL);
    const char *homeDir = getenv("HOME");
    if (homeDir == NULL) return;

    char *rcPath = malloc(strlen(homeDir) + 8);
    if (rcPath == NULL) {
        fprintf(stderr, "%s: Cannot allocate memory\n", progName);
        exit(EXIT_FAILURE);
    }
    strcpy(rcPath, homeDir);
    strcat(rcPath, "/.ishrc");

    FILE *rcFile = fopen(rcPath, "r");
    free(rcPath);
    if (!rcFile) return;

    char buf[MAX_LINE_SIZE+2];
    while (fgets(buf, MAX_LINE_SIZE, rcFile) != NULL) {
        fprintf(stdout, "%% %s", buf);
        fflush(stdout);
        handleSingleCommandLine(buf, progName);
    }
    fclose(rcFile);
}

int main(int argc, char *argv[]) {
    (void)argc;
    const char *programName = argv[0];

    setupSignals();
    initializeFromIshrc(programName);

    char inputBuffer[MAX_LINE_SIZE + 2];
    while (1) {
        fprintf(stdout, "%% ");
        fflush(stdout);

        if (fgets(inputBuffer, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }

        handleSingleCommandLine(inputBuffer, programName);
    }

    return 0;
}

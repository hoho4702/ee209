/*C program that functions as a Unix-like shell supporting built-in commands and command pipelines.*/
/*AlpysbekovAlikhan*/
/*20230936*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/wait.h>
#include "dynarray.h"
#include "util.h"
#include "lexsyn.h"
#include "token.h"

static const char *programName = NULL;

static void shellHelper(const char *inLine, int fromIshrc);
static void processIshrc(void);
static void executeCommand(DynArray_T oTokens);
static void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens);
static void executeExternal(DynArray_T oTokens);
static DynArray_T splitPipeline(DynArray_T oTokens);
static void executePipeline(DynArray_T pipeline);
static int hasRedirectionOrPipe(DynArray_T oTokens);

static int hasRedirectionOrPipe(DynArray_T oTokens) {
    int length = DynArray_getLength(oTokens);
    for (int i = 0; i < length; i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN || 
            t->eType == TOKEN_REDOUT || 
            t->eType == TOKEN_PIPE) {
            return 1;
        }
    }
    return 0;
}

static void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens) {
    if (hasRedirectionOrPipe(oTokens)) {
        fprintf(stderr, "%s: Redirection or piping not allowed with "
                        "built-in commands\n", programName);

        return;
    }

    int length = DynArray_getLength(oTokens);

    switch (btype) {
        case B_EXIT:
            exit(EXIT_SUCCESS);
            break;
        case B_CD: {
            if (length == 1) {
                char *home = getenv("HOME");
                if (home == NULL) {
                    fprintf(stderr, "%s: cd: HOME not set\n", programName);
                    return;
                }
                if (chdir(home) != 0) {
                    fprintf(stderr, "%s: cd: %s\n", programName, strerror(errno));
                }
            } else {
                struct Token *dirToken = DynArray_get(oTokens, 1);
                if (chdir(dirToken->pcValue) != 0) {
                    fprintf(stderr, "%s: cd: %s\n", programName, strerror(errno));
                }
            }
            break;
        }
        case B_SETENV: {
            if (length == 1) {
                fprintf(stderr, "%s: setenv: missing variable name\n", 
                        programName);
                return;
            } else {
                struct Token *varToken = DynArray_get(oTokens, 1);
                char *var = varToken->pcValue;
                char *value = "";
                if (length > 2) {
                    struct Token *valToken = DynArray_get(oTokens, 2);
                    value = valToken->pcValue;
                }
                if (setenv(var, value, 1) != 0) {
                    fprintf(stderr, "%s: setenv: %s\n", programName, 
                            strerror(errno));
                }
            }
            break;
        }
        case B_USETENV: {
            if (length == 1) {
                fprintf(stderr, "%s: unsetenv: missing variable name\n", 
                        programName);
                return;
            } else {
                struct Token *varToken = DynArray_get(oTokens, 1);
                char *var = varToken->pcValue;
                if (unsetenv(var) != 0) {
                    fprintf(stderr, "%s: unsetenv: %s\n", programName, 
                            strerror(errno));
                }
            }
            break;
        }
        case B_FG:
            fprintf(stderr, "%s: fg: not implemented\n", programName);
            break;
        default:
            break;
    }
}

static DynArray_T splitPipeline(DynArray_T oTokens) {
    DynArray_T pipeline = DynArray_new(0);
    if (pipeline == NULL) return NULL;

    DynArray_T currentCmd = DynArray_new(0);
    if (currentCmd == NULL) {
        DynArray_free(pipeline);
        return NULL;
    }

    int length = DynArray_getLength(oTokens);

    for (int i = 0; i < length; i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_PIPE) {
            if (DynArray_getLength(currentCmd) == 0) {
                DynArray_free(currentCmd);
                for (int j = 0; j < DynArray_getLength(pipeline); j++) {
                    DynArray_free(DynArray_get(pipeline, j));
                }
                DynArray_free(pipeline);
                return NULL;
            }
            DynArray_add(pipeline, currentCmd);
            currentCmd = DynArray_new(0);
            if (currentCmd == NULL) {
                for (int j = 0; j < DynArray_getLength(pipeline); j++) {
                    DynArray_free(DynArray_get(pipeline, j));
                }
                DynArray_free(pipeline);
                return NULL;
            }
        }
        else {
            DynArray_add(currentCmd, t);
        }
    }

    if (DynArray_getLength(currentCmd) == 0 && 
        DynArray_getLength(pipeline) > 0) {
        DynArray_free(currentCmd);
        for (int j = 0; j < DynArray_getLength(pipeline); j++) {
            DynArray_free(DynArray_get(pipeline, j));
        }
        DynArray_free(pipeline);
        return NULL;
    }

    DynArray_add(pipeline, currentCmd);
    return pipeline;
}

static void executePipeline(DynArray_T pipeline) {
    int numCommands = DynArray_getLength(pipeline);
    if (numCommands == 0) return;

    int (*pipes)[2] = NULL;
    if (numCommands > 1) {
        pipes = malloc(sizeof(int[2]) * (numCommands - 1));
        if (pipes == NULL) {
            fprintf(stderr, "%s: cannot allocate memory for pipes\n", 
                    programName);
            return;
        }

        for (int i = 0; i < numCommands - 1; i++) {
            if (pipe(pipes[i]) < 0) {
                fprintf(stderr, "%s: pipe failed: %s\n", programName, 
                        strerror(errno));
                for (int j = 0; j < i; j++) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                }
                free(pipes);
                return;
            }
        }
    }

    pid_t *pids = malloc(sizeof(pid_t) * numCommands);
    if (pids == NULL) {
        fprintf(stderr, "%s: cannot allocate memory for pids\n", 
                programName);
        if (pipes) {
            for (int i = 0; i < numCommands - 1; i++) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }
            free(pipes);
        }
        return;
    }

    for (int i = 0; i < numCommands; i++) {
        DynArray_T cmd = DynArray_get(pipeline, i);
        int cmdLength = DynArray_getLength(cmd);
        char *argv[cmdLength + 1];
        int argIndex = 0;
        for (int j = 0; j < cmdLength; j++) {
            struct Token *t = DynArray_get(cmd, j);
            if (t->eType == TOKEN_WORD) {
                argv[argIndex++] = t->pcValue;
            }
        }
        argv[argIndex] = NULL;

        if (argIndex == 0) {
            fprintf(stderr, "%s: invalid empty command in pipeline\n", 
                    programName);
            if (pipes) {
                for (int p = 0; p < numCommands - 1; p++) {
                    close(pipes[p][0]);
                    close(pipes[p][1]);
                }
                free(pipes);
            }
            free(pids);
            return;
        }

        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "%s: fork failed: %s\n", programName, 
                    strerror(errno));
            if (pipes) {
                for (int p = 0; p < numCommands - 1; p++) {
                    close(pipes[p][0]);
                    close(pipes[p][1]);
                }
                free(pipes);
            }
            free(pids);
            return;
        } else if (pid == 0) {
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
            }
            if (i < numCommands - 1) {
                dup2(pipes[i][1], STDOUT_FILENO);
            }

            if (pipes) {
                for (int p = 0; p < numCommands - 1; p++) {
                    close(pipes[p][0]);
                    close(pipes[p][1]);
                }
            }

            execvp(argv[0], argv);
            fprintf(stderr, "%s: %s: %s\n", programName, argv[0], 
                    strerror(errno));
            exit(EXIT_FAILURE);
        } else {
            pids[i] = pid;
            if (i > 0) {
                close(pipes[i-1][0]);
            }
            if (i < numCommands - 1) {
                close(pipes[i][1]);
            }
        }
    }

    if (pipes) {
        free(pipes);
    }

    for (int i = 0; i < numCommands; i++) {
        int status;
        waitpid(pids[i], &status, 0);
    }

    free(pids);
}

static void executeExternal(DynArray_T oTokens) {
    DynArray_T pipeline = splitPipeline(oTokens);
    if (pipeline == NULL) {
        fprintf(stderr, "%s: invalid pipeline\n", programName);
        return;
    }

    int numCommands = DynArray_getLength(pipeline);
    if (numCommands == 1) {
        DynArray_T cmd = DynArray_get(pipeline, 0);
        int length = DynArray_getLength(cmd);
        char *argv[length + 1];
        int argIndex = 0;
        for (int i = 0; i < length; i++) {
            struct Token *t = DynArray_get(cmd, i);
            if (t->eType == TOKEN_WORD) {
                argv[argIndex++] = t->pcValue;
            }
        }
        argv[argIndex] = NULL;

        if (argIndex == 0) {
            for (int i = 0; i < numCommands; i++)
                DynArray_free(DynArray_get(pipeline, i));
            DynArray_free(pipeline);
            return;
        }

        pid_t pid = fork();
        if (pid < 0) {
            fprintf(stderr, "%s: fork failed: %s\n", programName, 
                    strerror(errno));
        } else if (pid == 0) {
            execvp(argv[0], argv);
            fprintf(stderr, "%s: %s: %s\n", programName, argv[0], 
                    strerror(errno));
            exit(EXIT_FAILURE);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    } else {
        executePipeline(pipeline);
    }

    for (int i = 0; i < numCommands; i++) {
        DynArray_free(DynArray_get(pipeline, i));
    }
    DynArray_free(pipeline);
}

static void executeCommand(DynArray_T oTokens) {
    assert(oTokens != NULL);
    int length = DynArray_getLength(oTokens);
    if (length == 0) return;

    struct Token *firstToken = DynArray_get(oTokens, 0);
    enum BuiltinType btype = checkBuiltin(firstToken);

    if (btype != NORMAL) {
        executeBuiltin(btype, oTokens);
    }
    else {
        executeExternal(oTokens);
    }
}

static void shellHelper(const char *inLine, int fromIshrc) {
    assert(inLine != NULL);

    DynArray_T oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        fprintf(stderr, "%s: cannot allocate memory\n", programName);
        exit(EXIT_FAILURE);
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
                    executeCommand(oTokens);
                } else {
                    switch (syncheck) {
                        case SYN_FAIL_NOCMD:
                            errorPrint("Missing command name", FPRINTF);
                            break;
                        case SYN_FAIL_MULTREDOUT:
                            errorPrint("Multiple redirection of standard out", 
                                        FPRINTF);
                            break;
                        case SYN_FAIL_NODESTOUT:
                            errorPrint("Standard output redirection without file name", 
                                        FPRINTF);
                            break;
                        case SYN_FAIL_MULTREDIN:
                            errorPrint("Multiple redirection of standard input", 
                                        FPRINTF);
                            break;
                        case SYN_FAIL_NODESTIN:
                            errorPrint("Standard input redirection without file name", 
                                        FPRINTF);
                            break;
                        case SYN_FAIL_INVALIDBG:
                            errorPrint("Invalid use of background", 
                                        FPRINTF);
                            break;
                        default:
                            errorPrint("Unknown syntax error", 
                                        FPRINTF);
                            break;
                    }
                }
            }
            break;

        case LEX_QERROR:
            errorPrint("Unmatched quote", 
                        FPRINTF);
            break;

        case LEX_NOMEM:
            errorPrint("Cannot allocate memory", 
                        FPRINTF);
            break;

        case LEX_LONG:
            errorPrint("Command is too large", 
                        FPRINTF);
            break;

        default:
            errorPrint("lexLine returned an unexpected result", 
                        FPRINTF);
            exit(EXIT_FAILURE);
    }

    DynArray_free(oTokens);
}

static void processIshrc(void) {
    char *home = getenv("HOME");
    if (home == NULL) {
        return;
    }

    size_t len = strlen(home) + strlen("/.ishrc") + 1;
    char *ishrcPath = malloc(len);
    if (ishrcPath == NULL) {
        return;
    }
    snprintf(ishrcPath, len, "%s/.ishrc", home);

    FILE *fp = fopen(ishrcPath, "r");
    free(ishrcPath);

    if (fp == NULL) {
        return;
    }

    char acLine[MAX_LINE_SIZE + 2];
    while (fgets(acLine, MAX_LINE_SIZE, fp) != NULL) {
        fprintf(stdout, "%% %s", acLine);
        fflush(stdout);

        shellHelper(acLine, 1);
    }

    fclose(fp);
}

int main(int argc, char *argv[]) {
    programName = argv[0];
    errorPrint((char*)programName, SETUP);

    processIshrc();

    char acLine[MAX_LINE_SIZE + 2];
    for (;;) {
        fprintf(stdout, "%% ");
        fflush(stdout);

        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }

        shellHelper(acLine, 0);
    }
}

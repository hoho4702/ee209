#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>

#include "execute.h"
#include "util.h"
#include "lexsyn.h"

/* change DynArray of tokens to array of strings for execvp */
static char** tokensToArgv(DynArray_T oTokens, int start, int end) {
    int i, argCount = 0;
    char** argv;
    struct Token* psToken;

    /* count args but not redirects*/
    for (i = start; i < end; i++) {
        psToken = DynArray_get(oTokens, i);
        if (psToken->eType == TOKEN_WORD)
            argCount++;
        else if (psToken->eType == TOKEN_REDIN || psToken->eType == TOKEN_REDOUT)
            i++; /* skip filename */
    }

    /* allocate argv arr */
    argv = calloc(argCount + 1, sizeof(char*));
    if (!argv) return NULL;

    argCount = 0;
    for (i = start; i < end; i++) {
        psToken = DynArray_get(oTokens, i);
        if (psToken->eType == TOKEN_WORD) {
            argv[argCount++] = psToken->pcValue;
        }
        else if (psToken->eType == TOKEN_REDIN || psToken->eType == TOKEN_REDOUT)
            i++; /* skip filename */
    }
    argv[argCount] = NULL;

    return argv;
}

enum ExecResult executeBuiltin(DynArray_T oTokens) {
    struct Token* cmdToken;
    enum BuiltinType btype;

    assert(oTokens != NULL);
    assert(DynArray_getLength(oTokens) > 0);

    cmdToken = DynArray_get(oTokens, 0);
    btype = checkBuiltin(cmdToken);

    switch (btype) {
        case B_CD: {
            struct Token* pathToken;
            const char* path;

            /* path argument or HOME path */
            if (DynArray_getLength(oTokens) > 1) {
                pathToken = DynArray_get(oTokens, 1);
                path = pathToken->pcValue;
            } else {
                path = getenv("HOME");
                if (!path) {
                    errorPrint("HOME not set", FPRINTF);
                    return EXEC_FAIL_IO_ERROR;
                }
            }

            if (chdir(path) != 0) {
                errorPrint(NULL, PERROR);
                return EXEC_FAIL_IO_ERROR;
            }
            break;
        }

        case B_SETENV: {
            struct Token* t;
            int i, argCount = 0;
            const char* varName = NULL;
            const char* value = "";
            
            /* count args and check for redirects */
            for (i = 0; i < DynArray_getLength(oTokens); i++) {
                t = DynArray_get(oTokens, i);
                if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
                    errorPrint("setenv takes one or two parameters", FPRINTF);
                    return EXEC_FAIL_INVALID_ARGS;
                }
                if (t->eType == TOKEN_WORD && i > 0) {  /* skipping cmd  */
                    argCount++;
                    if (argCount == 1) varName = t->pcValue;
                    else if (argCount == 2) value = t->pcValue;
                }
            }

            /* checking argc */
            if (argCount == 0) {
                errorPrint("setenv: Too few arguments", FPRINTF);
                return EXEC_FAIL_INVALID_ARGS;
            }
            if (argCount > 2) {
                errorPrint("setenv takes one or two parameters", FPRINTF);
                return EXEC_FAIL_INVALID_ARGS;
            }

            if (setenv(varName, value, 1) != 0) {
                errorPrint(NULL, PERROR);
                return EXEC_FAIL_IO_ERROR;
            }
            break;
        }

        case B_USETENV: {
            struct Token* varToken;

            /* need var name */
            if (DynArray_getLength(oTokens) < 2) {
                errorPrint("unsetenv: Too few arguments", FPRINTF);
                return EXEC_FAIL_IO_ERROR;
            }

            varToken = DynArray_get(oTokens, 1);
            unsetenv(varToken->pcValue);
            break;
        }

        default:
            return EXEC_FAIL_NOT_FOUND;
    }

    return EXEC_SUCCESS;
}

int handleRedirection(DynArray_T oTokens, int* inFd, int* outFd) {
    int i;
    struct Token* psToken;

    assert(oTokens != NULL);
    assert(inFd != NULL);
    assert(outFd != NULL);

    *inFd = -1;
    *outFd = -1;

    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        psToken = DynArray_get(oTokens, i);

        if (psToken->eType == TOKEN_REDIN) {
            /* get input file name */
            if (i + 1 >= DynArray_getLength(oTokens)) return -1;
            psToken = DynArray_get(oTokens, i + 1);
            
            /* open input file */
            *inFd = open(psToken->pcValue, O_RDONLY);
            if (*inFd == -1) {
                errorPrint(NULL, PERROR);
                return -1;
            }
        }
        else if (psToken->eType == TOKEN_REDOUT) {
            /* get output file name */
            if (i + 1 >= DynArray_getLength(oTokens)) return -1;
            psToken = DynArray_get(oTokens, i + 1);
            
            /* open output file with proper permissions */
            *outFd = open(psToken->pcValue, 
                         O_WRONLY | O_CREAT | O_TRUNC,
                         0600);
            if (*outFd == -1) {
                errorPrint(NULL, PERROR);
                return -1;
            }
        }
    }

    return 0;
}

void cleanupExecution(int inFd, int outFd) {
    if (inFd != -1) close(inFd);
    if (outFd != -1) close(outFd);
}

static int splitPipeline(DynArray_T oTokens, struct CommandInfo* commands, int maxCommands) {
    int cmdCount = 0;
    int i;
    struct Token* psToken;
    int foundRedirection = 0;

    /* init first command */
    commands[0].startIndex = 0;
    commands[0].inputFd = -1;
    commands[0].outputFd = -1;
    commands[0].hasRedirection = 0;

    /* find pipe tokens and split into commands */
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        psToken = DynArray_get(oTokens, i);
        if (psToken->eType == TOKEN_PIPE) {
            if (cmdCount >= maxCommands - 1) return -1;  /* too many cmds */
            
            commands[cmdCount].endIndex = i;
            commands[cmdCount].hasRedirection = foundRedirection;
            cmdCount++;
            commands[cmdCount].startIndex = i + 1;
            commands[cmdCount].inputFd = -1;
            commands[cmdCount].outputFd = -1;
            commands[cmdCount].hasRedirection = 0;
            foundRedirection = 0;
        }
        else if (psToken->eType == TOKEN_REDIN || psToken->eType == TOKEN_REDOUT) {
            foundRedirection = 1;
        }
    }
    commands[cmdCount].endIndex = i;
    commands[cmdCount].hasRedirection = foundRedirection;
    return cmdCount + 1;
}

enum ExecResult executePipeline(DynArray_T oTokens) {
    struct CommandInfo commands[MAX_ARGS_CNT];
    int cmdCount, i, status, j;
    int pipeFds[MAX_ARGS_CNT][2];  /* arr of pipe fd's */
    pid_t* pids;
    enum ExecResult result = EXEC_SUCCESS;
    int inFd = -1, outFd = -1;
    char** argv;

    /* tokens divided into separate cmds */
    cmdCount = splitPipeline(oTokens, commands, MAX_ARGS_CNT);
    if (cmdCount <= 0) return EXEC_FAIL_PIPE;

    /* input/output redirect for the entire pipeline */
    if (handleRedirection(oTokens, &inFd, &outFd) == -1) {
        cleanupExecution(inFd, outFd);
        return EXEC_FAIL_IO_ERROR;
    }

    /* space for child pids */
    pids = calloc(cmdCount, sizeof(pid_t));
    if (!pids) {
        cleanupExecution(inFd, outFd);
        return EXEC_FAIL_NO_MEM;
    }

    /* create pipes first */
    for (i = 0; i < cmdCount - 1; i++) {
        if (pipe(pipeFds[i]) == -1) {
            result = EXEC_FAIL_PIPE;
            goto cleanup;
        }
    }

    /* exec all cmd */
    for (i = 0; i < cmdCount; i++) {
        /* fork and exec cmd */
        pids[i] = fork();
        if (pids[i] == -1) {
            result = EXEC_FAIL_FORK;
            goto cleanup;
        }

        if (pids[i] == 0) { /* child */
            /* close all unused pipe ends */
            for (j = 0; j < cmdCount - 1; j++) {
                if (j == i - 1) {
                    /* read end of previous pipe */
                    close(pipeFds[j][1]);
                } else if (j == i) {
                    /* write end of current pipe */
                    close(pipeFds[j][0]);
                } else {
                    /* close pipes */
                    close(pipeFds[j][0]);
                    close(pipeFds[j][1]);
                }
            }

            /* setting up input */
            if (i == 0) {
                if (inFd != -1) {
                    if (dup2(inFd, STDIN_FILENO) == -1) {
                        errorPrint(NULL, PERROR);
                        exit(EXIT_FAILURE);
                    }
                }
            } else {
                if (dup2(pipeFds[i-1][0], STDIN_FILENO) == -1) {
                    errorPrint(NULL, PERROR);
                    exit(EXIT_FAILURE);
                }
            }

            /* setting up output */
            if (i == cmdCount - 1) {
                if (outFd != -1) {
                    if (dup2(outFd, STDOUT_FILENO) == -1) {
                        errorPrint(NULL, PERROR);
                        exit(EXIT_FAILURE);
                    }
                }
            } else {
                if (dup2(pipeFds[i][1], STDOUT_FILENO) == -1) {
                    errorPrint(NULL, PERROR);
                    exit(EXIT_FAILURE);
                }
            }

            /* close fd's */
            if (inFd != -1) close(inFd);
            if (outFd != -1) close(outFd);
            if (i > 0) close(pipeFds[i-1][0]);
            if (i < cmdCount - 1) close(pipeFds[i][1]);

            /* exec cmd */
            argv = tokensToArgv(oTokens, commands[i].startIndex, commands[i].endIndex);
            if (!argv) {
                errorPrint("Cannot allocate memory", FPRINTF);
                exit(EXIT_FAILURE);
            }

            execvp(argv[0], argv);
            errorPrint(NULL, PERROR);
            exit(EXIT_FAILURE);
        }
    }

    /* close pipe fds in parent */
    for (i = 0; i < cmdCount - 1; i++) {
        close(pipeFds[i][0]);
        close(pipeFds[i][1]);
    }
    if (inFd != -1) close(inFd);
    if (outFd != -1) close(outFd);

    /* wait for children */
    for (i = 0; i < cmdCount; i++) {
        if (waitpid(pids[i], &status, 0) == -1) {
            result = EXEC_FAIL_IO_ERROR;
            goto cleanup;
        }
    }

cleanup:
    free(pids);
    return result;
}


enum ExecResult executeCommand(DynArray_T oTokens) {
    pid_t pid;
    int status;
    char** argv;
    int inFd = -1, outFd = -1;
    enum ExecResult result = EXEC_SUCCESS;

    assert(oTokens != NULL);
    assert(DynArray_getLength(oTokens) > 0);

    /* built in cmds first */
    if (checkBuiltin(DynArray_get(oTokens, 0)) != NORMAL) {
        return executeBuiltin(oTokens);
    }

    /* set up redirects */
    if (handleRedirection(oTokens, &inFd, &outFd) == -1) {
        cleanupExecution(inFd, outFd);
        return EXEC_FAIL_IO_ERROR;
    }

    /* tokens to argv arr */
    argv = tokensToArgv(oTokens, 0, DynArray_getLength(oTokens));
    if (!argv) {
        cleanupExecution(inFd, outFd);
        return EXEC_FAIL_NO_MEM;
    }

    /* flush output */
    fflush(NULL);

    /* forking */
    pid = fork();
    if (pid == -1) {
        free(argv);
        cleanupExecution(inFd, outFd);
        return EXEC_FAIL_FORK;
    }

    if (pid == 0) { /* child */
        /* input redirect */
        if (inFd != -1) {
            if (dup2(inFd, STDIN_FILENO) == -1) {
                errorPrint(NULL, PERROR);
                exit(EXIT_FAILURE);
            }
            close(inFd);
        }

        /* output redirect */
        if (outFd != -1) {
            if (dup2(outFd, STDOUT_FILENO) == -1) {
                errorPrint(NULL, PERROR);
                exit(EXIT_FAILURE);
            }
            close(outFd);
        }

        /* exec cmd */
        execvp(argv[0], argv);
        
        /* oops, failed */
        errorPrint(argv[0], PERROR);
        exit(EXIT_FAILURE);
    }

    /* parent */
    free(argv);
    cleanupExecution(inFd, outFd);

    /* wait for child */
    if (waitpid(pid, &status, 0) == -1) {
        errorPrint(NULL, PERROR);
        result = EXEC_FAIL_IO_ERROR;
    }

    return result;
}

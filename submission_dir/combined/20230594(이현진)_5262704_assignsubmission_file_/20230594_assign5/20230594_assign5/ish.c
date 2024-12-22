#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include "lexsyn.h"
#include "util.h"
#include "token.h"
#include "dynarray.h"

static void executeCommand(DynArray_T oTokens);
static void handleRedirection(DynArray_T oTokens);
static void handlePipeline(DynArray_T oTokens);
static int containsPipe(DynArray_T oTokens);
static void splitPipeline(DynArray_T oTokens, DynArray_T *leftTokens, DynArray_T *rightTokens);
static void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens);
static void processIshrc();

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
                enum BuiltinType btype = checkBuiltin(DynArray_get(oTokens, 0));
                if (btype != NORMAL) {
                    executeBuiltin(btype, oTokens);
                } else if (containsPipe(oTokens)) {
                    handlePipeline(oTokens);
                } else {
                    handleRedirection(oTokens);
                    executeCommand(oTokens);
                }
            } else {
                errorPrint("Syntax error", FPRINTF);
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
            errorPrint("Unexpected lexLine error", FPRINTF);
            exit(EXIT_FAILURE);
    }

    DynArray_map(oTokens, freeToken, NULL);
    DynArray_free(oTokens);
}

static void executeCommand(DynArray_T oTokens) {
    pid_t pid;
    int status;
    char **argv;
    int argc = DynArray_getLength(oTokens);

    argv = calloc(argc + 1, sizeof(char *));
    if (argv == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < argc; i++) {
        argv[i] = ((struct Token *)DynArray_get(oTokens, i))->pcValue;
    }

    pid = fork();
    if (pid == 0) { // Child process
        execvp(argv[0], argv);
        perror(argv[0]);
        exit(EXIT_FAILURE);
    } else if (pid > 0) { // Parent process
        waitpid(pid, &status, 0);
    } else {
        perror("Fork failed");
    }

    free(argv);
}

static void handleRedirection(DynArray_T oTokens) {
    int inputRedirected = 0, outputRedirected = 0;

    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *token = DynArray_get(oTokens, i);

        if (token->eType == TOKEN_REDOUT) {
            if (outputRedirected) {
                errorPrint("Multiple redirection of standard output", FPRINTF);
                exit(EXIT_FAILURE);
            }
            int fd = open(((struct Token *)DynArray_get(oTokens, i + 1))->pcValue,
                          O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0) {
                perror("Open failed for output redirection");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
            outputRedirected = 1;
            DynArray_removeAt(oTokens, i); // Remove redirection token
            DynArray_removeAt(oTokens, i); // Remove filename token
            i--; // Adjust index
        } else if (token->eType == TOKEN_REDIN) {
            if (inputRedirected) {
                errorPrint("Multiple redirection of standard input", FPRINTF);
                exit(EXIT_FAILURE);
            }
            int fd = open(((struct Token *)DynArray_get(oTokens, i + 1))->pcValue, O_RDONLY);
            if (fd < 0) {
                perror("Open failed for input redirection");
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
            inputRedirected = 1;
            DynArray_removeAt(oTokens, i); // Remove redirection token
            DynArray_removeAt(oTokens, i); // Remove filename token
            i--; // Adjust index
        }
    }
}

static void handlePipeline(DynArray_T oTokens) {
    int pipefd[2], status;
    pid_t pid;
    DynArray_T leftTokens, rightTokens;
    splitPipeline(oTokens, &leftTokens, &rightTokens);

    if (pipe(pipefd) == -1) {
        perror("Pipe failed");
        exit(EXIT_FAILURE);
    }

    pid = fork();
    if (pid == 0) { // Left child process
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        handleRedirection(leftTokens);
        executeCommand(leftTokens);
    } else if (pid > 0) { // Parent process
        close(pipefd[1]);
        pid = fork();
        if (pid == 0) { // Right child process
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            handleRedirection(rightTokens);
            executeCommand(rightTokens);
        } else if (pid > 0) {
            close(pipefd[0]);
            wait(&status);
            wait(&status);
        }
    }
}

static int containsPipe(DynArray_T oTokens) {
    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *token = DynArray_get(oTokens, i);
        if (token->eType == TOKEN_PIPE) {
            return 1;
        }
    }
    return 0;
}

static void splitPipeline(DynArray_T oTokens, DynArray_T *leftTokens, DynArray_T *rightTokens) {
    int pipeIndex = -1;

    *leftTokens = DynArray_new(0);
    *rightTokens = DynArray_new(0);

    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *token = DynArray_get(oTokens, i);
        if (token->eType == TOKEN_PIPE) {
            pipeIndex = i;
            break;
        }
    }

    if (pipeIndex == -1) {
        errorPrint("No pipe found", FPRINTF);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < pipeIndex; i++) {
        DynArray_add(*leftTokens, DynArray_get(oTokens, i));
    }
    for (int i = pipeIndex + 1; i < DynArray_getLength(oTokens); i++) {
        DynArray_add(*rightTokens, DynArray_get(oTokens, i));
    }
}

static void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens) {
    switch (btype) {
        case B_CD:
            if (chdir(((struct Token *)DynArray_get(oTokens, 1))->pcValue) != 0) {
                perror("cd failed");
            }
            break;
        case B_EXIT:
            exit(EXIT_SUCCESS);
            break;
        case B_SETENV: {
            char *var = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
            char *value = ((struct Token *)DynArray_get(oTokens, 2))->pcValue;
            if (setenv(var, value, 1) != 0) {
                perror("setenv failed");
            }
            break;
        }
        case B_USETENV: {
            char *var = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
            if (unsetenv(var) != 0) {
                perror("unsetenv failed");
            }
            break;
        }
        default:
            errorPrint("Unsupported built-in command", FPRINTF);
    }
}

static void processIshrc() {
    char *home = getenv("HOME");
    if (!home) {
        errorPrint("HOME environment variable not set", FPRINTF);
        return;
    }

    char ishrcPath[1024];
    snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", home);

    FILE *file = fopen(ishrcPath, "r");
    if (!file) return;

    char line[MAX_LINE_SIZE + 2];
    while (fgets(line, sizeof(line), file)) {
        shellHelper(line);
    }

    fclose(file);
}

int main() {
    char acLine[MAX_LINE_SIZE + 2];
    errorPrint("ish", SETUP);

    processIshrc(); // Execute .ishrc at startup

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

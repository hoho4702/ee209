#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>


#include "lexsyn.h"
#include "token.h"
#include "dynarray.h"

/* Maximum size of a line */
#define MAX_LINE_SIZE 1024

/* Function Prototypes */
static void shellHelper(const char *inLine);
static void handleBuiltin(enum TokenType btype, DynArray_T oTokens);
static void executeCommand(DynArray_T oTokens);

/* Signal Handlers */
static void sigquitHandler(int sig) {
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    signal(SIGQUIT, exit);
    alarm(5);
}

static void sigalrmHandler(int sig) {
    signal(SIGQUIT, sigquitHandler);
}

/* shellHelper: Processes a single command line */
static void shellHelper(const char *inLine) {
    DynArray_T oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        perror("Cannot allocate memory\n");
        exit(EXIT_FAILURE);
    }

    enum LexResult lexResult = lexLine(inLine, oTokens);
    if (lexResult == LEX_SUCCESS) {
        if (DynArray_getLength(oTokens) == 0) {
            DynArray_free(oTokens);
            return;
        }

        enum SyntaxResult synResult = syntaxCheck(oTokens);
        if (synResult == SYN_SUCCESS) {
            struct Token *firstToken = DynArray_get(oTokens, 0);
            if (firstToken->eType == TOKEN_WORD) {
                if (strcmp(firstToken->pcValue, "cd") == 0) {
                    handleBuiltin(TOKEN_WORD, oTokens);
                } else if (strcmp(firstToken->pcValue, "setenv") == 0) {
                    handleBuiltin(TOKEN_WORD, oTokens);
                } else if (strcmp(firstToken->pcValue, "unsetenv") == 0) {
                    handleBuiltin(TOKEN_WORD, oTokens);
                } else if (strcmp(firstToken->pcValue, "exit") == 0) {
                    handleBuiltin(TOKEN_WORD, oTokens);
                } else {
                    executeCommand(oTokens);
                }
            }
        } else {
            /* Handle syntax errors */
            fprintf(stderr, "Syntax error: ");
            switch (synResult) {
                case SYN_FAIL_NOCMD:
                    fprintf(stderr, "Missing command name\n");
                    break;
                case SYN_FAIL_MULTREDIN:
                    fprintf(stderr, "Multiple input redirection\n");
                    break;
                case SYN_FAIL_NODESTIN:
                    fprintf(stderr, "Missing input file for redirection\n");
                    break;
                case SYN_FAIL_MULTREDOUT:
                    fprintf(stderr, "Multiple output redirection\n");
                    break;
                case SYN_FAIL_NODESTOUT:
                    fprintf(stderr, "Missing output file for redirection\n");
                    break;
                case SYN_FAIL_INVALIDBG:
                    fprintf(stderr, "Invalid background command\n");
                    break;
                default:
                    fprintf(stderr, "Unknown error\n");
            }
        }
    } else {
        /* Handle lexical errors */
        fprintf(stderr, "Lexical error: ");
        switch (lexResult) {
            case LEX_QERROR:
                fprintf(stderr, "Unmatched quote\n");
                break;
            case LEX_NOMEM:
                fprintf(stderr, "Memory allocation error\n");
                break;
            case LEX_LONG:
                fprintf(stderr, "Input line too long\n");
                break;
            default:
                fprintf(stderr, "Unknown error\n");
        }
    }

    DynArray_free(oTokens);
}

/* handleBuiltin: Handles built-in commands */
static void handleBuiltin(enum TokenType btype, DynArray_T oTokens) {
    if (DynArray_getLength(oTokens) == 0) return;

    struct Token *token = DynArray_get(oTokens, 0);
    if (strcmp(token->pcValue, "cd") == 0) {
        const char *dir = (DynArray_getLength(oTokens) > 1) ? ((struct Token *)DynArray_get(oTokens, 1))->pcValue : getenv("HOME");
        if (chdir(dir) != 0) perror("cd");
    } else if (strcmp(token->pcValue, "setenv") == 0) {
        if (DynArray_getLength(oTokens) < 2) {
            fprintf(stderr, "setenv: Missing variable name\n");
            return;
        }
        const char *var = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
        const char *value = (DynArray_getLength(oTokens) > 2) ? ((struct Token *)DynArray_get(oTokens, 2))->pcValue : "";
        if (setenv(var, value, 1) != 0) perror("setenv");
    } else if (strcmp(token->pcValue, "unsetenv") == 0) {
        if (DynArray_getLength(oTokens) < 2) {
            fprintf(stderr, "unsetenv: Missing variable name\n");
            return;
        }
        const char *var = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
        if (unsetenv(var) != 0) perror("unsetenv");
    } else if (strcmp(token->pcValue, "exit") == 0) {
        exit(EXIT_SUCCESS);
    }
}

/* executeCommand: Executes non-built-in commands */
static void executeCommand(DynArray_T oTokens) {
    size_t len = DynArray_getLength(oTokens);
    char **argv = malloc((len + 1) * sizeof(char *));
    if (argv == NULL) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    for (size_t i = 0; i < len; i++) {
        struct Token *token = DynArray_get(oTokens, i);
        argv[i] = token->pcValue;
    }
    argv[len] = NULL;

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        free(argv);
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        execvp(argv[0], argv);
        perror("execvp");
        free(argv);
        exit(EXIT_FAILURE);
    } else {
        int status;
        waitpid(pid, &status, 0);
    }

    free(argv);
}

int main(void) {
    signal(SIGQUIT, sigquitHandler);
    signal(SIGALRM, sigalrmHandler);

    char line[MAX_LINE_SIZE];

    while (1) {
        printf("%% ");
        fflush(stdout);
        if (fgets(line, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            break;
        }
        shellHelper(line);
    }

    return EXIT_SUCCESS;
}
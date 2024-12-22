// Assignment 5
// 20230436 우현호 Hyunho Woo

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include "lexsyn.h"
#include "util.h"

#define MAX_LINE_SIZE 1024

/* Signal*/
static void quitHandler(int iSig) {
    static int quitFlag = 0;

    if (iSig == SIGQUIT) {
        if (quitFlag == 0) {
            printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
            quitFlag = 1;
            alarm(5);
        } else {
            exit(EXIT_SUCCESS);
        }
    } else if (iSig == SIGALRM) {
        quitFlag = 0; // Reset
    }
}

static void execute_command(DynArray_T oTokens) {
    char *args[MAX_LINE_SIZE];
    char *redirectionIn = NULL, *redirectionOut = NULL;
    int argCount = 0;

    // Parse tokens
    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *token = DynArray_get(oTokens, i);

        if (token->eType == TOKEN_REDIN) {
            redirectionIn = 
            ((struct Token *)DynArray_get(oTokens, i + 1))->pcValue;
            i++; // Skip 
        } else if (token->eType == TOKEN_REDOUT) {
            redirectionOut = 
            ((struct Token *)DynArray_get(oTokens, i + 1))->pcValue;
            i++; // Skip
        } else if (token->eType == TOKEN_WORD) {
            args[argCount++] = token->pcValue;
        }
    }
    args[argCount] = NULL;

    // Fork process
    pid_t pid = fork();
    if (pid == 0) { // Child
        if (redirectionIn) {
            int fdIn = open(redirectionIn, O_RDONLY);
            if (fdIn == -1) {
                perror(redirectionIn);
                exit(EXIT_FAILURE);
            }
            dup2(fdIn, STDIN_FILENO);
            close(fdIn);
        }

        if (redirectionOut) {
            int fdOut = 
            open(redirectionOut, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fdOut == -1) {
                perror(redirectionOut);
                exit(EXIT_FAILURE);
            }
            dup2(fdOut, STDOUT_FILENO);
            close(fdOut);
        }

        execvp(args[0], args);
        fprintf(stderr, "%s: No such file or directory\n", args[0]);
        exit(EXIT_FAILURE);
    } else if (pid > 0) { // Parent
        wait(NULL);
    } else { // Fork failed
        perror("fork");
    }
}

static void shellHelper(const char *inLine) {
    DynArray_T oTokens;

    enum LexResult lexcheck;
    enum SyntaxResult syncheck;
    enum BuiltinType btype;

    // Initialize
    oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }

    lexcheck = lexLine(inLine, oTokens);
    if (lexcheck == LEX_SUCCESS) {
        if (DynArray_getLength(oTokens) == 0) {
            DynArray_free(oTokens);
            return;
        }

        dumpLex(oTokens); // Debugging

        syncheck = syntaxCheck(oTokens);
        if (syncheck == SYN_SUCCESS) {
            btype = checkBuiltin(DynArray_get(oTokens, 0));
            switch (btype) {
                case B_CD: {
                    struct Token *arg = (DynArray_getLength(oTokens) > 1) 
                                        ? DynArray_get(oTokens, 1)
                                        : NULL;
                    const char *dir = 
                    (arg != NULL) ? arg->pcValue : getenv("HOME");
                    if (chdir(dir) != 0) {
                        perror("cd");
                    }
                    break;
                }
                case B_SETENV: {
                    struct Token *var = (DynArray_getLength(oTokens) > 1) 
                                        ? DynArray_get(oTokens, 1)
                                        : NULL;
                    struct Token *val = (DynArray_getLength(oTokens) > 2) 
                                        ? DynArray_get(oTokens, 2)
                                        : NULL;
                    if (var) {
                        const char *value = 
                        (val != NULL) ? val->pcValue : "";
                        if (setenv(var->pcValue, value, 1) != 0) {
                            perror("setenv");
                        }
                    } else {
                        fprintf(stderr, "Usage: setenv VAR [VALUE]\n");
                    }
                    break;
                }
                case B_USETENV: {
                    struct Token *var = (DynArray_getLength(oTokens) > 1) 
                                        ? DynArray_get(oTokens, 1)
                                        : NULL;
                    if (var) {
                        if (unsetenv(var->pcValue) != 0) {
                            perror("unsetenv");
                        }
                    } else {
                        fprintf(stderr, "Usage: unsetenv VAR\n");
                    }
                    break;
                }
                case B_EXIT:
                    DynArray_free(oTokens);
                    exit(0);
                default:
                    execute_command(oTokens);
            }
        } else { //syntax errors
            switch (syncheck) {
                case SYN_FAIL_NOCMD:
                    errorPrint("Missing command name", FPRINTF);
                    break;
                case SYN_FAIL_MULTREDOUT:
                    errorPrint("Multiple redirection of standard out", 
                    FPRINTF);
                    break;
                case SYN_FAIL_NODESTOUT:
                    errorPrint("Standard output redirection without file name", FPRINTF);
                    break;
                case SYN_FAIL_MULTREDIN:
                    errorPrint("Multiple redirection of standard input", FPRINTF);
                    break;
                case SYN_FAIL_NODESTIN:
                    errorPrint("Standard input redirection without file name", FPRINTF);
                    break;
                case SYN_FAIL_INVALIDBG:
                    errorPrint("Invalid use of background", FPRINTF);
                    break;
            }
        }
    } else { //lexical errors
        switch (lexcheck) {
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
                errorPrint("Unknown lexical error", FPRINTF);
        }
    }

    DynArray_free(oTokens);
}

int main() {
    char acLine[MAX_LINE_SIZE + 2];
    errorPrint("./ish", SETUP);

    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, quitHandler);
    signal(SIGALRM, quitHandler);

    char *homeDir = getenv("HOME");
    if (homeDir != NULL) {
        char ishrcPath[MAX_LINE_SIZE];
        snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", homeDir);
        FILE *ishrcFile = fopen(ishrcPath, "r");
        if (ishrcFile != NULL) {
            while (fgets(acLine, MAX_LINE_SIZE, ishrcFile) != NULL) {
                printf("%% %s", acLine); 
                shellHelper(acLine);   
            }
            fclose(ishrcFile);
        }
    }

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

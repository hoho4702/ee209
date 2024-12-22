/* EE209 Assignment 5 by 20230610 Seongbin Yim                        */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include "dynarray.h"
#include "util.h"

void errorPrint(char *input, enum PrintMode mode) {
    static char *shell_name = NULL;

    if (mode == SETUP) {
        shell_name = input;
    } else {
        if (shell_name == NULL) {
            fprintf(stderr, "[WARN] Shell name is not set. Please fix this bug in main function\n");
        }
        if (mode == PERROR) {
            if (input == NULL) {
                fprintf(stderr, "%s: %s\n", shell_name, strerror(errno));
            } else {
                fprintf(stderr, "%s: %s\n", input, strerror(errno));
            }
        } else if (mode == FPRINTF) {
            fprintf(stderr, "%s: %s\n", shell_name, input);
        } else if (mode == ALIAS) {
            fprintf(stderr, "%s: alias: %s: not found\n", shell_name, input);
        } else {
            fprintf(stderr, "mode %d not supported in print_error\n", mode);
        }
    }
}

enum BuiltinType checkBuiltin(struct Token *token) {
    assert(token);
    assert(token->pcValue);

    if (strcmp(token->pcValue, "cd") == 0) {
        return B_CD;
    } else if (strcmp(token->pcValue, "fg") == 0) {
        return B_FG;
    } else if (strcmp(token->pcValue, "exit") == 0) {
        return B_EXIT;
    } else if (strcmp(token->pcValue, "setenv") == 0) {
        return B_SETENV;
    } else if (strcmp(token->pcValue, "unsetenv") == 0) {
        return B_USETENV;
    } else if (strcmp(token->pcValue, "alias") == 0) {
        return B_ALIAS;
    } else {
        return NORMAL;
    }
}

int countPipe(DynArray_T tokens) {
    int count = 0;
    for (int i = 0; i < DynArray_getLength(tokens); i++) {
        struct Token *token = DynArray_get(tokens, i);
        if (token->eType == TOKEN_PIPE) {
            count++;
        }
    }
    return count;
}

int checkBG(DynArray_T tokens) {
    for (int i = 0; i < DynArray_getLength(tokens); i++) {
        struct Token *token = DynArray_get(tokens, i);
        if (token->eType == TOKEN_BG) {
            return 1;
        }
    }
    return 0;
}

const char* token_type_to_string(struct Token* token) {
    switch (token->eType) {
        case TOKEN_PIPE:
            return "TOKEN_PIPE(|)";
        case TOKEN_REDIN:
            return "TOKEN_REDIRECTION_IN(<)";
        case TOKEN_REDOUT:
            return "TOKEN_REDIRECTION_OUT(>)";
        case TOKEN_BG:
            return "TOKEN_BACKGROUND(&)";
        case TOKEN_WORD:
        default:
            assert(0 && "Unreachable");
            return NULL;
    }
}

void dumpLex(DynArray_T tokens) {
    if (getenv("DEBUG") != NULL) {
        for (int i = 0; i < DynArray_getLength(tokens); i++) {
            struct Token *token = DynArray_get(tokens, i);
            if (token->pcValue == NULL) {
                fprintf(stderr, "[%d] %s\n", i, token_type_to_string(token));
            } else {
                fprintf(stderr, "[%d] TOKEN_WORD(\"%s\")\n", i, token->pcValue);
            }
        }
    }
}

void execute_cd(DynArray_T tokens) {
    int argc = DynArray_getLength(tokens);

    if (argc == 1) {
        const char *home_dir = getenv("HOME");
        if (chdir(home_dir) == -1) {
            print_error(NULL, PERROR);
        }
    } else if (argc > 2) {
        print_error("cd takes one parameter", FPRINTF);
    } else {
        const char *dir = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
        if (chdir(dir) == -1) {
            print_error(NULL, PERROR);
        }
    }
}

void execute_exit(DynArray_T tokens) {
    int argc = DynArray_getLength(tokens);

    if (argc > 1) {
        print_error("exit takes no parameter", FPRINTF);
        return;
    }

    DynArray_free(tokens);
    exit(EXIT_SUCCESS);
}

void execute_setenv(DynArray_T tokens) {
    int argc = DynArray_getLength(tokens);

    if (argc == 2) {
        const char *var = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
        setenv(var, "", 1);
    } else if (argc < 2 || argc > 3) {
        print_error("setenv takes one or two parameters", FPRINTF);
    } else {
        const char *var = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
        const char *value = ((struct Token *)DynArray_get(tokens, 2))->pcValue;
        setenv(var, value, 1);
    }
}

void execute_unsetenv(DynArray_T tokens) {
    int argc = DynArray_getLength(tokens);

    if (argc != 2) {
        print_error("unsetenv takes one parameter", FPRINTF);
        return;
    }
    const char *var = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
    if (getenv(var) != NULL) {
        unsetenv(var);
    }
}

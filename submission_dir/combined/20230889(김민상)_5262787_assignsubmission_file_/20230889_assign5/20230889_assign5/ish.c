/*************************************************************************
 * Author: Minsang Kim
 * Student ID: 20230889
 * Description: This file implements a custom shell program. It supports 
 *              command execution, redirection, pipes, and several built-in
 *              commands such as cd, exit, setenv, and unsetenv.
 *************************************************************************/

#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <ctype.h>
#include "lexsyn.h"
#include "dynarray.h"
#include "token.h"
#include "util.h"

static volatile sig_atomic_t quit_requested = 0;

static char *progName = NULL;

static void shellHelper(const char *inLine, int from_ishrc);
static void executeCommand(DynArray_T oTokens);
static void handleBuiltIn(DynArray_T oTokens);
static void redirectIO(int in_fd, int out_fd);
static void freeTokens(DynArray_T oTokens);
static void readIshrc(void);
static void parseAndExecuteLine(const char *line, int from_ishrc);
static void installSignals(void);
static void sigintHandler(int sig);
static void sigquitHandler(int sig);
static void sigalrmHandler(int sig);
static int setupPipes(DynArray_T oTokens, int *pipe_count, DynArray_T **commands);

static void builtinSetenv(DynArray_T oTokens);
static void builtinUnsetenv(DynArray_T oTokens);
static void builtinCd(DynArray_T oTokens);
static void builtinExit(DynArray_T oTokens);

static int getRedirections(DynArray_T oTokens, char **infile, char **outfile, int *in_index, int *out_index);
static void removeRedirections(DynArray_T oTokens, int in_index, int out_index);


/**
 * Main function for the shell program. 
 * 
 * Initializes environment variables, installs signal handlers, reads 
 * the configuration file, and enters an infinite loop to process user input.
 * 
 * argc Number of command-line arguments.
 * argv Array of command-line arguments.
 * int Returns 0 on successful execution.
 */

int main(int argc, char *argv[]) {
    progName = argv[0];
    errorPrint(progName, SETUP);

    if (getenv("PATH") == NULL) {
        if (setenv("PATH", "/usr/bin:/bin", 1) < 0) {
            errorPrint("Cannot set PATH environment variable", PERROR);
            exit(EXIT_FAILURE);
        }
    }

    sigset_t set;
    sigemptyset(&set);
    sigprocmask(SIG_SETMASK, &set, NULL);

    installSignals();
    readIshrc();

    char acLine[MAX_LINE_SIZE + 2];
    while (1) {
        fprintf(stdout, "%% ");
        fflush(stdout);
        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }

        size_t len = strlen(acLine);
        if (len > 0 && acLine[len - 1] == '\n') {
            acLine[len - 1] = '\0';
        }

        parseAndExecuteLine(acLine, 0);
    }

    return 0;
}

/**
 * Parses a line of input and executes it.
 * 
 * line The input command line.
 * from_ishrc Indicates if the line came from the .ishrc configuration file.
 */

static void parseAndExecuteLine(const char *line, int from_ishrc) {
    if (line == NULL) return;
    shellHelper(line, from_ishrc);
}

/**
 * Helper function to process a line of input.
 * 
 * Tokenizes the input, checks syntax, and executes commands as necessary.
 * 
 * inLine The input command line.
 * from_ishrc Indicates if the line came from the .ishrc configuration file.
 */

static void shellHelper(const char *inLine, int from_ishrc) {
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
                if (from_ishrc) {
                    fprintf(stdout, "%% %s\n", inLine);
                    fflush(stdout);
                } 
                executeCommand(oTokens);
            }
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

    

    freeTokens(oTokens);
}

/**
 * Executes the given command.
 * 
 * Handles simple commands, redirections, and pipes. 
 * If the command is a built-in command, it is handled directly.
 * 
 * oTokens Array of tokens that make up the command.
 */

static void executeCommand(DynArray_T oTokens) {
    assert(oTokens);
    struct Token *t = DynArray_get(oTokens, 0);
    enum BuiltinType btype = checkBuiltin(t);
    
    if (btype != NORMAL) {
        for (int i = 0; i < DynArray_getLength(oTokens); i++) {
            struct Token *tk = DynArray_get(oTokens, i);
            if (tk->eType == TOKEN_REDIN || tk->eType == TOKEN_REDOUT) {
                errorPrint("Redirection with built-in command is not allowed", FPRINTF);
                return;
            }
        }
        handleBuiltIn(oTokens);
        return;
    }

    int pipe_count = countPipe(oTokens);
    if (pipe_count == 0) {
        char *infile = NULL, *outfile = NULL;
        int in_index = -1, out_index = -1;
        int ret = getRedirections(oTokens, &infile, &outfile, &in_index, &out_index);
        if (ret != 0) return;

        removeRedirections(oTokens, in_index, out_index);

        fflush(NULL);
        pid_t pid = fork();
        if (pid < 0) {
            errorPrint(NULL, PERROR);
            return;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            int infd = -1, outfd = -1;
            if (infile != NULL) {
                infd = open(infile, O_RDONLY);
                if (infd < 0) {
                    errorPrint(infile, PERROR);
                    exit(EXIT_FAILURE);
                }
            }
            if (outfile != NULL) {
                outfd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                if (outfd < 0) {
                    errorPrint(outfile, PERROR);
                    if (infd >= 0) close(infd);
                    exit(EXIT_FAILURE);
                }
            }
            redirectIO(infd, outfd);
            if (infd >= 0) close(infd);
            if (outfd >= 0) close(outfd);
            
            int length = DynArray_getLength(oTokens);
            char **args = calloc(length+1, sizeof(char*));
            for (int i = 0; i < length; i++) {
                struct Token *tt = DynArray_get(oTokens, i);
                args[i] = tt->pcValue;
            }
            args[length] = NULL;

            execvp(args[0], args);
            errorPrint(args[0], PERROR);
            free(args);
            exit(EXIT_FAILURE);
        } else {
            int status;
            wait(&status);
        }

        if (infile) free(infile);
        if (outfile) free(outfile);
    } else {
        if(pipe_count == 0) {

        }
        else {
            int pipes[pipe_count][2];
            for (int i = 0; i < pipe_count; i++) {
                if (pipe(pipes[i]) < 0) {
                    errorPrint(NULL, PERROR);
                    return;
                }
            }

            DynArray_T *commands = NULL;
            int segments = setupPipes(oTokens, &pipe_count, &commands);
            if (segments < 0) {
                if (commands) {

                }
                return;
            }

            for (int i = 0; i < segments; i++) {
                fflush(NULL);
                pid_t pid = fork();
                if (pid < 0) {
                    errorPrint(NULL, PERROR);
                    for (int j = 0; j < segments; j++) {
                        if (commands[j]) DynArray_free(commands[j]);
                    }
                    free(commands);
                    return;
                }
                if (pid == 0) {
                    signal(SIGINT, SIG_DFL);
                    signal(SIGQUIT, SIG_DFL);
                    if (i == 0 && pipe_count > 0) {
                        if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                            errorPrint(NULL, PERROR);
                            exit(EXIT_FAILURE);
                        }
                    }

                    else if (i > 0 && i < segments - 1) {
                        if (dup2(pipes[i-1][0], STDIN_FILENO) < 0) {
                            errorPrint(NULL, PERROR);
                            exit(EXIT_FAILURE);
                        }
                        if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
                            errorPrint(NULL, PERROR);
                            exit(EXIT_FAILURE);
                        }
                    }
                    
                    else if (i == segments - 1 && pipe_count > 0) {
                        if (dup2(pipes[i-1][0], STDIN_FILENO) < 0) {
                            errorPrint(NULL, PERROR);
                            exit(EXIT_FAILURE);
                        }
                    }

                    for (int j = 0; j < pipe_count; j++) {
                        close(pipes[j][0]);
                        close(pipes[j][1]);
                    }

                    int length = DynArray_getLength(commands[i]);
                    char **args = calloc(length+1, sizeof(char*));

                    if (!args) {
                        errorPrint("Cannot allocate memory", FPRINTF);
                        exit(EXIT_FAILURE);
                    }

                    for (int a = 0; a < length; a++) {
                        struct Token *tt = DynArray_get(commands[i], a);
                        args[a] = tt->pcValue;
                    }
                    args[length] = NULL;

                    execvp(args[0], args);
                    errorPrint(args[0], PERROR);
                    free(args);
                    exit(EXIT_FAILURE);
                }
            }
            for (int i = 0; i < pipe_count; i++) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }

            for (int i = 0; i < segments; i++) {
                wait(NULL);
                DynArray_free(commands[i]);
            }
            free(commands);
        }
    }
}

/**
 * Handles built-in commands such as cd, exit, setenv, and unsetenv.
 * 
 * oTokens Array of tokens that make up the built-in command.
 */

static void handleBuiltIn(DynArray_T oTokens) {
    assert(oTokens);
    struct Token *t = DynArray_get(oTokens, 0);
    enum BuiltinType btype = checkBuiltin(t);
    switch(btype) {
        case B_CD:
            builtinCd(oTokens);
            break;
        case B_EXIT:
            builtinExit(oTokens);
            break;
        case B_SETENV:
            builtinSetenv(oTokens);
            break;
        case B_USETENV:
            builtinUnsetenv(oTokens);
            break;
        default:
            break;
    }
}

/**
 * Handles the "setenv" built-in command.
 * 
 * Sets an environment variable to a specified value.
 * 
 * oTokens Array of tokens that make up the command.
 */

static void builtinSetenv(DynArray_T oTokens) {
    int length = DynArray_getLength(oTokens);
    if (length < 2) {
        errorPrint("setenv: Missing variable name", FPRINTF);
        return;
    }
    struct Token *varToken = DynArray_get(oTokens, 1);
    char *var = varToken->pcValue;
    char *val = "";
    if (length > 2) {
        struct Token *valToken = DynArray_get(oTokens, 2);
        val = valToken->pcValue;
    }
    if (setenv(var, val, 1) < 0) {
        errorPrint(NULL, PERROR);
    }
}

/**
 * Handles the "unsetenv" built-in command.
 * 
 * Unsets an environment variable.
 * 
 * oTokens Array of tokens that make up the command.
 */

static void builtinUnsetenv(DynArray_T oTokens) {
    int length = DynArray_getLength(oTokens);
    if (length < 2) {
        return;
    }
    struct Token *varToken = DynArray_get(oTokens, 1);
    char *var = varToken->pcValue;
    unsetenv(var);
}

/**
 * Handles the "cd" built-in command.
 * 
 * Changes the current working directory.
 * 
 * oTokens Array of tokens that make up the command.
 */

static void builtinCd(DynArray_T oTokens) {
    int length = DynArray_getLength(oTokens);
    char *dir = NULL;
    if (length < 2) {
        dir = getenv("HOME");
        if (dir == NULL) {
            errorPrint("cd: HOME not set", FPRINTF);
            return;
        }
    } else {
        struct Token *dirToken = DynArray_get(oTokens, 1);
        dir = dirToken->pcValue;
    }
    if (chdir(dir) < 0) {
        errorPrint(dir, PERROR);
    }
}

/**
 * Handles the "exit" built-in command.
 * 
 * Exits the shell program.
 * 
 * oTokens Array of tokens that make up the command.
 */

static void builtinExit(DynArray_T oTokens) {
    exit(0);
}

/**
 * Redirects input and output streams.
 * 
 * in_fd File descriptor for input redirection.
 * out_fd File descriptor for output redirection.
 */

static void redirectIO(int in_fd, int out_fd) {
    if (in_fd >= 0) {
        dup2(in_fd, STDIN_FILENO);
    }
    if (out_fd >= 0) {
        dup2(out_fd, STDOUT_FILENO);
    }
}

/**
 * Frees the array of tokens.
 * 
 * oTokens Array of tokens to be freed.
 */

static void freeTokens(DynArray_T oTokens) {
    if (oTokens == NULL) return;
    DynArray_map(oTokens, freeToken, NULL);
    DynArray_free(oTokens);
}

/**
 * Reads and executes commands from the .ishrc configuration file.
 */

static void readIshrc(void) {
    char *home = getenv("HOME");
    if (!home) return;
    char path[2048];
    snprintf(path, 2048, "%s/.ishrc", home);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[MAX_LINE_SIZE+2];
    while (fgets(line, MAX_LINE_SIZE, f) != NULL) {
        size_t len = strlen(line);
        if(len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        parseAndExecuteLine(line, 1);
    }
    fclose(f);
}

/**
 * Installs signal handlers for SIGINT, SIGQUIT, and SIGALRM.
 */

static void installSignals(void) {
    struct sigaction sa_int, sa_quit, sa_alrm;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = sigintHandler;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa_int, NULL) < 0) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
    }

    memset(&sa_quit, 0, sizeof(sa_quit));
    sa_quit.sa_handler = sigquitHandler;
    sigemptyset(&sa_quit.sa_mask);
    sa_quit.sa_flags = SA_RESTART;
    if (sigaction(SIGQUIT, &sa_quit, NULL) < 0) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
    }

    memset(&sa_alrm, 0, sizeof(sa_alrm));
    sa_alrm.sa_handler = sigalrmHandler;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = SA_RESTART;
    if (sigaction(SIGALRM, &sa_alrm, NULL) < 0) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
    }
}

/**
 * Handles SIGINT signal.
 * 
 * sig The signal number.
 */

static void sigintHandler(int sig) {
    (void)sig;
}

/**
 * Handles SIGQUIT signal.
 * 
 * sig The signal number.
 */

static void sigquitHandler(int sig) {
    (void)sig;
    if (!quit_requested) {
        fprintf(stdout, "Type Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        quit_requested = 1;
        alarm(5);
    } else {
        exit(0);
    }
}

/**
 * Handles SIGALRM signal.
 * 
 * sig The signal number.
 */

static void sigalrmHandler(int sig) {
    (void)sig;
    quit_requested = 0;
}

/**
 * Extracts and identifies input and output redirections.
 * 
 * oTokens Array of tokens that make up the command.
 * infile Pointer to the input file name (if any).
 * outfile Pointer to the output file name (if any).
 * in_index Index of the input redirection token.
 * out_index Index of the output redirection token.
 * int Returns 0 on success, -1 on failure.
 */

static int getRedirections(DynArray_T oTokens, char **infile, char **outfile, int *in_index, int *out_index) {
    int length = DynArray_getLength(oTokens);
    int inCount = 0, outCount = 0;
    for (int i = 0; i < length; i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN) {
            inCount++;
            if (i == length-1) {
                errorPrint("Standard input redirection without file name", FPRINTF);
                return -1;
            }
            struct Token *next = DynArray_get(oTokens, i+1);
            if (next->eType != TOKEN_WORD) {
                errorPrint("Standard input redirection without file name",FPRINTF);
                return -1;
            }
            *infile = strdup(next->pcValue);
            if (*infile == NULL) {
                errorPrint("Cannot allocate memory",FPRINTF);
                return -1;
            }
            *in_index = i;
        } else if (t->eType == TOKEN_REDOUT) {
            outCount++;
            if (i == length-1) {
                errorPrint("Standard output redirection without file name", FPRINTF);
                return -1;
            }
            struct Token *next = DynArray_get(oTokens, i+1);
            if (next->eType != TOKEN_WORD) {
                errorPrint("Standard output redirection without file name",FPRINTF);
                return -1;
            }
            *outfile = strdup(next->pcValue);
            if (*outfile == NULL) {
                errorPrint("Cannot allocate memory",FPRINTF);
                return -1;
            }
            *out_index = i;
        }
    }
    if (inCount > 1) {
        errorPrint("Multiple redirection of standard input", FPRINTF);
        return -1;
    }
    if (outCount > 1) {
        errorPrint("Multiple redirection of standard out", FPRINTF);
        return -1;
    }
    return 0;
}

/**
 * Removes tokens related to redirections from the token array.
 * 
 * oTokens Array of tokens that make up the command.
 * in_index Index of the input redirection token.
 * out_index Index of the output redirection token.
 */

static void removeRedirections(DynArray_T oTokens, int in_index, int out_index) {
    int max_index = (in_index > out_index) ? in_index : out_index;
    int min_index = (in_index < out_index) ? in_index : out_index;

    if (max_index >= 0) {
        DynArray_removeAt(oTokens, max_index+1);
        DynArray_removeAt(oTokens, max_index);
    }
    if (min_index >= 0 && min_index != max_index) {
        DynArray_removeAt(oTokens, min_index+1);
        DynArray_removeAt(oTokens, min_index);
    }
}

/**
 * Sets up pipes for a command that includes pipes.
 * 
 * oTokens Array of tokens that make up the command.
 * pipe_count Number of pipes in the command.
 * commands Array of commands to be executed.
 * int Returns the number of command segments.
 */

static int setupPipes(DynArray_T oTokens, int *pipe_count, DynArray_T **commands) {
    int total_cmds = *pipe_count + 1;

    *commands = calloc(total_cmds, sizeof(DynArray_T));
    if (*commands == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        return -1;
    }

    int start = 0;
    int cmd_index = 0;
    int length = DynArray_getLength(oTokens);

    #define CLEANUP_AND_RETURN()
        do {
            for (int _i = 0; _i < total_cmds; _i++) {
                if ((*commands)[_i]) DynArray_free((*commands)[_i]);
            }
            free(*commands);
            *commands = NULL;
            return -1;
        } while(0);

    for (int i = 0; i < length; i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_PIPE) {
            if (i == start) {
                errorPrint("Missing command name", FPRINTF);
                CLEANUP_AND_RETURN();
            }

            int cmd_len = i - start;
            if (cmd_len <= 0) {
                errorPrint("Missing command name", FPRINTF);
                CLEANUP_AND_RETURN();
            }

            DynArray_T cmdTokens = DynArray_new(cmd_len);
            if (!cmdTokens) {
                errorPrint("Cannot allocate memory", FPRINTF);
                CLEANUP_AND_RETURN();
            }

            for (int k = start; k < i; k++) {
                DynArray_add(cmdTokens, DynArray_get(oTokens, k));
            }
            (*commands)[cmd_index] = cmdTokens;
            cmd_index++;
            start = i + 1;
        }
    }

    if (start >= length) {
        errorPrint("Missing command name", FPRINTF);
        CLEANUP_AND_RETURN();
    }

    int cmd_len = length - start;
    if (cmd_len <= 0) {
        errorPrint("Missing command name", FPRINTF);
        CLEANUP_AND_RETURN();
    }

    DynArray_T cmdTokens = DynArray_new(cmd_len);
    if (!cmdTokens) {
        errorPrint("Cannot allocate memory", FPRINTF);
        CLEANUP_AND_RETURN();
    }

    for (int k = start; k < length; k++) {
        DynArray_add(cmdTokens, DynArray_get(oTokens, k));
    }
    (*commands)[cmd_index] = cmdTokens;
    cmd_index++;

    return cmd_index;
}

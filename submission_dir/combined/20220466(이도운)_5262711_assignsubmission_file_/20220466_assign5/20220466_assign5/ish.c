/*
 * Author: 이도운
 * Student ID: 20220466
 * Description: Implementation of a shell program that supports 
 * basic commands, file redirection, pipes, and built-in commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "lexsyn.h"
#include "util.h"

#define ISHRC_PATH_SIZE 256

/*
 * Global variable to track SIGQUIT signal status.
 */
static volatile int sigquit_received = 0;

/*
 * Signal handler for SIGQUIT. 
 * Displays a warning on the first press and exits on the second within 5 seconds.
 * Parameters:
 *   signum - Signal number (unused).
 */
void handle_sigquit(int signum) {
    (void)signum;
    if (sigquit_received) {
        printf("Exiting...\n");
        exit(EXIT_SUCCESS);
    } else {
        sigquit_received = 1;
        printf("Type Ctrl-\\ again within 5 seconds to exit.\n");
        alarm(5);
    }
}

/*
 * Signal handler for SIGINT. 
 * Prints the shell prompt when Ctrl+C is pressed.
 * Parameters:
 *   signum - Signal number (unused).
 */
void handle_sigint(int signum) {
    (void)signum;
    printf("\n%% ");
    fflush(stdout);
}

/*
 * Resets the alarm triggered by SIGQUIT.
 * Parameters:
 *   signum - Signal number (unused).
 */
void reset_alarm(int signum) {
    (void)signum;
    sigquit_received = 0;
}

/*
 * Sets up file redirection for commands.
 * Parameters:
 *   oTokens - Array of tokens representing the command and arguments.
 * Returns:
 *   0 on success, negative values on error.
 */
static int setup_redirection(DynArray_T oTokens) {
    int i;
    int infile = -1, outfile = -1;
    struct Token *t;

    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN) {
            if (i + 1 >= DynArray_getLength(oTokens) || 
                ((struct Token *)DynArray_get(oTokens, i + 1))->eType != TOKEN_WORD) {
                return -1;
            }
            if (infile != -1) {
                return -2;  // Multiple input redirection
            }
            t = DynArray_get(oTokens, i + 1);
            infile = open(t->pcValue, O_RDONLY);
            if (infile == -1) {
                return -3;  // Cannot open input file
            }
            dup2(infile, STDIN_FILENO);
            close(infile);
        } else if (t->eType == TOKEN_REDOUT) {
            if (i + 1 >= DynArray_getLength(oTokens) || 
                ((struct Token *)DynArray_get(oTokens, i + 1))->eType != TOKEN_WORD) {
                return -4;  // Missing output file
            }
            if (outfile != -1) {
                return -5;  // Multiple output redirection
            }
            t = DynArray_get(oTokens, i + 1);
            outfile = open(t->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (outfile == -1) {
                return -6;  // Cannot create output file
            }
            dup2(outfile, STDOUT_FILENO);
            close(outfile);
        }
    }
    return 0;
}

/*
 * Executes built-in commands like cd, exit, setenv, and unsetenv.
 * Parameters:
 *   btype - The type of the built-in command.
 *   tokens - Array of tokens representing the command and arguments.
 * Returns:
 *   1 if a built-in command was executed, 0 otherwise.
 */
static int executeBuiltin(enum BuiltinType btype, DynArray_T tokens) {
    switch (btype) {
        case B_CD:
            if (DynArray_getLength(tokens) > 1) {
                char *dir = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
                if (chdir(dir) != 0) {
                    errorPrint(dir, PERROR);
                }
            } else {
                char *home = getenv("HOME");
                if (home && chdir(home) != 0) {
                    errorPrint("cd", PERROR);
                }
            }
            return 1;
        case B_EXIT:
            exit(EXIT_SUCCESS);
        case B_SETENV:
            if (DynArray_getLength(tokens) >= 2) {
                char *var = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
                char *value = (DynArray_getLength(tokens) > 2) ? 
                    ((struct Token *)DynArray_get(tokens, 2))->pcValue : "";
                if (setenv(var, value, 1) != 0) {
                    errorPrint("setenv", PERROR);
                }
            }
            return 1;
        case B_USETENV:
            if (DynArray_getLength(tokens) >= 2) {
                char *var = ((struct Token *)DynArray_get(tokens, 1))->pcValue;
                unsetenv(var);
            }
            return 1;
        default:
            return 0;
    }
}

/*
 * Executes external commands, including handling pipes and redirection.
 * Parameters:
 *   tokens - Array of tokens representing the command and arguments.
 */
static void executeExternal(DynArray_T tokens) {
    pid_t pid;
    int status;
    int pipe_count = countPipe(tokens);
    int curr_pipe = 0;
    int pipefd[2];
    int last_pipe_read = -1;

    if (pipe_count > 0) {
        // Handle pipe
        struct Token *t;
        int i;
        DynArray_T curr_cmd = DynArray_new(0);

        for (i = 0; i <= DynArray_getLength(tokens); i++) {
            if (i == DynArray_getLength(tokens) || 
                ((struct Token *)DynArray_get(tokens, i))->eType == TOKEN_PIPE) {
                
                if (pipe(pipefd) == -1) {
                    errorPrint("pipe", PERROR);
                    return;
                }

                pid = fork();
                if (pid == 0) {  // Child process
                    signal(SIGINT, SIG_DFL);
                    signal(SIGQUIT, SIG_DFL);

                    // Set up pipes
                    if (last_pipe_read != -1) {
                        dup2(last_pipe_read, STDIN_FILENO);
                        close(last_pipe_read);
                    }
                    if (curr_pipe < pipe_count) {
                        close(pipefd[0]);
                        dup2(pipefd[1], STDOUT_FILENO);
                        close(pipefd[1]);
                    }

                    // Execute command
                    char *argv[DynArray_getLength(curr_cmd) + 1];
                    for (int j = 0; j < DynArray_getLength(curr_cmd); j++) {
                        argv[j] = ((struct Token *)DynArray_get(curr_cmd, j))->pcValue;
                    }
                    argv[DynArray_getLength(curr_cmd)] = NULL;

                    execvp(argv[0], argv);
                    errorPrint(argv[0], PERROR);
                    exit(EXIT_FAILURE);
                }

                // Parent process
                if (last_pipe_read != -1)
                    close(last_pipe_read);
                if (curr_pipe < pipe_count) {
                    close(pipefd[1]);
                    last_pipe_read = pipefd[0];
                }
                curr_pipe++;

                // Clear current command array
                DynArray_free(curr_cmd);
                curr_cmd = DynArray_new(0);

            } else if (i < DynArray_getLength(tokens)) {
                t = DynArray_get(tokens, i);
                DynArray_add(curr_cmd, t);
            }
        }

        // Wait for all child processes
        while (wait(NULL) > 0);

    } else {
        // No pipe, simple command
        if ((pid = fork()) < 0) {
            errorPrint(NULL, PERROR);
            return;
        }

        if (pid == 0) {  // Child process
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            int red_result = setup_redirection(tokens);
            if (red_result < 0) {
                exit(EXIT_FAILURE);
            }

            char *argv[DynArray_getLength(tokens) + 1];
            int arg_count = 0;
            for (int i = 0; i < DynArray_getLength(tokens); i++) {
                struct Token *t = DynArray_get(tokens, i);
                if (t->eType == TOKEN_WORD) {
                    argv[arg_count++] = t->pcValue;
                } else if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
                    i++;  // Skip the next token (filename)
                }
            }
            argv[arg_count] = NULL;

            execvp(argv[0], argv);
            errorPrint(argv[0], PERROR);
            exit(EXIT_FAILURE);
        } else {  // Parent process
            waitpid(pid, &status, 0);
        }
    }
}

/*
 * Processes a single input line, parsing tokens and executing commands.
 * Parameters:
 *   inLine - Input command line string.
 *   echo_command - If non-zero, echoes the command to the terminal.
 */
static void processLine(const char *inLine, int __attribute__((unused)) echo_command) {
    DynArray_T oTokens;
    enum LexResult lexcheck;
    enum SyntaxResult syncheck;
    enum BuiltinType btype;

    oTokens = DynArray_new(0);
    
    if (echo_command) {
        printf("%% %s", inLine);
        fflush(stdout);
    }
    if (oTokens == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        return;
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
                btype = checkBuiltin((struct Token *)DynArray_get(oTokens, 0));
                if (!executeBuiltin(btype, oTokens)) {
                    executeExternal(oTokens);
                }
            } else {
                switch (syncheck) {
                    case SYN_FAIL_NOCMD:
                        errorPrint("Missing command name", FPRINTF);
                        break;
                    case SYN_FAIL_MULTREDIN:
                        errorPrint("Multiple redirection of standard input", FPRINTF);
                        break;
                    case SYN_FAIL_MULTREDOUT:
                        errorPrint("Multiple redirection of standard out", FPRINTF);
                        break;
                    case SYN_FAIL_NODESTIN:
                        errorPrint("Standard input redirection without file name", FPRINTF);
                        break;
                    case SYN_FAIL_NODESTOUT:
                        errorPrint("Standard output redirection without file name", FPRINTF);
                        break;
                    default:
                        errorPrint("Syntax error", FPRINTF);
                        break;
                }
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
            errorPrint("Unknown error", FPRINTF);
            break;
    }

    DynArray_free(oTokens);
}

/*
 * Executes commands from the .ishrc file located in the user's home directory.
 */
static void executeIshrc(void) {
    char ishrcPath[ISHRC_PATH_SIZE];
    char line[MAX_LINE_SIZE];
    char *home = getenv("HOME");

    if (home == NULL) {
        return;
    }

    snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", home);
    FILE *file = fopen(ishrcPath, "r");
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            processLine(line, 1);
        }
        fclose(file);
    }
}

/*
 * Main function for the shell program.
 * Initializes signal handlers, executes the .ishrc file, and enters the command loop.
 */
int main(int __attribute__((unused)) argc, char *argv[]) {
    char acLine[MAX_LINE_SIZE];
    
    // Set up signal handlers
    signal(SIGINT, handle_sigint);
    signal(SIGQUIT, handle_sigquit);
    signal(SIGALRM, reset_alarm);

    // Initialize error handling with program name
    errorPrint(argv[0], SETUP);

    // Execute .ishrc file
    executeIshrc();

    // Main command loop
    while (1) {
        printf("%% ");
        fflush(stdout);
        
        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }
        
        processLine(acLine, 0);
    }

    return 0;
}

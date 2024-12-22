#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include "lexsyn.h"
#include "util.h"

#define MAX_LINE_SIZE 1024

/* Function prototypes */
static void shellHelper(const char *inLine);
static void executeCommand(DynArray_T oTokens);
static void handleRedirection(DynArray_T oTokens);
static void setupSignalHandlers();
static void handleSIGQUIT(int sig);

/* Signal handling state */
static volatile int sigquitCount = 0;

/*--------------------------------------------------------------------*/
/* Main shell loop                                                    */
/*--------------------------------------------------------------------*/
int main() {
    char acLine[MAX_LINE_SIZE + 2];
    
    setupSignalHandlers();

    while (1) {
        fprintf(stdout, "%% ");
        fflush(stdout);

        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }
        shellHelper(acLine);
    }
    return 0;
}

/*--------------------------------------------------------------------*/
/* shellHelper: Parse and execute commands                           */
/*--------------------------------------------------------------------*/
static void shellHelper(const char *inLine) {
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
                DynArray_free(oTokens);
                return;
            }

            dumpLex(oTokens);
            syncheck = syntaxCheck(oTokens);

            if (syncheck == SYN_SUCCESS) {
                btype = checkBuiltin(DynArray_get(oTokens, 0));
                if (btype != BUILTIN_NONE) {
                    char *arg = DynArray_get(oTokens, 1);

                    switch (btype) {
                        case BUILTIN_SETENV:
                            setenv(arg, DynArray_get(oTokens, 2), 1);
                            break;

                        case BUILTIN_UNSETENV:
                            unsetenv(arg);
                            break;

                        case BUILTIN_CD:
                            if (arg == NULL) arg = getenv("HOME");
                            if (chdir(arg) != 0) perror("chdir");
                            break;

                        case BUILTIN_EXIT:
                            DynArray_free(oTokens);
                            exit(EXIT_SUCCESS);

                        default:
                            errorPrint("Unknown built-in command", FPRINTF);
                    }
                } else {
                    executeCommand(oTokens);
                }
            } else {
                /* Handle syntax errors */
                switch (syncheck) {
                    case SYN_FAIL_NOCMD:
                        errorPrint("Missing command name", FPRINTF);
                        break;
                    case SYN_FAIL_MULTREDOUT:
                        errorPrint("Multiple redirection of standard out", FPRINTF);
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
                    default:
                        errorPrint("Unknown syntax error", FPRINTF);
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
            errorPrint("lexLine needs to be fixed", FPRINTF);
            exit(EXIT_FAILURE);
    }

    DynArray_free(oTokens);
}

/*--------------------------------------------------------------------*/
/* executeCommand: Fork and execute non-built-in commands            */
/*--------------------------------------------------------------------*/
static void executeCommand(DynArray_T oTokens) {
    pid_t pid;
    int status;
    char **argv = tokensToArgv(oTokens);  /* Helper to convert DynArray to argv */

    if ((pid = fork()) == -1) {
        perror("fork failed");
        return;
    }

    if (pid == 0) {
        /* Child process */
        handleRedirection(oTokens);
        execvp(argv[0], argv);
        perror("execvp failed");
        exit(EXIT_FAILURE);
    } else {
        /* Parent process */
        wait(&status);
    }

    free(argv);
}

/*--------------------------------------------------------------------*/
/* handleRedirection: Manage I/O redirection                         */
/*--------------------------------------------------------------------*/
static void handleRedirection(DynArray_T oTokens) {
    for (size_t i = 0; i < DynArray_getLength(oTokens); i++) {
        char *token = DynArray_get(oTokens, i);

        if (strcmp(token, "<") == 0) {
            int fd_in = open(DynArray_get(oTokens, i + 1), O_RDONLY);
            if (fd_in == -1) {
                perror("Failed to open input file");
                exit(EXIT_FAILURE);
            }
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        } else if (strcmp(token, ">") == 0) {
            int fd_out = open(DynArray_get(oTokens, i + 1), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd_out == -1) {
                perror("Failed to open output file");
                exit(EXIT_FAILURE);
            }
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }
    }
}

/*--------------------------------------------------------------------*/
/* setupSignalHandlers: Install signal handlers                      */
/*--------------------------------------------------------------------*/
static void setupSignalHandlers() {
    struct sigaction sa;
    sa.sa_handler = handleSIGQUIT;
    sa.sa_flags = 0;
    sigaction(SIGQUIT, &sa, NULL);

    signal(SIGINT, SIG_IGN);  /* Ignore Ctrl-c in parent */
}

/*--------------------------------------------------------------------*/
/* handleSIGQUIT: Handle SIGQUIT signal                              */
/*--------------------------------------------------------------------*/
static void handleSIGQUIT(int sig) {
    (void)sig;  /* Suppress unused variable warning */
    sigquitCount++;
    if (sigquitCount == 1) {
        fprintf(stdout, "Type Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        alarm(5);  /* Reset sigquitCount after 5 seconds */
    } else {
        exit(EXIT_SUCCESS);
    }
}

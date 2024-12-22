// assignment5
// --------------------------------------------------------------------
// ish.s
// unix shell
// Student ID: 20220752
// --------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>

#include "lexsyn.h"
#include "util.h"

#define MAX_ARGS_CNT 100
time_t last_quit_time = 0;  // Time of last SIGQUIT


static void sigquit_handler(int signum) 
{
    time_t current_time = time(NULL);

    if (last_quit_time > 0 && difftime(current_time, last_quit_time) <= 5) 
    {
        exit(EXIT_SUCCESS); //terminate the parent process
    } 
    else 
    {
        printf("Type Ctrl-\\ again within 5 seconds to exit.\n"); // If it's the first Ctrl-\ or the second one is not within 5 seconds
        fflush(stdout);
        last_quit_time = current_time;  // Record the time of the first SIGQUIT
    }
}

// Function to close file descriptors
static void closeFds(int inputFd, int outputFd, int numPipes, int pipefds[])
{
    if (inputFd >= 0) 
        close(inputFd);
    if (outputFd >= 0) 
        close(outputFd);
    
    for (int i = 0; i < 2 * numPipes; i++) 
    {
        close(pipefds[i]);
    }
}

static void shellHelper(const char *inLine) 
{
    DynArray_T oTokens;
    enum LexResult lexcheck;
    enum SyntaxResult syncheck;
    enum BuiltinType btype;

    // Allocate a dynamic array for tokens
    oTokens = DynArray_new(0);
    if (oTokens == NULL) 
    {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }

    // Perform lexical analysis on the input line
    lexcheck = lexLine(inLine, oTokens);
    switch (lexcheck) 
    {
        case LEX_SUCCESS:
            if (DynArray_getLength(oTokens) == 0) 
            {
                DynArray_free(oTokens);
                return;
            }

            // Dump tokens for debugging purposes
            dumpLex(oTokens);

            // Perform syntax check on the tokens
            syncheck = syntaxCheck(oTokens);
            if (syncheck == SYN_SUCCESS) 
            {
                // Count the number of pipes in the command line
                int numPipes = countPipe(oTokens);
                int pipefds[2 * numPipes];
                // Create pipes as needed
                for (int i = 0; i < numPipes; i++) 
                {
                    if (pipe(pipefds + i * 2) == -1) 
                    {
                        perror("pipe error");
                        exit(EXIT_FAILURE);
                    }
                }

                int commandIndex = 0;
                int inputFd = -1;
                int outputFd = -1;
                // Loop through each command in the pipeline
                for (int i = 0; i <= numPipes; i++) 
                {
                    char* arguments[MAX_ARGS_CNT] = {NULL};
                    int pipeNum = 0;  
                    char* inputFile = NULL;
                    char* outputFile = NULL;

                    // Check for built-in commands
                    struct Token *cmdToken = DynArray_get(oTokens, commandIndex - pipeNum); // Get the first token of the command
                    // Ensure that the command token is not NULL before calling checkBuiltin
                    if (cmdToken == NULL || cmdToken->pcValue == NULL) 
                    {
                        fprintf(stderr, "Error: Invalid command (NULL pcValue)\n");
                        printf("Error here");
                        continue;
                    }

                    // Parse the command and its arguments
                    while (commandIndex < DynArray_getLength(oTokens)) 
                    {
                        struct Token* token = DynArray_get(oTokens, commandIndex++);
                        if (token->eType == TOKEN_PIPE) break;
                        if (token->eType == TOKEN_REDIN) 
                        {
                            if (inputFile != NULL) 
                            {
                                fprintf(stderr, "./ish: Multiple redirection of standard input\n");
                                goto close_fds;
                            }
                            if (commandIndex >= DynArray_getLength(oTokens)) 
                            {
                                fprintf(stderr, "./ish: Standard input redirection without file name\n");
                                goto close_fds;
                            }
                            inputFile = ((struct Token *)DynArray_get(oTokens, commandIndex++))->pcValue;
                        } 
                        else if (token->eType == TOKEN_REDOUT) 
                        {
                            if (outputFile != NULL) 
                            {
                                fprintf(stderr, "./ish: Multiple redirection of standard output\n");
                                goto close_fds;
                            }
                            if (commandIndex >= DynArray_getLength(oTokens)) 
                            {
                                fprintf(stderr, "./ish: Standard output redirection without file name\n");
                                goto close_fds;
                            }
                            outputFile = ((struct Token *)DynArray_get(oTokens, commandIndex++))->pcValue;
                        } 
                        else 
                        {
                            arguments[pipeNum++] = token->pcValue;  // Changed from `j` to `pipeNum`
                        }
                    }

                    if (pipeNum == 0) 
                    {
                        // Skip empty commands caused by multiple pipes or redirection errors
                        continue;
                    }

                    btype = checkBuiltin(cmdToken);  // Check if it is a built-in command
                    if (btype == B_CD || btype == B_EXIT || btype == B_SETENV || btype == B_USETENV) 
                    {  // If it is a built-in command
                        // Handle built-in commands
                        if (btype == B_CD) 
                        {
                            if (pipeNum == 1) 
                            {
                                chdir(getenv("HOME"));
                            } 
                            else if (pipeNum > 2) 
                            {
                                fprintf(stderr, "./ish: cd takes one parameter\n");
                            } 
                            else 
                            {
                                if (chdir(arguments[1]) == -1) 
                                {
                                    perror("");
                                }
                            }
                        } 
                        else if (btype == B_EXIT) 
                        {
                            exit(EXIT_SUCCESS);
                        } 
                        else if (btype == B_SETENV) 
                        {
                            if (pipeNum == 2) 
                            {
                                if (setenv(arguments[1], "", 1) != 0) 
                                {
                                    perror("setenv");
                                }
                            } 
                            else if (pipeNum == 3) 
                            {
                                if (setenv(arguments[1], arguments[2], 1) != 0) 
                                {
                                    perror("setenv");
                                }
                            } 
                            else 
                            {
                                fprintf(stderr, "setenv: setenv takes one or two parameters\n");
                            }
                        } 
                        else if (btype == B_USETENV) 
                        {
                            if (pipeNum == 2) 
                            {
                                if (unsetenv(arguments[1]) != 0) 
                                {
                                    perror("unsetenv");
                                }
                            } 
                            else 
                            {
                                fprintf(stderr, "unsetenv: unsetenv takes one parameter\n");
                            }
                        }
                        continue;  // Skip the rest of the process for built-in commands
                    }

                    // Fork a child process to execute the command
                    pid_t pid = fork();
                    if (pid == 0) 
                    {
                        // Pipe connection: connect the output of the previous pipe to the input of the current command
                        if (i != 0) 
                        {
                            dup2(pipefds[(i - 1) * 2], 0);  // stdin
                        }
                        if (i != numPipes) 
                        {
                            dup2(pipefds[i * 2 + 1], 1);  // stdout
                        }

                        // Handle input redirection
                        if (inputFile) 
                        {
                            inputFd = open(inputFile, O_RDONLY);
                            if (inputFd < 0) 
                            {
                                perror("input file error");
                                exit(EXIT_FAILURE);
                            }
                            dup2(inputFd, STDIN_FILENO);  // stdin redirection
                            close(inputFd);
                        }

                        // Handle output redirection
                        if (outputFile) 
                        {
                            outputFd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                            if (outputFd < 0) 
                            {
                                perror("output file error");
                                exit(EXIT_FAILURE);
                            }
                            dup2(outputFd, STDOUT_FILENO);  // stdout redirection
                            close(outputFd);
                        }

                        // Close all pipe file descriptors
                        for (int k = 0; k < 2 * numPipes; k++) 
                        {
                            close(pipefds[k]);
                        }

                        // Prepare command and arguments
                        arguments[pipeNum] = NULL;  // execvp requires a NULL-terminated array
                        signal(SIGINT, SIG_DFL);
                        signal(SIGQUIT, SIG_DFL);
                        // Execute command
                        if (execvp(arguments[0], arguments) == -1) 
                        {
                            fprintf(stderr, "%s: No such file or directory\n", arguments[0]);
                            exit(EXIT_FAILURE);
                        }
                    }
                }

                // Close all pipe file descriptors in the parent process
                for (int i = 0; i < 2 * numPipes; i++) 
                {
                    close(pipefds[i]);
                }

                // Wait for all child processes to complete
                for (int i = 0; i <= numPipes; i++) 
                {
                    wait(NULL);
                }

                // Perform explicit cleanup after processing
                close_fds:
                closeFds(inputFd, outputFd, numPipes, pipefds);
            }

            /* syntax error cases */
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
    DynArray_free(oTokens);
}


int main() 
{
    /* TODO */
    // Unblock SIGINT, SIGQUIT
    sigset_t set;

    // Add SIGINT, SIGQUIT to set
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGQUIT);

    // Unblock the signals
    sigprocmask(SIG_UNBLOCK, &set, NULL);
    // Ignore SIGINT (Ctrl-C) to prevent interruption
    signal(SIGINT, SIG_IGN); 
    signal(SIGQUIT, sigquit_handler);

    errorPrint("./ish", SETUP);

    FILE *fp;
    char *home = getenv("HOME"); // Get the HOME environment variable
    char acLine[MAX_LINE_SIZE + 2]; // Buffer to store the input line
    char filePath[MAX_LINE_SIZE]; // Buffer to store the file path

    snprintf(filePath, sizeof(filePath), "%s/.ishrc", home);  // .ishrc file path

    fp = fopen(filePath, "r");
    if (fp == NULL) 
    {
        fp = stdin;  // If .ishrc doesn't exist, use stdin
    } 
    
    // Main shell loop
    while (1) 
    {
        // Prompt for input if reading from stdin
        if (fp == stdin) 
        {
            fprintf(stdout, "%% ");
            fflush(stdout);
        }

        // Read a line of input
        if (fgets(acLine, MAX_LINE_SIZE, fp) == NULL) 
        {
            if (fp != stdin) 
            {
                fclose(fp);
                fp = stdin;
                continue;
            }

            printf("\n");
            exit(EXIT_SUCCESS);  // Exit on Ctrl-D
        }

        // Ignore empty lines
        if (acLine[0] == '\n') 
        {
            continue;
        }
        
        // Print the command if reading from .ishrc
        if (fp != stdin) 
        {
            fprintf(stdout, "%% %s", acLine);
            fflush(stdout);
        }
        shellHelper(acLine);  // Process the command line input
    }
}

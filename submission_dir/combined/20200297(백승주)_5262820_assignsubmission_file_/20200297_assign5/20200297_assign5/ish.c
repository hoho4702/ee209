/*--------------------------------------------------------------------*/
/* ish.c                                                                */
/* Author: SeungJu Back                                                 */
/* Student ID: 20200297                                                 */
/*                                                                      */
/* Implementation of a minimal Unix shell.                              */
/* This shell supports basic command execution, I/O redirection,        */
/* pipe handling, signal handling, and built-in commands.               */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* Global variables for signal and process handling                     */
/*--------------------------------------------------------------------*/

static int quitSignalCount = 0;     /* Counter for SIGQUIT signals */
static time_t lastQuitTime = 0;     /* Timestamp of last SIGQUIT */
static volatile sig_atomic_t childExited = 0;  /* Flag for child process exit */

/* Forward declarations of helper functions */
static void shellHelper(const char *inLine);

/*--------------------------------------------------------------------*/
/* sigintHandler                                                        */
/* Handles SIGINT (Ctrl-C) signals.                                     */
/*                                                                      */
/* Parameters:                                                          */
/*  sig - Signal number (unused)                                         */
/*                                                                      */
/* Returns: nothing                                                     */
/*                                                                      */
/* Global variables: none                                               */
/* Side effects: Reinstalls itself as the signal handler               */
/*--------------------------------------------------------------------*/
static void sigintHandler(int sig) {
    signal(SIGINT, sigintHandler);
}

/*--------------------------------------------------------------------*/
/* sigchldHandler                                                       */
/* Handles SIGCHLD signals (child process termination).                 */
/*                                                                      */
/* Parameters:                                                          */
/*  sig - Signal number (unused)                                        */
/*                                                                      */
/* Returns: nothing                                                     */
/*                                                                      */
/* Global variables:                                                    */
/*  childExited - Set to 1 when a child process exits                  */
/*                                                                      */
/* Side effects:                                                        */
/*  - Reaps zombie processes                                           */
/*  - Prints background process status                                 */
/*  - Reinstalls itself as the signal handler                          */
/*--------------------------------------------------------------------*/
static void sigchldHandler(int sig) {
    int saved_errno = errno;  /* Save errno */
    int status;
    pid_t pid;

    while (1) {
        pid = waitpid(-1, &status, WNOHANG);
        
        if (pid == 0)  /* No more children have exited */
            break;
            
        if (pid == -1) {
            if (errno == EINTR)  /* Interrupted, try again */
                continue;
            else if (errno == ECHILD)  /* No more children */
                break;
            else
                break;
        }
        
        childExited = 1;
    }
    
    errno = saved_errno;  /* Restore errno */
    signal(SIGCHLD, sigchldHandler);
}

/*--------------------------------------------------------------------*/
/* sigquitHandler                                                       */
/* Handles SIGQUIT (Ctrl-\) signals.                                    */
/*                                                                      */
/* Parameters:                                                          */
/*  sig - Signal number (unused)                                         */
/*                                                                      */
/* Returns: nothing                                                     */
/*                                                                      */
/* Global variables:                                                    */
/*  quitSignalCount - Counts SIGQUIT signals                            */
/*  lastQuitTime - Stores timestamp of last SIGQUIT                     */
/*                                                                      */
/* Side effects:                                                        */
/*  - May exit the program if two SIGQUITs within 5 seconds             */
/*  - Writes to stdout                                                   */
/*  - Reinstalls itself as the signal handler                           */
/*--------------------------------------------------------------------*/
static void sigquitHandler(int sig) {
    time_t currentTime = time(NULL);
    
    if (quitSignalCount == 0 || (currentTime - lastQuitTime) > 5) {
        printf("Type Ctrl-\\ again within 5 seconds to exit.\n");
        quitSignalCount = 1;
        lastQuitTime = currentTime;
    } else {
        exit(EXIT_SUCCESS);
    }
    
    signal(SIGQUIT, sigquitHandler);
}

/*--------------------------------------------------------------------*/
/* processIshrc                                                        */
/* Processes the .ishrc initialization file from the user's home       */
/* directory or falls back to stdin if .ishrc is not present.          */
/*                                                                      */
/* Parameters: none                                                    */
/*                                                                      */
/* Returns: nothing                                                    */
/*                                                                      */
/* Side effects:                                                       */
/*  - Reads commands from .ishrc file in the user's home directory,    */
/*    if it exists.                                                    */
/*  - If .ishrc does not exist, defaults to using stdin for commands.  */
/*  - Writes commands to stdout when reading from .ishrc.              */
/*--------------------------------------------------------------------*/
static void processIshrc() {
    FILE *fp;
    char filePath[MAX_LINE_SIZE];
    char acLine[MAX_LINE_SIZE + 2];
    char *home = getenv("HOME");
    
    /* Construct the file path for .ishrc in the user's home directory */
    if (home != NULL) {
        snprintf(filePath, sizeof(filePath), "%s/.ishrc", home);
    } else {
        /* If HOME is not set, fallback to stdin */
        fp = stdin;
    }

    /* Attempt to open .ishrc file */
    fp = fopen(filePath, "r");
    if (fp == NULL) {
        /* If .ishrc is not found, use stdin for commands */
        fp = stdin;
    }

    /* Read and process commands */
    while (1) {
        /* Prompt for input if reading from stdin */
        if (fp == stdin) {
            printf("%% ");
            fflush(stdout);
        }

        /* Read a line of input */
        if (fgets(acLine, MAX_LINE_SIZE, fp) == NULL) {
            if (fp != stdin) {
                /* End of .ishrc, switch to stdin */
                fclose(fp);
                fp = stdin;
                continue;
            }
            /* End of input (Ctrl-D), exit the shell */
            printf("\n");
            exit(EXIT_SUCCESS);
        }

        /* Ignore empty lines */
        if (acLine[0] == '\n') {
            continue;
        }

        /* Print the command if reading from .ishrc */
        if (fp != stdin) {
            printf("%% %s", acLine);
            fflush(stdout);
        }

        /* Process the command */
        shellHelper(acLine);
    }
}


/*--------------------------------------------------------------------*/
/* handleCd                                                             */
/* Handles the built-in 'cd' command.                                   */
/*                                                                      */
/* Parameters:                                                          */
/*  oTokens - DynArray of tokens containing the command and arguments    */
/*                                                                      */
/* Returns: nothing                                                     */
/*                                                                      */
/* Side effects:                                                        */
/*  - Changes current working directory                                  */
/*  - May write error messages to stderr                                 */
/*--------------------------------------------------------------------*/
static void handleCd(DynArray_T oTokens) {
    char *dir;
    
    if (DynArray_getLength(oTokens) == 1) {
        dir = getenv("HOME");
        if (dir == NULL) {
            errorPrint("HOME not set", FPRINTF);
            return;
        }
    } else if (DynArray_getLength(oTokens) > 2) {
        errorPrint("cd takes one parameter", FPRINTF);
        return;
    } else {
        struct Token *t = DynArray_get(oTokens, 1);
        dir = t->pcValue;
    }
    
    if (chdir(dir) != 0) {
        errorPrint(dir, PERROR);
    }
}

/*--------------------------------------------------------------------*/
/* handleSetenv                                                         */
/* Handles the built-in 'setenv' command.                               */
/*                                                                      */
/* Parameters:                                                          */
/*  oTokens - DynArray of tokens containing the command and arguments    */
/*                                                                      */
/* Returns: nothing                                                     */
/*                                                                      */
/* Side effects:                                                        */
/*  - Modifies environment variables                                     */
/*  - May write error messages to stderr                                 */
/*--------------------------------------------------------------------*/
static void handleSetenv(DynArray_T oTokens) {
    if (DynArray_getLength(oTokens) < 2) {
        errorPrint("setenv takes one or two parameters", FPRINTF);
        return;
    }
    
    struct Token *var = DynArray_get(oTokens, 1);
    char *value = "";
    
    if (DynArray_getLength(oTokens) > 2) {
        struct Token *val = DynArray_get(oTokens, 2);
        value = val->pcValue;
    }
    
    if (setenv(var->pcValue, value, 1) != 0) {
        errorPrint("setenv failed", PERROR);
    }
}

/*--------------------------------------------------------------------*/
/* handleUnsetenv                                                       */
/* Handles the built-in 'unsetenv' command.                             */
/*                                                                      */
/* Parameters:                                                          */
/*  oTokens - DynArray of tokens containing the command and arguments    */
/*                                                                      */
/* Returns: nothing                                                     */
/*                                                                      */
/* Side effects:                                                        */
/*  - Removes environment variables                                      */
/*  - May write error messages to stderr                                 */
/*--------------------------------------------------------------------*/
static void handleUnsetenv(DynArray_T oTokens) {
    if (DynArray_getLength(oTokens) != 2) {
        errorPrint("unsetenv takes one parameter", FPRINTF);
        return;
    }
    
    struct Token *var = DynArray_get(oTokens, 1);
    if (unsetenv(var->pcValue) != 0) {
        errorPrint("unsetenv failed", PERROR);
    }
}

/*--------------------------------------------------------------------*/
/* setupRedirection                                                     */
/* Sets up input/output redirection for a command.                      */
/*                                                                      */
/* Parameters:                                                          */
/*  oTokens - DynArray of tokens containing the command and redirections */
/*  inFd - Pointer to store input file descriptor                        */
/*  outFd - Pointer to store output file descriptor                      */
/*                                                                      */
/* Returns:                                                            */
/*  1 if successful, 0 if error                                         */
/*                                                                      */
/* Side effects:                                                        */
/*  - Opens files for redirection                                       */
/*  - May write error messages to stderr                                */
/*--------------------------------------------------------------------*/
static int setupRedirection(DynArray_T oTokens, int *inFd, int *outFd) {
    int i;
    int inRedirect = 0, outRedirect = 0;
    char *inFile = NULL, *outFile = NULL;
    
    *inFd = -1;   
    *outFd = -1;  
    
    /* Loop through tokens to find redirection operators */
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        
        if (t->eType == TOKEN_REDIN) {
            if (inRedirect) {
                errorPrint("Multiple redirection of standard input", FPRINTF);
                return 0;
            }
            if (i + 1 >= DynArray_getLength(oTokens) || 
                ((struct Token *)DynArray_get(oTokens, i + 1))->eType != TOKEN_WORD) {
                errorPrint("Standard input redirection without file name", FPRINTF);
                return 0;
            }
            inRedirect = 1;
            inFile = ((struct Token *)DynArray_get(oTokens, i + 1))->pcValue;
        }
        else if (t->eType == TOKEN_REDOUT) {
            if (outRedirect) {
                errorPrint("Multiple redirection of standard out", FPRINTF);
                return 0;
            }
            if (i + 1 >= DynArray_getLength(oTokens) || 
                ((struct Token *)DynArray_get(oTokens, i + 1))->eType != TOKEN_WORD) {
                errorPrint("Standard output redirection without file name", FPRINTF);
                return 0;
            }
            outRedirect = 1;
            outFile = ((struct Token *)DynArray_get(oTokens, i + 1))->pcValue;
        }
    }
    
   
    if (inFile) {
        *inFd = open(inFile, O_RDONLY);
        if (*inFd == -1) {
            errorPrint(inFile, PERROR);
            return 0;
        }
    }
    
    if (outFile) {
        *outFd = open(outFile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (*outFd == -1) {
            if (*inFd != -1) {
                close(*inFd); 
                *inFd = -1;
            }
            errorPrint(outFile, PERROR);
            return 0;
        }
    }
    
    return 1;
}

/*--------------------------------------------------------------------*/
/* executeCommand                                                       */
/* Executes a single command with its arguments.                        */
/*                                                                      */
/* Parameters:                                                          */
/*  oTokens - DynArray of tokens containing command and arguments        */
/*  inFd - File descriptor for input redirection (-1 if none)           */
/*  outFd - File descriptor for output redirection (-1 if none)         */
/*                                                                      */
/* Returns:                                                            */
/*  1 if successful, 0 if error                                         */
/*                                                                      */
/* Side effects:                                                        */
/*  - May create child processes                                        */
/*  - May modify file descriptors                                       */
/*  - May write error messages to stderr                                */
/*  - May execute external commands                                      */
/*--------------------------------------------------------------------*/
static int executeCommand(DynArray_T oTokens, int inFd, int outFd) {
   char *argv[MAX_ARGS_CNT];
   int argc = 0;
   int i;
   int isBackground = checkBG(oTokens);
   
   /* Build argument array from tokens */
   for (i = 0; i < DynArray_getLength(oTokens); i++) {
       struct Token *t = DynArray_get(oTokens, i);
       if (t->eType == TOKEN_WORD) {
           if (argc >= MAX_ARGS_CNT - 1) {
               errorPrint("Too many arguments", FPRINTF);
               return 0;
           }
           argv[argc++] = t->pcValue;
       }
       else if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT || 
                t->eType == TOKEN_BG) {
           if (t->eType != TOKEN_BG) {
               i++; /* Skip the filename token */
           }
       }
   }
   argv[argc] = NULL;
   
   /* Check for and handle built-in commands */
   struct Token *firstToken = DynArray_get(oTokens, 0);
   enum BuiltinType btype = checkBuiltin(firstToken);
   
   if (btype != NORMAL) {
       /* Built-in commands shouldn't have redirections or background */
       if ((inFd != -1 || outFd != -1 || isBackground) && btype != B_CD) {
        errorPrint("Invalid redirection or background", FPRINTF);
        return 0;
    }
       
    switch (btype) {
        case B_CD:
            handleCd(oTokens);
            break;
        case B_SETENV:
            handleSetenv(oTokens);
            break;
        case B_USETENV:
            handleUnsetenv(oTokens);
            break;
        case B_EXIT:
            exit(EXIT_SUCCESS);
        default:
            break;
    }
    return 1;
   }
   
   /* Fork and execute external command */
   pid_t pid = fork();
   if (pid == 0) {  /* Child process */
       /* Reset signal handlers to default */
       signal(SIGINT, SIG_DFL);
       signal(SIGQUIT, SIG_DFL);
       signal(SIGCHLD, SIG_DFL);
       
       /* Set up redirections */
       if (inFd != -1) {
           dup2(inFd, STDIN_FILENO);
           close(inFd);
       }
       if (outFd != -1) {
           dup2(outFd, STDOUT_FILENO);
           close(outFd);
       }
       
       execvp(argv[0], argv);
       errorPrint(argv[0], PERROR);
       _exit(EXIT_FAILURE);
   }
   else if (pid < 0) {
       errorPrint("Fork failed", PERROR);
       return 0;
   }
   else {  /* Parent process */
       if (!isBackground) {
           int status;
           pid_t waitResult;
           
           /* Wait for foreground process with EINTR handling */
           while ((waitResult = waitpid(pid, &status, 0)) == -1) {
               if (errno != EINTR) {
                   errorPrint("waitpid failed", PERROR);
                   return 0;
               }
           }
       } else {
           int status;
           waitpid(pid, &status, WNOHANG);  // Non-blocking wait
       }
       return 1;
   }
}

/*--------------------------------------------------------------------*/
/* executePipe                                                          */
/* Executes a pipeline of commands.                                     */
/*                                                                      */
/* Parameters:                                                          */
/*  oTokens - DynArray of tokens containing entire pipeline              */
/*                                                                      */
/* Returns:                                                            */
/*  1 if successful, 0 if error                                         */
/*                                                                      */
/* Side effects:                                                        */
/*  - Creates pipes and child processes                                 */
/*  - May modify file descriptors                                       */
/*  - May write error messages to stderr                                */
/*  - Executes commands in pipeline                                     */
/*--------------------------------------------------------------------*/
static int executePipe(DynArray_T oTokens) {
   int pipeCount = countPipe(oTokens);
   int commandCount = pipeCount + 1;
   int i, j;
   
   /* Handle single command case */
   if (pipeCount == 0) {
       int inFd = -1, outFd = -1;
       if (!setupRedirection(oTokens, &inFd, &outFd)) return 0;
       int result = executeCommand(oTokens, inFd, outFd);
       if (inFd != -1) close(inFd);
       if (outFd != -1) close(outFd);
       return result;
   }
   
   /* Create array of command arrays */
   DynArray_T *commands = malloc(sizeof(DynArray_T) * commandCount);
   if (!commands) {
       errorPrint("Cannot allocate memory", FPRINTF);
       return 0;
   }
   
   /* Initialize command arrays */
   for (i = 0; i < commandCount; i++) {
       commands[i] = DynArray_new(0);
       if (!commands[i]) {
           for (j = 0; j < i; j++) {
               DynArray_map(commands[j], freeToken, NULL);
               DynArray_free(commands[j]);
           }
           free(commands);
           errorPrint("Cannot allocate memory", FPRINTF);
           return 0;
       }
   }
   
   /* Split commands at pipe tokens */
   int currentCommand = 0;
   for (i = 0; i < DynArray_getLength(oTokens); i++) {
       struct Token *t = DynArray_get(oTokens, i);
       if (t->eType == TOKEN_PIPE) {
           if (DynArray_getLength(commands[currentCommand]) == 0) {
               errorPrint("Missing command name before pipe", FPRINTF);
               goto cleanup;
           }
           if (i == DynArray_getLength(oTokens) - 1) {
               errorPrint("Missing command name after pipe", FPRINTF);
               goto cleanup;
           }
           currentCommand++;
       } else {
           struct Token *newToken = makeToken(t->eType, t->pcValue);
           if (!newToken) {
               errorPrint("Cannot allocate memory", FPRINTF);
               goto cleanup;
           }
           DynArray_add(commands[currentCommand], newToken);
       }
   }

   
   /* Create pipes */
   int (*pipes)[2] = malloc(sizeof(int[2]) * pipeCount);
   if (!pipes) {
       errorPrint("Cannot allocate memory", FPRINTF);
       goto cleanup;
   }
   
   /* Create all pipes */
   for (i = 0; i < pipeCount; i++) {
       if (pipe(pipes[i]) == -1) {
           for (j = 0; j < i; j++) {
               close(pipes[j][0]);
               close(pipes[j][1]);
           }
           free(pipes);
           errorPrint("Pipe creation failed", PERROR);
           goto cleanup;
       }
   }
   
   /* Execute all commands in pipeline */
   pid_t *pids = malloc(sizeof(pid_t) * commandCount);
   if (!pids) {
       for (i = 0; i < pipeCount; i++) {
           close(pipes[i][0]);
           close(pipes[i][1]);
       }
       free(pipes);
       errorPrint("Cannot allocate memory", FPRINTF);
       goto cleanup;
   }
   
   for (i = 0; i < commandCount; i++) {
       pids[i] = fork();
       if (pids[i] == 0) {  /* Child process */
           /* Reset signal handlers */
           signal(SIGINT, SIG_DFL);
           signal(SIGQUIT, SIG_DFL);
           signal(SIGCHLD, SIG_DFL);
           
           /* Set up pipes */
           if (i > 0) {
               if (dup2(pipes[i-1][0], STDIN_FILENO) == -1) {
                   errorPrint("Failed to set up pipe input", PERROR);
                   _exit(EXIT_FAILURE);
               }
           }
           if (i < pipeCount) {
               if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                   errorPrint("Failed to set up pipe output", PERROR);
                   _exit(EXIT_FAILURE);
               }
           }
           
           /* Close all pipe ends */
           for (j = 0; j < pipeCount; j++) {
               if (close(pipes[j][0]) == -1) {
                   errorPrint("Failed to close pipe", PERROR);
               }
               if (close(pipes[j][1]) == -1) {
                   errorPrint("Failed to close pipe", PERROR);
               }
           }
           
           /* Set up redirections for first and last commands */
           int inFd = -1, outFd = -1;
           if (i == 0 || i == commandCount-1) {
               if (!setupRedirection(commands[i], &inFd, &outFd)) {
                   free(pipes);
                   free(pids);
                   _exit(EXIT_FAILURE);
               }
               if (inFd != -1 && i == 0) {
                   if (dup2(inFd, STDIN_FILENO) == -1) {
                       errorPrint("Failed to set up input redirection", PERROR);
                       _exit(EXIT_FAILURE);
                   }
                   close(inFd);
               }
               if (outFd != -1 && i == commandCount-1) {
                   if (dup2(outFd, STDOUT_FILENO) == -1) {
                       errorPrint("Failed to set up output redirection", PERROR);
                       _exit(EXIT_FAILURE);
                   }
                   close(outFd);
               }
           }
           
           free(pipes);
           free(pids);
           executeCommand(commands[i], -1, -1);
           _exit(EXIT_FAILURE);
       }
       else if (pids[i] < 0) {
           /* Clean up on fork failure */
           for (j = 0; j < i; j++) {
               kill(pids[j], SIGTERM);
               waitpid(pids[j], NULL, 0);
           }
           for (j = 0; j < pipeCount; j++) {
               if (close(pipes[j][0]) == -1) {
                   errorPrint("Failed to close pipe", PERROR);
               }
               if (close(pipes[j][1]) == -1) {
                   errorPrint("Failed to close pipe", PERROR);
               }
           }
           free(pipes);
           free(pids);
           errorPrint("Fork failed", PERROR);
           goto cleanup;
       }
   }
   
   /* Close all pipe ends in parent */
   for (i = 0; i < pipeCount; i++) {
       if (close(pipes[i][0]) == -1) {
           errorPrint("Failed to close pipe", PERROR);
       }
       if (close(pipes[i][1]) == -1) {
           errorPrint("Failed to close pipe", PERROR);
       }
   }
   
   /* Wait for all children */
   for (i = 0; i < commandCount; i++) {
       int status;
       pid_t waitResult;
       while ((waitResult = waitpid(pids[i], &status, 0)) == -1) {
           if (errno != EINTR) {
               errorPrint("Wait for child failed", PERROR);
               break;
           }
       }
   }
   
   free(pipes);
   free(pids);
   
   /* Clean up command arrays */
   for (i = 0; i < commandCount; i++) {
       DynArray_map(commands[i], freeToken, NULL);
       DynArray_free(commands[i]);
   }
   free(commands);
   return 1;
   
cleanup:
   if (commands) {
       for (i = 0; i < commandCount; i++) {
           if (commands[i]) {
               DynArray_map(commands[i], freeToken, NULL);
               DynArray_free(commands[i]);
           }
       }
       free(commands);
   }
   return 0;
}

/*--------------------------------------------------------------------*/
/* shellHelper                                                          */
/* Processes a single command line.                                     */
/*                                                                      */
/* Parameters:                                                          */
/*  inLine - String containing the command line to process               */
/*                                                                      */
/* Returns: nothing                                                     */
/*                                                                      */
/* Side effects:                                                        */
/*  - May create processes and execute commands                         */
/*  - May modify file descriptors                                       */
/*  - May write to stdout and stderr                                    */
/*  - May modify environment variables                                  */
/*--------------------------------------------------------------------*/
static void shellHelper(const char *inLine) {
    DynArray_T oTokens;
    enum LexResult lexcheck;
    enum SyntaxResult syncheck;
    
    oTokens = DynArray_new(0);
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
            
            dumpLex(oTokens);
            
            syncheck = syntaxCheck(oTokens);
            if (syncheck == SYN_SUCCESS) {
                executePipe(oTokens);
            }
            else {
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
                        errorPrint("Invalid use of background", FPRINTF);
                        break;
                    default:
                        errorPrint("Invalid syntax", FPRINTF);
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
    
    DynArray_map(oTokens, freeToken, NULL);
    DynArray_free(oTokens);
}

/*--------------------------------------------------------------------*/
/* main                                                                 */
/* Entry point of the shell program.                                    */
/*                                                                      */
/* Parameters:                                                          */
/* none                                                                 */
/*                                                                      */
/* Returns:                                                             */
/* 0 on successful exit, non-zero on error                              */
/*                                                                      */
/* Side effects:                                                        */
/* - Sets up signal handlers                                            */
/* - Processes .ishrc file                                              */
/* - Reads from stdin                                                   */
/* - Writes to stdout and stderr                                        */
/* - Creates processes and executes commands                            */
/*--------------------------------------------------------------------*/
int main(void) {
    char acLine[MAX_LINE_SIZE + 2];
    
    /* Install signal handlers for managing interrupts and child processes */
    if (signal(SIGINT, sigintHandler) == SIG_ERR) {
        fprintf(stderr, "./ish: Could not install SIGINT handler\n");
        return EXIT_FAILURE;
    }
    
    if (signal(SIGQUIT, sigquitHandler) == SIG_ERR) {
        fprintf(stderr, "./ish: Could not install SIGQUIT handler\n");
        return EXIT_FAILURE;
    }
    
    if (signal(SIGCHLD, sigchldHandler) == SIG_ERR) {
        fprintf(stderr, "./ish: Could not install SIGCHLD handler\n");
        return EXIT_FAILURE;
    }
    
    /* Initialize error printing with program name */
    errorPrint("./ish", SETUP);
    
    /* Process initialization file */
    processIshrc();
    
    /* Main command processing loop */
    while (1) {
        /* Display prompt and flush stdout */
        printf("%% ");
        fflush(stdout);
        
        /* Read command line */
        if (fgets(acLine, MAX_LINE_SIZE + 2, stdin) == NULL) {
            /* Handle end of file (Ctrl-d) */
            if (feof(stdin)) {
                printf("\n");
                return EXIT_SUCCESS;
            }
            /* Handle other read errors */
            else if (ferror(stdin)) {
                errorPrint("Error reading input", FPRINTF);
                return EXIT_FAILURE;
            }
        }
        
        /* Check for line too long */
        if (strlen(acLine) > MAX_LINE_SIZE && 
            acLine[MAX_LINE_SIZE-1] != '\n') {
            errorPrint("Command line too long", FPRINTF);
            /* Clear the rest of the line */
            int c;
            while ((c = getchar()) != '\n' && c != EOF)
                ;
            continue;
        }
        
        /* Process the command line */
        shellHelper(acLine);
        
        /* Reset child exit flag */
        childExited = 0;
    }
    /* Wait for background processes before exit */
    
    return EXIT_SUCCESS;  /* Never reached */
}
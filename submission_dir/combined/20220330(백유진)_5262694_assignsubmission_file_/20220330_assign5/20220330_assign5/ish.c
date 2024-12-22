// 20220330 백유진 assignment 5
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static time_t last_sigquit_time = 0;

static void sigquit_handler(int iSig) {
    time_t current_time = time(NULL);
    if (last_sigquit_time == 0) {
        // First SIGQUIT received
        printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
        last_sigquit_time = current_time; 
    } else {
        // Potential second SIGQUIT
        double diff = difftime(current_time, last_sigquit_time);
        if (diff <= 5.0) {
            // Received second SIGQUIT within 5 seconds, terminate
            exit(EXIT_SUCCESS);
        } else {
            // More than 5 seconds have passed, treat this like the first SIGQUIT again
            printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
            last_sigquit_time = current_time;
        }
    }
}

int cd_handler(char *argv[]) {
    // cd [dir]
    // If dir is omitted (argv[1] == NULL), use HOME.
    if (argv[1] == NULL) {
        const char *home = getenv("HOME");
        if (!home) {
            errorPrint("HOME not set", FPRINTF);
        }
        if (chdir(home) != 0) {
            errorPrint(NULL, PERROR);
        }
    }
    // cd ~ ~ ...
    else if (argv[2] != NULL) {
        errorPrint("cd takes one parameter", FPRINTF);
    }
    // cd dir
    else {
      if (chdir(argv[1]) != 0) {
          errorPrint(NULL, PERROR);
      }
    }
    return 1;
}

int exit_handler(char *argv[]) {
    exit(0);
    return 1;
}

int setenv_handler(char *argv[]) {
    // setenv var [value]
    // If [value] is omitted, set to empty string.
    // If var doesn't exist, create it; otherwise update it.
    if (!argv[1]) {
        // No var => usage error
        errorPrint("setenv takes one or two parameters", FPRINTF);
        return 1;
    }

    // If value is omitted, set to ""
    const char *value = (argv[2] == NULL) ? "" : argv[2];

    // The '1' tells setenv to overwrite an existing var
    if (setenv(argv[1], value, 1) != 0) {
        errorPrint(NULL, PERROR);
        return 1;
    }
    return 1; 
}

int usetenv_handler(char *argv[]) {
    // unsetenv var
    // If var does not exist, do nothing.
    if (!argv[1]) {
        // No var => usage error
        errorPrint("unsetenv takes one parameter", FPRINTF);
        return 1;
    }
    // If var does not exist, unsetenv simply returns 0, no real error there.
    // We call unsetenv, ignoring "nonexistent" scenario.
    if (unsetenv(argv[1]) != 0) {
        errorPrint(NULL, PERROR);
        return 1;
    }
    return 1;
}

static void
shellHelper(const char *inLine) {
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
      if (DynArray_getLength(oTokens) == 0)
        return;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        int numPipes = countPipe(oTokens);
        int pipefds[2 * numPipes];

        // Create pipes
        for (int i = 0; i < numPipes; i++) {
          if (pipe(&pipefds[i * 2]) == -1) {
            errorPrint("pipe creation failed", FPRINTF);
            exit(EXIT_FAILURE);
          }
        }

        int inLineIndex = 0;

        for (int i = 0; i <= numPipes; i++) {
          
          char *inputFile = NULL;
          char *outputFile = NULL;
          char *argv[MAX_ARGS_CNT] = {NULL};
          struct Token *cmdToken;
          int argvIndex = 0;

          while(inLineIndex < DynArray_getLength(oTokens)) {
            struct Token *token = DynArray_get(oTokens, inLineIndex++);
            if(token->eType == TOKEN_PIPE) {
              break;
            }
            if(token->eType == TOKEN_REDIN) {
              struct Token *inputFile_token = DynArray_get(oTokens, inLineIndex++);
              inputFile = inputFile_token->pcValue;
            } 
            else if (token->eType == TOKEN_REDOUT) {
              struct Token *outputFile_token = DynArray_get(oTokens, inLineIndex++);
              outputFile = outputFile_token->pcValue;
            }
            else {
              if (argvIndex == 0) {
                cmdToken = token;
              }
              argv[argvIndex++] = token->pcValue;
            }
          }

          btype = checkBuiltin(cmdToken);

          int builtinFlag = 0; // check if it is builtin
          switch (btype) {
            case B_CD:
              builtinFlag = cd_handler(argv);
              break;
            case B_EXIT:
              builtinFlag = exit_handler(argv);
              break;
            case B_SETENV:
              builtinFlag = setenv_handler(argv);
              break;
            case B_USETENV:
              builtinFlag = usetenv_handler(argv);
              break;
            default:
              break;
          }

          if (builtinFlag == 1) { // builtin done
            continue;
          }

          // not a builtin
          pid_t pid = fork();
          if (pid < 0) {
            errorPrint("fork failed", FPRINTF);
            exit(EXIT_FAILURE);
          }
          else if (pid == 0) {
            // Child process
            // If not the first command, set up pipe read (stdin from previous pipe)
            if (i > 0) {
              if (dup2(pipefds[(i - 1) * 2], STDIN_FILENO) == -1) {
                errorPrint("dup2 input pipe failed", FPRINTF);
                exit(EXIT_FAILURE);
              }
            }

            // If not the last command, set up pipe write (stdout to next pipe)
            if (i < numPipes) {
              if (dup2(pipefds[i * 2 + 1], STDOUT_FILENO) == -1) {
                errorPrint("dup2 output pipe failed", FPRINTF);
                exit(EXIT_FAILURE);
              }
            }

            // If inputFile is set, redirect stdin from file
            if (inputFile != NULL) {
              int fdIn = open(inputFile, O_RDONLY);
              if (fdIn < 0) {
                // Print an error and exit child (so the pipeline doesn’t continue this command)
                errorPrint(NULL, PERROR);
                exit(EXIT_FAILURE);
              }
              if (dup2(fdIn, STDIN_FILENO) == -1) {
                errorPrint("dup2 inputFile failed", FPRINTF);
                close(fdIn);
                exit(EXIT_FAILURE);
              }
              close(fdIn);
            }

            // If outputFile is set, redirect stdout to file
            if (outputFile != NULL) {
              // Note the permissions are 0600 (owner read/write, nobody else)
              int fdOut = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
              if (fdOut < 0) {
                errorPrint(NULL, PERROR);
                exit(EXIT_FAILURE);
              }
              if (dup2(fdOut, STDOUT_FILENO) == -1) {
                errorPrint("dup2 outputFile failed", FPRINTF);
                close(fdOut);
                exit(EXIT_FAILURE);
              }
              close(fdOut);
            }

            // Close all pipe fds in child
            for (int j = 0; j < 2 * numPipes; j++) {
              close(pipefds[j]);
            }

            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            // Execute the command
            execvp(argv[0], argv);
            errorPrint(argv[0], PERROR);
            exit(EXIT_FAILURE);
          }
          else {
            // Parent process
            // Close the ends of the pipes we no longer need open
            if (i > 0) {
              close(pipefds[(i - 1) * 2]);
            }
            if (i < numPipes) {
              close(pipefds[i * 2 + 1]);
            }
            int status;
            if (waitpid(pid, &status, 0) == -1) {
                errorPrint("waitpid failed", FPRINTF);
            }
          }    
        }
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
}


int main() {

    // unblock SIGINT, SIGQUIT
    sigset_t sSet;
    sigemptyset(&sSet);
    sigaddset(&sSet, SIGINT);
    sigaddset(&sSet, SIGQUIT);
    sigprocmask(SIG_UNBLOCK, &sSet, NULL);

    // ignore SIGINT
    signal(SIGINT, SIG_IGN);
    // install signal handler to SIGQUIT
    signal(SIGQUIT, sigquit_handler);

    // set shell name
    errorPrint("./ish", SETUP);

    char acLine[MAX_LINE_SIZE + 2];
    bool usingIshrc = false;
    FILE *fp = NULL;

    // Get HOME directory
    const char *homeDir = getenv("HOME");
    if (homeDir != NULL) {
        // Construct path: homeDir + "/.ishrc"
        char ishrcPath[1024];
        snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", homeDir);

        fp = fopen(ishrcPath, "r");
        if (fp != NULL) {
            usingIshrc = true;
        }
    }

    while (1) {
        // If reading from ishrc, read from fp, else read from stdin
        FILE *inputStream = usingIshrc ? fp : stdin;

        if(!usingIshrc) {
            fprintf(stdout, "%% ");
            fflush(stdout);
        }

        // Attempt to read a line
        if (fgets(acLine, MAX_LINE_SIZE, inputStream) == NULL) {
            // If we're using ishrc and it ended, close it and switch to stdin
            if (usingIshrc) {
                fclose(fp);
                usingIshrc = false;
                continue; // move on to interactive mode
            } else {
                // If stdin ended, exit
                printf("\n");
                exit(EXIT_SUCCESS);
            }
        }

        if (usingIshrc) {
            // Print the prompt and the command from ishrc
            fprintf(stdout, "%% %s", acLine);
            fflush(stdout);
        }

        // Handle the command line using shellHelper
        shellHelper(acLine);
    }

    return 0;
}


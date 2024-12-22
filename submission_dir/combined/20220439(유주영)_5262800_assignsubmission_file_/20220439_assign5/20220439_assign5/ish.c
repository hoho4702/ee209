#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Further Modified by : Yoo Juyoung  */
/* Student ID : 20220439              */
/* Assignment 5                       */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/
volatile sig_atomic_t quit_count = 0;

/* Handles the SIGINT signal.
Parameters:
- signum: The signal number (unused in this handler).                 */
/* SIGINT : The parent process should ignore the SIGINT signal. */
void sigint_handler(int signum){
  return;
}
/* Handles the SIGQUIT signal.
Parameters:
- signum: The signal number (unused in this handler).
SIGQUIT : 
The parent process should print the message 
"Type Ctrl-\ again within 5 seconds to exit." to the standard output stream. 
If and only if the user indeed types Ctrl-\ again within 5 seconds of wall-clock time,
 then the parent process should terminate.*/
void sigquit_handler(int signum){
  if(quit_count == 0){
    fprintf(stdout, "Type Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    quit_count = 1;

    alarm(5);
  } else {
    exit(EXIT_SUCCESS);
  }
}
/* Handles the SIGALRM signal. 
Parameters:
- signum: The signal number (unused in this handler).
Resets the global variable `quit_count` to 0 when the alarm triggers.*/
void alarm_handler(int signum){
  quit_count = 0;
}

/* Built-in Command Handler                                           */
/*--------------------------------------------------------------------*/

/*Handles built-in shell commands (setenv, unsetenv, cd, exit).
Parameters:
- cmd: The command name to process.
- args: Array of arguments for the command.
Returns:
- 1 if the command was handled as a built-in command.
- 0 if the command was not recognized as a built-in.         */
static int handler_Builtin(char *cmd, char **args) {
  if (strcmp(cmd, "setenv") == 0) {
    if (args[1] && args[2] && (args[3] == NULL)) {
      if (setenv(args[1], args[2], 1) != 0) {
        perror("setenv");
      }
    } else {
      fprintf(stderr, "./ish: setenv requires one or two arguments\n");
    }
    return 1;
  } else if (strcmp(cmd, "unsetenv") == 0) {
    if (args[1] && (args[2] == NULL)) {
      if (unsetenv(args[1]) != 0) {
        perror("unsetenv");
      }
    } else {
      fprintf(stderr, "./ish: unsetenv requires one arguments\n");
    }
    return 1;
  } else if (strcmp(cmd, "cd") == 0) {
    if (args[1] == NULL) {
      chdir(getenv("HOME"));
    } else {
      if (chdir(args[1]) == -1) {
        fprintf(stderr, "./ish: No such file or directory\n");
      }
    }
    return 1;
  } else if (strcmp(cmd, "exit") == 0) {
    exit(0);
  } 
  return 0; // Not a built-in command
}

/* Processes a single line of shell input.
Parameters:
- inLine: The input line to parse and execute. 
Uses lexical and syntax analysis to parse the input and execute commands.
Handles built-in commands directly and spawns child processes for external commands.*/
static void shellHelper(const char *inLine) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  // enum BuiltinType btype;

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }


  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0){
        DynArray_free(oTokens);
        return;
      }

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {

        char* arguments[MAX_ARGS_CNT] = {NULL};
        char* input_file = NULL;
        char* output_file = NULL;
        int input_fd = -1;
        int output_fd = -1;

        for (int cmdIdx = 0, argIdx = 0; cmdIdx < DynArray_getLength(oTokens); ++cmdIdx) {
          struct Token* token = DynArray_get(oTokens, cmdIdx);
        switch (token->eType) {
          case TOKEN_REDIN:
            if (input_file) {
              fprintf(stderr, "./ish: Multiple redirection of standard input\n");
              goto cleanup;
            }
            if (++cmdIdx >= DynArray_getLength(oTokens)) {
              fprintf(stderr, "./ish: Standard input redirection without file name\n");
              goto cleanup;
            }
            input_file = ((struct Token*)DynArray_get(oTokens, cmdIdx))->pcValue;
            break;
          case TOKEN_REDOUT:
            if (output_file) {
              fprintf(stderr, "./ish: Multiple redirection of standard output\n");
              goto cleanup;
            }
            if (++cmdIdx >= DynArray_getLength(oTokens)) {
              fprintf(stderr, "./ish: Standard output redirection without file name\n");
              goto cleanup;
            }
            output_file = ((struct Token*)DynArray_get(oTokens, cmdIdx))->pcValue;
            break;
          default:
            arguments[argIdx++] = token->pcValue;
            break;
          }
        }
        
        if (!arguments[0]) goto cleanup; // No command provided

        // Handle built-in commands
        if (handler_Builtin(arguments[0], arguments)) goto cleanup;

        // Fork a child process to execute the command
        pid_t pid = fork();
        if (pid == 0) {
          // Handle input redirection
          if (input_file) {
            if ((input_fd = open(input_file, O_RDONLY)) < 0) {
              perror("Input file error");
              exit(EXIT_FAILURE);
            }
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
          }
          // Handle output redirection
          if (output_file) {
            if ((output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
                perror("Output file error");
                exit(EXIT_FAILURE);
            }
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
          }

          // Reset signal handler
          signal(SIGINT, SIG_DFL);
          signal(SIGQUIT, SIG_DFL);
          
          if (execvp(arguments[0], arguments) == -1) {
            fprintf(stderr, "%s: No such file or directory\n", arguments[0]);
            exit(EXIT_FAILURE);
          }
        } else if (pid >0) {
          // Wait for the child process
          wait(NULL);
        } else {
          perror("Fork error");
        }

        cleanup:
        // Clean up the file descriptors
        if (input_fd >= 0) close(input_fd);
        if (output_fd >= 0) close(output_fd);
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

/* Main Function                                                      */
/* Initializes signal handlers and processes commands 
from either a script(if `.ishrc` exists) or standard input.
Does not take arguments or return a value. 
 */
int main() {
  signal(SIGINT, sigint_handler);
  signal(SIGQUIT, sigquit_handler);
  signal(SIGALRM, alarm_handler);

  errorPrint("./ish", SETUP);

  FILE* fp = NULL;
  char filePath[MAX_LINE_SIZE];
  char acLine[MAX_LINE_SIZE + 2];

  const char*home = getenv("HOME");
  if(home) {
    snprintf(filePath, sizeof(filePath), "%s/.ishrc",home);
    fp = fopen(filePath, "r");
  }

  if(!fp) fp = stdin;

  while (1) {
    // Prompt for input if reading from stdin
    if (fp == stdin) {
      fprintf(stdout, "%% ");
      fflush(stdout);
    }
    // Read a line of input
    if (fgets(acLine, MAX_LINE_SIZE, fp) == NULL) {
      if (fp != stdin) {
        fclose(fp);
        fp = stdin;
        continue;
      }

      printf("\n");
      exit(EXIT_SUCCESS);
    }
    // Ignore empty lines
    if (acLine[0] == '\n') continue;
    // Print the command if reading from .ishrc
    if (fp != stdin) {
      fprintf(stdout, "%% %s", acLine);
      fflush(stdout);
    }
    // Process the input line
    shellHelper(acLine);
  }
}


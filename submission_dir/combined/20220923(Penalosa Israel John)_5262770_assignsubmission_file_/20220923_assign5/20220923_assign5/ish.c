#include <stdio.h>
#include <stdlib.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

#include <signal.h>
#include <time.h>
#include <fcntl.h>



static void shellHelper(const char *inLine);

void handle_sigint(int sig) {
  //printf("\nCaught SIGINT (Ctrl-C) \n%% ");
  fflush(stdout);
}

void handle_sigquit(int sig) {

  static time_t last_quit_time = 0;
  time_t current_time = time(NULL);

  if (current_time - last_quit_time <= 5) {
    fflush(stdout);
    exit(EXIT_SUCCESS);
  }
  else {
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  }
  last_quit_time = current_time;
  fflush(stdout);
}

void signal_setup() {
  signal(SIGINT, handle_sigint);
  signal(SIGQUIT, handle_sigquit);
}

void executeExternalCommand(DynArray_T oTokens) {
    size_t tokenLen = DynArray_getLength(oTokens);
    if (tokenLen == 0) {
        fprintf(stderr, "./ish: No command provided.\n");
        return;
    }

    int input_fd = -1, output_fd = -1;
    int original_stdin = dup(STDIN_FILENO);
    int original_stdout = dup(STDOUT_FILENO);
    char **args = malloc((tokenLen + 1) * sizeof(char *));
    if (args == NULL) {
        fprintf(stderr, "Error: Memory allocation failed for arguments.\n");
        return;
    }

    size_t argIndex = 0;
    for (size_t i = 0; i < tokenLen; i++) {
        struct Token *token = (struct Token *)DynArray_get(oTokens, i);

        // Check if the token is NULL or empty
        if (!token || !token->pcValue || strlen(token->pcValue) == 0) {
            fprintf(stderr, "./ish: Invalid token encountered.\n");
            goto cleanup;
        }

        if (strcmp(token->pcValue, "<") == 0) {
            // Input redirection
            if (input_fd != -1) {
                fprintf(stderr, "./ish: Multiple redirection of standard input.\n");
                goto cleanup;
            }
            if (i + 1 >= tokenLen) {
                fprintf(stderr, "./ish: No file specified for input redirection.\n");
                goto cleanup;
            }
            i++;
            token = (struct Token *)DynArray_get(oTokens, i);
            if (!token || !token->pcValue) {
                fprintf(stderr, "./ish: No file specified for input redirection.\n");
                goto cleanup;
            }
            input_fd = open(token->pcValue, O_RDONLY);
            if (input_fd == -1) {
                fprintf(stderr, "./ish: %s: No such file or directory.\n", token->pcValue);
                goto cleanup;
            }
        } else if (strcmp(token->pcValue, ">") == 0) {
            // Output redirection
            if (output_fd != -1) {
                fprintf(stderr, "./ish: Multiple redirection of standard output.\n");
                goto cleanup;
            }
            if (i + 1 >= tokenLen) {
                fprintf(stderr, "./ish: No file specified for output redirection.\n");
                goto cleanup;
            }
            i++;
            token = (struct Token *)DynArray_get(oTokens, i);
            if (!token || !token->pcValue) {
                fprintf(stderr, "./ish: No file specified for output redirection.\n");
                goto cleanup;
            }
            output_fd = open(token->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (output_fd == -1) {
                fprintf(stderr, "./ish: %s: Cannot create or open file.\n", token->pcValue);
                goto cleanup;
            }
        } else {
            // Add argument to the command
            args[argIndex++] = token->pcValue;
        }
    }
    args[argIndex] = NULL; // Null-terminate the argument list

    // Fork and execute the command
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        goto cleanup;
    }

    if (pid == 0) {
        // Child process: Redirect I/O and execute command
        if (input_fd != -1) dup2(input_fd, STDIN_FILENO);
        if (output_fd != -1) dup2(output_fd, STDOUT_FILENO);
        execvp(args[0], args);
        perror("execvp"); // If execvp fails
        exit(EXIT_FAILURE);
    } else {
        // Parent process: Wait for child to finish
        int status;
        waitpid(pid, &status, 0);
    }

cleanup:
    if (input_fd != -1) close(input_fd);
    if (output_fd != -1) close(output_fd);
    dup2(original_stdin, STDIN_FILENO);
    dup2(original_stdout, STDOUT_FILENO);
    close(original_stdin);
    close(original_stdout);
    free(args);
}



char *getIshrcPath() {
  char *homeDir = getenv("HOME");
  if (homeDir == NULL) {
    fprintf(stderr, "Error: HOME environment variable not set.\n");
    return NULL;
  }

  // allocate memory for the file path
  size_t pathLen = strlen(homeDir) + strlen("/.ishrc") + 1;
  char *ishrcPath = malloc(pathLen);

  if (ishrcPath == NULL) {
    fprintf(stderr, "Error: Memory allocation failed.\n");
    return NULL;
  }

  // make a path
  snprintf(ishrcPath, pathLen, "%s/.ishrc", homeDir);

  //sprintf(ishrcPath, "%s/.ishrc", homeDir); ni
  
  return ishrcPath;
}

int processIshrc() {
    char *home = getenv("HOME");
    if (home == NULL) {
        errorPrint("HOME environment variable not set.", FPRINTF);
        return -1;
    }

    char ishrcPath[MAX_LINE_SIZE];
    snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", home);

    if (access(ishrcPath, F_OK) != 0) {
        return 0;
    }

    FILE *ishrc = fopen(ishrcPath, "r");
    if (ishrc == NULL) {
        errorPrint("Failed to open .ishrc file.", PERROR);
        return -1;
    }

    char line[MAX_LINE_SIZE];
    while (fgets(line, sizeof(line), ishrc) != NULL) {
        //printf("Debug: Processing line: %s", line);
        shellHelper(line);
    }

    fclose(ishrc);
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
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        switch (btype) {
          case B_EXIT:
            DynArray_free(oTokens);
            exit(EXIT_SUCCESS);
          
          case B_CD:
            if (DynArray_getLength(oTokens) > 1) {
              char *dir = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
              if (chdir(dir) != 0) {
                errorPrint(dir, PERROR);
              }
              else {
                //printf("Directory changed to: %s\n", get_current_dir_name());
              }
            }
            else {
              char *homeDir = getenv("HOME");
              if (homeDir == NULL || chdir(homeDir) != 0) {
                  errorPrint("HOME directory not set or cannot be accessed", FPRINTF);
              } else {
                  //printf("Directory changed to HOME: %s\n", homeDir);
              }
            }
            
            break;

          case B_SETENV:

            if (DynArray_getLength(oTokens) >= 2) {
              char *var = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;

              char *value = (DynArray_getLength(oTokens) > 2) ? ((struct Token *)DynArray_get(oTokens, 2))->pcValue : "";

              setenv(var, value, 1);
              //printf("Debug: Set environment variable %s=%s\n", var, value);
            }
            else {
              errorPrint("Usage: setenv VAR VALUE", FPRINTF);
            }

            break;

          case B_USETENV:
            
            if (DynArray_getLength(oTokens) > 1) {
               char *var = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
              if (unsetenv(var) != 0) {
                errorPrint(var, PERROR);
              }
              if (unsetenv(var) == 0) {
                //printf("Debug: Unset environment variable %s\n", var);
              }
              else {
                errorPrint("Debug: Environment variable does not exist\n", FPRINTF);
              }
            }
            else {
              errorPrint("Usage: unsetenv VAR", FPRINTF);
            }
            
            break;

          case NORMAL:
            // Logic for NORMAL commands (usually external commands)
            executeExternalCommand(oTokens);
            break;

          case B_ALIAS:
              fprintf(stderr, "Alias functionality not implemented.\n");
              break;

          case B_FG:
              fprintf(stderr, "Foreground functionality not implemented.\n");
              break;

          default:
              fprintf(stderr, "Unknown command type.\n");
              break;

          
        }


        // NORMAL, B_EXIT, B_SETENV, B_USETENV, B_CD, B_ALIAS, B_FG
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
  /* TODO */
  signal_setup();

  errorPrint("ish", SETUP);
  setenv("HOME", "/mnt/home/20220923", 1);


  int ishrcStatus = processIshrc();
  if (ishrcStatus == -1) {
    fprintf(stderr, "Warning: Issues occured while processing .ishrc.\n");
  }

  /* TODO */
  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);

    
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n"); 
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
}



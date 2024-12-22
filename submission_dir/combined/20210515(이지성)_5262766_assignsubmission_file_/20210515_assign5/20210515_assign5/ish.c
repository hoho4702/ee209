#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <wait.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/


int quitTimer = 0;

static void handleQuit(int sign) {
    if (quitTimer == 0) {
        fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
        alarm(5);
        quitTimer++;
    } else {
        exit(0);
    }
}

static void handleAlarm(int sign) {
  quitTimer = 0;
}

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
      // fprintf(stdout, "LEX_SUCCESSED! \n");
      if (DynArray_getLength(oTokens) == 0)
        return;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        // while !(\n)
        if (btype == B_CD) {
          int success;
          int tokenCount = DynArray_getLength(oTokens);
          if (tokenCount == 1) {
            const char *homeDir = getenv("HOME");
            if (homeDir != NULL) {
              success = chdir(homeDir);
            } else {
              errorPrint("HOME environment variable not set", FPRINTF);
              success = -1;
            }
          } else if (tokenCount == 2) {
            struct Token *token = DynArray_get(oTokens, 1);
            success = chdir(token->pcValue);
          } else {
            errorPrint("cd requires exactly one argument", FPRINTF);
            success = 0;  // Indicate no directory change attempted
          }
          if (success == -1) {
            struct Token *commandToken = DynArray_get(oTokens, 0);
            perror(commandToken->pcValue);
          }
        } else if (btype == B_FG) {
          // Handle foreground process
          printf("fg: Command not implemented\n");
        } else if (btype == B_EXIT) {
          DynArray_free(oTokens);
          exit(EXIT_SUCCESS);
        } else if (btype == B_SETENV) {
          int success;
          int tokenCount = DynArray_getLength(oTokens);

          if (tokenCount == 2) {
              struct Token *varName = DynArray_get(oTokens, 1);
              success = setenv(varName->pcValue, "", 1);
          } else if (tokenCount == 3) {
              struct Token *varName = DynArray_get(oTokens, 1);
              struct Token *varValue = DynArray_get(oTokens, 2);
              success = setenv(varName->pcValue, varValue->pcValue, 1);
          } else {
              errorPrint("setenv expects one or two arguments", FPRINTF);
              success = 0;  // No environment variable modification attempted
          }

          if (success == -1) {
              struct Token *commandToken = DynArray_get(oTokens, 0);
              perror(commandToken->pcValue);
          }
        } else if (btype == B_USETENV) {
          int success;
          int tokenCount = DynArray_getLength(oTokens);
          if (tokenCount == 2) {
              struct Token *varName = DynArray_get(oTokens, 1);
              success = unsetenv(varName->pcValue);
          } else {
              errorPrint("unsetenv requires exactly one argument", FPRINTF);
              success = 0;  // Indicate no environment variable removal attempted
          }
          if (success == -1) {
              struct Token *commandToken = DynArray_get(oTokens, 0);
              perror(commandToken->pcValue);
          }
          // return 0;
        } else if (btype == B_ALIAS) {
          printf("alias: Command not implemented\n");
        } else {  // btype == NORMAL
          fflush(NULL);
          pid_t pid = fork();
          char *argv[1024] = {0};
          int tokenCount = DynArray_getLength(oTokens);
          for (int i = 0; i < tokenCount; i++) {
            struct Token *token = DynArray_get(oTokens, i);
            if (token->eType == TOKEN_REDIN && pid == 0) {
              // Input redirection
              struct Token *fileToken = DynArray_get(oTokens, i + 1);
              int fd = open(fileToken->pcValue, O_RDONLY);
              if (fd == -1) {
                errorPrint("No such file or directory", FPRINTF);
              } else {
                close(0);  // Close stdin
                dup(fd);   // Duplicate file descriptor to stdin
                close(fd); // Close the original file descriptor
              }
              i++;  // Skip the redirection target token
            } else if (token->eType == TOKEN_REDOUT && pid == 0) {
              // Output redirection
              struct Token *fileToken = DynArray_get(oTokens, i + 1);
              int fd = open(fileToken->pcValue, O_RDWR | O_CREAT | O_TRUNC, 0600);
              if (fd == -1) {
                errorPrint("No such file or directory", FPRINTF);
              } else {
                close(1);  // Close stdout
                dup(fd);   // Duplicate file descriptor to stdout
                close(fd); // Close the original file descriptor
              }
              i++;  // Skip the redirection target token
            }
          }
          // Build argv array
          for (int i = 0; i < tokenCount; i++) {
            argv[i] = ((struct Token *)DynArray_get(oTokens, i))->pcValue;
          }
          if (pid == 0) {
            // Reset signal handlers to default
            if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
              perror("Failed to reset SIGINT handler");
              exit(EXIT_FAILURE);
            }
            if (signal(SIGQUIT, SIG_DFL) == SIG_ERR) {
              perror("Failed to reset SIGQUIT handler");
              exit(EXIT_FAILURE);
            }
            // Execute the command
            execvp(argv[0], argv);
            perror(argv[0]);  // Exec failed
            exit(EXIT_FAILURE);
          }
          // Parent process waits for child
          wait(NULL);
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

int main(int argc, char **argv) {
    char buf[MAX_LINE_SIZE + 2];
    char acLine[MAX_LINE_SIZE + 2];
    errorPrint(argv[0], SETUP);

    // Set up SIGQUIT and SIGALRM handlers with local quitTimer
    signal(SIGQUIT, (void (*)(int))handleQuit);
    signal(SIGALRM, (void (*)(int))handleAlarm);
    signal(SIGINT, SIG_IGN);
    
    sigset_t sSet;
    // Initialize the signal set
    sigemptyset(&sSet);
    sigaddset(&sSet, SIGINT);
    sigaddset(&sSet, SIGQUIT);
    sigaddset(&sSet, SIGALRM);

    // Unblock SIGINT, SIGQUIT, SIGALRM
    if (sigprocmask(SIG_UNBLOCK, &sSet, NULL) != 0) {
        perror("sigprocmask failed");
        return 1;
    }

    char *currentDir = get_current_dir_name();
    if (currentDir == NULL) {
        perror("Failed to get current directory");
        exit(EXIT_FAILURE);
    }

    if (chdir(getenv("HOME")) != 0) {
        perror("Failed to change to home directory");
        free(currentDir);
        exit(EXIT_FAILURE);
    }

    FILE *ishrc = fopen(".ishrc", "r");
    if (ishrc != NULL) {
        while (fgets(buf, MAX_LINE_SIZE, ishrc) != NULL) {
            fprintf(stdout, "%% %s", buf);
            fflush(stdout);
            shellHelper(buf);
        }
        fclose(ishrc);
    }

    if (chdir(currentDir) != 0) {
        perror("Failed to restore original directory");
    }
    free(currentDir);

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

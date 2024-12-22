#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <assert.h>
#include <signal.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

int sigHandlercount = 0;

static void quitHandler(int iSig) {
  if (sigHandlercount) {
    exit(0);
  }
  else {
    sigHandlercount = 1; // First SIGQUIT detected
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5);
  }
}

static void alrmHandler(int iSig) {
  sigHandlercount = 0;
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
        break;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));

        if (btype == B_CD) {
          // Handle 'cd' command
          char *dir = NULL;

          if (DynArray_getLength(oTokens) == 1) {
          }
          else if (DynArray_getLength(oTokens) == 2) {
            dir = ((struct Token *) DynArray_get(oTokens, 1))->pcValue;
          }
          else {
            errorPrint("cd takes one parameter", FPRINTF);
            break;
          }
          if (dir == NULL) {
            dir = getenv("HOME");
          }
          if (chdir(dir) == -1) {
              errorPrint("No such file or directory", FPRINTF);
          }
        }
        else if (btype == B_SETENV) {
          // Handle 'setenv' command
          char *var = NULL;
          char *value = NULL;

          if (DynArray_getLength(oTokens) == 2) {
            var = ((struct Token *) DynArray_get(oTokens, 1))->pcValue;
            setenv(var, "", 1);
          }
          else if (DynArray_getLength(oTokens) == 3 && 
          ((struct Token *) DynArray_get(oTokens, 1))->eType == TOKEN_WORD) {
            var = ((struct Token *) DynArray_get(oTokens, 1))->pcValue;
            value = ((struct Token *) DynArray_get(oTokens, 2))->pcValue;
            setenv(var, value, 1);
          }
          else {
            errorPrint("setenv takes one or two parameters", FPRINTF);
            break;
          }
        }
        else if (btype == B_USETENV) {
          // Handle 'unsetenv' command
          char *var = NULL;

          if (DynArray_getLength(oTokens) == 2) {
            var = ((struct Token *) DynArray_get(oTokens, 1))->pcValue;
            unsetenv(var);
          }
          else {
            errorPrint("unsetenv takes one parameter", FPRINTF);
            break;
          }
        }
        else if (btype == B_EXIT) {
          // Handle 'exit' command
          exit(0);
        }
        else {
          // External Command Execution
          pid_t pid = fork();
          char **args = malloc(sizeof(char *) * (DynArray_getLength(oTokens) + 1));
          if (pid == 0) {
            // Child process

            // Restore SIGINT (Ctrl-C)
            void (*pfRet)(int);
            pfRet = signal(SIGINT, SIG_DFL);
            assert(pfRet != SIG_ERR);

            int inputRedirect = -1;
            int outputRedirect = -1;
            char *inputFile = NULL;
            char *outputFile = NULL;
            
            // Check for input and output redirection
            for (int i = 1; i < DynArray_getLength(oTokens); i = i + 1) {
              struct Token *token = (struct Token *)DynArray_get(oTokens, i);
              
              if (token->eType == TOKEN_REDIN) {
                // Input redirection
                if (i + 1 < DynArray_getLength(oTokens)) {
                  struct Token *fileToken = (struct Token *)DynArray_get(oTokens, i + 1);
                  inputFile = fileToken->pcValue;
                  inputRedirect = open(inputFile, O_RDONLY);
                  if (inputRedirect == -1) {
                    errorPrint("No such file or directory", FPRINTF);
                    close(inputRedirect);
                    exit(EXIT_FAILURE);
                  }
                  dup2(inputRedirect, STDIN_FILENO);
                  close(inputRedirect);
                  i++; // Skip the filename
                }
              }
              else if (token->eType == TOKEN_REDOUT) {
                // Output redirection
                if (i + 1 < DynArray_getLength(oTokens)) {
                  struct Token *fileToken = (struct Token *)DynArray_get(oTokens, i + 1);
                  outputFile = fileToken->pcValue;
                  outputRedirect = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                  if (outputRedirect == -1) {
                    errorPrint("No such file or directory", FPRINTF);
                    close(outputRedirect);
                    exit(EXIT_FAILURE);
                  }
                  dup2(outputRedirect, STDOUT_FILENO);
                  close(outputRedirect);
                  i++; // Skip the filename
                }
              }
            }

            // Prepare arguments for execvp
            int argCount = 0;
            
            for (int i = 0; i < DynArray_getLength(oTokens); i++) {
              struct Token *token = (struct Token *)DynArray_get(oTokens, i);
              
              // Skip redirection tokens and their corresponding filenames
              if (token->eType == TOKEN_REDIN || token->eType == TOKEN_REDOUT) {
                i++; // Skip filename
                continue;
              }
              
              // Skip tokens with background mode
              if (token->eType == TOKEN_BG) {
                continue;
              }
              
              // Add command and arguments
              args[argCount++] = token->pcValue;
            }
            args[argCount] = NULL;
            // Execute the command
            if (execvp(args[0], args) == -1) {
              perror(args[0]);
              exit(EXIT_FAILURE);
            }
          }
          else {
            // Parent process
            int status;
            // Always wait for the child process
            waitpid(pid, &status, 0);
            free(args);
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
  for (int i = 0; i < DynArray_getLength(oTokens); i = i + 1) {
    struct Token *token = NULL;
    token = ((struct Token *) DynArray_get(oTokens, i));
    if (token != NULL) freeToken(token, NULL);
  }
  DynArray_free(oTokens);
}

int main() {
  sigset_t set;

  // Initialize the signal set to empty
  sigemptyset(&set);

  // Add SIGINT, SIGQUIT, and SIGALRM to the set
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGQUIT);
  sigaddset(&set, SIGALRM);

  // Now, unblock these signals
  sigprocmask(SIG_UNBLOCK, &set, NULL);

  // Ignore SIGINT (Ctrl-C) For Parent
  void (*pfRet)(int);
  pfRet = signal(SIGINT, SIG_IGN);
  assert(pfRet != SIG_ERR);

  // Handling SIGQUIT (Ctrl-\)
  pfRet = signal(SIGQUIT, quitHandler);
  assert(pfRet != SIG_ERR);

  // Handling SIGALRM
  pfRet = signal(SIGALRM, alrmHandler);
  assert(pfRet != SIG_ERR);

  // Get the HOME environment variable
  char *homeDir = getenv("HOME");

  // Construct the path to .ishrc file in the HOME directory
  char ishrcPath[MAX_LINE_SIZE + 2];
  snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", homeDir);

  // Check if the .ishrc file exists and is readable
  FILE *ishrcFile = fopen(ishrcPath, "r");
  if (ishrcFile != NULL) {
    // If the file exists, read and interpret each line
    char fileLine[MAX_LINE_SIZE + 2];
    while (fgets(fileLine, sizeof(fileLine), ishrcFile) != NULL) {
      fprintf(stdout, "%% %s", fileLine);
      fflush(stdout);
      errorPrint("./ish", SETUP);
      shellHelper(fileLine);
    }
    fclose(ishrcFile);
  }

  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    errorPrint("./ish", SETUP);
    shellHelper(acLine);
  }
}

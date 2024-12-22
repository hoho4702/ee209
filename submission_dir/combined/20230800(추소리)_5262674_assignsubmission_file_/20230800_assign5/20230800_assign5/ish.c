/*20230800 ChuSori*/
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include "lexsyn.h"
#include "util.h"

#define MAX_LINE_SIZE 1024

static volatile sig_atomic_t quitCount = 0;

void shellInitializer();
static void shellHelper(const char *inLine);
void setEnvironmentVariable(DynArray_T oTokens);
void unsetEnvironmentVariable(DynArray_T oTokens);
void handleSigint(int sig);
void handleSigquit(int sig);
static void executePipedCommands(DynArray_T oTokens);

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

void shellInitializer() {
    errorPrint("./ish", SETUP);
    char *homeDir = getenv("HOME");
    if (homeDir == NULL) {
        perror("Error: Unable to get HOME environment variable.");
        return;
    }

    char filePath[1024];
    snprintf(filePath, sizeof(filePath), "%s/.ishrc", homeDir);

    FILE *file = fopen(filePath, "r");
    if (file == NULL) {
        return;
    }

    char line[MAX_LINE_SIZE];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        printf("%% %s\n", line);
        fflush(stdout);
        shellHelper(line);
    }

    fclose(file);
}

void setEnvironmentVariable(DynArray_T oTokens) {
    assert(oTokens != NULL);
    if (DynArray_getLength(oTokens) < 2 || DynArray_getLength(oTokens) > 3) {
        fprintf(stderr, "Usage: setenv VAR [VALUE]\n");
        return;
    }

    struct Token* varToken = DynArray_get(oTokens, 1);
    struct Token* valueToken = (DynArray_getLength(oTokens) > 2) ? DynArray_get(oTokens, 2) : NULL;

    if (setenv(varToken->pcValue, valueToken ? valueToken->pcValue : "", 1) != 0) {
        perror("setenv failed");
    }
}

void unsetEnvironmentVariable(DynArray_T oTokens) {
    if (DynArray_getLength(oTokens) != 2) {
        fprintf(stderr, "Usage: unsetenv VAR\n");
        return;
    }

    struct Token *varToken = DynArray_get(oTokens, 1);

    if (unsetenv(varToken->pcValue) != 0) {
        perror("unsetenv failed");
    }
}

void changeDirectory(DynArray_T oTokens) {
    assert(oTokens != NULL);

    if (DynArray_getLength(oTokens) > 2) {
        fprintf(stderr, "Usage: cd [path]\n");
        return;
    }

    struct Token *dirToken = (DynArray_getLength(oTokens) == 2) ? DynArray_get(oTokens, 1) : NULL;
    const char *path = dirToken ? dirToken->pcValue : getenv("HOME");

    if (path == NULL) {
        fprintf(stderr, "Error: HOME environment variable is not set\n");
        return;
    }

    if (chdir(path) != 0) {
        perror("cd failed");
    }
}

void exitShell(DynArray_T oTokens) {
    assert(oTokens != NULL);
    int exitCode = 0;
    if (DynArray_getLength(oTokens) > 1) {
        struct Token *token = (struct Token *)DynArray_get(oTokens, 1);  
        if (token != NULL && token->pcValue != NULL) {
            exitCode = atoi(token->pcValue);  
        }
    }
    exit(exitCode);
}

void handleBuiltinCommand(DynArray_T oTokens) {
    if (DynArray_getLength(oTokens) == 0) {
        fprintf(stderr, "Error: No command provided\n");
        return;
    }

    struct Token *cmdToken = DynArray_get(oTokens, 0);
    enum BuiltinType cmdType = checkBuiltin(cmdToken);

    switch (cmdType) {
        case B_SETENV:
            setEnvironmentVariable(oTokens);
            break;
        case B_USETENV:
            unsetEnvironmentVariable(oTokens);
            break;
        case B_CD:
            changeDirectory(oTokens);
            break;
        case B_EXIT:
            exitShell(oTokens);
            break;
        default:
            fprintf(stderr, "Unknown builtin command: %s\n", cmdToken->pcValue);
            break;
    }
}

void handleSigint(int sig) {
    fprintf(stdout, "\n[Parent Process] SIGINT ignored\n%% ");
    fflush(stdout);
}

void handleSigquit(int sig) {
    static time_t firstSigquitTime = 0;
    time_t currentTime = time(NULL);

    if (quitCount == 0) {
        fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n%% ");
        fflush(stdout);
        firstSigquitTime = currentTime;
        quitCount++;
    } else {
        if (difftime(currentTime, firstSigquitTime) <= 5) {
            fprintf(stdout, "\nExiting shell...\n");
            fflush(stdout);
            exit(EXIT_SUCCESS);
        } else {
            quitCount = 0;
        }
    }
}

static void executePipedCommands(DynArray_T oTokens) {
    int pipeCount = 0;
    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *token = DynArray_get(oTokens, i);
        if (strcmp(token->pcValue, "|") == 0) {
            pipeCount++;
        }
    }

    int pipefds[2 * pipeCount];
    for (int i = 0; i < pipeCount; i++) {
        if (pipe(pipefds + i * 2) < 0) {
            perror("Pipe creation failed");
            exit(EXIT_FAILURE);
        }
    }

    int commandStart = 0;
    int j = 0;
    for (int i = 0; i <= DynArray_getLength(oTokens); i++) {
        struct Token *token = (i < DynArray_getLength(oTokens)) ? DynArray_get(oTokens, i) : NULL;
        if (token == NULL || strcmp(token->pcValue, "|") == 0) {
            pid_t pid = fork();
            if (pid == 0) {
                if (j > 0) {
                    dup2(pipefds[(j - 1) * 2], STDIN_FILENO);
                }
                if (j < pipeCount) {
                    dup2(pipefds[j * 2 + 1], STDOUT_FILENO);
                }
                for (int k = 0; k < 2 * pipeCount; k++) {
                    close(pipefds[k]);
                }

                int cmdLen = i - commandStart;
                char **argv = malloc((cmdLen + 1) * sizeof(char *));
                if (argv == NULL) {
                    perror("Memory allocation failed");
                    exit(EXIT_FAILURE);
                }

                for (int k = 0; k < cmdLen; k++) {
                    struct Token *cmdToken = DynArray_get(oTokens, commandStart + k);
                    argv[k] = cmdToken->pcValue;
                }
                argv[cmdLen] = NULL;

                execvp(argv[0], argv);
                perror("Exec failed");
                free(argv);
                exit(EXIT_FAILURE);
            } else if (pid < 0) {
                perror("Fork failed");
                exit(EXIT_FAILURE);
            }

            commandStart = i + 1;
            j++;
        }
    }

    for (int i = 0; i < 2 * pipeCount; i++) {
        close(pipefds[i]);
    }
    for (int i = 0; i <= pipeCount; i++) {
        wait(NULL);
    }
}

void freeTokens(DynArray_T oTokens) {
    if (oTokens == NULL) {
        fprintf(stderr, "Error: Null token array\n");
        return;
    }

    for (size_t i = 0; i < DynArray_getLength(oTokens); i++) {
        freeToken(DynArray_get(oTokens, i), NULL);
    }
    DynArray_free(oTokens);
}

static void handleRedirectionCommand(DynArray_T oTokens) {
    assert(oTokens != NULL);

    int hasPipe = 0;
    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_PIPE) {
            hasPipe = 1;
            break;
        }
    }

    if (hasPipe) {
        executePipedCommands(oTokens);
        return;
    }

    FILE *inputFile = NULL, *outputFile = NULL;
    int inputFd = -1, outputFd = -1;
    int savedStdout = -1, savedStdin = -1;

    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN) {
            if (i + 1 < DynArray_getLength(oTokens)) {
                struct Token *fileToken = DynArray_get(oTokens, i + 1);
                inputFile = fopen(fileToken->pcValue, "r");
                if (inputFile == NULL) {
                    perror("Failed to open input file");
                    goto cleanup;
                }
                inputFd = fileno(inputFile);
                savedStdin = dup(STDIN_FILENO);
                if (dup2(inputFd, STDIN_FILENO) == -1) {
                    perror("Failed to redirect input");
                    goto cleanup;
                }
                DynArray_removeAt(oTokens, i);
                DynArray_removeAt(oTokens, i);
                i--;
            } else {
                fprintf(stderr, "Syntax error: No input file specified\n");
                goto cleanup;
            }
        } else if (t->eType == TOKEN_REDOUT) {
            if (i + 1 < DynArray_getLength(oTokens)) {
                struct Token *fileToken = DynArray_get(oTokens, i + 1);
                outputFile = fopen(fileToken->pcValue, "w");
                if (outputFile == NULL) {
                    perror("Failed to open output file");
                    goto cleanup;
                }
                outputFd = fileno(outputFile);
                savedStdout = dup(STDOUT_FILENO);
                if (dup2(outputFd, STDOUT_FILENO) == -1) {
                    perror("Failed to redirect output");
                    goto cleanup;
                }
                DynArray_removeAt(oTokens, i);
                DynArray_removeAt(oTokens, i);
                i--;
            } else {
                fprintf(stderr, "Syntax error: No output file specified\n");
                goto cleanup;
            }
        }
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork failed");
        goto cleanup;
    }

    if (pid == 0) {
        char **argv = malloc((DynArray_getLength(oTokens) + 1) * sizeof(char *));
        if (argv == NULL) {
            perror("Memory allocation failed");
            freeTokens(oTokens);
            exit(EXIT_FAILURE);
        }

        for (int i = 0; i < DynArray_getLength(oTokens); i++) {
            struct Token *token = DynArray_get(oTokens, i);
            argv[i] = token->pcValue;
        }
        argv[DynArray_getLength(oTokens)] = NULL;

        execvp(argv[0], argv);
        perror("Execution failed");
        free(argv);
        freeTokens(oTokens);
        exit(EXIT_FAILURE);
    } else {
        wait(NULL);
    }

cleanup:
    if (inputFile != NULL) {
        fclose(inputFile);
        if (savedStdin != -1) {
            dup2(savedStdin, STDIN_FILENO);
            close(savedStdin);
        }
    }
    if (outputFile != NULL) {
        fclose(outputFile);
        if (savedStdout != -1) {
            dup2(savedStdout, STDOUT_FILENO);
            close(savedStdout);
        }
    }
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
        if (btype != NORMAL) {
            handleBuiltinCommand(oTokens);
        } else {
    int hasPipe = 0;
    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_PIPE) { 
            hasPipe = 1;
            break;
        }
    }
    if (hasPipe) {
        executePipedCommands(oTokens);
    } else {
        handleRedirectionCommand(oTokens); 
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

  freeTokens(oTokens);
}

int main() {
  setenv("ISHNAME", "./ish", 1);
  shellInitializer();

  /* Register signal handlers */
  signal(SIGINT, handleSigint);
  signal(SIGQUIT, handleSigquit);

  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
        fprintf(stdout, "%% ");
        fflush(stdout);
        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            if (feof(stdin)) {
                printf("\n");
                break;
            } else {
                perror("Error reading input");
                continue;
            }
        }
        acLine[strcspn(acLine, "\n")] = '\0'; // Remove newline character
        shellHelper(acLine);
    }

}
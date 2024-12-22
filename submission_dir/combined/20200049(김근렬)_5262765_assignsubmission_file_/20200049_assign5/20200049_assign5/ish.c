#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include "lexsyn.h"
#include "util.h"
#include "token.h"
#include "dynarray.h"

#define MAX_LINE_SIZE 1024

/*Variables to handle SIGQUIT*/
int sigQuitCount = 0;
int lastQuitTime = 0;

/* Signal handler for SIGQUIT */
void handleSigQuit(int sig) {
  (void) sig;
  time_t now = time(NULL);
  if (now - lastQuitTime < 5) {
    sigQuitCount++;
  } else {
    sigQuitCount = 1;
    printf("\n");
  }
  lastQuitTime = now;
  if (sigQuitCount == 2) {
    exit(EXIT_SUCCESS);
  } else {
    printf("Type Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
  }
}

/*Handle Pipes*/
void handlePipes(DynArray_T commands) {
  size_t numCommands = DynArray_getLength(commands);
  int pipefds[2 * (numCommands - 1)];
  pid_t pid;
  int status;
  /*Generating Pipes*/
  for (size_t i = 0; i < numCommands - 1; i++) {
    if (pipe(pipefds + 2 * i) == -1) {
      errorPrint("pipe", PERROR);
      return;
    }
  }
  for (size_t i = 0; i < numCommands; i++) {
    pid = fork();
    if (pid < 0) {
      errorPrint("fork", PERROR);
      return;
    }
    /*Child Process*/
    if (pid == 0) {
      /*Redirect Input*/
      if (i > 0) {
        if (dup2(pipefds[2 * (i - 1)], STDIN_FILENO) == -1) {
          errorPrint("dup2", PERROR);
          exit(EXIT_FAILURE);
        }
      }
      /*Redirect Output*/
      if (i < numCommands - 1) {
        if (dup2(pipefds[2 * i + 1], STDOUT_FILENO) == -1) {
          errorPrint("dup2", PERROR);
          exit(EXIT_FAILURE);
        }
      }
      /*Close pipe descriptors*/
      for (size_t j = 0; j < 2 * (numCommands - 1); j++) {
        close(pipefds[j]);
      }
      /*Execute Command*/
      DynArray_T oTokens = DynArray_get(commands, i);
      size_t tokenCount = DynArray_getLength(oTokens);
      char **args = malloc((tokenCount + 1) * sizeof(char *));
      if (args == NULL) {
        errorPrint("malloc", PERROR);
        exit(EXIT_FAILURE);
      }
      for (size_t k = 0; k < tokenCount; k++) {
        args[k] = ((struct Token *)DynArray_get(oTokens, k))->pcValue;
      }
      args[tokenCount] = NULL;
      execvp(args[0], args);
      errorPrint(args[0], PERROR);
      free(args);
      exit(EXIT_FAILURE);
    }
  }
  /*Close pipe descriptors on the parent process*/
  for (size_t i = 0; i < 2 * (numCommands - 1); i++) {
    close(pipefds[i]);
  }
  /*Wait for child*/
  for (size_t i = 0; i < numCommands; i++) {
    wait(&status);
  }
}

/* Handle Redirection */
int handleRedirection(DynArray_T oTokens) {
  int fd_in = -1, fd_out = -1;
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    struct Token *t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_REDIN) {
      struct Token *next = DynArray_get(oTokens, i + 1);
      /*Open input*/
      fd_in = open(next->pcValue, O_RDONLY);
      if (fd_in == -1) {
        errorPrint("open", PERROR);
        return -1;
      }
      /*Redirect Input*/
      if (dup2(fd_in, STDIN_FILENO) == -1) {
        errorPrint("dup2", PERROR);
        close(fd_in);
        return -1;
      }
      close(fd_in);
    } else if (t->eType == TOKEN_REDOUT) {
      struct Token *next = DynArray_get(oTokens, i + 1);
      /*Open output*/
      fd_out = open(next->pcValue, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
      if (fd_out == -1) {
        errorPrint("open", PERROR);
        return -1;
      }
      /*Redirect Output*/
      if (dup2(fd_out, STDOUT_FILENO) == -1) {
        errorPrint("dup2", PERROR);
        close(fd_out);
        return -1;
      }
      close(fd_out);
    }
  }
  return 0;
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
        /*check for pipes*/
        int numPipes = countPipe(oTokens);
        if (numPipes > 0) {
          DynArray_T commands = DynArray_new(0);
          DynArray_T currentCommand = DynArray_new(0);
          for (int i = 0; i < DynArray_getLength(oTokens); i++) {
            struct Token *t = DynArray_get(oTokens, i);
            if (t->eType == TOKEN_PIPE) {
              DynArray_add(commands, currentCommand);
              currentCommand = DynArray_new(0);
            } else {
              DynArray_add(currentCommand, t);
            }
          }
          DynArray_add(commands, currentCommand);
          handlePipes(commands);
          for (int i = 0; i < DynArray_getLength(commands); i++) {
            DynArray_T acommand = DynArray_get(commands, i);
            for (int j = 0; j < DynArray_getLength(acommand); j++){
              freeToken(DynArray_get(acommand, j), NULL);
            }
            DynArray_free(acommand);
          }
          DynArray_free(commands);
          DynArray_free(oTokens);
          return;
        }
        if(btype == B_CD) {
          const char *dir = DynArray_get(oTokens, 1) ? ((struct Token *)DynArray_get(oTokens, 1))->pcValue : getenv("HOME");
          if (chdir(dir) != 0) {
            errorPrint("cd", PERROR);
          }
          return;
        } else if (btype == B_EXIT) {
          exit(EXIT_SUCCESS);
          
        } else if (btype == B_SETENV) {
          const char *var = DynArray_get(oTokens, 1) ? ((struct Token *)DynArray_get(oTokens, 1))->pcValue : NULL;
          const char *value = DynArray_get(oTokens, 2) ? ((struct Token *)DynArray_get(oTokens, 2))->pcValue : "";
          if (var == NULL || setenv(var, value, 1) != 0) {
            errorPrint("setenv", PERROR);
          }
          return;
        } else if (btype == B_USETENV) {
          const char *var = DynArray_get(oTokens, 1) ? ((struct Token *)DynArray_get(oTokens, 1))->pcValue : NULL;
          if (var == NULL || unsetenv(var) != 0) {
            errorPrint("unsetenv", PERROR);
          }
          return;
        }
        pid_t pid = fork();
        if (pid < 0) {
          errorPrint("fork", PERROR);
          return;
        }
        if (pid == 0) {
          /* Child process */
          signal(SIGINT, SIG_DFL);
          signal(SIGQUIT, SIG_DFL);

          if (handleRedirection(oTokens) < 0) {
              exit(EXIT_FAILURE);
          }

          size_t tokenCount = DynArray_getLength(oTokens);
          char **args = malloc((tokenCount + 1) * sizeof(char *));
          if (args == NULL) {
            errorPrint("malloc", PERROR);
            exit(EXIT_FAILURE);
          }

          for (size_t i = 0; i < tokenCount; i++) {
              args[i] = ((struct Token *)DynArray_get(oTokens, i))->pcValue;
          }
          args[tokenCount] = NULL;
          execvp(args[0], args);
          errorPrint(args[0], PERROR);
          free(args);
          exit(EXIT_FAILURE);
        } else {
          /* Parent process waits for child */
          waitpid(pid, NULL, 0);
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
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    freeToken(DynArray_get(oTokens, i), NULL);
  }
  DynArray_free(oTokens);
}

/* Handle .ishrc if existing */
void handleIshrc(void) {
  const char *home = getenv("HOME");
  if (home == NULL) {
    return;
  }
  size_t pathLength = strlen(home) + strlen("/.ishrc") + 1;
  char *path = malloc(pathLength);
  if (path == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    return;
  }
  strcpy(path, home);
  strcat(path, "/.ishrc");
  FILE *file = fopen(path, "r");
  free(path);
  if (file == NULL) {
    return;
  }
  char line[MAX_LINE_SIZE + 2];
  while (fgets(line, sizeof(line), file)) {
    size_t len = strlen(line);
    if (len >0 && line[len - 1] == '\n'){
      line[len - 1] = '\0';
    }
    printf("%% %s\n", line);
    fflush(stdout);
    shellHelper(line);
  }
  fclose(file);
}

/* Main function */
int main(int argc, char *argv[]) {
  (void)argc;
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, handleSigQuit);
  const char *shellName = argv[0];
  errorPrint((char *)shellName, SETUP);
  handleIshrc();
  char acLine[MAX_LINE_SIZE + 2];
  
  while (1) {
    printf("%% ");
    fflush(stdout);
    if (fgets(acLine, sizeof(acLine), stdin) == NULL) {
      printf("\n");
      break;
    }
    shellHelper(acLine);
  }
  return EXIT_SUCCESS;
}

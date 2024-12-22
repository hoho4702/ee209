/* 20230540 Lee Jaeyun
Implements a simple shell that processes commands,
supports piping and redirection, and handles user input and signals.
It uses lexical analysis to parse and execute commands. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <unistd.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

void executePipeCommands(DynArray_T oTokens);

/* Function to handle the Ctrl-\ signal
Parameters: signalNum: An integer representing the signal number.
Returns: None. */
static void signalExit(int signalNum) {
  exit(EXIT_SUCCESS);
}

/* Function to handle the Ctrl-\ signal with a delay
Parameters: signalNum: An integer representing the signal number. */
static void signalQuit(int signalNum) {
  assert(signal(SIGQUIT, signalExit) != SIG_ERR);
  printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  fflush(stdout);
  alarm(0);
  alarm(5);
}

// Function to handle the alarm signal
static void signalAlarm(int signalNum) {
  assert(signal(SIGQUIT, signalQuit) != SIG_ERR);
}

// Function to set up signal handlers
void initializeSignalHandlers() {
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigaddset(&signals, SIGQUIT);
  sigaddset(&signals, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &signals, NULL);
  assert(signal(SIGINT, SIG_IGN) != SIG_ERR);
  assert(signal(SIGQUIT, signalQuit) != SIG_ERR);
  assert(signal(SIGALRM, signalAlarm) != SIG_ERR);
}

// Function to find the index of the pipe token in the token array
int getPipeIndex(DynArray_T oTokens) {
  assert(oTokens != NULL);
  struct Token *t;
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE) {
      return i;
    }
  }
  return -1;
}

// parent process
void executeParentProcess (int pipefd[], DynArray_T oTokens, int pipeIndex) {
  int status;
  wait(&status);
  close(pipefd[1]);
  if (dup2(pipefd[1], STDIN_FILENO) == -1) {
    exit(EXIT_FAILURE);
  }
  DynArray_removeRange(oTokens, 0, pipeIndex + 1);
  executePipeCommands(oTokens);
}

// child process
void executeChildProcess (int pipefd[], DynArray_T oTokens, int pipeIndex) {
  close(pipefd[0]);
  if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
    exit(EXIT_FAILURE);
  }
  char *args[pipeIndex + 1];
  DynArray_parseTokens(oTokens, args, NULL);
  execvp(args[0], args);
  exit(EXIT_FAILURE);  
}

void executePipeCommands(DynArray_T oTokens) {
  int pipefd[2];
  int pipeIndex = getPipeIndex(oTokens);
  int pipeNum = countPipe(oTokens);

  if (pipeNum == 0) {
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGINT);
    char *args[DynArray_getLength(oTokens)+1];
    DynArray_parseTokens(oTokens, args, NULL);
    sigprocmask(SIG_UNBLOCK, &signals, NULL);
    execvp(args[0], args);
    return;
  }

  if (pipe(pipefd) == -1) {
    errorPrint(NULL, PERROR);
    return;
  }

  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0) {
    errorPrint(NULL, PERROR);
    close(pipefd[0]);
    close(pipefd[1]);
    return;
  }
  else if (pid == 0) {
    executeChildProcess(pipefd, oTokens, pipeIndex);
  }
  else {
    executeParentProcess(pipefd, oTokens, pipeIndex);
  }
}

void runCommand(char **args) {
  sigset_t signals;
  sigemptyset(&signals);
  sigaddset(&signals, SIGINT);
  sigprocmask(SIG_UNBLOCK, &signals, NULL);
  execvp(args[0], args);
  errorPrint(args[0], PERROR);
  exit(EXIT_FAILURE);
}

void handleRedirection(char **redirection) {
  int fd;
  if (redirection[0]) {
    fd = open(redirection[0], O_RDONLY);
    if (fd == -1) {
      errorPrint(NULL, PERROR);
    }
    if (dup2(fd, 0) == -1) {
      assert(0);
    }
    close(fd);
  }
  if (redirection[1]) {
    fd = creat(redirection[1], 0600);
    if (fd == -1) {
      errorPrint(NULL, PERROR);
    }
    if (dup2(fd, 1) == -1) {
      assert(0);
    }
    close(fd);
  }
}

/* bType: An enumeration of type BuiltinType that specifies
          the built-in command to execute.
  oTokens: A dynamic array of tokens containing the arguments
          for the built-in command. */
void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens) {
  if (btype == B_CD) {
    executeCd(oTokens);
  }
  else if (btype == B_EXIT) {
    executeExit(oTokens);
  }
  else if (btype == B_SETENV) {
    executeSetenv(oTokens);
  }
  else if (btype == B_USETENV) {
    executeUnsetenv(oTokens);
  }
  else {
    assert(0 && "Unreachable");
  }
}

void executeCommand(DynArray_T oTokens) {
  int status;
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    assert(signal(SIGINT, SIG_DFL) != SIG_ERR);
    assert(signal(SIGQUIT, signalQuit) != SIG_ERR);
    char *args[DynArray_getLength(oTokens) + 1];
    char *redirection[2] = {NULL};
    int pipeNum = countPipe(oTokens);
    DynArray_parseTokens(oTokens, args, redirection);
    handleRedirection(redirection);
    if (pipeNum != 0) {
      executePipeCommands(oTokens);
    }
    else {
      runCommand(args);
    }
    DynArray_free(oTokens);
    exit(EXIT_FAILURE);
  }
  else if (pid < 0) {
    errorPrint(NULL, PERROR);
    exit(EXIT_FAILURE);
  }
  pid = wait(&status);
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
        if (btype != NORMAL) {
          executeBuiltin(btype, oTokens);
        } else {
          executeCommand(oTokens);
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

// Function to initialize the shell
static char *initialize(char *argv) {
  const char *homeDir = getenv("HOME");
  if (homeDir == NULL) {
    exit(EXIT_FAILURE);
  }
  const char *workingDir = getenv("PWD");
  initializeSignalHandlers();
  errorPrint(argv, SETUP);
  chdir(homeDir);
  FILE *ishrc = fopen(".ishrc", "r");
  char *acLine = (char *)malloc(MAX_LINE_SIZE +2);

  if (ishrc != NULL) {
    if (acLine == NULL) {
      fprintf(stderr, "Cannot allocate memory\n");
      exit(EXIT_FAILURE);
    }
    while(1) {
      fflush(stdout);
      if (fgets(acLine, MAX_LINE_SIZE, ishrc) == NULL) {
        break;
      }
      if (*(acLine + strlen(acLine) - 1) != '\n') {
                *(acLine + strlen(acLine)) = '\n';
                *(acLine + strlen(acLine) + 1) = '\0';
            }
      fprintf(stdout, "%% %s", acLine);
      shellHelper(acLine);
    }
    fclose(ishrc);
  }
  chdir(workingDir);
  return acLine;
}

int main(int argc, char *argv[]) {
  /* TODO */
  char *acLine = initialize(argv[0]);
  if (acLine == NULL) {
    exit(EXIT_FAILURE);
  }
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      free(acLine);
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
  free(acLine);
}


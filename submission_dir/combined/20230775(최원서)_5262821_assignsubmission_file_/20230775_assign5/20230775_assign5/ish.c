/**
 * Assignment 5: Unix Shell
 * 20230775 Wonseo Choi
 *
 * Implements pipes
 */

#define _GNU_SOURCE

#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
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

/**
 * Builds an argument list from a part of the token array `oToken`.
 * `n` tokens are read from `oTokens`, starting at `start`.
 *
 * The constructed argument list is returned, and `redin` and `redout` is set to
 * the file names stdin and stdout should be redirected to if they are
 * specified or NULL.
 */
char **buildArgv(DynArray_T oTokens, int start, int n, char **redin,
                 char **redout) {
  int i;
  struct Token *t;
  char **argv;
  int argc = 0;
  int l = DynArray_getLength(oTokens);

  assert(oTokens);
  assert(start >= 0);
  assert(start + n <= l);

  for (i = start; i < start + n; i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_WORD) {
      argc++;
    } else if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
      i++;
    } else {
      // other tokens should not be in this range
      assert(0);
    }
  }

  argv = malloc((argc + 1) * sizeof(char *));
  if (argv == NULL) {
    return NULL;
  }

  argc = 0;
  *redin = NULL;
  *redout = NULL;
  for (i = start; i < start + n; i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_WORD) {
      argv[argc] = t->pcValue;
      argc++;
    } else if (t->eType == TOKEN_REDIN) {
      if (redin != NULL) {
        t = DynArray_get(oTokens, ++i);
        *redin = t->pcValue;
      }
    } else if (t->eType == TOKEN_REDOUT) {
      if (redout != NULL) {
        t = DynArray_get(oTokens, ++i);
        *redout = t->pcValue;
      }
    }
  }
  argv[argc] = NULL;

  return argv;
}

/**
 * Spawns a new child process that runs the program given in `argv`.
 *
 * `infd` and `outfd` specifies the file descriptors stdin and stdout should be
 * redirected to. `redin` and `redout` specifies the file stdin and stdout
 * should be redirected to. If both a file descriptor and file is given as a
 * redirection target, the file is used.
 *
 * To disable redirection, set `infd` and `outfd` to 1 and 0, and set `redin`
 * and `redout` to NULL.
 */
int spawn(char *const argv[], int infd, int outfd, char *redin, char *redout) {
  int pid;

  assert(argv);
  assert(argv[0]);

  fflush(NULL);
  pid = fork();
  if (pid == 0) {
    signal(SIGINT, SIG_DFL);

    if (redin) {
      infd = open(redin, O_CLOEXEC | O_RDONLY);
      if (infd == -1) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
    }
    if (redout) {
      outfd = open(redout, O_CLOEXEC | O_WRONLY | O_CREAT | O_TRUNC,
                   S_IRUSR | S_IWUSR);
      if (outfd == -1) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
    }

    if (dup2(infd, 0) == -1) {
      errorPrint(NULL, PERROR);
      exit(EXIT_FAILURE);
    }
    if (dup2(outfd, 1) == -1) {
      errorPrint(NULL, PERROR);
      exit(EXIT_FAILURE);
    }

    if (execvp(argv[0], argv) == -1) {
      errorPrint(argv[0], PERROR);
    }
    exit(EXIT_FAILURE);
  } else {
    return pid;
  }
}

/**
 * Runs a non-builtin command corresponding to the token array `oTokens`
 */
void command_normal(DynArray_T oTokens) {
  struct Token *t;
  int l, pipes;
  int i, j;
  int *pids, *pipe_positions;
  char **argv;
  char *redin, *redout;
  int infd, outfd, start, end;
  int pipefd[2] = {-1, -1};

  assert(oTokens);

  l = DynArray_getLength(oTokens);
  pipes = countPipe(oTokens);

  pipe_positions = malloc(pipes * sizeof(int));
  if (pipe_positions == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }
  pids = malloc((pipes + 1) * sizeof(int));
  if (pipe_positions == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }

  // find pipe positions
  j = 0;
  for (i = 0; i < l; i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE) {
      pipe_positions[j++] = i;
    }
  }

  for (i = 0; i < pipes + 1; i++) {
    start = 0;
    end = l;
    infd = 0;
    outfd = 1;

    if (i > 0) {
      // redirect input
      start = pipe_positions[i - 1] + 1;
      infd = pipefd[0];
    }
    if (i < pipes) {
      // create pipes
      if (pipe2(pipefd, O_CLOEXEC) == -1) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
      // redirect output
      end = pipe_positions[i];
      outfd = pipefd[1];
    }

    // spawn child
    argv = buildArgv(oTokens, start, end - start, &redin, &redout);
    if (argv == NULL) {
      errorPrint("Cannot allocate memory", FPRINTF);
      exit(EXIT_FAILURE);
    }

    pids[i] = spawn(argv, infd, outfd, redin, redout);
    if (pids[i] == -1) {
      errorPrint(NULL, PERROR);
      exit(EXIT_FAILURE);
    }

    if (i > 0) {
      close(infd);
    }
    if (i < pipes) {
      close(outfd);
    }
    free(argv);
  }

  for (i--; i >= 0; i--) {
    if (waitpid(pids[i], NULL, 0) == -1) {
      errorPrint(NULL, PERROR);
    }
  }

  free(pipe_positions);
  free(pids);
}

/**
 * Runs the builtin command `exit` corresponding to the token array `oTokens`
 */
void command_exit(DynArray_T oTokens) {
  int l = DynArray_getLength(oTokens);

  if (l != 1) {
    errorPrint("exit does not take any parameters", FPRINTF);
    return;
  }

  exit(0); }

/**
 * Runs the builtin command `setenv` corresponding to the token array `oTokens`
 */
void command_setenv(DynArray_T oTokens) {
  struct Token *t;
  char *var, *value;
  int i, l;

  l = DynArray_getLength(oTokens);

  if (l < 2 || l > 3) {
    errorPrint("setenv takes one or two parameters", FPRINTF);
    return;
  }

  value = "";
  for (i = 0; i < l; i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType != TOKEN_WORD) {
      errorPrint("setenv takes one or two parameters", FPRINTF);
      return;
    }

    if (i == 1) {
      var = t->pcValue;
    } else if (i == 2) {
      t = DynArray_get(oTokens, 2);
      value = t->pcValue;
    }
  }

  setenv(var, value, 1);
}

/**
 * Runs the builtin command `unsetenv` corresponding to the token array
 * `oTokens`
 */
void command_unsetenv(DynArray_T oTokens) {
  struct Token *t;
  int l = DynArray_getLength(oTokens);

  if (l != 2) {
    errorPrint("unsetenv takes one parameter", FPRINTF);
    return;
  }

  t = DynArray_get(oTokens, 1);
  if (t->eType != TOKEN_WORD) {
    errorPrint("unsetenv takes one parameter", FPRINTF);
    return;
  }

  unsetenv(t->pcValue);
}

/**
 * Runs the builtin command `cd` corresponding to the token array `oTokens`
 */
void command_cd(DynArray_T oTokens) {
  struct Token *t;
  char *var;
  int l = DynArray_getLength(oTokens);

  if (l > 2) {
    errorPrint("cd takes one parameter", FPRINTF);
    return;
  }

  if (l == 1) {
    var = getenv("HOME");
    if (var == NULL) {
      return;
    }
  } else {
    t = DynArray_get(oTokens, 1);
    if (t->eType != TOKEN_WORD) {
      errorPrint("cd takes one parameter", FPRINTF);
      return;
    }
    var = t->pcValue;
  }

  if (chdir(var) == -1) {
    errorPrint(NULL, PERROR);
  }
}

/**
 * Frees the dynamic array `oTokens` and the tokens stored in it.
 * `oTokens` must be a DynArray_T storing pointers to `struct Token`s.
 */
void freeTokenArray(DynArray_T oTokens) {
  int i;
  struct Token *t;

  assert(oTokens);

  for (i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    freeToken(t, NULL);
  }
  DynArray_free(oTokens);
}

/**
 * Processes one line of input to the shell.
 * `inLine` must be a null or newline terminated string.
 */
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
    if (DynArray_getLength(oTokens) == 0)
      break;

    /* dump lex result when DEBUG is set */
    dumpLex(oTokens);

    syncheck = syntaxCheck(oTokens);
    if (syncheck == SYN_SUCCESS) {
      btype = checkBuiltin(DynArray_get(oTokens, 0));
      switch (btype) {
      case NORMAL:
        command_normal(oTokens);
        break;
      case B_EXIT:
        command_exit(oTokens);
        break;
      case B_SETENV:
        command_setenv(oTokens);
        break;
      case B_USETENV:
        command_unsetenv(oTokens);
        break;
      case B_CD:
        command_cd(oTokens);
        break;
      case B_ALIAS:
      case B_FG:
        break;
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

  freeTokenArray(oTokens);
}

int quit = 0;

void sigquit_handler(int signum) {
  if (quit) {
    exit(0);
  }
  quit = 1;
  printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  alarm(5);
}

void sigalrm_handler(int signum) { quit = 0; }

/**
 * A simple unix shell supporting redirection and pipes.
 *
 * Reads and executes commands from `$HOME/.ishrc` on startup.
 * Builtin commands:setenv, unsetenv, cd, exit
 */
int main(int argc, char *argv[]) {
  FILE *f;
  char *home, *ishrc;
  char acLine[MAX_LINE_SIZE + 2];
  sigset_t sSet;

  // make sure important signals are not blocked
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, sigquit_handler);
  signal(SIGALRM, sigalrm_handler);

  // setup logging
  errorPrint(argv[0], SETUP);

  // read $HOME/.ishrc if it exists
  home = getenv("HOME");
  if (home) {
    ishrc = malloc(strlen(home) + 8);
    strcpy(ishrc, home);
    strcat(ishrc, "/.ishrc");
    f = fopen(ishrc, "r");
    if (f) {
      while (1) {
        if (fgets(acLine, MAX_LINE_SIZE, f) == NULL) {
          break;
        }
        printf("%% %s", acLine);
        shellHelper(acLine);
      }
    }
  }

  // main loop
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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dynarray.h"
#include "lexsyn.h"
#include "token.h"
#include "util.h"

#define FD_IN 0
#define FD_OUT 1

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo, Junbong We(20180401)                     */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

void signal_handler(int signo) {
  /* signal handler for SIGQUIT, SIGALARM
   * @signo: signal number
   */
  static int on_quit = 0;
  if (signo == SIGQUIT) {
    if (on_quit) {
      exit(EXIT_SUCCESS);
    }
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    /* `on_quit` will set 0 again when it gets SIGALARM after 5s. */
    on_quit = 1;
    alarm(5);
  } else if (signo == SIGALRM)
    on_quit = 0;
}

static int do_exec(char **argv, int fd_in, int fd_out) {
  /* fork a process and execvp
   * @argv: argument vector
   * @fd_in: fd to be set as stdin of the child process
   * @fd_out: fd to be set as stdout of the child process
   */
  int pid;

  assert(argv != NULL && fd_in >= 0 && fd_out >= 0);

#ifdef JB_DEBUG
  if (getenv("DEBUG") != NULL) {
    int i;
    fprintf(stderr, "Exec: ");
    for (i = 0; argv[i] != NULL; i++)
      fprintf(stderr, "%s ", argv[i]);
    fprintf(stderr, "/ fd_in: %d, fd_out: %d\n", fd_in, fd_out);
  }
#endif

  /* Your program should call fflush(NULL) before each call of fork to clear all
   * I/O buffers.
   */
  fflush(stdout);
  if ((pid = fork()) < 0) {
    errorPrint(strerror(errno), FPRINTF);
    return pid;
  } else if (pid == 0) {
    if (signal(SIGINT, SIG_DFL) == SIG_ERR) {
      errorPrint(strerror(errno), FPRINTF);
      exit(EXIT_FAILURE);
    }
    if (signal(SIGQUIT, SIG_DFL) == SIG_ERR) {
      errorPrint(strerror(errno), FPRINTF);
      exit(EXIT_FAILURE);
    }
    if (signal(SIGALRM, SIG_DFL) == SIG_ERR) {
      errorPrint(strerror(errno), FPRINTF);
      exit(EXIT_FAILURE);
    }
    if (fd_in != FD_IN) {
      dup2(fd_in, FD_IN);
      close(fd_in);
    }
    if (fd_out != FD_OUT) {
      dup2(fd_out, FD_OUT);
      close(fd_out);
    }
    execvp(argv[0], argv);
    errorPrint(argv[0], SETUP);
    errorPrint(strerror(errno), FPRINTF);
    exit(EXIT_FAILURE);
  } else {
    if (fd_in != FD_IN)
      close(fd_in);
    if (fd_out != FD_OUT)
      close(fd_out);
  }

  return pid;
}

static void handle_normal(DynArray_T oTokens) {
  /* handle non built-in command
   * @oTokens: token array
   */
  struct Token *t;
  char **argv = NULL;
  int *pidv;
  int argc, pidc = 1;
  int cur_argv = 0, cur_pidv = 0;
  int fd_in = 0, fd_out = 1;
  int fd_pipe[2];
  int i, j;
  enum TokenType t_state = TOKEN_WORD;

  for (i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE)
      pidc++;
  }
  pidv = calloc(pidc, sizeof(int));

  for (i = 0; i < DynArray_getLength(oTokens); i++) {
    if (argv == NULL) {
      argc = 0;
      for (j = i; j < DynArray_getLength(oTokens); j++) {
        t = DynArray_get(oTokens, j);
        if (t->pcValue == NULL) {
          if ((t->eType == TOKEN_PIPE) || (t->eType == TOKEN_BG)) {
            break;
          }
          // Redirection
          assert(t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT);
          argc--;
        } else
          argc++;
      }
      argv = calloc((argc + 1), sizeof(char *));
      cur_argv = 0;
    }
    t = DynArray_get(oTokens, i);
    if (t->pcValue == NULL) {
      switch (t->eType) {
      case TOKEN_PIPE:
        if (pipe(fd_pipe) < 0) {
          errorPrint(strerror(errno), FPRINTF);
          if (argv != NULL)
            free(argv);
          for (j = 0; j < cur_pidv; j++)
            waitpid(pidv[j], NULL, 0);
          free(pidv);
          return;
        }
        assert(fd_out == 1);
        pidv[cur_pidv++] = do_exec(argv, fd_in, fd_pipe[1]);
        // Set value for next exec
        free(argv);
        argv = NULL;
        fd_in = fd_pipe[0];
        fd_out = 1;
        break;
      case TOKEN_REDIN:
      case TOKEN_REDOUT:
        t_state = t->eType;
        break;
      case TOKEN_BG:
        /* All child processes forked by your program should run in the
         * foreground.
         */
        break;
      case TOKEN_WORD:
      default:
        assert(0 && "Unreachable");
      }
    } else {
      if (t_state == TOKEN_REDIN) {
        assert(fd_in == 0);
        fd_in = open(t->pcValue, O_RDONLY);
        if (fd_in < 0) {
          errorPrint(strerror(errno), FPRINTF);
          if (argv != NULL)
            free(argv);
          for (j = 0; j < cur_pidv; j++)
            waitpid(pidv[j], NULL, 0);
          free(pidv);
          return;
        }
        t_state = TOKEN_WORD;
      } else if (t_state == TOKEN_REDOUT) {
        assert(fd_out == 1);
        /* O_CREAT: Your program should create a file if the command's output is
         * redirected to a non-existing file.
         * O_TRUNC: Your program should truncate a file if the command's output
         * is redirect to an existing file. */
        fd_out = open(t->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd_out < 0) {
          errorPrint(strerror(errno), FPRINTF);
          if (argv != NULL)
            free(argv);
          for (j = 0; j < cur_pidv; j++)
            waitpid(pidv[j], NULL, 0);
          free(pidv);
          return;
        }
        t_state = TOKEN_WORD;
      } else {
        assert(t_state == TOKEN_WORD);
        argv[cur_argv++] = t->pcValue;
      }
    }
  }
  pidv[cur_pidv++] = do_exec(argv, fd_in, fd_out);
  free(argv);

  for (i = 0; i < pidc; i++)
    waitpid(pidv[i], NULL, 0);
  free(pidv);
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
    if (DynArray_getLength(oTokens) == 0)
      return;

    /* dump lex result when DEBUG is set */
    dumpLex(oTokens);

    syncheck = syntaxCheck(oTokens);
    if (syncheck == SYN_SUCCESS) {
      btype = checkBuiltin(DynArray_get(oTokens, 0));
      switch (btype) {
      case NORMAL:
        handle_normal(oTokens);
        break;
      case B_EXIT:
        exit(EXIT_SUCCESS);
      case B_SETENV:
        switch (DynArray_getLength(oTokens)) {
        case 0:
          assert(0);
          break;
        case 2:
          if (setenv(((struct Token *)DynArray_get(oTokens, 1))->pcValue, "",
                     1) < 0)
            errorPrint(strerror(errno), FPRINTF);
          break;
        case 3:
          if (((struct Token *)DynArray_get(oTokens, 1))->pcValue == NULL ||
              ((struct Token *)DynArray_get(oTokens, 1))->pcValue == NULL) {
            errorPrint("setenv takes one or two parameters", FPRINTF);
            break;
          }
          if (setenv(((struct Token *)DynArray_get(oTokens, 1))->pcValue,
                     ((struct Token *)DynArray_get(oTokens, 2))->pcValue,
                     1) < 0)
            errorPrint(strerror(errno), FPRINTF);
          break;
        default:
          errorPrint("setenv takes one or two parameters", FPRINTF);
          break;
        }
        break;
      case B_USETENV:
        if (DynArray_getLength(oTokens) != 2) {
          errorPrint("unsetenv takes one parameter", FPRINTF);
          break;
        }
        if (unsetenv(((struct Token *)DynArray_get(oTokens, 1))->pcValue) < 0)
          errorPrint(strerror(errno), FPRINTF);
        break;
      case B_CD:
        switch (DynArray_getLength(oTokens)) {
        case 0:
          assert(0);
          break;
        case 1:
          if (chdir(getenv("HOME")) < 0)
            errorPrint(strerror(errno), FPRINTF);
          break;
        case 2:
          if (chdir(((struct Token *)DynArray_get(oTokens, 1))->pcValue) < 0)
            errorPrint(strerror(errno), FPRINTF);
          break;
        default:
          errorPrint("cd takes one parameter", FPRINTF);
          break;
        }
        break;
      case B_ALIAS:
      case B_FG:
        errorPrint("Not implemented", FPRINTF);
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
}

int main(int argc, char *argv[]) {
  char acLine[MAX_LINE_SIZE + 2];
  char filepath[MAX_LINE_SIZE];
  FILE *ishrc;
  sigset_t signal_set;

  errorPrint(argv[0], SETUP);

  /* Your program should call the sigprocmask function near the beginning of the
   * main function to make sure that SIGINT, SIGQUIT, and SIGALRM signals are
   * not blocked.
   */
  sigemptyset(&signal_set);
  sigaddset(&signal_set, SIGINT);
  sigaddset(&signal_set, SIGQUIT);
  sigaddset(&signal_set, SIGALRM);
  if (sigprocmask(SIG_UNBLOCK, &signal_set, NULL) != 0) {
    errorPrint(strerror(errno), FPRINTF);
    return 1;
  }
  if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
    errorPrint(strerror(errno), FPRINTF);
    return 1;
  }
  if (signal(SIGQUIT, signal_handler) == SIG_ERR) {
    errorPrint(strerror(errno), FPRINTF);
    return 1;
  }
  if (signal(SIGALRM, signal_handler) == SIG_ERR) {
    errorPrint(strerror(errno), FPRINTF);
    return 1;
  }

  snprintf(filepath, MAX_LINE_SIZE, "%s/.ishrc", getenv("HOME"));
  if ((ishrc = fopen(filepath, "r"))) {
    while (1) {
      if (fgets(acLine, MAX_LINE_SIZE, ishrc) == NULL) {
        break;
      }
      fprintf(stdout, "%% %s", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
  }

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

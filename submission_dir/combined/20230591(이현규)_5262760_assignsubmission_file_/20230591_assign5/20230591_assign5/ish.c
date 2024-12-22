#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
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

static const char *home;
static int alarm_toggle = 0;
static int inchild = 0;

typedef struct command_args {
  int index;
  char *argv[MAX_ARGS_CNT + 1];
  bool redin;
  bool redout;
  char *io_red[2];
  struct command_args *next;
}cmdarg;

cmdarg *cmdarg_new(void) {
  cmdarg *ret = (cmdarg *)malloc(sizeof(cmdarg));
  ret->index = 0;
  memset(ret->argv, 0, sizeof(ret->argv));
  ret->redin = false;
  ret->redout = false;
  ret->next = NULL;
  return ret;
}

static void alarmHandler(int s) {
  alarm_toggle = 0;
}

static void SQHandler(int s) {
  if (alarm_toggle) exit(0);
  printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  fflush(stdout);
  alarm(5);
  alarm_toggle = 1;
}

static void SIHandler(int s) {
  if (inchild) raise(SIGINT);
}

static void ish_setenv(DynArray_T tokens) {
  int l = DynArray_getLength(tokens);
  if (l == 2) {
    if (((struct Token*)DynArray_get(tokens, 1))->eType == TOKEN_WORD) {
      setenv(((struct Token*)DynArray_get(tokens, 1))->pcValue, "", 1);
    }
    // error message for arg not being TOKEN_WORD?
  }
  else if (l == 3) {
    if (((struct Token*)DynArray_get(tokens, 1))->eType == TOKEN_WORD
    && ((struct Token*)DynArray_get(tokens, 2))->eType == TOKEN_WORD) {
      setenv(((struct Token*)DynArray_get(tokens, 1))->pcValue,
      ((struct Token*)DynArray_get(tokens, 2))->pcValue, 1);
    }
    // error message for arg not being TOKEN_WORD?
  }
  else errorPrint("setenv takes one or two parameters", FPRINTF);
  // error message for redirection?
}

static void ish_unsetenv(DynArray_T tokens) {
  int l = DynArray_getLength(tokens);
  if (l == 2) {
    if (((struct Token*)DynArray_get(tokens, 1))->eType == TOKEN_WORD) {
      unsetenv(((struct Token*)DynArray_get(tokens, 1))->pcValue);
      // error message for arg not being TOKEN_WORD?
    }
  }
  else errorPrint("unsetenv takes one parameter", FPRINTF);
  // error message for redirection?
}

static void ish_cd(DynArray_T tokens) {
  int l = DynArray_getLength(tokens);
  if (l == 1) chdir(home);
  else if (l == 2) {
    if (((struct Token*)DynArray_get(tokens, 1))->eType == TOKEN_WORD) {
      char *d = (((struct Token*)DynArray_get(tokens, 1))->pcValue);
      if (chdir(d) == -1) errorPrint("No such file or directory", FPRINTF);
    }
    // error message for arg not being TOKEN_WORD?
  }
  else errorPrint("cd takes one parameter", FPRINTF);
  // error message for redirection?
}

static void ish_exit(DynArray_T tokens) {
  int l = DynArray_getLength(tokens);
  if (l == 1) exit(0);
  else errorPrint("error: exit has no parameters", FPRINTF);
  // error message for redirection?
}

static void ish_notbuiltin(DynArray_T tokens) {
  /* TODO */
  /* 1. child = foreground; use wait */
  /* Redirection cases */
  /* pipe */
  int i = 0;
  int nonword = 0;
  char *argv[MAX_ARGS_CNT + 1];
  char *iored[2];
  int ri = 0, ro = 0;
  int infd, outfd;
  int l = DynArray_getLength(tokens);
  pid_t p[MAX_ARGS_CNT / 2];
  int proccount = 1;
  int status;
  int pipefd[2];
  while (i < l) {
    if (((struct Token*)DynArray_get(tokens, i))->eType == TOKEN_WORD) {
        argv[i - nonword] = ((struct Token*)DynArray_get(tokens, i))->pcValue;
        i++;
    }
    else if (((struct Token*)DynArray_get(tokens, i))->eType == TOKEN_REDIN) {
        nonword = nonword + 2;
        iored[0] = ((struct Token*)DynArray_get(tokens, ++i))->pcValue;
        ri = 1;
        i++;
    }
    else if (((struct Token*)DynArray_get(tokens, i))->eType == TOKEN_REDOUT) {
        nonword = nonword + 2;
        iored[1] = ((struct Token*)DynArray_get(tokens, ++i))->pcValue;
        ro = 1;
        i++;
    }
    else if (((struct Token*)DynArray_get(tokens, i))->eType == TOKEN_PIPE) {
        argv[i - nonword] = NULL;
        p[proccount - 1] = fork();
        pipe(pipefd);
        if (p[proccount - 1] == 0) {
            if (ri) {
                int infd = open(iored[0], O_RDONLY);
                dup2(infd, STDIN_FILENO);
                close(infd);
            }
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            close(pipefd[0]);
            execvp(argv[0], argv);
            errorPrint(argv[0], SETUP);
            errorPrint("No such file or directory", FPRINTF);
            errorPrint("./ish", SETUP);
            exit(0);
        }
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        proccount++;
        ri = 0, ro = 0;
        nonword = i + 1;
        i++;
    }
  }
    argv[i - nonword] = NULL;
    p[proccount - 1] = fork();
    if (p[proccount - 1] == 0) {
        if (ri) {
            int infd = open(iored[0], O_RDONLY);
            dup2(infd, STDIN_FILENO);
            close(infd);
        }
        if (ro) {
            int outfd = open(iored[1], O_WRONLY | O_CREAT, 0600);
            dup2(outfd, STDOUT_FILENO);
            close(outfd);
        }
        execvp(argv[0], argv);
        errorPrint(argv[0], SETUP);
        errorPrint("No such file or directory", FPRINTF);
        errorPrint("./ish", SETUP);
        exit(0);
    }
    while (proccount > 0) {
        waitpid(p[proccount - 1], &status, 0);
        proccount--;
    }
    return;
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
        switch(btype) {
          case B_SETENV:
            ish_setenv(oTokens);
            break;
          case B_USETENV:
            ish_unsetenv(oTokens);
            break;
          case B_CD:
            ish_cd(oTokens);
            break;
          case B_EXIT:
            ish_exit(oTokens);
            break;
          default:
            ish_notbuiltin(oTokens);
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
  DynArray_free(oTokens);
}

int main() {
  /* TODO */
  signal(SIGINT, SIHandler);
  signal(SIGQUIT, SQHandler);
  signal(SIGALRM, alarmHandler);
  
  char line[MAX_LINE_SIZE + 2];
  home = getenv("HOME");
  char path[strlen(home)];
  strcpy(path, home);
  strcat(path, "/.ishrc");
  errorPrint("./ish", SETUP);

  if (access(path, R_OK) == 0) {
    FILE *f = fopen(path, "r");
    while (fgets(line, MAX_LINE_SIZE, f) != NULL) {
      printf("%% %s", line);
      fflush(stdout);
      shellHelper(line);
    }
    fclose(f);
  }
  
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(line, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(line);
  }
}


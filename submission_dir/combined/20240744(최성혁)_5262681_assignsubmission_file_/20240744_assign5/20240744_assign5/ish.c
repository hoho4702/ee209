#define _DEFAULF_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "lexsyn.h"
#include "util.h"
#include "token.h"
#include "dynarray.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static volatile sig_atomic_t quit_requested = 0;

static void handle_sigint(int signo) {
  (void)signo;
}

static void handle_sigalarm(int signo) {
  (void)signo;
  quit_requested = 0;
}

static void handle_sigquit(int signo) {
  (void)signo;
  if (quit_requested == 0) {
    fprintf(stdout, "Type Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    quit_requested = 1;
    alarm(5);
  }
  else if (quit_requested == 1) {
    quit_requested = 2;
  }
}

static void install_signal(void) {
  struct sigaction sa_int, sa_quit, sa_alrm;
  sigemptyset(&sa_int.sa_mask);
  sa_int.sa_handler = handle_sigint;
  sa_int.sa_flags = 0;
  sigaction(SIGINT, &sa_int, NULL);

  sigemptyset(&sa_quit.sa_mask);
  sa_quit.sa_handler = handle_sigquit;
  sa_quit.sa_flags = 0;
  sigaction(SIGQUIT, &sa_quit, NULL);

  sigemptyset(&sa_alrm.sa_mask);
  sa_alrm.sa_handler = handle_sigalarm;
  sa_alrm.sa_flags = 0;
  sigaction(SIGALRM, &sa_alrm, NULL);
}

static int execute_builtin(enum BuiltinType btype, DynArray_T oTokens) {
  int length = DynArray_getLength(oTokens);
  struct Token *t;
  char *var, *val;
  switch(btype) {
    case B_EXIT:
      exit(0);
      break;
    case B_CD: {
      char *dir;
      if (length ==1) {
        dir = getenv("HOME");
        if (dir == NULL) {
          errorPrint("HOME not set", FPRINTF);
          return 0;
        }
      }
      else {
        t = DynArray_get(oTokens, 1);
        if (t->eType != TOKEN_WORD) {
          errorPrint("Invalid directory", FPRINTF);
          return 0;
        }
        dir = t->pcValue;
      }
      if (chdir(dir) == -1) {
        errorPrint(NULL, PERROR);
        return 0;
      }
      break;
    }
    case B_SETENV:
      if (length == 1) {
        errorPrint("Missing variable name", FPRINTF);
        return 0;
      }
      t = DynArray_get(oTokens, 1);
      var = t->pcValue;
      if (length == 2) {
        if (setenv(var, "", 1) == -1) {
          errorPrint(NULL, PERROR);
          return 0;
        }
      } else {
        t = DynArray_get(oTokens, 2);
        val = t->pcValue;
        if (setenv(var, val, 1) == -1) {
          errorPrint(NULL, PERROR);
          return 0;
        }
      }
      break;
    case B_USETENV:
      if (length == 1) {
        errorPrint("Missing variable name", FPRINTF);
        return 0;
      }
      t = DynArray_get(oTokens, 1);
      var = t->pcValue;
      if (unsetenv(var) == -1) {

      }
      break;
    case B_FG:
      break;
    case B_ALIAS:
      break;
    case NORMAL:
    default:
      break;
  }
  return 1;
}

static int execute_external(DynArray_T oTokens) {
  int length = DynArray_getLength(oTokens);
  char *argv[MAX_ARGS_CNT+1];
  char *infile = NULL;
  char *outfile = NULL;
  int arg_count = 0;
  int i;
  struct Token *t;
  int redirect_in = 0, redirect_out = 0;

  for (i = 0; i <length; i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_WORD) {
      argv[arg_count++] = t->pcValue;
      if (arg_count >= MAX_ARGS_CNT) {
        errorPrint("Too many argements", FPRINTF);
        return 0;
      }
    }
    else if (t->eType == TOKEN_REDIN) {
      if (i == length - 1) {
        errorPrint("Standard input redirection without file name", FPRINTF);
        return 0;
      }
      i++;
      t = DynArray_get(oTokens, i);
      if (t->eType != TOKEN_WORD) {
        errorPrint("Standard input redirection without file name", FPRINTF);
        return 0;
      }
      infile = t->pcValue;
      redirect_in = 1;
    }
    else if (t->eType == TOKEN_REDOUT) {
      if (i == length -1) {
        errorPrint("Standard output redirection without file name", FPRINTF);
        return 0;
      }
      i++;
      t = DynArray_get(oTokens, i);
      if (t->eType != TOKEN_WORD) {
        errorPrint("Standard output redirection without file name", FPRINTF);
        return 0;
      }
      outfile = t->pcValue;
      redirect_out = 1;
    }
    else if (t->eType == TOKEN_PIPE || t->eType == TOKEN_BG) {
      if (t->eType == TOKEN_PIPE)
        errorPrint("Pipes not implemented", FPRINTF);
      else
        errorPrint("Background not implemented", FPRINTF);
      return 0;
    }
  }

  if (arg_count == 0) {
    errorPrint("Missing command name", FPRINTF);
    return 0;
  }

  argv[arg_count] = NULL;

  fflush(NULL);
  pid_t pid = fork();
  if (pid == -1) {
    errorPrint(NULL, PERROR);
    return 0;
  }
  if (pid == 0) {
    if (infile != NULL) {
      int fd = open(infile, O_RDONLY);
      if (fd < 0) {
        errorPrint(infile, PERROR);
        exit(EXIT_FAILURE);
      }
      dup2(fd, STDIN_FILENO);
      close(fd);
    }
    if (outfile != NULL) {
      int fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd < 0) {
        errorPrint(outfile, PERROR);
        exit(EXIT_FAILURE);
      }
      dup2(fd, STDOUT_FILENO);
      close(fd);
    }
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    execvp(argv[0], argv);
    errorPrint(argv[0], PERROR);
    exit(EXIT_FAILURE);
  }
  else {
    int status;
    waitpid(pid, &status, 0);
  }
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
      if (DynArray_getLength(oTokens) == 0) {
        DynArray_free(oTokens);
        return;
      }
      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        if (btype != NORMAL) {
          int i;
          for (i=1; i < DynArray_getLength(oTokens); i++) {
            struct Token *tk = DynArray_get(oTokens, i);
            if (tk->eType == TOKEN_REDIN || tk->eType == TOKEN_REDOUT) {
              errorPrint("Redirection with built-in cammand not allowed", FPRINTF);
              DynArray_map(oTokens, freeToken, NULL);
              DynArray_free(oTokens);
              return;
            }
          }
          execute_builtin(btype, oTokens);
        }
        else {
          execute_external(oTokens);
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
  DynArray_map(oTokens, freeToken, NULL);
  DynArray_free(oTokens);
}

static void process_ishrc() {
  char *home = getenv("HOME");
  if (home == NULL) {
    return;
  }
  char path[1024];
  snprintf(path, 1024, "%s/.ishrc", home);
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    return;
  }

  char line[MAX_LINE_SIZE+2];
  while (fgets(line, MAX_LINE_SIZE, fp) != NULL) {
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n') {
      line[len-1] = '\0';
    }
    fprintf(stdout, "%% %s\n", line);
    fflush(stdout);
    shellHelper(line);
  }
  fclose(fp);
}

int main(int argc, char *argv[]) {
  errorPrint(argv[0], SETUP);
  install_signal();
  process_ishrc();

  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
    if (quit_requested == 2) {
      exit(0);
    }
    fprintf(stdout, "%% ");
    fflush(stdout);

    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
  return 0;
}


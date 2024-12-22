#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <ctype.h>
#include <signal.h>

#include "lexsyn.h"
#include "util.h"
#include "dynarray.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static int sig_quit_flag = 0;

static void sigQuitHandling(int a)
{
  if (sig_quit_flag == 0)
  {
    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    sig_quit_flag = 1;

    alarm(5);
  }
  else
    exit(EXIT_SUCCESS);
}

static void sigAlarmHandling(int a)
{
  sig_quit_flag = 0;
}

static void
shellHelper(const char *inLine)
{
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  oTokens = DynArray_new(0);
  if (oTokens == NULL)
  {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }

  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck)
  {
  case LEX_SUCCESS:
    if (DynArray_getLength(oTokens) == 0)
      return;

    /* dump lex result when DEBUG is set */
    dumpLex(oTokens);

    syncheck = syntaxCheck(oTokens);
    if (syncheck == SYN_SUCCESS)
    {
      btype = checkBuiltin(DynArray_get(oTokens, 0));
      // NORMAL, B_EXIT, B_SETENV, B_USETENV, B_CD, B_ALIAS, B_FG
      /* TODO */
      if (btype == B_EXIT)
      {
        if (DynArray_getLength(oTokens) != 1)
          errorPrint("exit does not take any parameters", FPRINTF);
        else
          exit(EXIT_SUCCESS);
      }
      else if (btype == B_SETENV)
      {
        if (!(DynArray_getLength(oTokens) == 2 || DynArray_getLength(oTokens) == 3))
          errorPrint("setenv takes one or two parameters", FPRINTF);
        else
        {
          struct Token *t1 = DynArray_get(oTokens, 1);
          int isSuccess;

          if (DynArray_getLength(oTokens) == 2)
          {
            isSuccess = setenv(t1->pcValue, "", 1);
          }
          else
          {
            struct Token *t2 = DynArray_get(oTokens, 2);
            isSuccess = setenv(t1->pcValue, t2->pcValue, 1);
          }

          if (isSuccess == -1)
          {
            errorPrint("setenv error!!", PERROR);
          }
        }
      }
      else if (btype == B_USETENV)
      {
        if (DynArray_getLength(oTokens) != 2)
          errorPrint("unsetenv takes one parameter", FPRINTF);
        else
        {
          struct Token *t = DynArray_get(oTokens, 1);
          int isSuccess = unsetenv(t->pcValue);

          if (isSuccess == -1)
          {
            errorPrint("unsetenv error!!", PERROR);
          }
        }
      }
      else if (btype == B_CD)
      {
        char *dir = NULL;

        if (DynArray_getLength(oTokens) == 1)
        {
          // Go to HOME
          dir = getenv("HOME");
          if (dir == NULL)
          {
            errorPrint("cd: There is no HOME", PERROR);
            return;
          }
        }
        else if (DynArray_getLength(oTokens) == 2)
        {
          struct Token *t = DynArray_get(oTokens, 1);
          dir = t->pcValue;
        }
        else
        {
          errorPrint("cd takes one parameter", FPRINTF);
          return;
        }
        // change directory
        if (chdir(dir) != 0)
        {
          errorPrint("No such file or directory", PERROR);
        }
      }
      else if (btype == B_ALIAS)
      {
      }
      else if (btype == B_FG)
      {
      }
      else
      {
        // NORMAL case
        // try child process
        pid_t pid = fork();
        if (pid == 0)
        {
          struct Token *t1 = DynArray_get(oTokens, 0);
          int arrLen = DynArray_getLength(oTokens);
          // void **ppvArray = malloc(arrLen * sizeof(void *));
          // DynArray_toArray(oTokens, ppvArray);
          char **args = malloc((arrLen + 1) * sizeof(char *));

          struct Token *t;

	  int i;
          for (i = 0; i < arrLen; i++)
          {
            t = DynArray_get(oTokens, i);
            args[i] = t->pcValue;
          }
          args[arrLen] = NULL;

          execvp(t1->pcValue, args);
          perror(t1->pcValue);

          free(args);
          exit(EXIT_FAILURE);
        }
        else if (pid > 0)
        {
          // wait child process
          int status;
          waitpid(pid, &status, 0);
        }
        else
        {
          // fork fail
          errorPrint("fork error!", PERROR);
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

  // dynamic array free
  DynArray_free(oTokens);
}

int main(int argc, char *argv[])
{
  /* TODO */
  char acLine[MAX_LINE_SIZE + 2];

  if (argc > 0)
  {
    // setup this file name
    errorPrint(argv[0], SETUP);
  }
  else
  {
    errorPrint("./ish", SETUP);
  }

  // signal handling
  signal(SIGINT, SIG_IGN); // it means ignore
  signal(SIGQUIT, sigQuitHandling);
  signal(SIGALRM, sigAlarmHandling);

  // Initialization and Termination
  void *home_env = getenv("HOME");

  size_t path_len = strlen(home_env) + strlen("/.ishrc") + 1; // 끝 NULL
  void *ishrc_path = malloc(path_len);
  snprintf(ishrc_path, path_len, "%s/.ishrc", (char *)home_env);

  FILE *ishrc_file = fopen((char *)ishrc_path, "r");

  if (ishrc_file != NULL)
  {
    while (fgets(acLine, MAX_LINE_SIZE, ishrc_file) != NULL)
    {
      fprintf(stdout, "%% %s", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
    fclose(ishrc_file);
  }
  else
  {
    // there is no .ishrc file in HOME
    // do nothing just pass
  }

  free(ishrc_path);
  fflush(NULL);

  // stdin is connect to file
  // change to terminal console
  // freopen("/dev/tty", "r", stdin);

  while (1)
  {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL)
    {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
}

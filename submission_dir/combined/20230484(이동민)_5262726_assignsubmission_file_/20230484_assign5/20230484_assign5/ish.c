#define GNU_SOURCE 1
#define DEFAULT_SOURCE 1

#include <string.h>
#include <signal.h>
#include <wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/
void quit_function(int signum); // sig_quit signal handler declaration

void normalHandle(DynArray_T oTokens) // bByte==NORMAL case handle
{
  int eT;
  int exResult;
  char *inputVal;
  int inputFile;
  char *outputVal;
  int outputFile;
  int index = 0;
  char *exSomething;
  char *normalArgv[MAX_LINE_SIZE] = {
      0,
  };
  int pipefd[MAX_LINE_SIZE] = {
      0,
  };
  int pipeIndex = 0;
  pid_t normalPid;
  int normalInfo;
  for (;;)
  {
    if (DynArray_getLength(oTokens) == 0)
    {
      exResult = execvp(normalArgv[0], normalArgv);
      if (exResult == -1)
        errorPrint(normalArgv[0], PERROR);
      return;
    }
    else
    {
      eT = ((struct Token *)DynArray_get(oTokens, 0))->eType;
      switch (eT)
      {
      case TOKEN_REDIN: // REDIN(<)
        inputVal = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
        inputFile = open(inputVal, O_RDONLY);
        if (inputFile == -1)
          errorPrint(inputVal, PERROR);
        close(0);
        dup(inputFile); // dup inpuFile
        close(inputFile);
        for (int i = 0; i < 2; i++)
          DynArray_removeAt(oTokens, 0);
        break;
      case TOKEN_REDOUT: // REDOUT(>)
        outputVal = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
        outputFile = open(outputVal, O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (outputFile == -1)
          errorPrint(outputVal, PERROR);
        close(1);
        dup(outputFile); // dup outputFile
        close(outputFile);
        for (int i = 0; i < 2; i++)
          DynArray_removeAt(oTokens, 0);
        break;
      case TOKEN_WORD: // WORD
        exSomething = ((struct Token *)DynArray_get(oTokens, 0))->pcValue;
        normalArgv[index++] = exSomething;
        DynArray_removeAt(oTokens, 0);
        break;
      case TOKEN_PIPE: // PIPE case
        if (pipe(pipefd) == -1)
          errorPrint("pipe failed", PERROR);
        fflush(NULL);
        pipeIndex += 1;
        DynArray_removeAt(oTokens, 0);
        normalPid = fork(); // fork
        if (normalPid == 0)
        {
          close(pipefd[0]);
          dup2(pipefd[1], 1);
          close(pipefd[1]);
          execvp(normalArgv[0], normalArgv); // exe in child
        }
        else
        {
          close(pipefd[1]);
          dup2(pipefd[0], 0);
          close(pipefd[0]);
          waitpid(normalPid, &normalInfo, 0); // wait in parent
          for (int i = 0; i < MAX_LINE_SIZE; i++)
          {
            normalArgv[i] = NULL;
          }
          index = 0;
        }
        break;
      }
    }
  }
  return;
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
  // char *tokenValue;
  int info;
  pid_t pid;
  char *zeroVal;
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
      if (btype == B_EXIT) // exit instr
      {
        if (DynArray_getLength(oTokens) > 1)
        { // exit --> no parameter
          errorPrint("exit does not take any parameters", FPRINTF);
          return;
        }
        exit(0);
        return;
      }
      else if (btype == B_SETENV) // setenv instr
      {
        if (DynArray_getLength(oTokens) > 3) // set --> no more than 2 paras
          errorPrint("setenv takes one or two parameters", FPRINTF);
        else
        {
          char *envVar;
          envVar = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
          char *envVal;
          if (DynArray_getLength(oTokens) == 2)
            envVal = ""; // set to ""
          else
            envVal = ((struct Token *)DynArray_get(oTokens, 2))->pcValue;
          setenv(envVar, envVal, 1); // set to pcValue
        }
        return;
      }
      else if (btype == B_USETENV) // unsetenv instr
      {
        if (DynArray_getLength(oTokens) > 2) // unset --> no more than 1 para
        {
          errorPrint("unsetenv takes one parameter", FPRINTF);
          return;
        }
        else
        {
          if (getenv(((struct Token *)DynArray_get(oTokens, 1))->pcValue) != NULL)
          {
            unsetenv(((struct Token *)DynArray_get(oTokens, 1))->pcValue);
          } // unset when getenv result isn't NULL
        }
        return;
      }
      else if (btype == B_CD)
      {
        if (DynArray_getLength(oTokens) == 1)
        {
          char *dir = getenv("HOME");
          chdir(dir); // no para --> set to HOME
        }
        else if (DynArray_getLength(oTokens) == 2)
        {
          chdir(((struct Token *)DynArray_get(oTokens, 1))->pcValue);
        } // chdir to pcVal of oTokens 1
        else
        {
          errorPrint(
              "cd takes one parameter", FPRINTF);
        } // cd takes only one or zero para
      }

      else if (btype == NORMAL)
      {
        zeroVal = ((struct Token *)DynArray_get(oTokens, 0))->pcValue;
        if (*zeroVal == '\n')
          return;
        if (*zeroVal == '\0')
          return;

        fflush(NULL);
        pid = fork(); // fork
        if (pid == 0)
        {
          signal(SIGINT, SIG_DFL);
          normalHandle(oTokens); // normal handle in child
          exit(0);
        }
        else
        {
          waitpid(pid, &info, 0); // wait child
        }
        return;
      }
      else
        return;
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

void quit_DFL(int signum)
{
  exit(0);
}

void alarm_function(int signum)
{
  signal(SIGQUIT, quit_function);
  return;
}

void quit_function(int signum)
{ // quit handler
  fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
  alarm(5);
  signal(SIGQUIT, quit_DFL);
  signal(SIGALRM, alarm_function);
  return;
}

int main(int argc, char *argv[])
{
  /* TODO */

  char acLine[MAX_LINE_SIZE + 2];
  char mainPath[MAX_LINE_SIZE + 2];
  char mainBuf[MAX_LINE_SIZE + 2];

  sigset_t sSet; // signal setting
  int setError;
  setError = sigemptyset(&sSet);
  if (setError == -1)
    fprintf(stderr, "SIGNAL ERROR\n");
  signal(SIGINT, SIG_IGN);
  sigaddset(&sSet, SIGINT);
  signal(SIGQUIT, quit_function);
  sigaddset(&sSet, SIGALRM);
  sigaddset(&sSet, SIGQUIT);

  setError = sigprocmask(SIG_UNBLOCK, &sSet, NULL);
  if (setError == -1)
    fprintf(stderr, "SIGNAL ERROR\n");

  FILE *fpMain;

  // set shell and open .ishrc
  errorPrint(argv[0], SETUP);
  strcpy(mainPath, getenv("HOME"));
  strcat(mainPath, "/.ishrc");
  fpMain = fopen(mainPath, "r");

  // execute .ishrc
  if (fpMain != NULL)
  {
    while (fgets(mainBuf, MAX_LINE_SIZE, fpMain) != NULL)
    {
      fprintf(stdout, "%% %s", mainBuf);
      shellHelper(mainBuf);
    }
  }

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

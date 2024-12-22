#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
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

static int quit_flag = 0;

/* Define signal handler functions */
static void
handleSIGINT(int iSig) { }

static void
handleSIGQUIT(int iSig) {
  if (quit_flag == 0) {
    quit_flag = 1;
    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5);
  }
  else {
    exit(EXIT_SUCCESS);
  }
}

static void
handleSIGALRM(int iSig) {
  quit_flag = 0;
}

/* Function: doRedirection
   Parameters:
    - oTokens: Tokens from input command
   Handle redirection referring input arguments */
static int
doRedirection(DynArray_T oTokens) {
  int isInputRedirect = 0;
  int isOutputRedirect = 0;
  int inputFd, outputFd;

  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    struct Token *token = DynArray_get(oTokens, i);

    if (token->eType == TOKEN_REDIN) {
      /* Redirect input stream */
      if (isInputRedirect) {
        errorPrint("Multiple redirection of standard input", FPRINTF);
        return -1;
      }
      
      if (i >= DynArray_getLength(oTokens) - 1) {
        errorPrint("Standard input redirection without file name", FPRINTF);
        return -1;
      }

      struct Token *nextToken = DynArray_get(oTokens, i + 1);
      if (nextToken->eType == TOKEN_REDIN || nextToken->eType == TOKEN_REDOUT) {
        errorPrint("Standard input redirection without file name", FPRINTF);
        return -1;
      }

      inputFd = open(nextToken->pcValue, O_RDONLY);
      if (inputFd < 0) {
        errorPrint("No such file or directory", FPRINTF);
        return -1;
      }
      isInputRedirect = 1;

      /* Remove '<' and file name from array */
      DynArray_removeAt(oTokens, i);
      DynArray_removeAt(oTokens, i);
      i--;
    }
    else if (token->eType == TOKEN_REDOUT) {
      /* Redirect output stream */
      if (isOutputRedirect) {
        errorPrint("Multiple redirection of standard out", FPRINTF);
        return -1;
      }
      
      if (i >= DynArray_getLength(oTokens) - 1) {
        errorPrint("Standard output redirection without file name", FPRINTF);
        return -1;
      }

      struct Token *nextToken = DynArray_get(oTokens, i + 1);
      if (nextToken->eType == TOKEN_REDIN || nextToken->eType == TOKEN_REDOUT) {
        errorPrint("Standard output redirection without file name", FPRINTF);
        return -1;
      }

      outputFd = open(nextToken->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (outputFd < 0) {
        errorPrint("Cannot open file", FPRINTF);
        return -1;
      }
      isOutputRedirect = 1;

      /* Remove '>' and file name from array */
      DynArray_removeAt(oTokens, i);
      DynArray_removeAt(oTokens, i);
      i--;
    }
  }
  
  /* Perform redirection */
  if (isInputRedirect) {
    dup2(inputFd, STDIN_FILENO);
    close(inputFd);
  }
  if (isOutputRedirect) {
    dup2(outputFd, STDOUT_FILENO);
    close(inputFd);
  }

  return 0;
}

/* Function: executeBuiltinCommand
   Parameters:
    - btype: Built-in command type
    - oTokens: Tokens from input command
   Execute built-in command line */
static void
executeBuiltinCommand(enum BuiltinType btype, DynArray_T oTokens) {
  /* Parse arguments */
  struct Token *token;
  char *arg1 = NULL;
  char *arg2 = NULL;

  if (DynArray_getLength(oTokens) > 1) {
    token = DynArray_get(oTokens, 1);
    arg1 = token->pcValue;
  }
  if (DynArray_getLength(oTokens) > 2) {
    token = DynArray_get(oTokens, 2);
    arg2 = token->pcValue;
  }

  /* Handle built-in commands */
  switch (btype) {
    case B_CD:
      /* cd [dir] */
      if (arg1 != NULL) {
        /* Change working directory to [dir] */
        if (chdir(arg1) != 0) {
          errorPrint("No such file or directory", FPRINTF);
        }
      }
      else {
        /* If [dir] is omitted, change to the HOME directory */
        const char *homeDir = getenv("HOME");
        if (chdir(homeDir) != 0) {
          errorPrint("Failed to change directory", PERROR);
        }
      }
      break;
    
    case B_EXIT:
      /* exit */
      exit(EXIT_FAILURE);
      break;
    
    case B_SETENV:
      /* setenv var [value] */
      if (arg1 == NULL) {
        errorPrint("setenv takes one or two parameters", FPRINTF);
        break;
      }
      if (arg2 == NULL) {
        arg2 = "";
      }
      if (setenv(arg1, arg2, 1) != 0) {
        errorPrint("Failed to set environment variable", PERROR);
      }
      break;
    
    case B_USETENV:
      /* unsetenv var */
      if (arg1 == NULL) {
        errorPrint("unsetenv takes one parameter", FPRINTF);
        break;
      }
      if (unsetenv(arg1) != 0) {
        errorPrint("Failed to unset environment variable", PERROR);
      }
      break;

    default:
      break;
  }
}

/* Function: executeExternalCommand
   Parameters:
    - oTokens: Tokens from input command
   Execute external (not a built-in) command line */
static void
executeExternalCommand(DynArray_T oTokens) {
  int numTokens = DynArray_getLength(oTokens);
  if (numTokens == 0) return;

  char **args;
  if ((args = malloc((numTokens + 1) * sizeof(char *))) == NULL) {
    errorPrint("Cannot allocate memory for arguments", FPRINTF);
  }

  struct Token *token;
  for (int i = 0; i < numTokens; i++) {
    token = DynArray_get(oTokens, i);
    args[i] = token->pcValue;
  }
  args[numTokens] = NULL;

  pid_t pid = fork();

  if (pid == 0) {
    /* Child process */
    if (doRedirection(oTokens) == 0) {
      execvp(args[0], args);
      /* If execvp() fails */
      errorPrint(args[0], PERROR);
    }
    free(args);
    exit(EXIT_FAILURE);
  }
  else {
    /* Parent process */
    int status;
    waitpid(pid, &status, 0);
  }

  free(args);
}

/* Function: shellHelper
   Parameters:
    - inLine: Input line from shell
   Handle input line and perform appropriately */
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
          /* Built-in command */
          executeBuiltinCommand(btype, oTokens);
        }
        else {
          /* Not a built-in command (external command) */
          executeExternalCommand(oTokens);
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

int main() {
  /* TODO */
  /* Initialize program name */
  errorPrint("./ish", SETUP);

  /* Set up signal handlers */
  signal(SIGINT, handleSIGINT);
  signal(SIGQUIT, handleSIGQUIT);
  signal(SIGALRM, handleSIGALRM);

  /* Read and interpret .ishrc file in HOME directory */
  char ishrcPath[1024] = {0};
  const char *homeDir = getenv("HOME");
  strcpy(ishrcPath, homeDir);
  strcat(ishrcPath, "/.ishrc");

  if (access(ishrcPath, R_OK) == 0) {
    FILE *ishrc = fopen(ishrcPath, "r");
    if (ishrc != NULL) {
      char acLine[MAX_LINE_SIZE + 2];
      while (fgets(acLine, MAX_LINE_SIZE, ishrc) != NULL) {
        fprintf(stdout, "%% %s", acLine);
        fflush(stdout);
        shellHelper(acLine);
      }
      fclose(ishrc);
    }
  }

  char acLine[MAX_LINE_SIZE + 2];
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


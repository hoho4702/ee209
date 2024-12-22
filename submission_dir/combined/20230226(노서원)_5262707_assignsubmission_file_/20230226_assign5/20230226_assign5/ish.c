/*--------------------------------------------------------------------*/
/* Assignment 5: A Unix Shell                                         */
/* Student Name / ID : Seowon Noh / 20230226                          */
/*--------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include "lexsyn.h"
#include "util.h"
/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/
/* Function: Execute Built-in Commands.                               */
/*--------------------------------------------------------------------*/
static void execBCMD(enum BuiltinType btype, DynArray_T oTokens) {
  /*----------------------------------------------------------------*/
  /* setenv var [value]                                             */
  /* If environment variable var does not exist, create.            */
  /* Set value of var to value or to empty string if value omitted. */
  /*----------------------------------------------------------------*/
  if (btype == B_SETENV) {
    const char* var = DynArray_get(oTokens, 1);
    const char* value = DynArray_get(oTokens, 2);
    if (setenv(var, value ? value : "", 1) != 0) {
      perror("B_SETENV failed.");
    }
  }
  /*----------------------------------------------------------------*/
  /* unsetenv var                                                   */
  /* Destroy the environment variable var.                          */
  /* If the environment variable does not exist, ignore.            */
  /*----------------------------------------------------------------*/
  else if (btype == B_USETENV) {
    const char* var = DynArray_get(oTokens, 1);
    if (unsetenv(var) != 0) { perror("unsetenv failed"); }
  }
  /*----------------------------------------------------------------*/
  /* cd [dir]                                                       */
  /* Change working directory to dir                                */
  /* Or to the HOME directory if dir is omitted.                    */
  /*----------------------------------------------------------------*/
  else if (btype == B_CD) {
    const char* dir = DynArray_get(oTokens, 1);
    /* Default dir set to HOME. */
    if (dir == NULL) { dir = getenv("HOME"); }
    if (chdir(dir) != 0) { perror("chdir failed"); }
  }
  /*----------------------------------------------------------------*/
  /* exit                                                           */
  /* Exit with exit status 0.                                       */
  /*----------------------------------------------------------------*/
  else if (btype == B_EXIT) { exit(EXIT_SUCCESS); } else {
    fprintf(stderr, "Invalid built-in command.\n");
  }
}
/*--------------------------------------------------------------------*/
/* Function: Execute Commands.                                        */
/*--------------------------------------------------------------------*/
static void execCMD(DynArray_T oTokens) {
  pid_t pid = fork();
  if (pid < 0) {
    perror("fork failed.");
    return;
  } else if (pid == 0) {
    /*------------------------------------------------------------*/
    /* Inside Child Process                                       */
    /* Restore original haldler for SIGINT and SIGQUIT.           */
    /*------------------------------------------------------------*/
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    char** argv = NULL;
    DynArray_toArray(oTokens, (void**)&argv);

    int inpRedirc = 0, outRedirc = 0;
    for (size_t i = 0; i < DynArray_getLength(oTokens); i++) {
      char* token = DynArray_get(oTokens, i);
      /* Handling input redirection. */
      if (strcmp(token, "<") == 0) {
        if (inpRedirc) {
          fprintf(stderr, "stdin redirection multiple.\n");
          exit(EXIT_FAILURE);
        }
        if (i + 1 >= DynArray_getLength(oTokens)) {
          fprintf(stderr, "stdin redirection w/o file name.\n");
          exit(EXIT_FAILURE);
        }
        char* fName = DynArray_get(oTokens, i + 1);
        int fd = open(fName, O_RDONLY);
        if (fd == -1) {
          perror("Failed to open input file.");
          exit(EXIT_FAILURE);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
        inpRedirc = 1;
        DynArray_removeAt(oTokens, i);
        DynArray_removeAt(oTokens, i);
        i--;
      }
      /* Handling output redirection. */
      else if (strcmp(token, ">") == 0) {
        if (outRedirc) {
          fprintf(stderr, "stdout redirection multiple.\n");
          exit(EXIT_FAILURE);
        }
        if (i + 1 >= DynArray_getLength(oTokens)) {
          fprintf(stderr, "stdout redirection w/o file name.\n");
          exit(EXIT_FAILURE);
        }
        char* fName = DynArray_get(oTokens, i + 1);
        int fd = open(fName, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd == -1) {
          perror("Failed to open output file.");
          exit(EXIT_FAILURE);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
        outRedirc = 1;
        DynArray_removeAt(oTokens, i);
        DynArray_removeAt(oTokens, i);
        i--;
      }
    }
    /* Execute original built in command. */
    if (execvp(argv[0], argv) == -1) {
      fprintf(stderr, "execvp failed for %s\n", argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  /* Parent Process: wait for child process to finish. */
  else {
    int status;
    waitpid(pid, &status, 0);
  }
}
/*--------------------------------------------------------------------*/
/* Function: Handle parsing and CMD execution.                        */
/*--------------------------------------------------------------------*/
static void
shellHelper(const char* inLine) {
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
      /* Ececute execBCMD if it is a built in command. */
      /* Execute execCMD if it is other.               */
      if (btype) { execBCMD(btype, oTokens); } else { execCMD(oTokens); }
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
/*--------------------------------------------------------------------*/
/* Handler for SIGQUIT signal                                         */
/* lastQT: last time when quit signal was sent.                       */
/* currT: current time to compare with lastQT.                        */
/*--------------------------------------------------------------------*/
void handleSQuit(int sig) {
  static time_t lastQT = 0;
  time_t currT = time(NULL);
  if (currT - lastQT <= 5) { exit(EXIT_SUCCESS); } else {
    fprintf(stdout, "Type Ctrl-\\ again within %d seconds to exit.\n", 5);
  }
  lastQT = currT;
}
/*--------------------------------------------------------------------*/
/* Handler for SIGINT signal: Ignore SIGINT in Parent Process.        */
/*--------------------------------------------------------------------*/
void handleSInt(int sig) { return; }
int main() {
  struct sigaction sigacquit, sigacint;
  /* Set SIGQUIT handler to handleSQuit. */
  sigacquit.sa_handler = handleSQuit;
  sigemptyset(&sigacquit.sa_mask);
  sigacquit.sa_flags = 0;
  sigaction(SIGQUIT, &sigacquit, NULL);
  /* Set SIGINT handler to handleSInt. */
  sigacint.sa_handler = handleSInt;
  sigemptyset(&sigacint.sa_mask);
  sigacint.sa_flags = 0;
  sigaction(SIGINT, &sigacint, NULL);
  /* Find home directory and find path to .ishrc file. */
  FILE* fp = NULL;
  char* homeDirc = getenv("HOME");
  char filePth[MAX_LINE_SIZE];
  char acLine[MAX_LINE_SIZE + 2];
  if (homeDirc != NULL) {
    snprintf(filePth, MAX_LINE_SIZE, "%s/.ishrc", homeDirc);
    fp = fopen(filePth, "r");
  }
  if (fp != NULL) {
    /* Display and process line by line from .ishrc file. */
    while (fgets(acLine, sizeof(acLine), fp) != NULL) {
      printf("%% %s", acLine);
      shellHelper(acLine);
    }
    fclose(fp);
  }
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    /* Processs user input commands. */
    shellHelper(acLine);
  }
  return 0;
}

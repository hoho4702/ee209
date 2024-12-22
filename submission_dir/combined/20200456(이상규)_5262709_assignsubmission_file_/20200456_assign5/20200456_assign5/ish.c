#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
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

static volatile sig_atomic_t quitCount = 0;
static time_t lastQuitTime = 0;

static void sigintHandler(int sig) {
  //ignore sigint
}

static void sigquitHandler(int sig) {
  //process sigquit
  time_t now = time(NULL);
  if (quitCount == 0 || difftime(now, lastQuitTime) > 5.0) {
    fprintf(stderr, "Type Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stderr);
    quitCount = 1;
    lastQuitTime = now;
  } else {
    //exit shell
    exit(0);
  }
}

static int handleBuiltin(DynArray_T oTokens, enum BuiltinType btype) {
  int i;
  struct Token *t;
  int length = DynArray_getLength(oTokens);

  //only builtin command
  for (i = 1; i < length; i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT || 
      t->eType == TOKEN_PIPE || t->eType == TOKEN_BG) {
      errorPrint("Redirection or background not allowed for built-in commands", FPRINTF);
      return 0;
    }
  }

  switch (btype) {
    case B_CD: {
      //cd process:home
      const char *targetDir = NULL;
      if (length == 1) {
          targetDir = getenv("HOME");
          if (targetDir == NULL) {
              errorPrint("HOME not set", FPRINTF);
              return 0;
          }
      }
      else {
        struct Token *dirToken = DynArray_get(oTokens, 1);
        targetDir = dirToken->pcValue;
      }

      if (chdir(targetDir) == -1) {
        errorPrint(NULL, PERROR);
        return 0;
      }
      return 1;
    }

    case B_EXIT: {
      //exit command
      exit(0);
    }

    case B_SETENV: {
      //setenc var implement
      if (length < 2) {
        errorPrint("setenv: Missing variable name", FPRINTF);
        return 0;
      }

      struct Token *varToken = DynArray_get(oTokens, 1);
      const char *varName = varToken->pcValue;
      const char *varValue = "";
      if (length > 2) {
        struct Token *valToken = DynArray_get(oTokens, 2);
        varValue = valToken->pcValue;
      }

      if (setenv(varName, varValue, 1) == -1) {
        errorPrint(NULL, PERROR);
        return 0;
      }
      return 1;
    }

    case B_USETENV: {
      //unsetenv var
      if (length < 2) {
        //ignore
        return 1;
      }

      struct Token *varToken = DynArray_get(oTokens, 1);
      const char *varName = varToken->pcValue;

      if (unsetenv(varName) == -1) {
        errorPrint(NULL, PERROR);
        return 0;
      }
      return 1;
    }

    default:
      return 0;
  }
}

//divide by pipe
struct Command {
  char **argv; //execvp
  int argc;
  char *infile; //< file 
  char *outfile; // file 
};

static struct Command* parseCommands(DynArray_T oTokens, int *numCmds, int *isBackground) {
  //num of pipe, check the background
  int length = DynArray_getLength(oTokens);
  int i;
  int pipeCount = 0;
  int bgFound = 0;

  for (i = 0; i < length; i++) {
    struct Token *t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE) pipeCount++;
    if (t->eType == TOKEN_BG) bgFound = 1;
  }

  //set the background command
  *isBackground = bgFound;

  //divide command by pipe
  int cmdCount = pipeCount + 1;
  *numCmds = cmdCount;
  struct Command *cmds = calloc(cmdCount, sizeof(struct Command));

  if (cmds == NULL) {
    return NULL;
  }

  int cmdIndex = 0;
  int argCount = 0;
  cmds[0].argv = malloc(sizeof(char*)*(length+1));
  int j;

  for (j = 0; j < length+1; j++) { 
    cmds[0].argv[j] = NULL; 
  }

  for (i = 0; i < length; i++) {
    struct Token *t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE) {
      //next command
      cmds[cmdIndex].argc = argCount;
      cmds[cmdIndex].argv[argCount] = NULL;

      cmdIndex++;
      argCount = 0;
      cmds[cmdIndex].argv = malloc(sizeof(char*)*(length+1));
      
      for (j = 0; j < length+1; j++) { 
        cmds[cmdIndex].argv[j] = NULL; 
      }
    }
    else if (t->eType == TOKEN_REDIN) {
      //input redirection
      struct Token *f = DynArray_get(oTokens, ++i);
      cmds[cmdIndex].infile = f->pcValue;
    }
    else if (t->eType == TOKEN_REDOUT) {
      //output redirection
      struct Token *f = DynArray_get(oTokens, ++i);
      cmds[cmdIndex].outfile = f->pcValue;
    }
    else if (t->eType == TOKEN_BG) {
      continue;
    }
    else {
      //normal
      cmds[cmdIndex].argv[argCount++] = t->pcValue;
    }
  }

  cmds[cmdIndex].argc = argCount;
  cmds[cmdIndex].argv[argCount] = NULL;

  return cmds;
}

static void freeCommands(struct Command *cmds, int numCmds) {
  //memory free
  if (cmds == NULL) return;

  for (int i = 0; i < numCmds; i++) {
      free(cmds[i].argv);
  }
  free(cmds);
}

static void executeCommands(struct Command *cmds, int numCmds, int isBackground) {
  //pipeline process
  int i;
  int pipes[2*numCmds];

  for (i = 0; i < numCmds-1; i++) {
    //make pipe
    if (pipe(&pipes[2*i]) < 0) {
      perror("pipe");
      return;
    }
  }

  for (i = 0; i < numCmds; i++) {
    pid_t pid = fork();
    if (pid < 0) {
      perror("fork");
      return;
    }
    if (pid == 0) {
      //basic signal
      struct sigaction sa;
      sa.sa_handler = SIG_DFL;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
      sigaction(SIGINT, &sa, NULL);
      sigaction(SIGQUIT, &sa, NULL);

      //process input pipe
      if (i > 0) {
        if (dup2(pipes[2*(i-1)], STDIN_FILENO) < 0) {
          perror("dup2 in");
          exit(EXIT_FAILURE);
        }
      }
      //process output pipe
      if (i < numCmds-1) {
        if (dup2(pipes[2*i+1], STDOUT_FILENO) < 0) {
          perror("dup2 out");
          exit(EXIT_FAILURE);
        }
      }

      //close pipe
      for (int k = 0; k < 2*(numCmds-1); k++) {
        close(pipes[k]);
      }

      //process redirection
      if (cmds[i].infile) {
        int infd = open(cmds[i].infile, O_RDONLY);
        if (infd < 0) {
          perror(cmds[i].infile);
          exit(EXIT_FAILURE);
        }
        if (dup2(infd, STDIN_FILENO) < 0) {
          perror("dup2 infile");
          exit(EXIT_FAILURE);
        }
        close(infd);
      }
      if (cmds[i].outfile) {
        int outfd = open(cmds[i].outfile, O_WRONLY|O_CREAT|O_TRUNC, 0600);
        if (outfd < 0) {
          perror(cmds[i].outfile);
          exit(EXIT_FAILURE);
        }
        if (dup2(outfd, STDOUT_FILENO) < 0) {
          perror("dup2 outfile");
          exit(EXIT_FAILURE);
        }
        close(outfd);
      }
      //command process
      execvp(cmds[i].argv[0], cmds[i].argv);
      perror(cmds[i].argv[0]);
      exit(EXIT_FAILURE);
    }
  }

  //close all pipe
  for (int k = 0; k < 2*(numCmds-1); k++) {
    close(pipes[k]);
  }

  if (!isBackground) {
    for (i = 0; i < numCmds; i++) {
      int status;
      wait(&status);
    }
  } 
}

static void handleExternalWithPipes(DynArray_T oTokens) {
  //not builtin process with pipe
  int numCmds = 0;
  int isBackground = 0;
  struct Command *cmds = parseCommands(oTokens, &numCmds, &isBackground);
  if (cmds == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    return;
  }

  //no command
  if (numCmds == 0 || cmds[0].argv[0] == NULL) {
    freeCommands(cmds, numCmds);
    return;
  }

  executeCommands(cmds, numCmds, isBackground);
  freeCommands(cmds, numCmds);
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
      if (DynArray_getLength(oTokens) == 0){
        DynArray_free(oTokens);
        return;
      }
      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        if (btype != NORMAL) {
          //builtin process
          handleBuiltin(oTokens, btype);
        } 
        else {
          //otherwise
          handleExternalWithPipes(oTokens);
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

static void runIshrc(void) {
  const char *home = getenv("HOME");
  if (home == NULL) {
    return;
  }

  char path[1024];
  snprintf(path, sizeof(path), "%s/.ishrc", home);
  FILE *fp = fopen(path, "r");
  if (fp == NULL) {
    return;
  }

  char line[MAX_LINE_SIZE+2];
  while (fgets(line, MAX_LINE_SIZE, fp) != NULL) {
    size_t len = strlen(line);
    if (len > 0 && line[len-1] == '\n')
        line[len-1] = '\0';

    fprintf(stdout, "%% %s\n", line);
    fflush(stdout);

    shellHelper(line);
  }

  fclose(fp);
}

int main(int argc, char *argv[]) {
  /* TODO */
  char acLine[MAX_LINE_SIZE + 2];

  errorPrint(argv[0], SETUP); 

  struct sigaction sa_int, sa_quit;
  sa_int.sa_handler = sigintHandler;
  sigemptyset(&sa_int.sa_mask);
  sa_int.sa_flags = 0;
  sigaction(SIGINT, &sa_int, NULL);

  sa_quit.sa_handler = sigquitHandler;
  sigemptyset(&sa_quit.sa_mask);
  sa_quit.sa_flags = 0;
  sigaction(SIGQUIT, &sa_quit, NULL);

  runIshrc();

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


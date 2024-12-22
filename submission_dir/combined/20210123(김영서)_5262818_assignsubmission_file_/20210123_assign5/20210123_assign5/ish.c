/*
Name: Youngseo Kim
Student ID: 20210123
Assignment No.: 5
File Name: ish.c
*/

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#define PATH_MAX 4096

#include "dynarray.h"
#include "lexsyn.h"
#include "token.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/


/// Function to handle built-in commands
static void execute_builtin(enum BuiltinType btype, DynArray_T oTokens) {
  switch (btype) {
    case B_CD: {
      int token_len = DynArray_getLength(oTokens);
      if (token_len == 1) {
        if (chdir(getenv("HOME")) == -1){
          errorPrint("No such file or directory", FPRINTF);
        }
      }
      else if (token_len == 2){
        struct Token *argToken = DynArray_get(oTokens, 1);
        if (chdir(argToken->pcValue) == -1) {
          errorPrint("No such file or directory", FPRINTF);
        }
      }
      else {
        errorPrint("cd: Invalid number of parameter", FPRINTF);
      }
      break;
      }
    case B_SETENV: {
      int token_len = DynArray_getLength(oTokens);
      if (token_len < 2){
        errorPrint("setenv: Missing arguments", FPRINTF);
        return;
      }
      if (token_len > 3){
        errorPrint("setenv: Too many arguments", FPRINTF);
        return;
      }
      struct Token *var = DynArray_get(oTokens, 1);
      struct Token *val = token_len == 3 ? DynArray_get(oTokens, 2) : NULL;
      if (setenv(var->pcValue, val ? val->pcValue : "", 1) != 0) {
        errorPrint("setenv: Failed to set environment variable", PERROR);
        }
      break;
      }
    case B_USETENV: {
      int token_len = DynArray_getLength(oTokens);
      if (token_len < 2){
        errorPrint("unsetenv: Missing arguments", FPRINTF);
        return;
      }
      if (token_len > 2){
        errorPrint("unsetenv: Too many arguments", FPRINTF);
        return;
      }
      struct Token *var = DynArray_get(oTokens, 1);
      if (unsetenv(var->pcValue) != 0 && errno != ENOENT) {
        // Ignore if variable does not exist, report other errors
        errorPrint("unsetenv: Failed to unset environment variable", PERROR);
      }
      break;
      }
    case B_EXIT:{
      int token_len = DynArray_getLength(oTokens);
      if (token_len != 1){
        errorPrint("Exit does not take any parameters", FPRINTF);
      }
      DynArray_map(oTokens, freeToken, NULL);
      //Free all allocation
      DynArray_free(oTokens);
      exit(0); 
    }
  }
}

static int handleRedirection(char *filename, int mode) {
    assert(filename);
    int fd;

    if (mode == 1) { // Input redirection
      fd = open(filename, O_RDONLY);
      if (fd == -1) {
        fprintf(stderr, "Error: Cannot open input file '%s': %s\n", filename, strerror(errno));
        return -1;
      }
      if (dup2(fd, STDIN_FILENO) == -1) {
          fprintf(stderr, "Error: Cannot redirect input: %s\n", strerror(errno));
          close(fd);
          return -1;
      }
    } else if (mode == 0) { // Output redirection
      fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd == -1) {
          fprintf(stderr, "Error: Cannot open or create output file '%s': %s\n", filename, strerror(errno));
          return -1;
      }
      if (dup2(fd, STDOUT_FILENO) == -1) {
          fprintf(stderr, "Error: Cannot redirect output: %s\n", strerror(errno));
          close(fd);
          return -1;
      }
    } else {
      fprintf(stderr, "Error: Invalid mode for redirection\n");
      return -1;
    }
  close(fd); // Close the original file descriptor after duplicating
  return 0;
}

static void execute(DynArray_T oTokens) {
  
  int status;
  char *argv[64];
  struct Token *t;
  int bg = checkBG(oTokens);

  int pipe_count = countPipe(oTokens);
  int pipe_fds[pipe_count][2];
  int command_start = 0;
  int command_end;

  for (int j = 0; j <= pipe_count; j++) {
    command_end = command_start;
    char *inputFile = NULL;
    char *outputFile = NULL;

    while (command_end < DynArray_getLength(oTokens) && ((t = DynArray_get(oTokens, command_end))->eType != TOKEN_PIPE)) {
      if (t->eType == TOKEN_REDIN) {
        struct Token *token = (struct Token *)DynArray_get(oTokens, ++command_end);
        inputFile = token->pcValue;
        
      } else if (t->eType == TOKEN_REDOUT) {
        struct Token *token = (struct Token *)DynArray_get(oTokens, ++command_end);
        outputFile = token->pcValue;
      } else {
        argv[command_end - command_start] = t->pcValue;
      }
      command_end++;
    }
    argv[command_end - command_start] = NULL;

    if (j < pipe_count && pipe(pipe_fds[j]) < 0) {
      fprintf(stderr, "Cannot create pipe: %s\n", strerror(errno));
      return;
    }

    pid_t pid;
    pid = fork();
    if (pid < 0) {
      fprintf(stderr, "Cannot fork: %s\n", strerror(errno));
      return;
    } else if (pid == 0) {
      if (inputFile && handleRedirection(inputFile, 1) < 0) exit(EXIT_FAILURE);
      if (outputFile && handleRedirection(outputFile, 0) < 0) exit(EXIT_FAILURE);
      
      if (j > 0) dup2(pipe_fds[j - 1][0], STDIN_FILENO);
      if (j < pipe_count) dup2(pipe_fds[j][1], STDOUT_FILENO);

      for (int p = 0; p < pipe_count; p++) {
        close(pipe_fds[p][0]);
        close(pipe_fds[p][1]);
      }
      execvp(argv[0], argv);
      fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
      exit(EXIT_FAILURE);
    }

    if (j > 0) close(pipe_fds[j - 1][0]);
    if (j < pipe_count) close(pipe_fds[j][1]);

    command_start = command_end + 1;

  }

  if (!bg) while (wait(&status) > 0);
}

// Function to parse and execute a single line
static void
shellHelper(const char *inLine, char *prgmname) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }
  
  errorPrint(prgmname, SETUP);

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
        if (btype == NORMAL){
          execute(oTokens);
        }
        else {execute_builtin(btype, oTokens);}
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


// Read and execute .ishrc
static void execute_ishrc(char *ishname) {
  char *home= getenv("HOME");
  assert(home);
  char Path[1024];

  snprintf(Path, sizeof(Path), "%s/.ishrc", home);
  chdir(home);
  FILE *file = fopen(Path, "r");  

  if (!file) return;
    
  char line[MAX_LINE_SIZE];
  while (fgets(line, sizeof(line), file)) {
    printf("%% %s", line);
    fflush(stdout);
    shellHelper(line, ishname);
  }
  fclose(file);
}

// Global variables
static int sigquit_flag = 0;

// Signal handlers
void handle_sigquit(int sig) {
    // Handle SIGQUIT in the parent process
    (void)sig;
    if (!sigquit_flag) {
        sigquit_flag = 1;
        alarm(5);
        printf("\nType Ctrl-\\ again within 5seconds to exit.\n");
        fflush(stdout);
    } else {
      exit(EXIT_SUCCESS);
    }
}

void handle_alarm(int sig) {
    // Handle alarm signal
    (void)sig;
    sigquit_flag = 0;
}

int main(int argc, char *argv[]) {
  /* TODO */
  sigset_t sig;

  sigemptyset(&sig);
  sigaddset(&sig, SIGINT);  
  sigaddset(&sig, SIGQUIT);
  sigaddset(&sig, SIGALRM);

  sigprocmask(SIG_UNBLOCK, &sig, NULL);

  signal(SIGINT, SIG_IGN);  //Ignore SIGINT in parent process
  signal(SIGQUIT, handle_sigquit);
  signal(SIGALRM, handle_alarm);

  char acLine[MAX_LINE_SIZE + 2];
  char *ishname=argv[0];

  execute_ishrc(ishname);

  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine, ishname);
  }
}


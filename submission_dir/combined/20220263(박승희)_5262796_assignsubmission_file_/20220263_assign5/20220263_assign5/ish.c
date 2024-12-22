/* EE209 Assignment5 by 20220263 Seunghee Park */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
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

volatile sig_atomic_t quit_flag = 0;

/* SIGQUIT handler */
void SIGQUIT_handler(int sig) {
  if (quit_flag) {
      exit(EXIT_SUCCESS);
  } else {
      printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
      fflush(stdout);
      quit_flag = 1;
      alarm(5);
  }
}

/* SIGALRM handler */
void SIGALRM_handler(int sig) {
    quit_flag = 0;
}

// Signal setup function
void handle_Signal() {
  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  // Set signal handlers
  assert(signal(SIGINT, SIG_IGN) != SIG_ERR);
  assert(signal(SIGQUIT, SIGQUIT_handler) != SIG_ERR);
  assert(signal(SIGALRM, SIGALRM_handler) != SIG_ERR);
}
/*--------------------------------------------------------------------*/
/* 
  executeBuiltin(oTokens)
  - Input : DynArray_T oTokens / Output : None 
  - Handles the execution of built-in commands in case-by-case
  - Error handling : Ensures invalid commands or arguments 
*/
void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens) {
  switch (btype) {
    case B_CD: { // Change directory
      size_t token_num = DynArray_getLength(oTokens);
      if (token_num > 2) { 
        errorPrint("cd: can take one parameter", FPRINTF);
        break;
      }
      else if (token_num == 1){ // change to home directory 
        const char *homeDir = getenv("HOME");
        if (chdir(homeDir) == -1) {
          errorPrint("cd: cannot change directory to HOME", PERROR);
        }
      }
      else { // change to dir 
        struct Token *Dir= (struct Token*) DynArray_get(oTokens, 1);
        if(chdir(Dir->pcValue) == -1) {
          errorPrint("cd: cannot change directory", PERROR);
        }
      }
      break;
    }

    case B_SETENV: { // Set environment variable

      size_t token_num = DynArray_getLength(oTokens);
      if (token_num < 2 || token_num > 3) { 
        errorPrint("setenv: can take one or two parameter", FPRINTF);
      }
      else if (token_num == 2){ // create var and set to empty string 
        struct Token *Var = (struct Token *)DynArray_get(oTokens, 1); 
        if (setenv(Var->pcValue, "", 1) == -1) {
            errorPrint("setenv: failed to set environment variable", PERROR);
        }
      }
      else { // set to value 
        struct Token *Var = (struct Token *)DynArray_get(oTokens, 1); 
        struct Token *Value = (struct Token *)DynArray_get(oTokens, 2); 
        if (setenv(Var->pcValue, Value->pcValue, 1) == -1) {
          errorPrint("setenv: failed to set environment variable", PERROR);
        }
      }
      break;
    }

    case B_USETENV: { // Unset environment variable
      size_t token_num = DynArray_getLength(oTokens);
      if (token_num != 2) { 
        errorPrint("unsetenv: can take one parameter", FPRINTF);
      }
      else { 
        struct Token *Var = (struct Token *)DynArray_get(oTokens, 1);
        if (unsetenv(Var->pcValue) == -1) {
            errorPrint("unsetenv: failed to unset environment variable", PERROR);
        }
      }
      break;
    }

    case B_EXIT: { // Exit shell
      size_t token_num = DynArray_getLength(oTokens);
      if (token_num > 1) { 
        errorPrint("exit: cannot take parameter", FPRINTF);
      }
      else {
        DynArray_free(oTokens);
        exit(EXIT_SUCCESS); // terminate shell 
      }
      break;
    }

    default: { // Unknown builtin
      errorPrint("Unknown builtin", FPRINTF);
      break;
    }
  }
}
/**********************************************************************/
/* 
  createArgArray(oTokens) 
  - Input : DynArray_T oTokens / Output : char ** ArgArray
  - Extract command name and its arguments from oTokens 
  - Iterates through oTokens while skipping input/output redirection 
  - Error handling : If memory allocation fail, free allocated memory
*/
char **createArgArray(DynArray_T oTokens) {
  assert(oTokens != NULL); 

  int token_num = DynArray_getLength(oTokens); 
  char **argArray = (char **)malloc((token_num + 1) * sizeof(char *)); 

  if (argArray == NULL) {
    errorPrint(NULL, PERROR);
    return NULL;
  }

  int index = 0; 
  int i;
  for (i = 0; i < token_num; i++) {
    struct Token *token = (struct Token *)DynArray_get(oTokens, i);

    // ignore redirection
    if (token->eType == TOKEN_REDIN || token->eType == TOKEN_REDOUT) {
      i++; 
      continue;
    }

    // Except redirection 
    if (token->eType == TOKEN_WORD) {
      argArray[index] = (char *)malloc(strlen(token->pcValue) + 1);
      if (argArray[index] == NULL) {
        errorPrint(NULL, PERROR);
        int j;
        for (j = 0; j < index; j++) {
          free(argArray[j]);
        }
        free(argArray);
        return NULL;
      }
      strcpy(argArray[index], token->pcValue);
      index++;
    }
  }

  argArray[index] = NULL; // end with NULL
  return argArray;
}
/* 
  handleRedirection(oTokens)
  - Input : DynArray_T oTokens / Output : -1 (fail), 0 (success)
  - Redirects standard input and standard output based on < and >
  - Iterates through oTokens while checking edge cases  
  - Error handling : multiple or missing redirections, not working
*/
int handleRedirection(DynArray_T oTokens) {
  int REDIN = 0; 
  int REDOUT = 0; 
  int i;
  for (i = 0; i < DynArray_getLength(oTokens); i++) {
    struct Token *token = (struct Token *)DynArray_get(oTokens, i);

    if (token->eType == TOKEN_REDIN) { 
      // redundancy 
      if(REDIN) {
        errorPrint("Invalid: Multiple redirection of standard input", FPRINTF);
        return -1;
      }
      // without file name 
      if (i + 1 >= DynArray_getLength(oTokens)) {
        errorPrint("Invalid: Standard input redirection without file name", FPRINTF);
        return -1;
      }
      struct Token *input_file = (struct Token *)DynArray_get(oTokens, i + 1); 
      int fd = open(input_file->pcValue, O_RDONLY);
      // fail to open file 
      if (fd == -1) {
        errorPrint(NULL, PERROR);
        return -1;
      }
      // fail redirection 
      if (dup2(fd, 0) == -1) {
        errorPrint(NULL, PERROR);
        return -1;
      }
      close(fd);
      REDIN = 1;
      i++;
    }
    else if (token->eType == TOKEN_REDOUT){
      // same logic with upper case (TOKEN_REDINPUT)
      if(REDOUT) {
        errorPrint("Invalid: Multiple redirection of standard output", FPRINTF);
        return -1;
      }
      if (i + 1 >= DynArray_getLength(oTokens)) {
        errorPrint("Invalid: Standard output redirection without file name", FPRINTF);
        return -1;
      }
      struct Token *output_file = (struct Token *)DynArray_get(oTokens, i + 1);
      int fd = open(output_file->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600); 
      if (fd == -1) {
        errorPrint(NULL, PERROR);
        return -1;
      }
      if (dup2(fd, STDOUT_FILENO) == -1) { 
        errorPrint(NULL, PERROR);
        close(fd);
        return -1;
      }
      close(fd); 
      REDOUT = 1;
      i++; 
    }
  }
  return 0; // Success redirection 
}
/* 
  executeCommand(oTokens)
  - Input : DynArray_T oTokens / Output : None 
  - Handles the execution of non built-in commands
    * parent  : Ignores SIGINT, waith child process
    * child   : Restore default signal, redirection, 
                create arg for execvp, execute command 
  - Error handling : fail redirection, execvp
*/
void executeCommand(DynArray_T oTokens){
  int status = 0;
  fflush(NULL);

  pid_t pid = fork();
  signal(SIGINT, SIG_IGN); 
  
  if (pid < 0) {
    errorPrint("Failed to create process", PERROR);
    exit(EXIT_FAILURE);
  }
  else if (pid == 0) {
    /* in child */
    /* 0. Restore signals' default */
    fflush(NULL);
    assert(signal(SIGINT, SIG_DFL) != SIG_ERR);
    assert(signal(SIGQUIT, SIG_DFL) != SIG_ERR);

    /* 1. Redirection */
    if (handleRedirection(oTokens) == -1) {
      exit(EXIT_FAILURE); // fail redirection 
    }

    /* 2. Execute command */
    char **args = createArgArray(oTokens);
    if (args == NULL) {
      exit(EXIT_FAILURE);
    }
    execvp(args[0], args);
    errorPrint(args[0], PERROR);
    int i; 
    for (i = 0; args[i] != NULL; i++) {
      free(args[i]);
    }
    free(args);
    exit(EXIT_FAILURE);
  }
  else {
    /* in parent */
    wait(&status);
  }
  
}

static void
shellHelper(const char *inLine) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck; 
  enum BuiltinType btype; 

  // 1. Create a new dynamic array for tokens
  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }

  // 2. Perform lexical analysis
  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0){
        DynArray_free(oTokens);
        return;
      }
      dumpLex(oTokens);

      // 3. Perform syntactic analysis
      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        // Check if the command is a built-in command
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        if (btype != NORMAL) { // built-in 
          executeBuiltin(btype, oTokens);
        } else { // non built-in
          executeCommand(oTokens);
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


int main(int argc, char *argv[]) {
  handle_Signal();

  char *homeDir = getenv("HOME"); // Get HOME directory
  assert(homeDir);

  char currentDir[1024];  // Get current program's directory
  char *curDir = getcwd(currentDir, sizeof(currentDir));
  assert(curDir);

  chdir(homeDir); // change to home dir 

  FILE *ishrc_fp = fopen(".ishrc", "r");
  errorPrint(argv[0], SETUP);

  char acLine[MAX_LINE_SIZE + 2];

  if (ishrc_fp != NULL) {
    while (fgets(acLine, MAX_LINE_SIZE, ishrc_fp) != NULL) {
      size_t len = strlen(acLine);
      if (len > 0 && acLine[len - 1] == '\n') {
        acLine[len - 1] = '\0'; 
      }
      printf("%% %s\n", acLine); 
      fflush(stdout);
      shellHelper(acLine); // Process the command
    }
    fclose(ishrc_fp);
  }

  chdir(currentDir); // restore 

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
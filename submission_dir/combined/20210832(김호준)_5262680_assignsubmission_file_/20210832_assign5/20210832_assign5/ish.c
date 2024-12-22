#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <bits/sigaction.h>
#include <bits/types/sigset_t.h>
#include <assert.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

volatile sig_atomic_t quit = 0;
const char *program_name = NULL;

void sigquit_handler(int sig){
  if (quit){
    exit(EXIT_SUCCESS);
  } else{
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    quit = 1;
    alarm(5);
  }
}

void alarm_handler(int sig){
  quit = 0;
}

void signal_handler_for_parent(){
  void (*pfRet)(int);
  pfRet = signal(SIGINT, SIG_IGN);
  assert(pfRet != SIG_ERR);
  pfRet = signal(SIGQUIT, sigquit_handler);
  assert(pfRet != SIG_ERR);
  pfRet = signal(SIGALRM, alarm_handler);
  assert(pfRet != SIG_ERR);
  return;
}

int setenv_builtin(DynArray_T tokens) {
  assert(tokens != NULL);
  int length = DynArray_getLength(tokens);
  if (length == 1 || length > 3){
    errorPrint("setenv takes one or two parameters", FPRINTF);
    return -1;
  }
  else if (length == 2){
    struct Token *varname = DynArray_get(tokens, 1);
    if (varname->pcValue == NULL || *varname->pcValue == '\0' || strchr(varname->pcValue, '=') != NULL){
      errorPrint("Invalid argument", FPRINTF);
      return -1;
    }
    if (setenv(varname->pcValue, "", 1) != 0){
      perror(program_name);
      return -1;
    }
    return 0;
  }
  else {
    struct Token *varname = DynArray_get(tokens, 1);
    struct Token *value = DynArray_get(tokens, 2);
    if (varname->pcValue == NULL || *varname->pcValue == '\0' || strchr(varname->pcValue, '=') != NULL){
      errorPrint("Invalid argument", FPRINTF);
      return -1;
    }
    if (setenv(varname->pcValue, value->pcValue, 1) != 0){
      perror(program_name);
      return -1;
    }
    return 0;
  }
}

int unsetenv_builtin(DynArray_T tokens) {
  assert(tokens != NULL);
  int length = DynArray_getLength(tokens);
  if (length == 1 || length > 2){
    errorPrint("unsetenv takes one parameter", FPRINTF);
    return -1;
  }
  else {
    struct Token *varname = DynArray_get(tokens, 1);
    if (varname->pcValue == NULL || *varname->pcValue == '\0' || strchr(varname->pcValue, '=') != NULL){
      errorPrint("Invalid argument", FPRINTF);
    }
    if (unsetenv(varname->pcValue) != 0){
      perror(program_name);
      return -1;
    }
    return 0;
  }
}

int cd_builtin(DynArray_T tokens) {
  assert(tokens != NULL);
  int length = DynArray_getLength(tokens);
  if (length > 2){
    errorPrint("cd takes one parameter", FPRINTF);
    return -1;
  }
  else {
    const char* destination;
    if (length == 1){
      destination = getenv("HOME");
    }
    else {
      struct Token *dirname = DynArray_get(tokens, 1);
      destination = dirname->pcValue;
    }
    if (chdir(destination) != 0){
      perror(program_name);
      return -1;
    }
    return 0;
  }

}

int not_builtin(DynArray_T tokens, char *inputfile, char *outputfile) {
  assert(tokens != NULL);

  // note that inputfile and outputfile can both be NULL, hence no need to assert() them.

  int length = DynArray_getLength(tokens);

  char **argv = malloc((length+1) * sizeof(char *));
  if (argv == NULL){
    perror(program_name);
    return -1;
  }

  for (size_t i = 0; i <= length-1; i++) {
    struct Token *arg_token = DynArray_get(tokens, i);
    argv[i] = arg_token->pcValue;
  }
  argv[length] = NULL;

  fflush(NULL);
  pid_t pid = fork();

  if (pid < 0){
    perror(program_name);
    return -1;
  } 
  else if (pid == 0){
    signal(SIGINT, SIG_DFL);

    if (inputfile != NULL){
      int fd_input = open(inputfile, O_RDONLY);
      if (fd_input < 0) {
        perror(program_name);
        exit(0);
      }
      if (dup2(fd_input, STDIN_FILENO) < 0){
        perror(program_name);
        exit(0);
      }
    }

    if (outputfile != NULL){
      int fd_output = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd_output < 0) {
        perror(program_name);
        exit(0);
      }
      if (dup2(fd_output, STDOUT_FILENO) < 0){
        perror(program_name);
        exit(0);
      }
    }
    execvp(argv[0], argv);
    perror(argv[0]);
    exit(0);
  }
  else {
    int status;
    if (waitpid(pid, &status, 0) < 0){
      perror(program_name);
      return -1;
    }
  }
  free(argv);
  return 0;
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

        char *inputfile = NULL; char *outputfile = NULL;
        int i = 0;
        while (i < DynArray_getLength(oTokens)){
          struct Token *t = DynArray_get(oTokens, i);
          if (t->eType == TOKEN_REDIN) {
            struct Token *file = DynArray_get(oTokens, i+1);
            inputfile = file->pcValue;
            DynArray_removeAt(oTokens, i+1); 
            DynArray_removeAt(oTokens, i);
          }
          else if (t->eType == TOKEN_REDOUT){
            struct Token *file = DynArray_get(oTokens, i+1);
            outputfile = file->pcValue;
            DynArray_removeAt(oTokens, i+1); 
            DynArray_removeAt(oTokens, i);
          }
          else {
            i++;
          }
        }

        btype = checkBuiltin(DynArray_get(oTokens, 0));
        switch (btype) {
          case B_EXIT:
            exit(0);
            break;
          case B_SETENV:
            setenv_builtin(oTokens);
            break;
          case B_USETENV:
            unsetenv_builtin(oTokens);
            break;
          case B_CD:
            cd_builtin(oTokens);
            break;
          case B_ALIAS:
            printf("ALIAS\n");
            break;
          case B_FG:
            printf("FG\n");
            break;
          case NORMAL:
            not_builtin(oTokens, inputfile, outputfile);
            break;
          default:
            break;
        }
        /* TODO */
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

void read_ishrc(){
  const char* home = getenv("HOME");
  assert(home != NULL);

  char ishrc_path[MAX_LINE_SIZE];
  snprintf(ishrc_path, sizeof(ishrc_path), "%s/.ishrc", home);
  
  FILE *file = fopen(ishrc_path, "r");
  if (file == NULL){
    return;
  } else {
    char line[MAX_LINE_SIZE];
    while (fgets(line, sizeof(line), file) != NULL) {
      printf("%% %s", line);
      shellHelper(line);
    }
    fclose(file); 
  }
}

int main(int argc, char *argv[]) {
  /* TODO */
  /*
  SETUP: setting the global variable 'program_name'
  to the name of the binary file, for future reference
  when printing out error messages.
  */
  program_name = argv[0];
  errorPrint(argv[0], SETUP);

  // Read the .ishrc file in HOME upon launch.
  read_ishrc();

  // Setup for signal handling: 
  // SIGINT, SIGQUIT, and SIGALRM must be unblocked.
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT); sigaddset(&set, SIGQUIT); sigaddset(&set, SIGALRM); 
  sigprocmask(SIG_UNBLOCK, &set, NULL);
  signal_handler_for_parent();

  // Receiving and handling user input indefinitely,
  // until SIGQUIT or SIGINT is received.
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


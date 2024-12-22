#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/


int pipe_findindex(DynArray_T oToken);

void run_child(int pipeFile[2], DynArray_T oToken, int pipeIndex);
void run_parent(int pipeFile[2], DynArray_T oToken, int pipeIndex);


//handler for ctrl- signal(Exit)
static void exit_handler(int sig) {
  exit(EXIT_SUCCESS);
}


//handler for Ctrl-\ signal with delay(Quit)
static void quit_handler(int sig) {
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);

    assert(signal(SIGQUIT, exit_handler) != SIG_ERR);
    alarm(5);
}


//alarm handler, SIGALRM
static void alarm_handler(int sig) {
    assert(signal(SIGQUIT, quit_handler) != SIG_ERR);
}


//setup for handling signals
void signal_handling_setup() {
    sigset_t sigSET;

    sigemptyset(&sigSET);
    sigaddset(&sigSET, SIGINT);
    sigaddset(&sigSET, SIGQUIT);
    sigaddset(&sigSET, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &sigSET, NULL);

    assert(signal(SIGINT, SIG_IGN) != SIG_ERR);
    assert(signal(SIGQUIT, quit_handler) != SIG_ERR);
    assert(signal(SIGALRM, alarm_handler) != SIG_ERR);
}


// use execvp to execute command
void execute_command(char **args) {
    execvp(args[0], args);
    errorPrint(args[0], PERROR);

    exit(EXIT_FAILURE);
}


// Setting up pipe
void pipe_setup(int pipeFile[2]) {
    if (pipe(pipeFile) == -1) {
        errorPrint(NULL, PERROR);

        exit(EXIT_FAILURE);
    }
}


// Handling of piped commands executions
void pipe_execute(DynArray_T oToken) {
    int pipeFile[2];
    int totalPipes = countPipe(oToken);
    int pipeIndex = pipe_findindex(oToken);

    if (totalPipes == 0) {
        signal_handling_setup();

        char *args[DynArray_getLength(oToken) + 1];
        DynArray_tocharArray(oToken, args, NULL);

        execute_command(args);
        return;
    }


    pipe_setup(pipeFile);
    pid_t child_PID = fork();

    if (child_PID < 0) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
    }
    else if (child_PID == 0) {
        run_child(pipeFile, oToken, pipeIndex);
    } 
    else {
        run_parent(pipeFile, oToken, pipeIndex);
    }
}


//find index of pipe token
int pipe_findindex(DynArray_T oToken) {
  int i;
  for (i = 0; i < DynArray_getLength(oToken); i++) {
    struct Token *tok = DynArray_get(oToken, i);
    if (tok->eType == TOKEN_PIPE) return i;
  }
    return -1;
}


//handle child processes
void run_child(int pipeFile[2], DynArray_T oToken, int pipeIndex) {
    close(pipeFile[0]);

    if (dup2(pipeFile[1], STDOUT_FILENO) == -1) assert(0);
    
    char *leftCommand[pipeIndex + 1];

    DynArray_tocharArray(oToken, leftCommand, NULL);
    execute_command(leftCommand);
}


//handle parent processes
void run_parent(int pipeFile[2], DynArray_T oToken, int pipeIndex) {
    int PROCESS_STATUS;
    wait(&PROCESS_STATUS);

    close(pipeFile[1]);

    if (dup2(pipeFile[0], STDIN_FILENO) == -1) assert(0);
    
    DynArray_rmvElements(oToken, 0, pipeIndex + 1);
    pipe_execute(oToken);
}


//execute a built in command
void execute_builtin(enum BuiltinType builtType, DynArray_T oTokens) {
    if (builtType == B_CD)  execute_CD(oTokens);
    else if (builtType == B_EXIT) execute_EXIT(oTokens);
    else if (builtType == B_SETENV) execute_SETENV(oTokens);
    else if (builtType == B_USETENV)  execute_UNSETENV(oTokens);
    else  assert(0 && "Unreachable");
}


//handle execution of command
void handle_command(DynArray_T oTokens) {
  int status;
  fflush(NULL);

  pid_t pid = fork();

  if (pid < 0) {
    errorPrint(NULL, PERROR);
    exit(EXIT_FAILURE);
  }

  if (pid == 0) {
    sigset_t sSet;
    sigemptyset(&sSet);
    sigaddset(&sSet, SIGINT);
    sigprocmask(SIG_BLOCK, &sSet, NULL);

    assert(signal(SIGQUIT, exit_handler) != SIG_ERR);
    assert(signal(SIGINT, SIG_DFL) != SIG_ERR);

    char *args[DynArray_getLength(oTokens) + 1];
    char *redirect[2] = {NULL};
        
    DynArray_tocharArray(oTokens, args, redirect);

    if (redirect[0]) {
      int fd = open(redirect[0], O_RDONLY);

      if (fd == -1) errorPrint(NULL, PERROR);
      if (dup2(fd, 0) == -1) assert(0);

      close(fd);
    }

    if (redirect[1]) {
      int fd = creat(redirect[1], 0600);

      if (fd == -1) errorPrint(NULL, PERROR);
      if (dup2(fd, 1) == -1) assert(0);

      close(fd);
    }

    if (countPipe(oTokens) != 0)  pipe_execute(oTokens);
    else {
      sigprocmask(SIG_UNBLOCK, &sSet, NULL);
      execute_command(args);
    }

    DynArray_free(oTokens);
    exit(EXIT_FAILURE);
  }

  pid = wait(&status);
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
      if (DynArray_getLength(oTokens) == 0) return;
      
      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        if (btype != NORMAL)  execute_builtin(btype, oTokens);
        else  handle_command(oTokens);
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

int main(int argc, char *argv[]) {
  /* TODO */

  signal_handling_setup();

  const char *working_dir = getenv("PWD");
  const char *home_dir = getenv("HOME");


  chdir(home_dir);
  FILE *ishrc = fopen(".ishrc", "r");

  char acLine[MAX_LINE_SIZE + 2];

  errorPrint(argv[0],SETUP);

  if (ishrc != NULL) {
    while (1) {
      fflush(stdout);

      if (fgets(acLine, MAX_LINE_SIZE, ishrc) == NULL) break;

      int command_len = strlen(acLine);

      if (acLine[command_len - 1] != '\n') {
        acLine[command_len] = '\n';
        acLine[command_len + 1] = '\0';
      }

      fprintf(stdout, "%% %s", acLine);
      shellHelper(acLine);
      }

      fclose(ishrc);
    }
  chdir(working_dir);


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


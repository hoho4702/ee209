#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include "lexsyn.h"
#include "util.h"

volatile sig_atomic_t i = 1;

/*--------------------------------------------------------------------*/

void
handler_quit(int sig)
{
  (void) sig;
  void (*pfRet)(int);
  if (i == 1){
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    alarm(5);
    i = 0;
  }
  else{
    exit(EXIT_SUCCESS);
  }
  pfRet = signal(SIGQUIT, handler_quit);
  assert(pfRet != SIG_ERR);
}

/*--------------------------------------------------------------------*/

void
handler_alrm(int sig)
{
  (void) sig;
  void (*pfRet)(int);
  i = 1;
  pfRet = signal(SIGALRM, handler_alrm);
  assert(pfRet != SIG_ERR);
}

/*--------------------------------------------------------------------*/

static void
executePipelineWithRedirection(DynArray_T oTokens, int pipeIdx, int rediIdx, int redoIdx)
{
  int pipeCount = countPipe(oTokens);
  int cmdCount = pipeCount + 1;
  int pipe_fds[pipeCount][2];
  pid_t pids[cmdCount];
  int input_fd = -1, output_fd = -1;

  struct Token *token2 = DynArray_get(oTokens, 0);

  if(strcmp(token2->pcValue, "cd") == 0){
    errorPrint("cd takes one parameter", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "setenv") == 0){
    errorPrint("setenv takes one or two parameters", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "unsetenv") == 0){
    errorPrint("unsetenv takes one parameter", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "exit") == 0){
    errorPrint("exit does not take any parameters", FPRINTF);
    return;
  }

  // Handle redirection
  if (rediIdx != -1) {
    struct Token *token = DynArray_get(oTokens, rediIdx + 1);
    input_fd = open(token->pcValue, O_RDONLY);
    if (input_fd == -1) {
      errorPrint("No such file or directory", FPRINTF);
      return;
    }
  }

  if (redoIdx != -1) {
    struct Token *token = DynArray_get(oTokens, redoIdx + 1);
    output_fd = creat(token->pcValue, 0600);
    if (output_fd == -1) {
      perror("Output redirection failed");
      exit(EXIT_FAILURE);
    }
  }

  // Set up pipes
  for (int i = 0; i < pipeCount; i++) {
    if (pipe(pipe_fds[i]) == -1) {
      perror("pipe error");
      exit(EXIT_FAILURE);
    }
  }

  int startIdx = 0;
  for (int i = 0; i < cmdCount; i++) {
    int endIdx = startIdx;

    // Find the end of the current command
    while (endIdx < DynArray_getLength(oTokens)) {
      struct Token *token = DynArray_get(oTokens, endIdx);
      if (token->eType == TOKEN_PIPE) break;
      endIdx++;
    }

    // Fork process for current command
    pids[i] = fork();
    if (pids[i] < 0) {
      perror("fork error");
      exit(EXIT_FAILURE);
    }

    if (pids[i] == 0) {
      // Child process
      if (i == 0 && input_fd != -1) {
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);
      }
      if (i == cmdCount - 1 && output_fd != -1) {
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
      }
      if (i > 0) dup2(pipe_fds[i - 1][0], STDIN_FILENO);
      if (i < pipeCount) dup2(pipe_fds[i][1], STDOUT_FILENO);

      for (int j = 0; j < pipeCount; j++) {
        close(pipe_fds[j][0]);
        close(pipe_fds[j][1]);
      }

      // Prepare arguments and execute
      int cmdLength = endIdx - startIdx;
      char *argv[cmdLength + 1];
      for (int j = 0; j < cmdLength; j++) {
        struct Token *token = DynArray_get(oTokens, startIdx + j);
        argv[j] = token->pcValue;
      }
      argv[cmdLength] = NULL;

      execvp(argv[0], argv);
      errorPrint(argv[0], SETUP);
      errorPrint("No such file or directory", FPRINTF);
      exit(EXIT_FAILURE);
    }

    // Move to the next command
    startIdx = endIdx + 1;
  }

  // Close parent pipes and wait
  for (int i = 0; i < pipeCount; i++) {
    close(pipe_fds[i][0]);
    close(pipe_fds[i][1]);
  }

  if (input_fd != -1) close(input_fd);
  if (output_fd != -1) close(output_fd);

  for (int i = 0; i < cmdCount; i++) {
    waitpid(pids[i], NULL, 0);
  }
}

/*--------------------------------------------------------------------*/

static void
executeRedout(DynArray_T oTokens, int redoIdx, int redonum)
{
  int fd;
  struct Token *token = DynArray_get(oTokens, redoIdx+1);

  struct Token *token2 = DynArray_get(oTokens, 0);

  if(strcmp(token2->pcValue, "cd") == 0){
    errorPrint("cd takes one parameter", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "setenv") == 0){
    errorPrint("setenv takes one or two parameters", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "unsetenv") == 0){
    errorPrint("unsetenv takes one parameter", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "exit") == 0){
    errorPrint("exit does not take any parameters", FPRINTF);
    return;
  }

  fd = creat(token->pcValue, 0600);

  if (redonum >= 2){
    perror("num error");
    exit(EXIT_FAILURE);
  }
  pid_t pid = fork();
  if (pid < 0){
    perror("fork error");
    exit(EXIT_FAILURE);
  }
  if (pid == 0){
    close(1);
    dup(fd);
    close(fd);

    char *argv[redoIdx + 1];
    for (int i = 0; i < redoIdx; i++) {
      struct Token *token = DynArray_get(oTokens, i);
      argv[i] = token->pcValue;
    }
    argv[redoIdx] = NULL;

    execvp(argv[0], argv);
    errorPrint(argv[0], SETUP);
    errorPrint("No such file or directory", FPRINTF);
    exit(EXIT_FAILURE);
  }
  close(fd);
  waitpid(pid, NULL, 0);
}

/*--------------------------------------------------------------------*/

static void
executeRedin(DynArray_T oTokens, int rediIdx, int redinum)
{
  int fd;
  struct Token *token = DynArray_get(oTokens, rediIdx+1);

  struct Token *token2 = DynArray_get(oTokens, 0);

  if(strcmp(token2->pcValue, "cd") == 0){
    errorPrint("cd takes one parameter", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "setenv") == 0){
    errorPrint("setenv takes one or two parameters", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "unsetenv") == 0){
    errorPrint("unsetenv takes one parameter", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "exit") == 0){
    errorPrint("exit does not take any parameters", FPRINTF);
    return;
  }

  fd = open(token->pcValue, 0600);

  if (redinum >= 2) {
    perror("num error");
    exit(EXIT_FAILURE);
  }
  
  if (fd == -1) {
    errorPrint("No such file or directory", FPRINTF);
    return;
  }
  
  pid_t pid = fork();
  if (pid < 0){
    perror("fork error");
    exit(EXIT_FAILURE);
  }
  if (pid == 0){
    close(0);
    dup(fd);
    close(fd);

    char *argv[rediIdx + 1];
    for (int i = 0; i < rediIdx; i++) {
      struct Token *token = DynArray_get(oTokens, i);
      argv[i] = token->pcValue;
    }
    argv[rediIdx] = NULL;

    execvp(argv[0], argv);
    errorPrint(argv[0], SETUP);
    errorPrint("No such file or directory", FPRINTF);
    exit(EXIT_FAILURE);
  }
  close(fd);
  waitpid(pid, NULL, 0);
}

/*--------------------------------------------------------------------*/

static void
executePipeline(DynArray_T oTokens, int pipeIdx)
{
  int pipeCount = countPipe(oTokens);
  int cmdCount = pipeCount + 1;
  int pipe_fds[pipeCount][2];
  pid_t pids[cmdCount];

    struct Token *token2 = DynArray_get(oTokens, 0);

  if(strcmp(token2->pcValue, "cd") == 0){
    errorPrint("cd takes one parameter", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "setenv") == 0){
    errorPrint("setenv takes one or two parameters", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "unsetenv") == 0){
    errorPrint("unsetenv takes one parameter", FPRINTF);
    return;
  }
  else if(strcmp(token2->pcValue, "exit") == 0){
    errorPrint("exit does not take any parameters", FPRINTF);
    return;
  }

  for (int i = 0; i < pipeCount; i++) {
      if (pipe(pipe_fds[i]) == -1) {
          perror("pipe error");
          exit(EXIT_FAILURE);
      }
  }

  int startIdx = 0;
  for (int i = 0; i < cmdCount; i++) {
    int endIdx = startIdx;

    // Find the end of the current command
    while (endIdx < DynArray_getLength(oTokens)) {
      struct Token *token = DynArray_get(oTokens, endIdx);
      if (token->eType == TOKEN_PIPE) {
        break;
      }
      endIdx++;
    }

    // Fork the process for the current command
    pids[i] = fork();
    if (pids[i] < 0) {
      perror("fork error");
      exit(EXIT_FAILURE);
    }

    if (pids[i] == 0) { // Child process
      // Handle input redirection from previous pipe if not the first command
      if (i > 0) {
        dup2(pipe_fds[i - 1][0], STDIN_FILENO);
      }

      // Handle output redirection to next pipe if not the last command
      if (i < pipeCount) {
        dup2(pipe_fds[i][1], STDOUT_FILENO);
      }

      // Close all pipe ends in the child process
      for (int j = 0; j < pipeCount; j++) {
        close(pipe_fds[j][0]);
        close(pipe_fds[j][1]);
      }

      // Prepare the argument list for execvp
      int cmdLength = endIdx - startIdx;
      char *argv[cmdLength + 1];
      for (int j = 0; j < cmdLength; j++) {
        struct Token *token = DynArray_get(oTokens, startIdx + j);
        argv[j] = token->pcValue;
      }
      argv[cmdLength] = NULL;

      execvp(argv[0], argv);
      errorPrint(argv[0], SETUP);
      errorPrint("No such file or directory", FPRINTF);
      exit(EXIT_FAILURE);
    }

    // Move to the next command
    startIdx = endIdx + 1;
  }

  // Close all pipe ends in the parent process
  for (int i = 0; i < pipeCount; i++) {
    close(pipe_fds[i][0]);
    close(pipe_fds[i][1]);
  }

  // Wait for all child processes to finish
  for (int i = 0; i < cmdCount; i++) {
    waitpid(pids[i], NULL, 0);
  }
}

/*--------------------------------------------------------------------*/

static void
shellHelper(const char *inLine)
{
  DynArray_T oTokens;
  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  char *home = getenv("HOME");
  char *modifiableLine = strdup(inLine);
  if (modifiableLine == NULL) {
    perror("strdup error");
    exit(EXIT_FAILURE);
  }

  // Remove trailing newline
  if (modifiableLine[strlen(modifiableLine) - 1] == '\n') {
    modifiableLine[strlen(modifiableLine) - 1] = '\0';
  }

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    free(modifiableLine);
    exit(EXIT_FAILURE);
  }

  lexcheck = lexLine(modifiableLine, oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0) {
        free(modifiableLine);
        return;
      }

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
          int pipeIdx = -1;
          int redoIdx = -1;
          int rediIdx = -1;
          int redonum = 0;
          int redinum = 0;

          for (int i = 0; i < DynArray_getLength(oTokens); i++) {
            struct Token *token = DynArray_get(oTokens, i);
            if (token->eType == TOKEN_PIPE) pipeIdx = i;
            else if (token->eType == TOKEN_REDOUT) { redoIdx = i; redonum++; }
            else if (token->eType == TOKEN_REDIN) { rediIdx = i; redinum++; }
          }

          if (pipeIdx != -1 && (rediIdx != -1 || redoIdx != -1)) {
            executePipelineWithRedirection(oTokens, pipeIdx, rediIdx, redoIdx);
          } else if (pipeIdx != -1) {
            executePipeline(oTokens, pipeIdx);
          } else if (redoIdx != -1) {
            executeRedout(oTokens, redoIdx, redonum);
          } else if (rediIdx != -1) {
            executeRedin(oTokens, rediIdx, redinum);
          } else {
            btype = checkBuiltin(DynArray_get(oTokens, 0));
            int length = DynArray_getLength(oTokens);
            char *argv[length + 1];

            for (int i = 0; i < length; i++) {
              struct Token *token = DynArray_get(oTokens, i);
              // if (token->pcValue[0] == '$') {
              //   char *envValue = getenv(&token->pcValue[1]);
              //   if (envValue) { argv[i] = strdup(envValue); }
              //   else { argv[i] = strdup(""); }
              // } else {
              //   argv[i] = token->pcValue;
              // }
              argv[i] = token->pcValue;
            }

            argv[length] = NULL;

            switch (btype) {
              case B_EXIT:
                if (argv[1] != NULL){
                  errorPrint("exit does not take any parameters", FPRINTF);
                  return;
                }
                free(modifiableLine);
                exit(EXIT_SUCCESS);
                break;

              case B_SETENV:
                if (argv[3] != NULL){
                  errorPrint("setenv takes one or two parameters", FPRINTF);
                  return;
                }
                else if (setenv(argv[1], argv[2], 1) == -1) {
                  fprintf(stderr, "setenv error\n");
                  free(modifiableLine);
                  exit(EXIT_FAILURE);
                }
                break;

              case B_USETENV:
                if (argv[2] != NULL){
                  errorPrint("unsetenv takes one parameter", FPRINTF);
                  return;
                }
                else if (unsetenv(argv[1]) == -1) {
                  fprintf(stderr, "unsetenv error\n");
                  free(modifiableLine);
                  exit(EXIT_FAILURE);
                }
                break;

              case B_CD:
                if (argv[1] == NULL)
                  chdir(home);
                else if (argv[2] != NULL){
                  errorPrint("cd takes one parameter", FPRINTF);
                  return;
                }
                else if (chdir(argv[1]) == -1) {
                  errorPrint("No such file or directory", FPRINTF);
                  free(modifiableLine);
                  return;
                }
                break;

              default: {
                pid_t pid = fork();
                if (pid < 0) {
                  fprintf(stderr, "fork error\n");
                } else if (pid == 0) {
                  execvp(argv[0], argv);
                  errorPrint(argv[0], SETUP);
                  errorPrint("No such file or directory", FPRINTF);
                  free(modifiableLine);
                  exit(EXIT_FAILURE);
                } else {
                  int status;
                  waitpid(pid, &status, 0);
                }
              }
              break;
            }
          }
        } else if (syncheck == SYN_FAIL_NOCMD) {
          errorPrint("Missing command name", FPRINTF);
        } else if (syncheck == SYN_FAIL_MULTREDOUT) {
          errorPrint("Multiple redirection of standard out", FPRINTF);
        } else if (syncheck == SYN_FAIL_NODESTOUT) {
          errorPrint("Standard output redirection without file name", FPRINTF);
        } else if (syncheck == SYN_FAIL_MULTREDIN) {
          errorPrint("Multiple redirection of standard input", FPRINTF);
        } else if (syncheck == SYN_FAIL_NODESTIN) {
          errorPrint("Standard input redirection without file name", FPRINTF);
        } else if (syncheck == SYN_FAIL_INVALIDBG) {
          errorPrint("Invalid use of background", FPRINTF);
        }
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

  free(modifiableLine);
  DynArray_free(oTokens);
}

/*--------------------------------------------------------------------*/

int
main()
{
  char acLine[MAX_LINE_SIZE + 2];
  char *home = getenv("HOME");
  char ishrcPath[1024];
  int k = 1;
  void (*pfRet)(int);
  sigset_t sSet;

  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  pfRet = signal(SIGQUIT, handler_quit);
  assert(pfRet != SIG_ERR);
  signal(SIGALRM, handler_alrm);
  assert(pfRet != SIG_ERR);
  pfRet = signal(SIGINT, SIG_IGN);
  assert(pfRet != SIG_ERR);

  if (home) {
    snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", home);
    FILE *fp = fopen(ishrcPath, "r");
    if (fp) {
      while (fgets(acLine, sizeof(acLine), fp)) {
        printf("%% %s", acLine);
        shellHelper(acLine);
      }
      fclose(fp);
    }
  }

  while (1) {
    if(k == 1){
      fprintf(stdout, "%% ");
      fflush(stdout);
    }
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      if (feof(stdin)){
        printf("\n");
        exit(EXIT_SUCCESS);
      }
      clearerr(stdin);
      k=0;
      continue;
    }
    shellHelper(acLine);
    k = 1;
  }
}

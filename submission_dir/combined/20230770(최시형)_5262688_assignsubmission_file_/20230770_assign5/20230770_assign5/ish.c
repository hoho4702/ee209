#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <errno.h>

#include "lexsyn.h"
#include "util.h"
#include "dynarray.h"

typedef char** StrArray;

static void freeArray(StrArray *arr, int x) {
  if (arr == NULL) return;
  int i;
  for (i = 0; i < x; i++){
    free(arr[i]);
  }
  free(arr);
} 

static void freeDynArray(DynArray_T oTokens) {
  if (oTokens == NULL) return;
  int tokenNum = DynArray_getLength(oTokens);
  int i;
  for (i = 0; i < tokenNum; i++) {
    struct Token *t = DynArray_get(oTokens, i);
    if (t != NULL) freeToken(t, NULL);
  }
  DynArray_free(oTokens);
}


static int oneredirection(struct Token *token, DynArray_T oTokens, int *i, int flag, int fd) {
  struct Token *fileToken = DynArray_get(oTokens, *i + 1);
  if (fileToken == NULL || fileToken->eType != TOKEN_WORD) {
    errorPrint("No such file or directory", FPRINTF);
    return EXIT_FAILURE;
  }

  int file = open(fileToken->pcValue, flag, 0600);
  if (file == -1){
    errorPrint("No such file or directory", FPRINTF);
    return EXIT_FAILURE;
  }

  if (dup2(file, fd) == -1) {
    close(file);
    errorPrint("Error in duplicating file descriptor", FPRINTF);
    return EXIT_FAILURE;
  }

  close(file);
  *i = *i + 1;
  return EXIT_SUCCESS;
}

static int bothredirection(DynArray_T oTokens, int rindex, int windex) {
  struct Token *input = DynArray_get(oTokens, rindex + 1);
  struct Token *output = DynArray_get(oTokens, windex + 1);

  if (input == NULL || input->eType != TOKEN_WORD || output == NULL || output->eType != TOKEN_WORD) {
    errorPrint("No such file or directory", FPRINTF);
    return EXIT_FAILURE;
  }

  int ifile = open(input->pcValue, O_RDONLY, 0600);
  if (ifile == -1){
    errorPrint("No Such file or directory", FPRINTF);
    return EXIT_FAILURE;
  }

  int ofile = open(output->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (ofile == -1){
    close(ifile);
    errorPrint("No Such file or directory", FPRINTF);
    return EXIT_FAILURE;
  }

  close(ifile);
  close(ofile);
  return EXIT_SUCCESS;
}

static void execDef(DynArray_T oTokens, StrArray *argv_arr, int infd, int outfd) {
  pid_t pid = fork();
  if (pid == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGALRM, SIG_DFL);
    if (infd != 0) {
      dup2(infd, 0);
      close(infd);
    }
    if (outfd != 1) {
      dup2(outfd, 1);
      close(outfd);
    }
    execvp(argv_arr[0][0], argv_arr[0]);
    errorPrint(argv_arr[0][0], PERROR);
    freeArray(argv_arr, 1);
    freeDynArray(oTokens);
    exit(EXIT_FAILURE);
  }
  else {
    wait(NULL);
  }
}

static void RunForPipe(DynArray_T oTokens, StrArray *argv_arr, int pipeNum) {
  int fd[2], ifd = 0, ofd = 1;
  int i;
  for (i = 0; i < pipeNum + 1; i++) {
    pipe(fd);
    if (i == pipeNum) ofd = 1;
    else ofd = fd[1];
    execDef(oTokens, &argv_arr[i], ifd, ofd);
    if (i < pipeNum) {
      close(fd[1]);
    }
    ifd = fd[0];
  }
}

static void excExit(DynArray_T oTokens, int tokenNum) {
  if (tokenNum == 1) {
    freeDynArray(oTokens);
    exit(EXIT_SUCCESS);
  } 
  else {
    freeDynArray(oTokens);
    errorPrint("exit does not take any parameters", FPRINTF);
  }
}

static void excSetenv(DynArray_T oTokens, int tokenNum) {
  char *name, *value = '\0';
  if (tokenNum == 2) {
    struct Token *t = DynArray_get(oTokens, 1);
    if (t->eType != TOKEN_WORD) {
      freeDynArray(oTokens);
      errorPrint("setenv takes one or two parameters", FPRINTF);
      return;
    }
    name = t->pcValue;
    if (setenv(name, value, 1) == -1) {
      freeDynArray(oTokens);
      errorPrint("Cannot set environment variable", FPRINTF);
    }
    else freeDynArray(oTokens);
  }
  else if (tokenNum == 3) {
    struct Token *t1 = DynArray_get(oTokens, 1);
    struct Token *t2 = DynArray_get(oTokens, 2);
    if (t1->eType != TOKEN_WORD || t2->eType != TOKEN_WORD) {
      freeDynArray(oTokens);
      errorPrint("setenv takes one or two parameters", FPRINTF);
      return;
    }
    name = t1->pcValue;
    value = t2->pcValue;
    if (setenv(name, value, 1) == -1) {
      freeDynArray(oTokens);
      errorPrint("Cannot set environment variable", FPRINTF);
    }
    else freeDynArray(oTokens);
  }
  else {
    freeDynArray(oTokens);
    errorPrint("setenv takes one or two parameters", FPRINTF);
  }
}

static void excUnsetenv(DynArray_T oTokens, int tokenNum) {
  if (tokenNum == 2) {
    struct Token *t = DynArray_get(oTokens, 1);
    if (t->eType != TOKEN_WORD) {
      freeDynArray(oTokens);
      errorPrint("unsetenv takes one parameter", FPRINTF);
    }
    char *name = t->pcValue;

    if (unsetenv(name) == -1) {
      freeDynArray(oTokens);
      errorPrint("Cannot unset environmental variable", FPRINTF);
    }
    else freeDynArray(oTokens);
  }
  else {
    freeDynArray(oTokens);
    errorPrint("unsetenv takes one parameter", FPRINTF);
  }
}

static void excCd(DynArray_T oTokens, int tokenNum) {
  if (tokenNum == 1) {
    if (chdir(getenv("HOME")) == -1) {
      freeDynArray(oTokens);
      errorPrint("Cannot change directory", FPRINTF);
    }
    else {
      freeDynArray(oTokens);
    }
  }
  else if (tokenNum == 2) {
    struct Token *t = DynArray_get(oTokens, 1);
    if (t->eType != TOKEN_WORD) {
      freeDynArray(oTokens);
      errorPrint("cd takes one parameter", FPRINTF);
      return;
    }
    else {
      char *path = t->pcValue;

      if (chdir(path) == -1) {
        if (errno == ENOTDIR) {
          errorPrint("Not a directory", FPRINTF);
        }
        else if (errno == ENOENT) {
          errorPrint("No such file or directory", FPRINTF);
        }
        else {
          errorPrint(strerror(errno), FPRINTF);
        }
      }
      freeDynArray(oTokens);
    }
  }
  else {
    freeDynArray(oTokens);
    errorPrint("cd takes one parameter", FPRINTF);
  }
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
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        
        int pipeNum = countPipe(oTokens);
        int tokenNum = DynArray_getLength(oTokens);

        int prev_stdin = dup(0);
        int prev_stdout = dup(1);

        if (btype == NORMAL) {

          StrArray *argv_arr = (StrArray *)calloc(pipeNum + 1, sizeof(StrArray));
          if (argv_arr == NULL) {
            errorPrint("Cannot allocate memory", FPRINTF);
            exit(EXIT_FAILURE);
          }
          int i;
          for (i = 0; i < pipeNum + 1; i++) {
            argv_arr[i] = (StrArray)calloc(tokenNum + 1, sizeof(char*));
            // Memory Allocation Failed
            if (argv_arr[i] == NULL) {
              freeArray(argv_arr, i);
              freeDynArray(oTokens);
              errorPrint("Cannot allocate memory", FPRINTF);
              exit(EXIT_FAILURE);
            }
          }

          int *argIndex = (int *)calloc(pipeNum + 1, sizeof(int));

          int rdinindex = -1, rdoutindex = -1;
          for (i = 0; i < tokenNum; i++) {
            struct Token *t = DynArray_get(oTokens, i);
            if (t->eType == TOKEN_REDIN) {
              rdinindex = i;
            }
            else if (t->eType == TOKEN_REDOUT) {
              rdoutindex = i;
            }
          }

          if (rdinindex != -1 && rdoutindex != -1) {
            if (bothredirection(oTokens, rdinindex, rdoutindex) == EXIT_FAILURE) {
              free(argIndex);
              return;
            }
          }
          else {
            int i, pipeIndex=0;
            for (i = 0; i < tokenNum; i++) {
              struct Token *t = DynArray_get(oTokens, i);
              if (t->eType == TOKEN_PIPE) {
                pipeIndex++;
              }
              else if (t->eType == TOKEN_REDIN) {
                if (oneredirection(t, oTokens, &i, O_RDONLY, 0) == EXIT_FAILURE) {
                  free(argIndex);
                  return;
                }
              }
              else if (t->eType == TOKEN_REDOUT) {
                if (oneredirection(t, oTokens, &i, O_WRONLY | O_CREAT | O_TRUNC, 1) == EXIT_FAILURE) {
                  free(argIndex);
                  return;
                }
              }
              else if (t->eType == TOKEN_WORD) {
                argv_arr[pipeIndex][argIndex[pipeIndex]] = t->pcValue;
                argIndex[pipeIndex]++;
              }
            }
          }
          if (pipeNum > 0) {
            RunForPipe(oTokens, argv_arr, pipeNum);
          }
          else {
            execDef(oTokens, argv_arr, 0, 1);
          }
          dup2(prev_stdin, 0);
          dup2(prev_stdout, 1);
          close(prev_stdin);
          close(prev_stdout);
          freeArray(argv_arr, pipeNum + 1);
          freeDynArray(oTokens);
        }
        else if (btype == B_SETENV) {
          excSetenv(oTokens, tokenNum);
        }
        else if (btype == B_USETENV) {
          excUnsetenv(oTokens, tokenNum);
        }
        else if (btype == B_EXIT) {
          excExit(oTokens, tokenNum);
        }
        else if (btype == B_CD) {
          excCd(oTokens, tokenNum);
        }
        else {
          freeDynArray(oTokens);
          errorPrint("Unknown built-in command", FPRINTF);
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

static int quit_flag = 0;

static void sigHandler(int iSig){
  if (quit_flag == 1) {
    exit(EXIT_SUCCESS);
  }
  printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  quit_flag = 1;
  alarm(5);
}

static void alrmHandler(int iSig){
  quit_flag = 0;
}

int main(int argc, char* argv[]) {

  errorPrint(argv[0], SETUP);
  
  char *dir_home = getenv("HOME");
  char dir_ishrc[PATH_MAX];
  char dir_work[PATH_MAX];
  getcwd(dir_work, PATH_MAX);

  //SIGNAL HANDLING
  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);

  sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, sigHandler);
  signal(SIGALRM, alrmHandler);

  // Read from .ishrc
  chdir(dir_home);

  strcpy(dir_ishrc, dir_home);
  strcat(dir_ishrc, "/.ishrc");

  FILE *fp = fopen(dir_ishrc, "r");
  if (fp != NULL) {
    char buffer[PATH_MAX + 2];
    while (fgets(buffer, sizeof(buffer), fp)) {
      fprintf(stdout, "%% ");
      fputs(buffer, stdout);
      if (buffer[strlen(buffer) - 1] != '\n') { 
        putchar('\n');
      }
      fflush(stdout);
      shellHelper(buffer);
    }
    fclose(fp);
  }

  chdir(dir_work);

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


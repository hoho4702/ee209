#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "lexsyn.h"
#include "util.h"

#define MAX_LINE_SIZE 1024

static void handleExternalCommand(DynArray_T oTokens) {
  pid_t pid;
  int status;

  pid = fork();
  if (pid == 0) {
    char *cmd = DynArray_get(oTokens, 0); 
    char **args = malloc(sizeof(char*) * (DynArray_getLength(oTokens) + 1));

    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
      args[i] = DynArray_get(oTokens, i);
    }
    args[DynArray_getLength(oTokens)] = NULL; 

    if (execvp(cmd, args) == -1) {
      perror("Execution failed");
      free(args);
      exit(EXIT_FAILURE);
    }
  } else if (pid < 0) {
    perror("Fork failed");
  } else {
    waitpid(pid, &status, 0);
  }
}

static void shellHelper(const char *inLine) {
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

      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));

        switch (btype) {
          case B_EXIT:
            exit(EXIT_SUCCESS);
            break;
          case B_SETENV:
            setenv(DynArray_get(oTokens, 1), DynArray_get(oTokens, 2), 1);
            break;
          case B_USETENV:
            unsetenv(DynArray_get(oTokens, 1));
            break;
          case B_CD:
            if (DynArray_getLength(oTokens) > 1)
              chdir(DynArray_get(oTokens, 1));
            else
              chdir(getenv("HOME"));
            break;
          default:
            handleExternalCommand(oTokens);
            break;
        }
      } else {
        switch (syncheck) {
          case SYN_FAIL_NOCMD:
            errorPrint("Missing command name", FPRINTF);
            break;
          case SYN_FAIL_MULTREDOUT:
            errorPrint("Multiple redirection of standard out", FPRINTF);
            break;
          case SYN_FAIL_NODESTOUT:
            errorPrint("Standard output redirection without file name", FPRINTF);
            break;
          case SYN_FAIL_MULTREDIN:
            errorPrint("Multiple redirection of standard input", FPRINTF);
            break;
          case SYN_FAIL_NODESTIN:
            errorPrint("Standard input redirection without file name", FPRINTF);
            break;
          case SYN_FAIL_INVALIDBG:
            errorPrint("Invalid use of background", FPRINTF);
            break;
        }
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
}

int main() {
  char acLine[MAX_LINE_SIZE + 2];

  char *home = getenv("HOME");
  char ishrc_path[1024];
  snprintf(ishrc_path, sizeof(ishrc_path), "%s/.ishrc", home);
  FILE *rc_file = fopen(ishrc_path, "r");
  if (rc_file) {
    char rc_line[MAX_LINE_SIZE];
    while (fgets(rc_line, sizeof(rc_line), rc_file)) {
      printf("%% %s", rc_line);
      shellHelper(rc_line);
    }
    fclose(rc_file);
  }

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









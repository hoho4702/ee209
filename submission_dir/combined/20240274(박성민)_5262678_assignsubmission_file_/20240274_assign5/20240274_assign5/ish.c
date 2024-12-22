#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <assert.h>

#include <string.h>
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
static void EXIT_handler(int iSig) {
  exit(EXIT_SUCCESS);
}

static void QUIT_handler(int iSig) {
  printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  fflush(stdout);
  assert(signal(SIGQUIT, EXIT_handler) != SIG_ERR);
  alarm(5);
}

static void ALRM_handler(int iSig) {
  assert(signal(SIGQUIT, QUIT_handler) != SIG_ERR);
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
        DynArray_free(oTokens);
        return;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        if (btype != NORMAL) {
          runBuiltinCommand(btype, oTokens);
        }
        else {
          int status;
          fflush(NULL);

          pid_t pid = fork();
          if (pid < 0) {
            errorPrint(NULL, PERROR);
            exit(EXIT_FAILURE);
          }
          else if (pid == 0) {
            sigset_t sSet;
            sigemptyset(&sSet);
            sigaddset(&sSet, SIGINT);
            sigprocmask(SIG_BLOCK, &sSet, NULL);

            assert(signal(SIGINT, SIG_DFL) != SIG_ERR);
            assert(signal(SIGQUIT, EXIT_handler) != SIG_ERR);

            char *args[DynArray_getLength(oTokens) + 1];
            char *redirection[2] = { NULL, NULL };
            int fd;

            convertDynArrayToStrings(oTokens, args, redirection);

            if (redirection[0]) {
              fd = open(redirection[0], O_RDONLY);
              if (fd == -1) errorPrint(NULL, PERROR);
              if (dup2(fd, 0) == -1) assert(0);
              close(fd);
            }

            if (redirection[1]) {
              fd = creat(redirection[1], 0600);
              if (fd == -1) errorPrint(NULL, PERROR);
              if (dup2(fd, 1) == -1) assert(0);
              close(fd);
            }

            sigprocmask(SIG_UNBLOCK, &sSet, NULL);
            execvp(args[0], args);

            errorPrint(args[0], PERROR);
            DynArray_free(oTokens);
            exit(EXIT_FAILURE);
          }
          else {
            wait(&status);
          }
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

  {
  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  assert(signal(SIGINT,  SIG_IGN)      != SIG_ERR);
  assert(signal(SIGQUIT, QUIT_handler) != SIG_ERR);
  assert(signal(SIGALRM, ALRM_handler) != SIG_ERR);

  errorPrint("ish", SETUP);

  const char *homeDir    = getenv("HOME");
  const char *workingDir = getenv("PWD");

  if (homeDir) chdir(homeDir);

  FILE *ishrc = fopen(".ishrc", "r");
    if (ishrc) {
      char lineBuf[MAX_LINE_SIZE + 2];
      while (1) {
        fflush(stdout);
        if (fgets(lineBuf, MAX_LINE_SIZE, ishrc) == NULL) break;

        int cmd_len = (int)strlen(lineBuf);
        if (cmd_len > 0 && lineBuf[cmd_len - 1] != '\n') {
          lineBuf[cmd_len]     = '\n';
          lineBuf[cmd_len + 1] = '\0';
        }

        fprintf(stdout, "%% %s", lineBuf);
        shellHelper(lineBuf);
      }
      fclose(ishrc);
    }

    if (workingDir) chdir(workingDir);
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

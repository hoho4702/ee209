/* 20200375 JoonhaYang */
/* ish.c */

#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
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



void parent_sigquit_handler(int sig) {
    static time_t last_time = 0;
    time_t current_time = time(NULL);

    if (last_time != 0 && (current_time - last_time) <= 5) {
        exit(EXIT_SUCCESS);
    } else {
        printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        last_time = current_time;
    }
}


static void
shellHelper(const char *inLine) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  //enum BuiltinType btype;

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
        //btype = checkBuiltin(DynArray_get(oTokens, 0));`
        struct Token *curToken;
        enum TokenType curType;
        char *curValue;
        char *red_in_filename;
        char *red_out_filename;
        int red_in_flag = 0;
        int red_out_flag = 0;
        int i;
        char *argv[1024];
        int argc = 0;

        for (i=0; i<DynArray_getLength(oTokens); i++) {
          curToken = (struct Token *)DynArray_get(oTokens, i);
          curType = curToken->eType;
          curValue = curToken->pcValue;
          switch (curType) {
            case TOKEN_WORD:
              if (red_in_flag < 0) {
                red_in_filename = curValue;
                red_in_flag = 1;
              }
              else if (red_out_flag < 0) {
                red_out_filename = curValue;
                red_out_flag = 1;
              }
              else {
                argv[argc++] = curValue;
              }
              break;
            case TOKEN_REDIN:
              red_in_flag = -1;
              break;
            case TOKEN_REDOUT:
              red_out_flag = -1;
              break;
            case TOKEN_PIPE:
              break;
            case TOKEN_BG:
              break;
          }
        }
        argv[argc] = NULL;

        // check built-in function
        if (!strcmp(argv[0], "setenv")) {
          if (argc == 2) {
            setenv(argv[1], "", 1);
            return;
          }
          else if (argc == 3) {
            setenv(argv[1], argv[2], 1);
            return;
          }
          else {
            errorPrint("setenv takes one or two parameters", FPRINTF);
            return;
          }
        }
        else if (!strcmp(argv[0], "unsetenv")) {
          if (argc == 2) {
            unsetenv(argv[1]);
            return;
          }
          else {
            errorPrint("unsetenv takes one parameter", FPRINTF);
            return;
          }
        }
        else if (!strcmp(argv[0], "cd")) {
          if (argc == 2) {
            chdir(argv[1]);
            return;
          }
          else {
            errorPrint("cd takes one parameter", FPRINTF);
            return;
          }
        }
        else if (!strcmp(argv[0], "exit")) {
          if (argc == 1)
            exit(EXIT_SUCCESS);
          else {
            errorPrint("exit does not take any parameters", FPRINTF);
            return;
          }
        }

        pid_t pid;
        int status;
        if ((pid = fork()) == 0) { // child process

          signal(SIGINT, SIG_DFL);
          signal(SIGQUIT, SIG_DFL);
          if (red_in_flag > 0) {
            int fd = open(red_in_filename, O_RDWR);
            if(fd == -1) {
              perror("Failed to open file");
              exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
          }
          if (red_out_flag > 0) {
            int fd = open(red_out_filename, O_CREAT | O_RDWR | O_TRUNC, 0600);
            if(fd == -1) {
              perror("Failed to open file");
              exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
          }

          execvp(argv[0], argv);
          perror(argv[0]);
          exit(EXIT_FAILURE);
        }
        else if (pid > 0) {
          waitpid(pid, &status, 0);
        }
        else {
          perror("fork failed");
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

int main(int argc, char *argv[]) {
  errorPrint(argv[0], SETUP);  
  char acLine[MAX_LINE_SIZE + 2];

  void (*pfRet)(int);
  pfRet = signal(SIGINT, SIG_IGN);
  assert(pfRet != SIG_ERR);

  void (*pfRet1)(int);
  pfRet1 = signal(SIGQUIT, parent_sigquit_handler);
  assert(pfRet1 != SIG_ERR);

  char ishrcPath[1024];
  FILE *ishrcFile = NULL;
  // open .ishrc file in HOME directory
  const char *home = getenv("HOME");
  if (home != NULL) {
      snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", home);
      ishrcFile = fopen(ishrcPath, "r");
  }
  

  if (ishrcFile != NULL){
    while ((fgets(acLine, MAX_LINE_SIZE, ishrcFile)) != NULL) {
      if (acLine[strlen((const char *)acLine)-1] == '\n')
        acLine[strlen((const char *)acLine)-1] = '\0';
      fprintf(stdout, "%% %s\n", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
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


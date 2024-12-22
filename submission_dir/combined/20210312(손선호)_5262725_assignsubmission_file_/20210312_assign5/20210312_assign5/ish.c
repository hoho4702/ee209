#include <stdio.h>
#include <stdlib.h>

#include "lexsyn.h"
#include "util.h"

#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

void exe_cd(int argc, char **argv) {
  char *path;
  if(argc == 1) {
    path = getenv("HOME");
  } else if (argc == 2) {
    path = argv[1];
  } else {
    errorPrint("cd takes one parameter", FPRINTF);
    return;
  }
  if (chdir(path) != 0)
    errorPrint("No such file or directory", FPRINTF);
  return;
}

void exe_setenv(int argc, char **argv) {
  char *var;
  char *value;
  if(argc == 2) {
    var = argv[1];
    value = "";
  } else if (argc == 3) {
    var = argv[1];
    value = argv[2];
  } else {
    errorPrint("setenv takes one or two parameters", FPRINTF);
    return;
  }
  setenv(var, value, 1);
  return;
}

void exe_unsetenv(int argc, char **argv) {
  char *var;
  if(argc == 2) {
    var = argv[1];
  } else {
    errorPrint("unsetenv takes one parameter", FPRINTF);
    return;
  }
  unsetenv(var);
  return;
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
        /* TODO */
        int argc = DynArray_getLength(oTokens);
	void **tokens = calloc(argc, sizeof(void *));
	char **argv = (char **)calloc(argc+1, sizeof(char *));
	DynArray_toArray(oTokens, tokens);
	for(int i=0;i<argc;i++) {
	  argv[i] = ((struct Token *)tokens[i])->pcValue;
	}
	argv[argc] = NULL;
	switch (btype) {
          case B_SETENV:
            exe_setenv(argc, argv);
            break;
          case B_USETENV:
            exe_unsetenv(argc, argv);
            break;
          case B_CD:
            exe_cd(argc, argv);
            break;
          case B_EXIT:
            exit(0);
          case NORMAL:
          {
            int pipe_len = countPipe(oTokens) + 1;
            // store each command's starting position
            int *cmds_idx = (int *)calloc(pipe_len, sizeof(int));
            cmds_idx[0] = 0;
            int ci = 1;
            for (int i = 1; i < argc; i++) {
              if(((struct Token *)tokens[i])->eType == TOKEN_PIPE) {
                argv[i] = NULL;
                cmds_idx[ci++] = i+1;
              }
            }
            // open pipes
            int pipes[pipe_len-1][2];
            for(int i=0;i<pipe_len-1;i++) {
              if (pipe(pipes[i]) == -1)
                exit(1);
            }
            // execute commands
            for (int i = 0; i < pipe_len; i++) {
              fflush(NULL);
              if(fork() == 0){
                // EXTRA 1: redirect stdout by pipe
                if(i < pipe_len-1) {
                  if(dup2(pipes[i][1], 1) == -1)
                    exit(1);
                } else { // redirect stdout by "<"
                  for(int j=cmds_idx[i]; j < argc; j++) {
                    if(((struct Token *)tokens[j])->eType == TOKEN_REDOUT) {
                      int fd = creat(argv[j+1], 0600);
                      dup2(fd, 1);
                      close(fd);
                      break;
                    } 
                  }
                }
                // EXTRA 1: redirect stdin by pipe
                if(i > 0) {
                  if(dup2(pipes[i-1][0], 0) == -1)
                    exit(1);
                } else { // redirect stdin by ">"
                  for(int j=0; j < argc; j++) {
                    if(((struct Token *)tokens[j])->eType == TOKEN_REDIN) {
                      int fd = open(argv[j+1], O_RDONLY, 0600);
                      dup2(fd, 0);
                      close(fd);
                      break;
                    } 
                  }
                }
                // close all pipes
                for(int j=0;j<pipe_len-1;j++) {
                  close(pipes[j][0]);
                  close(pipes[j][1]);
                }
                signal(SIGINT, SIG_DFL);
                signal(SIGQUIT, SIG_DFL);
                signal(SIGALRM, SIG_DFL);
                execvp(argv[cmds_idx[i]], &(argv[cmds_idx[i]]));
	      }
	    }
	    // close all pipes
	    for(int i=0;i<pipe_len-1;i++) {
	      close(pipes[i][0]);
	      close(pipes[i][1]);
	    }
	    // wait children
	    for (int i = 0; i < pipe_len; i++) {
	      int a;
	      wait(&a);
	    }
	    free(cmds_idx);
	    break;
	  }
	  default:
	    errorPrint("main needs to be fixed", FPRINTF);
	    exit(EXIT_FAILURE);
        }
        free(tokens);
        free(argv);
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

void signal_handler(int sig){
  static int current_state = 0;
  if (sig == SIGQUIT) {
    if (current_state == 0) {
      current_state = 1;
      fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
      alarm(5);
    } else {
      exit(0);
    }
  } else if (sig == SIGALRM) {
    current_state = 0;
  }
  return;
}

int main() {
  /* TODO */
  errorPrint("./ish", SETUP);
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGINT);
  sigaddset(&sigset, SIGQUIT);
  sigaddset(&sigset, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sigset, NULL);
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, signal_handler);
  signal(SIGALRM, signal_handler);
  char acLine[MAX_LINE_SIZE + 2];
  char path[1024];
  strcpy(path, getenv("HOME"));
  strcat(path, "/.ishrc");
  FILE *file;
  if ((file = fopen(path, "r"))) {
    while (1) {
      if (fgets(acLine, MAX_LINE_SIZE, file) == NULL) {
        break;
      }
      fflush(stdout);
      fprintf(stdout, "%% ");
      fprintf(stdout, "%s", acLine);
      shellHelper(acLine);
    }
    fclose(file);
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

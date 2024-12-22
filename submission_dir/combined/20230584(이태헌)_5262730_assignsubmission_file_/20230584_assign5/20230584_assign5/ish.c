#define _POSIX_C_SOURCE 200112L 

#include <stdio.h>
#include <stdlib.h>

#include "lexsyn.h"
#include "util.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>

#include "dynarray.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/


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
        if (DynArray_getLength(oTokens) == 0)
        return; 

        
        /* TODO */
        processCommand(oTokens);
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



static void processCommand(DynArray_T oTokens) {
        enum BuiltinType btype = checkBuiltin(DynArray_get(oTokens, 0));
        if (btype == B_CD){             //change directory (cd)
          if (DynArray_getLength(oTokens) < 2){     //short
            errorPrint("cd: Missing argument", FPRINTF);
          } else {  //long enough
            char *path = DynArray_get(oTokens, 1);
            if (chdir(path) != 0) {
              perror("cd");
            }
          }
        } else if (btype == B_EXIT) {         //exit
            DynArray_free(oTokens);
            exit(EXIT_SUCCESS);
        } else if (btype == B_SETENV) {       //set variable
            if (DynArray_getLength(oTokens) < 3) {    //no argument
                errorPrint("setenv: Missing arguments", FPRINTF);
            } else {
                char *name = DynArray_get(oTokens, 1);
                char *value = DynArray_get(oTokens, 2);
                if(setenv(name, value, 1) != 0) {
                    perror("setenv");
                }
            }
        } else if (btype == B_USETENV) {
            
            if (DynArray_getLength(oTokens) < 2) {
                errorPrint("unsetenv: Missing argument", FPRINTF);
            } else {
                char *name = DynArray_get(oTokens, 1);
                if(unsetenv(name) != 0) {
                    perror("unsetenv");
                }
            }
        } else {
          pid_t pid = fork();
          if (pid == 0) { // Child process
            char **argv = malloc((DynArray_getLength(oTokens)+1)* sizeof(char *));
            int i;
            
            if(!argv){
              perror("malloc");
              exit(EXIT_FAILURE);
            }

            for (i = 0; i < DynArray_getLength(oTokens); ++i)
                argv[i] = (char *)DynArray_get(oTokens, i);

            argv[DynArray_getLength(oTokens)] = NULL;
            execvp(argv[0], argv);
            perror("execvp"); // execution fail
            free(argv);
            exit(EXIT_FAILURE);
          } else if (pid > 0) { // parent process
            int status;
              if (waitpid(pid, &status, 0) == -1) {
                perror("waitpid");
              } else if (WIFEXITED(status)) {
                printf("Child exited with status %d\n", WEXITSTATUS(status));
              } else if (WIFSIGNALED(status)) {
                printf("Child terminated by signal %d\n", WTERMSIG(status));
              }
          } else {
            //fork fail
            perror("fork");
          }
        }
}

int main() {
  /* TODO */

  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);

    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }

    size_t len = strlen(acLine);
    if (len > 0 && acLine[len - 1] == '\n')
      acLine[len - 1] = '\0'; 

    if (strlen(acLine) == 0)
      continue;

    shellHelper(acLine);
  }
  return 0;
}


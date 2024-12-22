/*--------------------------------------------------------------------*/
/*Student number: 20230565                                            */
/*Name: Junhee Lee                                                    */
/*--------------------------------------------------------------------*/


#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
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
/*--------------------------------------------------------------------*/
/* Function: checkpipe                                                */
/* Purpose: Check if the given token array contains a pipe ('|').     */
/* Parameters:                                                       */
/*   oTokens (DynArray_T) - Dynamic array of tokens.                  */
/* Returns:                                                          */
/*   The index of the first pipe token, or -1 if no pipe is found.    */
/* Notes: This function does not modify any global variables.         */
/*--------------------------------------------------------------------*/
int checkpipe(DynArray_T oTokens) {
  assert(oTokens != NULL);
  struct Token *t;
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE)
    return i;
  }
  return -1;
}
/*--------------------------------------------------------------------*/
/* Function: checkREDIN                                               */
/* Purpose: Check if the given token array contains a redirection     */
/*          input ('<') symbol.                                       */
/* Parameters:                                                       */
/*   oTokens (DynArray_T) - Dynamic array of tokens.                  */
/* Returns:                                                          */
/*   The index of the first redirection input token, or -1 if none.   */
/* Notes: This function does not modify any global variables.         */
/*--------------------------------------------------------------------*/

int checkREDIN(DynArray_T oTokens){
  assert(oTokens != NULL);
  struct Token *t;
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_REDIN)
    return i;
  }
  return -1;
}
/*--------------------------------------------------------------------*/
/* Function: checkREDOUT                                              */
/* Purpose: Check if the given token array contains a redirection     */
/*          output ('>') symbol.                                      */
/* Parameters:                                                       */
/*   oTokens (DynArray_T) - Dynamic array of tokens.                  */
/* Returns:                                                          */
/*   The index of the first redirection output token, or -1 if none.  */
/* Notes: This function does not modify any global variables.         */
/*--------------------------------------------------------------------*/
int checkREDOUT(DynArray_T oTokens){
  assert(oTokens != NULL);
  struct Token *t;
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_REDOUT)
    return i;
  }
  return -1;
}
/*--------------------------------------------------------------------*/
/* Function: handle_Terminate                                         */
/* Purpose: Handle SIGQUIT or SIGINT by treating them as termination  */
/*          requests.                                                 */
/* Parameters: None                                                   */
/* Returns: None                                                      */
/* Notes: This function does not use or affect any global variables.  */
/*--------------------------------------------------------------------*/
static void handle_Terminate() {
  exit(EXIT_SUCCESS);
}
/*--------------------------------------------------------------------*/
/* Function: handle_SIGQUIT                                           */
/* Purpose: Handle the SIGQUIT signal by providing a warning message. */
/* Parameters: None                                                   */
/* Returns: None                                                      */
/* Notes: Sets a timer to re-enable the SIGQUIT handler after 5 secs. */
/*--------------------------------------------------------------------*/
static void handle_SIGQUIT() {
  signal(SIGQUIT, handle_Terminate);
  fprintf(stdout, "\n%s\n",
    "Type Ctrl-\\ again within 5 seconds to exit.");
  alarm(5);
}
/*--------------------------------------------------------------------*/
/* Function: handle_SIGALRM                                           */
/* Purpose: Handle the SIGALRM signal by resetting the SIGQUIT handler.*/
/* Parameters: None                                                   */
/* Returns: None                                                      */
/* Notes: Resets the SIGQUIT handler to its default behavior.         */
/*--------------------------------------------------------------------*/
static void handle_SIGALRM() {
  signal(SIGQUIT, handle_SIGQUIT);
}

/*--------------------------------------------------------------------*/
/* Function: shellHelper                                              */
/* Purpose: Parse, analyze, and execute a given shell command line.   */
/* Parameters:                                                       */
/*   inLine (const char*) - Input command line string.                */
/* Returns: None                                                      */
/* Notes: Handles lexical and syntax analysis, built-in commands,     */
/*        and external command execution.                             */
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
      if (DynArray_getLength(oTokens) == 0){
        DynArray_free(oTokens);
        return;
      }

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);
      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        int pos_REDIN;
        int pos_REDOUT;
        int pos_pipe;
        int count_pipe;
        
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        if(btype == B_SETENV){
          if (DynArray_getLength(oTokens) < 2 || 
          DynArray_getLength(oTokens) > 3) {
            errorPrint("setenv takes one or two parameters", FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          if (DynArray_getLength(oTokens) == 2) {
            char *var = 
            ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
            setenv(var, "\0", 1);
          }
          if (DynArray_getLength(oTokens) == 3){
            char *value = 
            ((struct Token *)DynArray_get(oTokens, 2))->pcValue;
            char *var = 
            ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
            setenv(var, value, 1);
          }
          DynArray_free(oTokens);
          return;
        }
        else if (btype == B_USETENV) {
          if(DynArray_getLength(oTokens) != 2) {
            errorPrint("unsetenv takes one parameter", FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          char *var = 
          ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
          unsetenv(var);
          DynArray_free(oTokens);
          return;
        }
        else if(btype == B_CD) {
          if (DynArray_getLength(oTokens) == 1) {
            char *homedir = getenv("HOME");
            chdir(homedir);
            DynArray_free(oTokens);
            return;
          }
          if (DynArray_getLength(oTokens) != 2) {
            errorPrint("cd takes one parameter", FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          char *dir_name =
          ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
          if (chdir(dir_name) == -1) {
            errorPrint(NULL, PERROR);
            DynArray_free(oTokens);
            return;
          }
          DynArray_free(oTokens);
          return;
        }
        else if (btype == B_EXIT){
          DynArray_free(oTokens);
          exit(0);
        }
        else if ((count_pipe = countPipe(oTokens)) != 0) {
          int status;
          int p[2];
          int prev_fd = -1;
          for (int i = 0; i <= count_pipe; i++) {
            if (i < count_pipe) {
              if (pipe(p) == -1) {
                errorPrint(NULL, PERROR);
                DynArray_free(oTokens);
                return;
              }
            }
            int pid = fork();
            if (pid == 0) {  
              signal(SIGINT, handle_Terminate);
              if (prev_fd != -1) {
                dup2(prev_fd, 0);  
                close(prev_fd);
              }
              if (i < count_pipe) {
                dup2(p[1], 1); 
                close(p[1]);
              }
                  
              char *file_name = 
              ((struct Token *)DynArray_get(oTokens, 0))->pcValue;
              pos_pipe = checkpipe(oTokens);
              if(pos_pipe == -1) {
                int token_count = DynArray_getLength(oTokens);
                char *argv[token_count + 1];
                for (int j = 0; j < token_count; j++) {
                  argv[j] = 
                  ((struct Token *)DynArray_get(oTokens, j))->pcValue;
                }
                argv[token_count] = NULL;
                if (execvp(file_name, argv) == -1) {
                  errorPrint(file_name, PERROR);
                  DynArray_free(oTokens);
                  exit(EXIT_FAILURE);
                }
              }
              else {
                char *argv[pos_pipe + 1];
                for (int j = 0; j < pos_pipe; j++) {
                  argv[j] = 
                  ((struct Token *)DynArray_get(oTokens, j))->pcValue;
                }
                argv[pos_pipe] = NULL;
                if (execvp(file_name, argv) == -1) {
                  errorPrint(file_name, PERROR);
                  DynArray_free(oTokens);
                  exit(EXIT_FAILURE);
                }
              }
            }
            else { 
              waitpid(pid, &status, 0);  
              if (prev_fd != -1) {
                close(prev_fd);
              }
              prev_fd = p[0];  
            }
          }
          DynArray_free(oTokens);
          return;
        }

        else {
          int status;
          fflush(NULL);
          int pid = fork();
          if (pid == 0) {
            signal(SIGINT, handle_Terminate);
            if((pos_REDIN = checkREDIN(oTokens)) != -1){
              FILE *fd1 = 
              fopen(((struct Token *)
              DynArray_get(oTokens, pos_REDIN + 1))->pcValue, "r");
              if(fd1 == NULL) {
                errorPrint(((struct Token *)
                DynArray_get(oTokens, pos_REDIN + 1))
                ->pcValue, PERROR);
                DynArray_free(oTokens);
                return;
              }
              close(0);
              dup(fileno(fd1));
              fclose(fd1);
              DynArray_removeAt(oTokens, pos_REDIN + 1);
              DynArray_removeAt(oTokens, pos_REDIN);
            }
            if((pos_REDOUT = checkREDOUT(oTokens)) != -1){
              FILE *fd1 = 
              fopen(((struct Token *)
              DynArray_get(oTokens, pos_REDOUT + 1))->pcValue, "w");
              close(1);
              dup(fileno(fd1));
              fclose(fd1);
              DynArray_removeAt(oTokens, pos_REDOUT + 1);
              DynArray_removeAt(oTokens, pos_REDOUT);
            }
            char *file_name = 
            ((struct Token *)DynArray_get(oTokens, 0))->pcValue;
            int Token_count = DynArray_getLength(oTokens);
            char *argv[Token_count + 1];
            for (int i =0; i < Token_count; i++) {
              argv[i] = ((struct Token *)
              DynArray_get(oTokens, i))->pcValue;
            }
            argv[Token_count] = NULL;
            if(execvp(file_name, argv) == -1) {
              errorPrint(file_name, PERROR);
              DynArray_free(oTokens);
              exit(EXIT_FAILURE);
            }
          }
          
          else {
            waitpid(pid,&status,0);
            DynArray_free(oTokens);
            return;
          }
        }
      }


      /* syntax error cases */
      else if (syncheck == SYN_FAIL_NOCMD)
        errorPrint("Missing command name", FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDOUT)
        errorPrint("Multiple redirection of standard out", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTOUT)
        errorPrint("Standard output redirection without file name",
         FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDIN)
        errorPrint("Multiple redirection of standard input", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTIN)
        errorPrint("Standard input redirection without file name",
         FPRINTF);
      else if (syncheck == SYN_FAIL_INVALIDBG)
        errorPrint("Invalid use of background", FPRINTF);
      DynArray_free(oTokens);
      break;

    case LEX_QERROR:
      errorPrint("Unmatched quote", FPRINTF);
      DynArray_free(oTokens);
      break;

    case LEX_NOMEM:
      errorPrint("Cannot allocate memory", FPRINTF);
      DynArray_free(oTokens);
      break;

    case LEX_LONG:
      errorPrint("Command is too large", FPRINTF);
      DynArray_free(oTokens);
      break;

    default:
      errorPrint("lexLine needs to be fixed", FPRINTF);
      DynArray_free(oTokens);
      exit(EXIT_FAILURE);
  }
}
/*--------------------------------------------------------------------*/
/* Function: initializeShell                                          */
/* Purpose: Initialize the shell by reading and executing commands    */
/*          from a configuration file (".ishrc").                     */
/* Parameters: None                                                   */
/* Returns: None                                                      */
/* Notes: Reads the configuration file line by line and executes      */
/*        each command using shellHelper().                           */
/*--------------------------------------------------------------------*/
void initializeShell() {
  const char *homedir = getenv("HOME");
  char filePath[MAX_LINE_SIZE] = {0};
  snprintf(filePath, sizeof(filePath), "%s/.ishrc", homedir);
  FILE *file = fopen(filePath, "r");
  if (file == NULL) {
    return;
  }
  char line[MAX_LINE_SIZE];
  while (fgets(line, sizeof(line), file)) {
    fprintf(stdout, "%% ");
    fprintf(stdout, "%s", line);
    shellHelper(line);
  }
  fclose(file);
  return;
}

/*--------------------------------------------------------------------*/
/* Function: main                                                     */
/* Purpose: Entry point of the shell program.                         */
/* Parameters:                                                       */
/*   argc (int) - Argument count.                                     */
/*   argv (char**) - Argument vector.                                 */
/* Returns: None                                                      */
/* Notes: Sets up signal handlers, initializes the shell, and         */
/*        processes user input in an infinite loop.                   */
/*--------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
  /* TODO */
  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGALRM);
  sigaddset(&sSet, SIGQUIT);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);
  signal(SIGQUIT, handle_SIGQUIT);
  signal(SIGINT, SIG_IGN);
  signal(SIGALRM, handle_SIGALRM);
  errorPrint(argv[0], SETUP);
  initializeShell();
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


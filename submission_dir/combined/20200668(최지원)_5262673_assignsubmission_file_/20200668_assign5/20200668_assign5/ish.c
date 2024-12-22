/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Name(Student ID): Jiwon Choi(20200668)                             */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include "lexsyn.h"
#include "util.h"

/* Additional Libraries */
#include <unistd.h>
#include <sys/types.h>
#include <sys/signal.h>
#include <string.h>
#include "dynarray.h"
#include "redirection.h"
#include "signal.h"
#include "pipeline.h"
#define GNU_SOURCE

void initialize_ishrc();

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
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        char *var, *value, *dir;
        int tokenCount = DynArray_getLength(oTokens);
        switch (btype) {
          case B_SETENV: // For sentenv
            if (tokenCount < 2 || tokenCount > 3) {
              errorPrint("setenv takes one or two parameters", FPRINTF);
              break;
            }
            var = (tokenCount > 1) ?
             ((struct Token *)DynArray_get(oTokens, 1))->pcValue : NULL;
            value = (tokenCount > 2) ?
             ((struct Token *)DynArray_get(oTokens, 2))->pcValue : "";
            setenv(var, value, 1);
            break;
          case B_USETENV: // For unsentenv
            if (tokenCount != 2) {
              errorPrint("unsetenv takes one parameter", FPRINTF);
              break;
            }
            var = (tokenCount > 1) ?
             ((struct Token *)DynArray_get(oTokens, 1))->pcValue : NULL;
            unsetenv(var);
            break;
          case B_CD: // For cd
            dir = (tokenCount > 1) ?
             ((struct Token *)DynArray_get(oTokens, 1))->pcValue : getenv("HOME");
            if (tokenCount > 2) {
              errorPrint("cd takes one parameter", FPRINTF);
              break;
            }
            if (chdir(dir) == -1) errorPrint(NULL, PERROR);
            break;
          case B_EXIT: // For exit with exit status 0
            if (tokenCount != 1) {
              errorPrint("exit does not take any parameters", FPRINTF);
              break;
            }
            exit(EXIT_SUCCESS);
            break;
          default: // External command
            if (countPipe(oTokens) > 0) process_pipeline(oTokens);
            else execute_with_redirection(oTokens);
            break;
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
/*--------------------------------------------------------------------*/
/* Entry point of the shell program. Initializes signal handlers,
   processes the .ishrc file, and starts the interactive shell loop.
   Parameters:
   - argc: Number of command-line arguments.
   - argv: Array of command-line arguments. */
int main(int argc, char *argv[]) {
  /* TODO */
  (void)argc;
  char acLine[MAX_LINE_SIZE + 2];
  /* Initialize program name for errorPrint */
  errorPrint(argv[0], SETUP); // util.h

  /* Install Signal Handlers */
  signal(SIGINT, SIG_IGN); // Ignore SIGINT
  signal(SIGQUIT, handler_SIGQUIT);
  signal(SIGALRM, handler_SIGALRM);
  
  /* Ensure SIGINT, SIGQUIT, SIGALRM are not blocked */
  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  if (sigprocmask(SIG_UNBLOCK, &sSet, NULL) < 0) {
    perror("sigprocmask");
    return EXIT_FAILURE;
  }

  /* Process .ishrc file */
  initialize_ishrc();

  /* Start Interactive Operation Shell Loop */
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    /* Read a line from the standard input stream */
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS); // Terminate the program
    }
    /* Remove trailing newline character */
    acLine[strcspn(acLine, "\n")] = '\0';
    shellHelper(acLine);
  }
  return 0;
}

/* initialize_ishrc reads and executes commands 
from the .ishrc file in the HOME directory if it exists. */                                      
void initialize_ishrc() {
  /* Get the HOME directory from the environment variable */
  char *HOME_dir = getenv("HOME");
  if (HOME_dir == NULL) {
    fprintf(stderr, "Error: Cannot find HOME environment variable.\n");
    return;
  }
  /* Construct the path to .ishrc file */
  char ishrc_path[MAX_LINE_SIZE];
  snprintf(ishrc_path, sizeof(ishrc_path), "%s/.ishrc", HOME_dir);
  
  /* Open .ishrc file */
  FILE *file = fopen(ishrc_path, "r");
  /* If the file does not exist or is not readable, just return */
  if (file == NULL) return;

  /* Read and process each line in .ishrc file */
  char line[MAX_LINE_SIZE];
  while (fgets(line, sizeof(line), file) != NULL) {
    line[strcspn(line, "\n")] = '\0';
    fprintf(stdout, "%% %s\n", line); // print the commands from .ishrc
    shellHelper(line); // Handle the command
  }
  fclose(file);  // Close the file
}

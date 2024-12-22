/* Background */
/* Name: Seheon Kim */
/* Student ID: 20200098 */
/* ish source code role: This is a simple Unix-based shell program that
 analyzes and executes user-populated instructions and processes 
 built-in and external instructions. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>

#include "lexsyn.h"
#include "util.h"

/*-------------------------------------------------------------------*/
/* ish.c                                                             */
/* Original Author: Bob Dondero                                      */
/* Modified by : Park Ilwoo                                          */
/* Illustrate lexical analysis using a deterministic finite state    */
/* automaton (DFA)                                                   */
/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/

/* Variable that process SIGQUIT */
volatile sig_atomic_t quitflag = 0;

/* SIGINT signal handler */
void handleSIGINT(int sig) {
  (void)sig;
}

/* SIGQUIT signal handler */
void handleSIGQUIT(int sig) {
  (void)sig;

  if(!quitflag) {
    // Ctrl-\ input
    quitflag = 1;
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5);
  }
  else {
    exit(EXIT_SUCCESS);
  }
}

/* SIGALRM signal handler */
void handleAlarm(int sig) {
  (void)sig;
  quitflag = 0; // Ctrl-\ timer reset
}

/*-------------------------------------------------------------------*/


/* Redirection Processing Function */
/* Find the redirection token (<, >) and its subsequent filename 
and link the standard input or output to the file */
/* After connecting, remove the tokens used for redirection from the
token array so that it does not affect the execution of the command */
static void ProcessRedirection(DynArray_T oTokens) {
  for (int i = 0 ; i < DynArray_getLength(oTokens) ; i++) {
    // Take current Token
    struct Token *curToken = DynArray_get(oTokens, i);
    /* Input redirection (< token) */
    if (curToken->eType == TOKEN_REDIN) {
      // If there is no next token, error
      if (DynArray_getLength(oTokens) <= i+1) {
        errorPrint("Input Error", FPRINTF);
        exit(EXIT_FAILURE); // Program End
      }

      /* Import File Name Token for Redirection */
      struct Token *fileToken = DynArray_get(oTokens, i+1);

      /* Open file Readonly */
      int filecheck = open(fileToken->pcValue, O_RDONLY);
      // Error if failed in open file
      if (filecheck == -1) {
        errorPrint(fileToken->pcValue, PERROR);
        exit(EXIT_FAILURE); // Program End
      }
      
      /* Exchange file descriptor as STDIN_FILENO */
      dup2(filecheck, STDIN_FILENO);
      close(filecheck);

      /* Remove Token related redirection in array */
      DynArray_removeAt(oTokens, i+1); // Remove file name token
      DynArray_removeAt(oTokens, i); // Remove "<" Token
      i = i - 1; // Adjust Index
    }

    /* Output redirection ( > token) */
    else if (curToken->eType == TOKEN_REDOUT) {
      // If there is no next token, error
      if (DynArray_getLength(oTokens) <= i+1) {
        errorPrint("Input Error", FPRINTF);
        exit(EXIT_FAILURE); // Program End
      }

      /* Import File Name Token for Redirection */
      struct Token *fileToken = DynArray_get(oTokens, i+1);

      /* Open file WriteOnly or 
      delete given content while generating */
      int filecheck = open(fileToken->pcValue, O_WRONLY | O_CREAT | 
      O_TRUNC, 0600);

      // Error if failed in open file
      if (filecheck == -1) {
        errorPrint(fileToken->pcValue, PERROR);
        exit(EXIT_FAILURE); // Program End
      }
      
      /* Exchange file descriptor as STDOUT_FILENO */
      dup2(filecheck, STDOUT_FILENO);
      close(filecheck);

      /* Remove Token related redirection in array */
      DynArray_removeAt(oTokens, i + 1); // Remove file name token
      DynArray_removeAt(oTokens, i); // Remove ">" Token
      i = i - 1; // Adjust Index
    }
  }
}

/*-------------------------------------------------------------------*/

/* Pipeline Processing Function */
/* Processing the pipe (|) included in the command */
/* Pipe each command to set the output to go to the 
input of the next command */
static void ProcessPipeline(DynArray_T oTokens) {
  // count # of pipes in the command
  int numPipe = countPipe(oTokens);

  // If there are no pipes, return immediately
  if (numPipe == 0) {
    return;
  }

  /* Create an array to hold the file descriptors */
  int pipecheck[2 * numPipe];
  for (int i = 0 ; i < numPipe ; i++) {
    // Create pipe for each command pair
    // Print Error if failed in generate pipe
    if (pipe(pipecheck + 2 * i) < 0) {
      errorPrint("Pipe Generation Failed", PERROR);
      exit(EXIT_FAILURE);
    }
  }

  /* Variable that detect command segments in tokens */
  int commandStart = 0; // Start index of current command in tokens
  int commandIndex = 0; // Current command index in pipeline

  for (int i = 0 ; i <= DynArray_getLength(oTokens); i++) {
    // Take current token
    struct Token *pipeToken;
    if (i < DynArray_getLength(oTokens)) {
      pipeToken = DynArray_get(oTokens, i);
    }
    // If token end, set NULL
    else {
      pipeToken = NULL;
    }

    /* If current Token is pipe or reach at end of command */
    if (pipeToken == NULL || pipeToken->eType == TOKEN_PIPE) {
      pid_t pid = fork();
      if (pid == 0) {
        /* If not the first command, connect previous pipe's
            read end to STDIN */
        if (commandIndex > 0) {
          dup2(pipecheck[(commandIndex - 1)*2], STDIN_FILENO);
        }
        /* If not last command, connect current pipe's
            write end to STDOUT */
        if (commandIndex < numPipe) {
          dup2(pipecheck[commandIndex * 2 + 1], STDOUT_FILENO);
        }
        
        /* Close all pipe descriptor which do not used */
        for (int j = 0 ; j < 2 * numPipe ; j++) {
          close(pipecheck[j]);
        }

        /* Get token which correspond to current command */
        DynArray_T commandTokens = DynArray_new(0);
        for (int j = commandStart ; j < i ; j++) {
          DynArray_add(commandTokens, DynArray_get(oTokens, j));
        }

        /* Redirection Process */
        ProcessRedirection(commandTokens);

        /* Execute execvp */
        char *argv[MAX_ARGS_CNT + 1];
        for (int j = 0 ; j < DynArray_getLength(commandTokens);
              j++) {
          argv[j] = ((struct Token*)
            DynArray_get(commandTokens, j))->pcValue;
        }
        argv[DynArray_getLength(commandTokens)]=NULL;
        
        execvp(argv[0], argv);
        errorPrint(argv[0], PERROR);
        exit(EXIT_FAILURE);
      }
      else if (pid > 0) {
        if (commandIndex > 0) {
          close(pipecheck[(commandIndex - 1) * 2]);
        }
        if (commandIndex < numPipe) {
          close(pipecheck[commandIndex * 2 + 1]);
        }
      }

      /* Update the starting position of the following command */
      commandStart = i + 1;  
      commandIndex = commandIndex + 1;
    }
  }
  
  
  /* Close all pipe descriptor */
  for (int i = 0 ; i < 2 * numPipe ; i ++) {
    close(pipecheck[i]);
  }

  /* Wait until all child process finished */
  for (int i = 0 ; i <= numPipe ; i++) {
    wait(NULL);
  }
}

/*-------------------------------------------------------------------*/

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
        /* case: B_CD, B_FG, B_EXIT, B_SETENV,
                  B_USETENV, B_ALIAS, NORMAL */
        switch(btype) {
          /* B_CD: Change Directory */
          case B_CD: {
            char *path; // path of directory
            /* First token is always cd, if DynArray_getLength(oTokens)
            is more than 1, it means path of directory */
            if (DynArray_getLength(oTokens) > 1) {
              path = 
              ((struct Token*)DynArray_get(oTokens, 1))->pcValue;
            }
            /* If not, using "Home" variable to basic value */
            else {
              path = getenv("HOME");
            }
            /* Failed directory change */
            if (chdir(path) != 0) {
              errorPrint(path, PERROR);
            }
            break;
          }

          /* B_FG */
          case B_FG: { 
            break;
          }

          /* B_EXIT: Exit Shell */
          case B_EXIT: {
          /* When user input exit command, shell end.
          Preventing memory leak, clear oTokens memory before exit */
            DynArray_free(oTokens);
            exit(EXIT_SUCCESS);
            break;
          }

          /* B_SETENV: Set Environment Variable */
          case B_SETENV: {
            /* If oTokens's length is less than 3, there is no
            variable name and value, so print error */
            if (DynArray_getLength(oTokens) < 3) {
              errorPrint("setenv: Missing var or Value", FPRINTF);
            }
            else {
              // Import environment variable name with varA
              char *varA = 
              ((struct Token*)DynArray_get(oTokens, 1))->pcValue;
              // Import environment variable vale with varB
              char *varB = 
              ((struct Token*)DynArray_get(oTokens, 2))->pcValue;
              // Set envrionment variable with setenv
              if (setenv(varA, varB, 1) != 0) {
                errorPrint("setenv Failed", PERROR);
              }
            }
            break;
          }
          /* B_USETENV: UnSet Environment Variable */
          case B_USETENV: {
            /* If otokens's length is less than 2, there is no 
            variable to unsetenv, so print error */
            if (DynArray_getLength(oTokens) < 2) {
              errorPrint("unsetenv: Missing unsetenv var", FPRINTF);
            }
            else {
              // Import environment variable which to unset
              char *var = 
              ((struct Token*)DynArray_get(oTokens, 1))->pcValue;
              // Unset environment variable with unsetenv
              if (unsetenv(var)!=0) {
                errorPrint("unsetenv Faield", PERROR);
              }
            }
            break;
          }

          /* B_ALIAS */
          case B_ALIAS: {
            break;
          }

          /* Normal : process out command */
          case NORMAL: {
            pid_t pid = fork();
            if (pid == 0) {
              ProcessPipeline(oTokens); // process pipeline
              ProcessRedirection(oTokens); // process redirection

              /* Create a factor array for use in execvp */
              char *argv[MAX_ARGS_CNT+1];
              // Array to restore command and variable
              for (int i=0 ; i < DynArray_getLength(oTokens);i++) {
                argv[i] =
                ((struct Token*)DynArray_get(oTokens, i))->pcValue;
                // take token value
              }
              argv[DynArray_getLength(oTokens)]=NULL;

              execvp(argv[0], argv);
              errorPrint(argv[0], PERROR);
              exit(EXIT_FAILURE);
            }
            else if (pid >0) {
              wait(NULL); // wait until child process finished
            }
            else { // Error if fork failed
              errorPrint("fork Failed", PERROR);
            }
            break;
          }

          /* Default */
          default: {
            break;
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

/* Role of Main Function
1. Reset when shell start
2. .ishrc processing (find .ishrc file in home directory and
   execute, then print command and result so check .ishrc action)
3. User Input loop (execute user input command, and request input
   by prompt)
4. When EOF input, shell end*/
int main() {
  /* TODO */
  /* print SETUP message */
  errorPrint("./ish", SETUP);

  /* Signal Handler */
  signal(SIGINT, handleSIGINT);
  signal(SIGQUIT, handleSIGQUIT);
  signal(SIGALRM, handleAlarm);
  
  /* .ishrc file processing */
  char *pathhome = getenv("HOME"); // get path of home directory
  if (pathhome != NULL) {
    char ishrcPath[MAX_LINE_SIZE]; // restore path of .ishrc file
    snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", pathhome);
    // compose path of .ishrc
    FILE *ishrcFilePath = fopen(ishrcPath, "r");
    if (ishrcFilePath) {
      char ishrcLine[MAX_LINE_SIZE + 2]; // buffer to read each line
      while (fgets(ishrcLine, sizeof(ishrcLine), ishrcFilePath)) {
      // read each line of file
        fprintf(stdout, "%% %s", ishrcLine); // print command
        fflush (stdout);
        shellHelper(ishrcLine);
      }
      fclose(ishrcFilePath); // close file
    }
  }

  /* USer Input Loop */
  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    // When EOF input, process end
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
}

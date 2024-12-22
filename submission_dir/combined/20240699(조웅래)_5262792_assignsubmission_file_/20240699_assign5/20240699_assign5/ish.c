/*
ish.c
Name: 조웅래
Student ID: 20240699
Descriptions:
Implementation of a simple Unix-like shell named 'ish'.
Handles command parsing, execution of built-in and external
commands, input/output redirection, pipelines, and signal
handling. Reads and executes commands from a .ishrc file
located in the user's home directory upon startup.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "lexsyn.h"
#include "util.h"
#include "token.h"
#include "dynarray.h"

#define TIMEOUT 5
#define MAX_LINE_SIZE 1023

/* Globals for SIGQUIT handling */
static int g_quitPressed = 0;
static char *g_programName = NULL;

/* Forward declarations of static helper functions */
static void shellHelper(const char *inLine);
static void setupSignals(void);
static void readIshrc(void);
static void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens);
static void executeExternal(DynArray_T oTokens);
static int processRedirections(DynArray_T oTokens, int *inFd, int *outFd);
static int hasRedirection(DynArray_T oTokens);
static void sigquitHandler(int sig);
static void sigalrmHandler(int sig);
static void processPipeline(DynArray_T oTokens);
static DynArray_T *splitPipeline(DynArray_T oTokens, int *count);
static void executePipeline(DynArray_T *cmds, int count);

/*
sigquitHandler
Parameters:
  - sig: The signal number (unused).
Description:
  Handles the SIGQUIT signal. On the first press, it prompts the user
  to press again within 5 seconds to exit. If pressed again within the
  timeout, the shell exits. Otherwise, it resets the quitPressed flag.
  Affects the global variable g_quitPressed.
*/
static void sigquitHandler(int sig) {
  (void)sig;
  if (g_quitPressed == 0) {
    fprintf(stdout, "Type Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    g_quitPressed = 1;
    alarm(TIMEOUT);
  } else {
    exit(EXIT_SUCCESS);
  }
}

/*
sigalrmHandler
Parameters:
  - sig: The signal number (unused).
Description:
  Handles the SIGALRM signal by resetting the g_quitPressed flag.
  Affects the global variable g_quitPressed.
*/
static void sigalrmHandler(int sig) {
  (void)sig;
  g_quitPressed = 0;
}

/*
main
Parameters:
  - argc: Argument count.
  - argv: Argument vector.
Returns:
  - Exit status.
Description:
  Initializes the shell by setting up signal handlers and reading the
  .ishrc file. Enters an infinite loop to read and execute commands
  from the user.
  Uses global variables:
    - g_programName: To store the shell's name for error messages.
*/
int main(int argc, char *argv[]) {
  if (argc > 0) {
    g_programName = argv[0];
  } else {
    g_programName = "ish";
  }
  errorPrint(g_programName, SETUP);  /* Set shell name for error messages */

  setupSignals();
  readIshrc();

  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      /* EOF (Ctrl-D) */
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
  return 0;
}

/*
shellHelper
Parameters:
  - inLine: The input command line to process.
Description:
  Processes a single command line by performing lexical and syntactic
  analysis. Executes built-in commands directly or external commands
  (with or without pipelines). Handles redirection errors and reports
  syntax errors.
  Reads from:
    - inLine: The input command line.
  Writes to:
    - Standard output and error via errorPrint.
  Uses global variables:
    - g_programName: For error message prefixes.
*/
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
      if (DynArray_getLength(oTokens) == 0) {
        DynArray_free(oTokens);
        return;
      }
      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));

        if (btype != NORMAL) {
          /* Built-in command */
          if (hasRedirection(oTokens)) {
            errorPrint("Redirection not permitted with built-in commands",
                       FPRINTF);
            DynArray_free(oTokens);
            return;
          }
          executeBuiltin(btype, oTokens);
        } else {
          /* External command (maybe with pipes) */
          int pipeCount = 0;
          for (int i = 0; i < DynArray_getLength(oTokens); i++) {
            struct Token *t = DynArray_get(oTokens, i);
            if (t->eType == TOKEN_PIPE)
              pipeCount++;
          }
          if (pipeCount > 0) {
            processPipeline(oTokens);
            DynArray_free(oTokens);
            return;
          } else {
            executeExternal(oTokens);
          }
        }
      } else {
        /* Syntax errors */
        if (syncheck == SYN_FAIL_NOCMD)
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
      }

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

/*
setupSignals
Parameters:
  - None.
Returns:
  - Nothing.
Description:
  Configures the shell's signal handlers. Ignores SIGINT (Ctrl-C) in the
  parent shell and sets up handlers for SIGQUIT and SIGALRM to manage
  graceful exits upon double pressing Ctrl-\.
  Uses global variables:
    - g_quitPressed: To track SIGQUIT presses.
*/
static void setupSignals(void) {
  sigset_t set;
  sigemptyset(&set);
  sigprocmask(SIG_SETMASK, &set, NULL);

  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, sigquitHandler);
  signal(SIGALRM, sigalrmHandler);
}

/*
readIshrc
Parameters:
  - None.
Returns:
  - Nothing.
Description:
  Reads and executes commands from the .ishrc file located in the user's
  home directory. Each command from .ishrc is printed before execution.
  Reads from:
    - .ishrc file.
  Writes to:
    - Standard output via fprintf for command display.
    - Executes commands which may write to standard output or error.
  Uses global variables:
    - g_programName: For error message prefixes.
*/
static void readIshrc(void) {
  char *home = getenv("HOME");
  if (!home)
    return;

  char ishrcPath[1024];
  snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", home);

  FILE *fp = fopen(ishrcPath, "r");
  if (!fp) {
    /* Not an error if doesn't exist or unreadable */
    return;
  }

  char acLine[MAX_LINE_SIZE + 2];
  while (fgets(acLine, MAX_LINE_SIZE + 1, fp)) {
    size_t len = strlen(acLine);
    if (len > 0 && acLine[len - 1] == '\n')
      acLine[len - 1] = '\0';

    /* Print the command read from .ishrc */
    fprintf(stdout, "%% %s\n", acLine);
    fflush(stdout);

    shellHelper(acLine);
  }
  fclose(fp);
}

/*
hasRedirection
Parameters:
  - oTokens: The dynamic array of tokens representing the command.
Returns:
  - 1 if redirection tokens are present, 0 otherwise.
Description:
  Checks if the command includes input or output redirection tokens.
  Reads from:
    - oTokens: To inspect each token.
  Writes to:
    - None.
  Uses global variables:
    - None.
*/
static int hasRedirection(DynArray_T oTokens) {
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    struct Token *t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT)
      return 1;
  }
  return 0;
}

/*
executeBuiltin
Parameters:
  - btype: The type of built-in command to execute.
  - oTokens: The dynamic array of tokens representing the command.
Returns:
  - Nothing.
Description:
  Executes built-in commands such as cd, setenv, unsetenv, and exit.
  Parses arguments from oTokens and performs the corresponding action.
  Writes to:
    - Standard output and error via errorPrint.
  Uses global variables:
    - g_programName: For error message prefixes.
*/
static void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens) {
  int length = DynArray_getLength(oTokens);
  char **argv = malloc((length + 1) * sizeof(char *));
  if (!argv) {
    errorPrint("Cannot allocate memory", FPRINTF);
    return;
  }
  for (int i = 0; i < length; i++) {
    struct Token *t = DynArray_get(oTokens, i);
    argv[i] = t->pcValue;
  }
  argv[length] = NULL;

  if (btype == B_CD) {
    if (length == 1) {
      char *home = getenv("HOME");
      if (!home)
        home = "/";
      if (chdir(home) != 0)
        errorPrint(home, PERROR);
    } else {
      if (chdir(argv[1]) != 0)
        errorPrint(argv[1], PERROR);
    }
  } else if (btype == B_EXIT) {
    free(argv);
    exit(0);
  } else if (btype == B_SETENV) {
    if (length == 2) {
      if (setenv(argv[1], "", 1) != 0)
        errorPrint(argv[1], PERROR);
    } else if (length == 3) {
      if (setenv(argv[1], argv[2], 1) != 0)
        errorPrint(argv[1], PERROR);
    } else {
      errorPrint("Usage: setenv var [value]", FPRINTF);
    }
  } else if (btype == B_USETENV) {
    if (length == 2) {
      if (unsetenv(argv[1]) != 0)
        errorPrint(argv[1], PERROR);
    } else {
      errorPrint("Usage: unsetenv var", FPRINTF);
    }
  }
  /* Additional built-ins like fg and alias can be handled here */

  free(argv);
}

/*
executeExternal
Parameters:
  - oTokens: The dynamic array of tokens representing the command.
Returns:
  - Nothing.
Description:
  Executes external (non-built-in) commands. Handles input/output
  redirection by duplicating file descriptors. Forks a child process
  to run the command using execvp and waits for its completion.
  Writes to:
    - Standard output and error via errorPrint.
  Uses global variables:
    - g_programName: For error message prefixes.
*/
static void executeExternal(DynArray_T oTokens) {
  int inFd, outFd;
  if (!processRedirections(oTokens, &inFd, &outFd))
    return; /* error already printed */

  int length = DynArray_getLength(oTokens);
  if (length == 0) {
    errorPrint("Missing command name", FPRINTF);
    return;
  }

  char **argv = malloc((length + 1) * sizeof(char *));
  if (!argv) {
    errorPrint("Cannot allocate memory", FPRINTF);
    return;
  }
  for (int i = 0; i < length; i++) {
    struct Token *t = DynArray_get(oTokens, i);
    argv[i] = t->pcValue;
  }
  argv[length] = NULL;

  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0) {
    errorPrint("fork failed", PERROR);
    free(argv);
    return;
  } else if (pid == 0) {
    /* Child process */
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    if (inFd != -1) {
      dup2(inFd, STDIN_FILENO);
      close(inFd);
    }
    if (outFd != -1) {
      dup2(outFd, STDOUT_FILENO);
      close(outFd);
    }
    execvp(argv[0], argv);
    errorPrint(argv[0], PERROR);
    exit(EXIT_FAILURE);
  } else {
    /* Parent process */
    if (inFd != -1)
      close(inFd);
    if (outFd != -1)
      close(outFd);
    wait(NULL);
  }
  free(argv);
}

/*
processRedirections
Parameters:
  - oTokens: The dynamic array of tokens representing the command.
  - inFd: Pointer to store the input file descriptor.
  - outFd: Pointer to store the output file descriptor.
Returns:
  - 1 on success, 0 on failure.
Description:
  Processes input and output redirection tokens by opening the
  specified files and duplicating file descriptors. Removes redirection
  tokens and their corresponding filenames from oTokens.
  Reads from:
    - oTokens: To identify redirection tokens and filenames.
  Writes to:
    - inFd and outFd: To set up redirections.
    - Standard error via errorPrint on failure.
  Uses global variables:
    - g_programName: For error message prefixes.
*/
static int processRedirections(DynArray_T oTokens, int *inFd, int *outFd) {
  *inFd = -1;
  *outFd = -1;
  int i = 0;
  int redInCount = 0, redOutCount = 0;

  while (i < DynArray_getLength(oTokens)) {
    struct Token *t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_REDIN) {
      redInCount++;
      if (redInCount > 1) {
        errorPrint("Multiple redirection of standard input", FPRINTF);
        return 0;
      }
      if (i + 1 >= DynArray_getLength(oTokens)) {
        errorPrint("Standard input redirection without file name",
                   FPRINTF);
        return 0;
      }
      struct Token *fileTok = DynArray_get(oTokens, i + 1);
      if (fileTok->eType != TOKEN_WORD) {
        errorPrint("Standard input redirection without file name",
                   FPRINTF);
        return 0;
      }
      int fd = open(fileTok->pcValue, O_RDONLY);
      if (fd < 0) {
        errorPrint(fileTok->pcValue, PERROR);
        return 0;
      }
      *inFd = fd;
      DynArray_removeAt(oTokens, i + 1);
      DynArray_removeAt(oTokens, i);
    } else if (t->eType == TOKEN_REDOUT) {
      redOutCount++;
      if (redOutCount > 1) {
        errorPrint("Multiple redirection of standard out", FPRINTF);
        return 0;
      }
      if (i + 1 >= DynArray_getLength(oTokens)) {
        errorPrint("Standard output redirection without file name",
                   FPRINTF);
        return 0;
      }
      struct Token *fileTok = DynArray_get(oTokens, i + 1);
      if (fileTok->eType != TOKEN_WORD) {
        errorPrint("Standard output redirection without file name",
                   FPRINTF);
        return 0;
      }
      int fd = open(fileTok->pcValue, O_WRONLY | O_CREAT | O_TRUNC,
                   0600);
      if (fd < 0) {
        errorPrint(fileTok->pcValue, PERROR);
        return 0;
      }
      *outFd = fd;
      DynArray_removeAt(oTokens, i + 1);
      DynArray_removeAt(oTokens, i);
    } else {
      i++;
    }
  }
  return 1;
}

/*
processPipeline
Parameters:
  - oTokens: The dynamic array of tokens representing the command.
Returns:
  - Nothing.
Description:
  Splits the command tokens into separate commands divided by pipes
  and executes them as a pipeline.
  Reads from:
    - oTokens: To identify pipeline segments.
  Writes to:
    - Executes each segment which may write to standard output or error.
  Uses global variables:
    - None.
*/
static void processPipeline(DynArray_T oTokens) {
  int count;
  DynArray_T *cmds = splitPipeline(oTokens, &count);
  if (!cmds)
    return;
  executePipeline(cmds, count);
  for (int i = 0; i < count; i++)
    DynArray_free(cmds[i]);
  free(cmds);
}

/*
splitPipeline
Parameters:
  - oTokens: The dynamic array of tokens representing the command.
  - count: Pointer to store the number of pipeline segments.
Returns:
  - Array of DynArray_T pointers, each representing a pipeline segment.
    Returns NULL on failure.
Description:
  Splits the command tokens into separate commands at each pipe symbol.
  Allocates memory for each segment's DynArray_T.
  Reads from:
    - oTokens: To identify pipe symbols and segment commands.
  Writes to:
    - count: To indicate the number of pipeline segments.
  Uses global variables:
    - None.
*/
static DynArray_T *splitPipeline(DynArray_T oTokens, int *count) {
  int pipeCount = 0;
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    struct Token *t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE)
      pipeCount++;
  }

  *count = pipeCount + 1;
  DynArray_T *cmds = malloc((*count) * sizeof(DynArray_T));
  if (!cmds) {
    errorPrint("Cannot allocate memory", FPRINTF);
    return NULL;
  }

  int cmdIndex = 0;
  cmds[0] = DynArray_new(0);
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    struct Token *t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE) {
      cmdIndex++;
      cmds[cmdIndex] = DynArray_new(0);
    } else {
      DynArray_add(cmds[cmdIndex], t);
    }
  }
  return cmds;
}

/*
executePipeline
Parameters:
  - cmds: Array of DynArray_T pointers, each representing a pipeline segment.
  - count: Number of pipeline segments.
Returns:
  - Nothing.
Description:
  Executes a series of commands connected by pipes. Sets up the necessary
  file descriptors for inter-process communication and manages child
  processes for each segment.
  Reads from:
    - cmds: Each pipeline segment's tokens.
  Writes to:
    - Executes each command which may write to standard output or error.
  Uses global variables:
    - g_programName: For error message prefixes.
*/
static void executePipeline(DynArray_T *cmds, int count) {
  int (*pipes)[2] = NULL;
  if (count > 1) {
    pipes = malloc((count - 1) * sizeof(int[2]));
    if (!pipes) {
      errorPrint("Cannot allocate memory", FPRINTF);
      return;
    }
    for (int i = 0; i < count - 1; i++) {
      if (pipe(pipes[i]) < 0) {
        errorPrint("pipe", PERROR);
        free(pipes);
        return;
      }
    }
  }

  for (int i = 0; i < count; i++) {
    int inFd, outFd;
    if (!processRedirections(cmds[i], &inFd, &outFd)) {
      if (pipes)
        free(pipes);
      return;
    }

    int length = DynArray_getLength(cmds[i]);
    if (length == 0) {
      errorPrint("Missing command name", FPRINTF);
      if (pipes)
        free(pipes);
      return;
    }

    char **argv = malloc((length + 1) * sizeof(char *));
    if (!argv) {
      errorPrint("Cannot allocate memory", FPRINTF);
      if (pipes)
        free(pipes);
      return;
    }
    for (int j = 0; j < length; j++) {
      struct Token *t = DynArray_get(cmds[i], j);
      argv[j] = t->pcValue;
    }
    argv[length] = NULL;

    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
      errorPrint("fork failed", PERROR);
      free(argv);
      if (pipes)
        free(pipes);
      return;
    } else if (pid == 0) {
      /* Child process */
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);

      if (i > 0) {
        dup2(pipes[i - 1][0], STDIN_FILENO);
      } else if (inFd != -1) {
        dup2(inFd, STDIN_FILENO);
        close(inFd);
      }

      if (i < count - 1) {
        dup2(pipes[i][1], STDOUT_FILENO);
      } else if (outFd != -1) {
        dup2(outFd, STDOUT_FILENO);
        close(outFd);
      }

      if (pipes) {
        for (int x = 0; x < count - 1; x++) {
          close(pipes[x][0]);
          close(pipes[x][1]);
        }
      }
      execvp(argv[0], argv);
      errorPrint(argv[0], PERROR);
      exit(EXIT_FAILURE);
    } else {
      /* Parent process */
      if (inFd != -1)
        close(inFd);
      if (outFd != -1)
        close(outFd);
    }
    free(argv);
  }

  if (pipes) {
    for (int x = 0; x < count - 1; x++) {
      close(pipes[x][0]);
      close(pipes[x][1]);
    }
    free(pipes);
  }

  for (int i = 0; i < count; i++)
    wait(NULL);
}
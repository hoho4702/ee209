#include <fcntl.h> /* File related */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>    /* getenv(),setenv(), unsetenv()*/
#include <sys/types.h> /* pid_t */
#include <sys/wait.h>  // For waitpid
#include <signal.h>
#include <assert.h> /* SIG_UNBLOCK*/

/* Custom Libraries */
#include "lexsyn.h"
#include "util.h"

/* Function to change the directory */
void changeDirectory(DynArray_T oTokens, char *ish)
{
  int length = DynArray_getLength(oTokens);

  if (length > 2)
  {
    fprintf(stderr, "%s: cd takes one parameter\n", ish);
    return;
  }
  else if (length == 2)
  { /* Get the directory from the second token */
    char *dir = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
    if (chdir(dir) != 0)
    {
      perror(ish);
      return;
    }
  }
  else
  { /* No parameter provided, change to HOME directory */
    char *home = getenv("HOME");
    if (home == NULL)
    { /* Home enviroment variable not set */
      return;
    }
    if (chdir(home) != 0)
    {
      perror(ish);
      return;
    }
  }
}

/* Function to set shell environment */
void setEnvironment(DynArray_T oTokens, char *ish)
{
  char *varName, *varValue; // variable name, value
  int length;               // Number of tokens
  length = DynArray_getLength(oTokens);

  /* Check correctness of arguments */
  if (length <= 1 || length > 3)
  {
    fprintf(stderr, "%s: setenv takes one or two parameters\n", ish);
    return;
  }

  /* Get the variable name from the second token */
  varName = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;

  if (length == 2)
  { /* variable of an empty value */
    if (setenv(varName, "", 1) != 0)
    {
      perror(ish);
    }
  }
  else if (length == 3)
  { /* Variable value given */
    varValue = ((struct Token *)DynArray_get(oTokens, 2))->pcValue;

    /* Set or modify the environment variable */
    if (setenv(varName, varValue, 1) != 0)
    {
      perror(ish);
    }
  }
}

/* Function to unset shell environment */
void unSetEnvironment(DynArray_T oTokens, char *ish)
{
  char *varName; // Variable name
  int length;    // Number of tokens
  length = DynArray_getLength(oTokens);

  /* Check correctness of arguments */
  if (length <= 1 || length > 2)
  {
    fprintf(stderr, "%s: unsetenv takes one parameter\n", ish);
    return;
  }

  /* Get the variable name from the second token */
  varName = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;

  // unset the environment variable
  if (unsetenv(varName) != 0)
  {
    return; // ignore if variable does not exist.
  }
}

void exitHandle(DynArray_T oTokens, char *ish)
{
  int length = DynArray_getLength(oTokens);

  /* Check correctness of arguments */
  if (length != 1)
  {
    fprintf(stderr, "%s: exit does not take any parameters\n", ish);
    return;
  }
  fflush(NULL);
  exit(EXIT_SUCCESS);
}

/* Function handler for output redirection */
int handleOutputRedirection(DynArray_T oTokens, int name_idx, char *ish)
{
  char *fileName = ((struct Token *)DynArray_get(oTokens, name_idx))->pcValue;
  int fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0)
  {
    perror(ish);
    fflush(NULL);
    return -1;
  }

  /* Redirect standard output to the opened file */
  if (dup2(fd, STDOUT_FILENO) < 0)
  {
    perror(ish);
    fflush(NULL);
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

/* Function handler for input redirection */
int handleInputRedirection(DynArray_T oTokens, int name_idx, char *ish)
{
  char *fileName = ((struct Token *)DynArray_get(oTokens, name_idx))->pcValue;
  // Open the file for reading
  int fd = open(fileName, O_RDONLY);
  if (fd < 0)
  {
    perror(ish);
    fflush(NULL);
    return -1;
  }

  // Redirect standard input to the opened file
  if (dup2(fd, STDIN_FILENO) < 0)
  {
    perror(ish);
    fflush(NULL);
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

/* Function handler to execute a single command */
void executeSingleCommand(DynArray_T oTokens, char *ish)
{
  pid_t pid;
  int status;
  fflush(NULL);
  pid = fork();

  if (pid == 0)
  { /* Child process */
    int argCount = 0;
    int length = DynArray_getLength(oTokens);

    /* SIGINT and SIGQUIT are default for child */
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);

    /* Allocate memory for arguments */
    char **args = malloc((length + 1) * sizeof(char *));
    if (args == NULL)
    {
      perror("malloc");
      exit(EXIT_FAILURE);
    }
    /* Construct the arguments, input and output
       redirections for the command to be executed */
    for (int i = 0; i < DynArray_getLength(oTokens); i++)
    {
      struct Token *token = DynArray_get(oTokens, i);

      if (token->eType == TOKEN_REDOUT)
      {
        if (handleOutputRedirection(oTokens, i + 1, ish) < 0)
        {
          exit(EXIT_FAILURE);
        }
        i++; /* Skip the file name after '>' */
      }
      else if (token->eType == TOKEN_REDIN)
      {
        if (handleInputRedirection(oTokens, i + 1, ish) < 0)
        {
          exit(EXIT_FAILURE);
        }
        i++; /* Skip the file name after '<' */
      }
      else
      {
        args[argCount++] = token->pcValue;
      }
    }

    /* Null terminate the argument */
    args[argCount] = NULL;

    /* Execute the command */
    execvp(args[0], args);

    /* Only reached if execvp fails */
    perror(args[0]);
    free(args);
    exit(EXIT_FAILURE);
  }
  else if (pid > 0)
  { // parent
    if (waitpid(pid, &status, 0) < 0)
    {
      perror(ish);
    }
  }
}

/* Function to execute piped commands */
void executePipedCommand(DynArray_T oTokens, char *ish)
{
  int numCommands = countPipe(oTokens); // Get the number of pipes
  int pipefds[2 * numCommands];         // Array to store pipe file descriptors
  pid_t pid;
  int status;
  int start = 0, j = 0;

  /* Save the original stdin and stdout file descriptors */
  int original_stdin = dup(STDIN_FILENO);
  int original_stdout = dup(STDOUT_FILENO);

  // Create pipes for each pair of commands
  for (int c = 0; c < numCommands; c++)
  {
    if (pipe(pipefds + c * 2) == -1)
    {
      perror(ish);
      exit(EXIT_FAILURE);
    }
  }

  /* Loop through each command in the pipeline */
  for (int i = 0; i <= numCommands; i++)
  {

    int argCount = 0;
    char *args[DynArray_getLength(oTokens)];

    /* Parse tokens for the current command */
    while ((j + start) < DynArray_getLength(oTokens) &&
           ((struct Token *)DynArray_get(oTokens, start + j))->eType != TOKEN_PIPE)
    {

      struct Token *token = DynArray_get(oTokens, start + j);
      if (token->eType == TOKEN_REDIN)
      { /* Applies for the first command */
        if (handleInputRedirection(oTokens, start + j + 1, ish) < 0)
        {
          exit(EXIT_FAILURE);
        }
        j++; // Skip the file name after '<'
      }
      else if (token->eType == TOKEN_REDOUT)
      { /* Applies for the last command */
        if (handleOutputRedirection(oTokens, start + j + 1, ish) < 0)
        {
          exit(EXIT_FAILURE);
        }
        j++; // Skip the file name after '>'
      }
      else
      {
        args[argCount++] = token->pcValue;
      }
      j++;
    }
    args[argCount] = NULL;

    /* Prepare for the next command */
    start += j + 1;
    j = 0; /* reset iterator */

    fflush(NULL);
    pid = fork();
    if (pid == 0)
    { // Child process

      /* SIGINT and SIGQUIT are default for child */
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);

      /* Handle piping: input from the previous pipe,
       output to the next pipe */
      if (i > 0 && dup2(pipefds[(i - 1) * 2], STDIN_FILENO) == -1)
      {
        perror("dup2 - input");
        exit(EXIT_FAILURE);
      }

      if (i < numCommands && dup2(pipefds[i * 2 + 1], STDOUT_FILENO) == -1)
      {
        perror("dup2 - output");
        exit(EXIT_FAILURE);
      }

      /* Close all pipe file descriptors for the child */
      for (int k = 0; k < 2 * numCommands; k++)
      {
        close(pipefds[k]);
      }

      /* Execute the command */
      execvp(args[0], args);
      perror(ish);
      exit(EXIT_FAILURE);
    }
  }

  /* Close all pipe file descriptors for parent */
  for (int i = 0; i < 2 * numCommands; i++)
  {
    close(pipefds[i]);
  }

  /* Wait for all child processes to finish */
  for (int i = 0; i <= numCommands; i++)
  {
    waitpid(-1, &status, 0);
  }

  /* Restore stdin and stdout */
  dup2(original_stdin, STDIN_FILENO);
  dup2(original_stdout, STDOUT_FILENO);

  /* Close the saved file descriptors */
  close(original_stdin);
  close(original_stdout);
}

static void shellHelper(const char *inLine, char *ish)
{
  DynArray_T oTokens;
  int numPipes;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  oTokens = DynArray_new(0);
  if (oTokens == NULL)
  {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }

  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck)
  {
  case LEX_SUCCESS:
    if (DynArray_getLength(oTokens) == 0)
      return;

    /* Dump lex result when DEBUG is set */
    dumpLex(oTokens);

    syncheck = syntaxCheck(oTokens);

    if (syncheck == SYN_SUCCESS)
    { /* Correct syntax */
      btype = checkBuiltin(DynArray_get(oTokens, 0));
      switch (btype)
      {
      case B_CD:
        changeDirectory(oTokens, ish);
        break;

      case B_SETENV:
        setEnvironment(oTokens, ish);
        break;

      case B_USETENV:
        unSetEnvironment(oTokens, ish);
        break;

      case B_EXIT:
        exitHandle(oTokens, ish);
        break;
      // case B_ALIAS:
      // break;

      // case B_FG:
      // break;

      // case NORMAL:
      // break;
      default: // Everything else will be treated as command
        numPipes = countPipe(oTokens);
        if (numPipes == 0)
        { /* single command */
          executeSingleCommand(oTokens, ish);
        }
        else
        { /* piped command */
          executePipedCommand(oTokens, ish);
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

/** Function to read and process the .ishrc file line by line */
void readIshrcFile(char *ish)
{
  const char *home = getenv("HOME");
  if (!home)
    return;

  /* Construct the path to the .ishrc */
  char ishrcPath[MAX_LINE_SIZE];
  snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", home);

  /* Try to open the .ishrc file */
  FILE *file = fopen(ishrcPath, "r");
  if (!file)
    return;

  /* Read and process each line from the file */
  char line[MAX_LINE_SIZE + 2];
  while (fgets(line, sizeof(line), file))
  {
    printf("%% %s", line);

    /* Pass the line to shellHelper for interpretation */
    shellHelper(line, ish);
  }

  fclose(file);
}

/** SIGNAL RELATED */
int q = 0; // Track SIGQUIT presses

void alrmHandler(int sig)
{ // SIGQUIT not pressed within 5 seconds
  q = 0;
  alarm(0);
}

static void quitHandler(int isig)
{
  q++;
  if (q > 1)
  { // SIGQUIT received twice with in 5 seconds
    exit(EXIT_SUCCESS);
  }
  else if (q == 1)
  {
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5); // Set the alarm timeout
  }
}

int main(int argc, char *argv[])
{
  char *ish = argv[0]; /* For error printing */

  /* Make sure that SIGINT, SIGQUIT,
     and SIGALRM signals are not blocked. */
  sigset_t s;
  sigaddset(&s, SIGALRM);
  sigaddset(&s, SIGQUIT);
  sigaddset(&s, SIGINT);
  int i = sigprocmask(SIG_UNBLOCK, &s, NULL);
  assert(i == 0);

  /* Signals */
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, quitHandler);
  signal(SIGALRM, alrmHandler);

  /* Read and process the .ishrc file */
  readIshrcFile(ish);

  char acLine[MAX_LINE_SIZE + 2]; /* Buffer to read user input */
  while (1)
  {
    /* Print the prompt */
    fprintf(stdout, "%% ");
    fflush(stdout);

    /* Read the input line */
    if (!fgets(acLine, MAX_LINE_SIZE, stdin))
    { // Ctrl + D
      printf("\n");
      exit(EXIT_SUCCESS);
    }

    /* Process the input line */
    shellHelper(acLine, ish);
  }
  return 0;
}
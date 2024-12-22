/*
20200404 SeungYeop Yu
This program should be a minimal but realistic interactive Unix shell.
The following procedure is done
1. Read command input from stdin
2. Analyze command input to tokens
3. Detect and handle errors if there are any
4. Execute given command properly
*/
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/signal.h>
#include <unistd.h>
#include <assert.h>
#include <sys/wait.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

/*
function_setenv gets DynArray_T type as parameter, and it returns 
nothing. If the DynArray_getLength(length) value of parameter is larger
than 3, it prints the message which refers error to stderr. Or if it
fails and returnvalue is -1, it prints error message to stderr. Rest
of the cases do the function of setenv.
*/
void function_setenv(DynArray_T oTokens)
{
  assert(oTokens);
  int returnvalue;
  if (DynArray_getLength(oTokens) == 3)
  {
    returnvalue = setenv(((struct Token*)DynArray_get(oTokens, 1))->pcValue,
    ((struct Token*)(DynArray_get(oTokens, 2)))->pcValue, 1);
  }
  else if (DynArray_getLength(oTokens) == 2)
  {
    returnvalue = setenv(((struct Token*)DynArray_get(oTokens, 1))->pcValue,
    "", 1);
  }
  else
  {
    errorPrint("setenv takes one or two parameters", FPRINTF);
  }

  if (returnvalue == -1) errorPrint(NULL, PERROR);
}

/*
function_unsetenv gets DynArray_T type as parameter, and it returns
nothing. If the DynArray_getLength(length) value of parameter is larger
than 2, it prints the message which refers to error to stderr. Or if it
fails and returnvalue is -1, it prints error message to stderr. Rest
of the cases do the function of unsetenv. 
*/
void function_unsetenv(DynArray_T oTokens)
{
  assert(oTokens);
  int returnvalue;
  if (DynArray_getLength(oTokens) == 2)
  {
    returnvalue = unsetenv(((struct Token *)DynArray_get(oTokens, 1))->pcValue);
  }
  else errorPrint("unsetenv takes one parameter", FPRINTF);

  if (returnvalue == -1) errorPrint(NULL, PERROR);
}

/*
function_cd gets DynArray_T type as parameter, and it returns nothing.
If the DynArray_getLength(length) value of parameter is larger than 2,
it prints the message which refers to error to stderr. Or if function
of chdir fails and returnvalue is -1, it prints error message to 
stderr. Rest of the cases do the function of cd.
*/
void function_cd(DynArray_T oTokens)
{
  assert(oTokens);
  int returnvalue;
  if (DynArray_getLength(oTokens) == 1)
  {
    returnvalue = chdir(getenv("HOME"));
  }
  else if (DynArray_getLength(oTokens) == 2)
  {
    returnvalue = chdir(((struct Token *)DynArray_get(oTokens, 1))->pcValue);
  }
  else errorPrint("cd takes one parameter", FPRINTF);

  if (returnvalue == -1) errorPrint(NULL, PERROR);
}

/*
function_exit gets DynArray_T type as parameter, and it returns 
nothing. If the DynArray_getLength(length) value of parameter is larger
than 1, it prints the message which refers to error to stderr. 
Otherwise, exit the program.
*/
void function_exit(DynArray_T oTokens)
{
  assert(oTokens);
  if (DynArray_getLength(oTokens) == 1)
  {
    //printf("\n");
    DynArray_free(oTokens);
    exit(0);
  }
  else errorPrint("exit does not take any parameters", FPRINTF);
}

/*
handle_redirection function do the redirction command. If version is 0,
it do the < command. If version is 1, it do the > command. If open
function fails, it prints the message which refers to error to stderr.
It gets one DynArray_T type, two int type as parameters, and return
nothing. 
*/
void handle_redirection(DynArray_T oTokens, int i, int version)
{
  int file_d;
  const char *FILE_NAME = ((struct Token*)DynArray_get(oTokens, i+1))->pcValue;

  if (version == 0)
  {
    file_d = open(FILE_NAME, O_RDONLY);
  }
  else file_d = open(FILE_NAME, O_RDWR | O_CREAT | O_TRUNC, 0600);

  if (file_d >= 0)
  {
    dup2(file_d, version);
    close(file_d);
  }
  else
  {
    errorPrint(NULL, PERROR);
    DynArray_free(oTokens);
    exit(1);
  }
}

/*
function_rest implements the rest of the command. (Not a built-in
command) If failure exists inside, it prints the message which refers
to error to stderr. It gets DynArray_T type as parameter, and returns
nothing.
*/
void function_rest(DynArray_T oTokens)
{
  assert(oTokens);
  pid_t pid;
  int status1;
  int status2;

  fflush(NULL);

  if ((pid = fork()) == 0)
  {
    //setup
    void (*p1)(int);
    void (*p2)(int);
    p1 = signal(SIGINT, SIG_DFL);
    assert(p1 != SIG_ERR);
    p2 = signal(SIGQUIT, SIG_DFL);
    assert(p2 != SIG_ERR);

    //execute
    char *argv[10] = {NULL};
    int i, j, index = 0;
    int pipe_fd[2] = {-1, -1};

    for (i = 0; i < DynArray_getLength(oTokens); i++)
    {
      struct Token *token = (struct Token*)DynArray_get(oTokens, i);

      if (token->pcValue == NULL)
      {
        if (token->eType == TOKEN_REDIN)
        {
          handle_redirection(oTokens, i++, 0);
        }
        else if (token->eType == TOKEN_REDOUT)
        {
          handle_redirection(oTokens, i++, 1);
        }
        else if (token->eType == TOKEN_PIPE)
        {
          pid_t pipe_pid;

          if (pipe(pipe_fd) == -1)
          {
            DynArray_free(oTokens);
            assert(0);
          }
          fflush(NULL);

          if ((pipe_pid = fork()) == 0)
          {
            close(pipe_fd[0]);
            dup2(pipe_fd[1], 1);
            close(pipe_fd[1]);
            break;
          }
          else if (pipe_pid > 0)
          {
            pipe_pid = wait(&status1);
            if(pipe_pid == -1)
            {
              DynArray_free(oTokens);
              assert(0);
            }
            close(pipe_fd[1]);
            dup2(pipe_fd[0], 0);
            close(pipe_fd[0]);

            index = i + 1;
            for (j = 0; j < 10; j++)
            {
              argv[j] = NULL;
            }
          }
          else
          {
            DynArray_free(oTokens);
            assert(0);
          }
        }
      }
      
      else argv[i - index] = token->pcValue;
    }

    char *somepgm = ((struct Token*)DynArray_get(oTokens, index))->pcValue;
    DynArray_free(oTokens);
    execvp(somepgm, argv);
    errorPrint(somepgm, PERROR);
    exit(1);
  }

  else if (pid > 0)
  {
    pid = wait(&status2);
    assert(pid != -1);
  }

  else assert(0);
}

/*
shellHelper function gets const char *(pointer) as parameter,
and returns nothing. In other words, it gets the full sentence
of the command. This function makes token, and analyze with
it. If the command entered in is not valid, then it prints the error
message to stderr. Otherwise, commands are executed.
*/
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
        if (btype == B_SETENV) function_setenv(oTokens);
        else if (btype == B_USETENV) function_unsetenv(oTokens);
        else if (btype == B_CD) function_cd(oTokens);
        else if (btype == B_EXIT) function_exit(oTokens);
        else function_rest(oTokens);
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

static void QuitAgain_handler(int iSig); // declaration to use
static void QuitNotAgain_handler(int iSig); // declaration to use

/*
Quit_handler gets int type as parameter, and returns nothing. 
It is called when Ctrl+\ is entered. Firstable, it prints the message
about the termination(exit), and call alarm signal with 5 seconds.
With the information whether Ctrl+\ is one more entered, it can call
the QuitAgain_handler, or QuitNotAgain_handler. If it fails, it print
error to stderr.
*/
static void Quit_handler(int iSig)
{
  printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  fflush(stdout); //fflush(stdout);

  void (*p1)(int);
  void (*p2)(int);
  p1 = signal(SIGQUIT, QuitAgain_handler);
  assert(p1 != SIG_ERR);
  p2 = signal(SIGALRM, QuitNotAgain_handler);
  assert(p2 != SIG_ERR);

  alarm(5);
}

/*
If the SIGQUIT is one more called in Quit_handler, it gets into the
QuitAgain_handler. It gets int type as parameter, and returns nothing.
Exit is done at the end, and SIGQUIT, SIGALRM signal gets into original
state. If it fails, it prints error to stderr.
*/
static void QuitAgain_handler(int iSig)
{
  void (*p1)(int);
  void (*p2)(int);
  p1 = signal(SIGQUIT, SIG_DFL);
  assert(p1 != SIG_ERR);
  p2 = signal(SIGALRM, SIG_DFL);
  assert(p2 != SIG_ERR);

  exit(0);
}

/*
If the SIGQUIT is not one more called in Quit_handler, it gets into the
QuitNotAgain_handler. It gets int type as parameter, and returns
nothing. Nothing is done inside, and SIGQUIT is treated as
Quit_handler, and SIGALRAM gets into original state. If it fails,
it prints error to stderr.
*/
static void QuitNotAgain_handler(int iSig)
{
  void (*p1)(int);
  void (*p2)(int);
  p1 = signal(SIGQUIT, Quit_handler);
  assert(p1 != SIG_ERR);
  p2 = signal(SIGALRM, SIG_DFL);
  assert(p2 != SIG_ERR);
}

/*
main function read the command in ./ishrc file. It gets the command,
and execute the command. It read command of input to stdin stream, and
execute the command. It gets int type and char pointer array type as 
parameters. First parameter is number of element. First element is
name of executable file. It returns 0.
*/
int main(int argc, char** argv) {
  /* TODO */
  sigset_t sSet;
  sigemptyset(&sSet);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  void (*p1)(int);
  void (*p2)(int);
  p1 = signal(SIGINT, SIG_IGN);
  assert(p1 != SIG_ERR);
  p2 = signal(SIGQUIT, Quit_handler);
  assert(p2 != SIG_ERR);

  errorPrint(argv[0], SETUP);
  char acLine[MAX_LINE_SIZE + 2];
  char buffer[MAX_LINE_SIZE + 2];

  char *cur_directory = get_current_dir_name();
  chdir(getenv("HOME"));
  FILE *ishrc = fopen(".ishrc", "r"); // relative pathname
  
  if (ishrc != NULL)
  {
    while (fgets(buffer, MAX_LINE_SIZE, ishrc) != NULL)
    {
      fprintf(stdout, "%% ");
      fprintf(stdout, "%s", buffer);
      fflush(stdout);
      shellHelper(buffer);
    }
    fclose(ishrc);
  }
  chdir(cur_directory);

  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }

  p1 = signal(SIGINT, SIG_DFL);
  assert(p1 != SIG_ERR);
  p2 = signal(SIGQUIT, SIG_DFL);
  assert(p2 != SIG_ERR);

  return 0;
}
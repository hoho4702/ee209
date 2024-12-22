#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "lexsyn.h"
#include "util.h"
#include <sys/wait.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>  

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static volatile sig_atomic_t quit_received = 0;
static int redirection_handler(DynArray_T oTokens);
static void exe_commands(DynArray_T oTokens);
static char ***command_division(DynArray_T oTokens, int cmdcount);
static void setSignals(void);
static void restoreChildSignals(void);
static void sigquit_Handler(int isig);
static void sigalrm_Handler(int isig);

static void sigalrm_Handler(int isig) {
    //5 second later move to default
    quit_received = 0;
}
static void sigquit_Handler(int isig) {
    if (quit_received == 0) { // before receving first sigquit, wait 5 second for next
        printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        quit_received = 1;
        alarm(5);
    } else {
        //second sigquit received within 5 second
        exit(EXIT_SUCCESS);
    }
}

static void restoreChildSignals(void) {
    //restore the childs signals to default value
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
}

static void setSignals(void) {
   //ignore the sigint in parents process, change the sigquit, sigalrm handler
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, sigquit_Handler);
    signal(SIGALRM, sigalrm_Handler);
}

static int redirection_handler(DynArray_T oTokens){//always called by child process
// handle the redirection
  FILE *inputfp = NULL;
  FILE *outputfp = NULL;
  int i = 0;

  while (i < DynArray_getLength(oTokens)){
    struct Token *token = DynArray_get(oTokens, i);
    // if token '<'
    if (token->eType == TOKEN_REDIN) {  
      struct Token *token_file = DynArray_get(oTokens, i+1);
      //open file with read mode 
      inputfp = fopen(token_file->pcValue, "r");
      if (inputfp == NULL) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
      // change stdin to file
      if (dup2(fileno(inputfp), 0) == -1) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
      // remove <,file
      fclose(inputfp);
      DynArray_removeAt(oTokens, i+1);
      DynArray_removeAt(oTokens, i);
    }
    // if token is >
    else if (token->eType == TOKEN_REDOUT) {
      struct Token *token_file = DynArray_get(oTokens, i+1);
      //open the output file
      outputfp = fopen(token_file->pcValue, "w");
      if (outputfp == NULL) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
      //change its mode to 600
      if (chmod(token_file->pcValue, 0600) == -1){
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
      //change stdout to file
      if (dup2(fileno(outputfp), 1) ==-1) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
      //close the file and remove >, file
      fclose(outputfp);
      DynArray_removeAt(oTokens, i+1);
      DynArray_removeAt(oTokens, i);
    }
    else {//keep searching for next token
      i++;
    }
  }
  return 0;
}

static char ***command_division(DynArray_T oTokens, int cmdcount){
  //store the command divide by pipe
  char ***cmds = (char ***)calloc(cmdcount, sizeof(char **));
  if (cmds == NULL){
    errorPrint(NULL, PERROR);
    return NULL;
  }

  int start =0, index =0;
  int length;
  int total_length = DynArray_getLength(oTokens);
  for (int i= 0; i <total_length; i++){
    struct Token *temp = DynArray_get(oTokens, i);
    //when token is pipe
    if (temp->eType == TOKEN_PIPE){
      length = i - start;
      //stores the commands next to pipe in cmds[index]
      cmds[index] = (char **)calloc(length + 1, sizeof(char *));
      if (cmds[index] == NULL){
        errorPrint(NULL, PERROR);
        return NULL;
      }
      // individual commands tokens are stored in comds[index][i]
      for (int j = 0; j < length; j++){
        struct Token *list = DynArray_get(oTokens, start + j);
        cmds[index][j] = list->pcValue;
      }
      cmds[index][length] = NULL;
      //searching for next pipe & following command
      index++;
      start= i +1;
    }
  }

  //handle the last section
  length = total_length - start;
  cmds[index] = (char **)calloc(length + 1, sizeof(char *));
  if (cmds[index] == NULL){
    errorPrint(NULL, PERROR);
    return NULL;
  }
  for (int j = 0; j < length; j++)
  {
    struct Token *list = DynArray_get(oTokens, start + j);
    cmds[index][j] = list->pcValue;
  }
  cmds[index][length] = NULL;

  return cmds;
}

static void exe_commands(DynArray_T oTokens){//execute the command
  int pipecount = countPipe(oTokens);
  int cmdcount = pipecount + 1;

  if (cmdcount == 1) // no pipe exist
  {
    fflush(NULL);
    pid_t pid = fork();
    if (pid == -1) {
      errorPrint(NULL, PERROR);
      return;
    }
    else if (pid == 0) {
      //handle redirection inside child process (with default signals)
      restoreChildSignals();
      if (redirection_handler(oTokens) == -1) {
        exit(EXIT_FAILURE);
      }
      int length = DynArray_getLength(oTokens);
      char **argv = (char **)calloc(length + 1, sizeof(char *));
      if (argv == NULL) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
      //set the argv in series since to pipe and execute
      for (int i = 0; i < length; i++)
      {
        struct Token *token_argv = DynArray_get(oTokens, i);
        argv[i] = token_argv->pcValue;
      }
      argv[length] = NULL;
      execvp(argv[0], argv);
      errorPrint(argv[0], PERROR);
      exit(EXIT_FAILURE);
    } 
    else {
      //parent wait for child
      int status;
      if (waitpid(pid, &status, 0) == -1) {
        errorPrint(NULL, PERROR);
      }
    }
  }
  else
  {
    //pipe existing so divide the section of command
    char ***cmds = command_division(oTokens, cmdcount);
    if (cmds == NULL){
      errorPrint(NULL, PERROR);
      return;
    }

    int *pipes = (int *)malloc(sizeof(int) * 2 * pipecount);
    if (pipes == NULL){
      errorPrint(NULL, PERROR);
      return;
    }

    // Create a pipe for each process pair and store its descriptors in the pipes array 2*i for read, 2*i+1 for write
    for (int i = 0; i < pipecount; i++){
      if (pipe(&pipes[2 * i]) == -1){
        errorPrint(NULL, PERROR);
        free(pipes);
        return;
      }
    }
    //pid for child processing
    pid_t *pids = (pid_t *)malloc(sizeof(pid_t) * cmdcount);
    if (pids == NULL)
    {
      errorPrint(NULL, PERROR);
      free(pipes);
      return;
    }
    // Create a new dynamic array to hold tokens for the current command section
    for (int i = 0; i < cmdcount; i++)
    {
      DynArray_T sectionTokens = DynArray_new(0);
      if (sectionTokens == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        free(pids);
        free(pipes);
        for (int k=0; k<cmdcount; k++) free(cmds[k]);
        free(cmds);
        return;
      }
      //Convert cmds[i] to tokens for redirection
      for (int j = 0; cmds[i][j] != NULL; j++) {
        struct Token *t = makeToken(TOKEN_WORD, cmds[i][j]);
        if (t == NULL || !DynArray_add(sectionTokens, t)){//if token generation fails or addition of token fails
          errorPrint(NULL, PERROR);
          exit(EXIT_FAILURE);
        }
      }
      fflush(NULL);
      pid_t pid = fork();
      if (pid < 0) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
      }
      else if (pid == 0) {
        //child process to handle redirection and pipe (with default signal)
        restoreChildSignals();
        if (i > 0){// If it isn't the first, redirect stdin from the previous pipe's read
          if (dup2(pipes[2 *(i - 1)], STDIN_FILENO) == -1){
            errorPrint(NULL, PERROR);
            exit(EXIT_FAILURE);
          }
        }

        if (i < cmdcount - 1){// If isn't the last, redirect stdout to the current pipe's write
          if (dup2(pipes[2 * i + 1], STDOUT_FILENO) == -1){
            errorPrint(NULL, PERROR);
            exit(EXIT_FAILURE);
          }
        }
        // close the pipes
        for (int j = 0; j < 2 * pipecount; j++)
        {
          close(pipes[j]);
        }

        // handle redirection
        if (redirection_handler(sectionTokens) == -1) {
          exit(EXIT_FAILURE);
        }
        // Prepare the argument list for execvp().
        int section_length = DynArray_getLength(sectionTokens);
        char **argv = (char**)calloc(section_length+1, sizeof(char*));
        if (argv == NULL) {
          errorPrint(NULL, PERROR);
          exit(EXIT_FAILURE);
        }
        // arrange the tokens to array argv to ready to execute
        for (int k=0; k<section_length; k++) {
          struct Token *tk = DynArray_get(sectionTokens, k);
          argv[k] = tk->pcValue;
        }
        argv[section_length] = NULL;
        execvp(argv[0], argv);
        errorPrint(argv[0], PERROR);
        exit(EXIT_FAILURE);
      }
      else{
        // Parent process store the child PID and free resources for the current section
        pids[i] = pid;
        DynArray_map(sectionTokens, freeToken, NULL);
        DynArray_free(sectionTokens);
      }
    }
    // Close all pipe file descriptors in the parent process.
    for (int i = 0; i < 2 * pipecount; i++){
      close(pipes[i]);
    }
    // Wait for all child processes to finish.
    for (int i = 0; i < cmdcount; i++){
      int status;
      if (waitpid(pids[i], &status, 0) == -1){
        errorPrint(NULL, PERROR);
      }
    }
    //free the elements
    free(pipes);
    for (int i = 0; i < cmdcount; i++) free(cmds[i]);
    free(cmds);
    free(pids);
  }
}

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
  switch (lexcheck)
  {
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
        int tokenlength = DynArray_getLength(oTokens);
        const char *homedir = getenv("HOME");

        switch (btype){
          case B_CD:
            switch (tokenlength){
              case 1://no other parameter, change to home directory
                if (homedir != NULL && chdir(homedir) == -1)
                  errorPrint("No such file or directory", FPRINTF);
                break;
              case 2:{// cd directory , move to directory
                struct Token *tokendir = DynArray_get(oTokens, 1);
                const char *dir = tokendir->pcValue;
                if (chdir(dir) == -1) {//cd fail for no directory exist
                  errorPrint("No such file or directory", FPRINTF);
                }
                break;
              }
              default:// too many parameter
                errorPrint("cd takes one parameter", FPRINTF);
                exit(EXIT_FAILURE);
            }
            break;

          case B_EXIT:
            switch (tokenlength){
              case 1://(exit())
                DynArray_free(oTokens);
                exit(EXIT_SUCCESS);
              default://no more parameter for exit)
                errorPrint("exit does not take any parameters", FPRINTF);
                exit(EXIT_FAILURE);
            }
            break;

          case B_SETENV:
            switch (tokenlength){
              case 1://need more parameter
                errorPrint("setenv takes one or two parameters", FPRINTF);
                exit(EXIT_FAILURE);
              case 2:{//setenv Var, handle this
                struct Token *tokenVar = DynArray_get(oTokens, 1);
                const char *Var = tokenVar->pcValue;
                if (setenv(Var, "", 1) == -1){
                  errorPrint(NULL, PERROR);
                }
                break;
              }
              case 3:{// setenv Var Value, handle this
                struct Token *tokenVar = DynArray_get(oTokens, 1);
                const char *Var = tokenVar->pcValue;
                struct Token *tokenValue = DynArray_get(oTokens, 2);
                const char *Value = tokenValue->pcValue;
                if (setenv(Var, Value, 1) == -1){
                  errorPrint(NULL, PERROR);
                }
                break;
              }
              default://too many parameter
                errorPrint("setenv takes one or two parameters", FPRINTF);
                exit(EXIT_FAILURE);
            }
            break;

          case B_USETENV:
            switch (tokenlength){
              case 1:// need one more parameter
                errorPrint("unsetenv takes one parameters", FPRINTF);
                exit(EXIT_FAILURE);
              case 2:{//handle unsetenv Var
                struct Token *tokenVar = DynArray_get(oTokens, 1);
                const char *Var = tokenVar->pcValue;
                if (unsetenv(Var) == -1){
                  errorPrint(NULL, PERROR);
                }
                break;
              }
              default://too many parameter
                errorPrint("unsetenv takes one parameter", FPRINTF);
                exit(EXIT_FAILURE);
            }
            break;

          default:// for case token doesnot meet any built-in commands, handle the behavior of redirection, pipe, and the other commands thru this
            
            exe_commands(oTokens);
            break;
        }
      }
      else{
      /* syntax error cases */
        if (syncheck == SYN_FAIL_NOCMD)
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
      }
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

int main(int argc, char *argv[]) {
  /* TODO */
  //set the starting condition
  errorPrint(argv[0], SETUP);
  setSignals();
  char acLine[MAX_LINE_SIZE + 2];
  char homepath[MAX_LINE_SIZE + 2];
  const char *home = getenv("HOME");
  // copy the address of home directory
  strcpy(homepath, home);
  strcat(homepath, "/.ishrc");
  FILE *fp;
  // use .ishrc as stdin and execute the contents
  if ((fp = fopen(homepath, "r"))){
    while (fgets(acLine, sizeof(acLine), fp)){
      fprintf(stdout, "%% %s", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
    fclose(fp);
  }
  while (1)
  {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
}

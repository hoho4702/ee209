#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "token.h"
#include "dynarray.h"
#include "lexsyn.h"
#include "util.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <assert.h>
#include <unistd.h>
#include <pwd.h>
#include <dirent.h>
#include <signal.h>
#include <fcntl.h>

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

pid_t pid;
int signal_quit = 0;

/*----------------------------------------------------------------------------------*/
/* Execute command; input, output redirection */
int execution(void **args, int size, int check, int input_fd, int output_fd){
    struct Token *t;
    char *argsArray[1024] = {0};
    int old_fd[2];
    int new_fd[2];
    int status;

    for (int i = 0; i < size; i++) {
      t = args[i];
      argsArray[i] = t->pcValue;
    }

    old_fd[0] = dup(0);  
    old_fd[1] = dup(1);
    
    if (pipe(new_fd) == -1) {
        perror("pipe");
        return 0;
    }
    
  fflush(NULL);

  pid = fork(); 
  assert(pid >= 0);
  if (pid == 0){
    /* Child Process */ 
    /* check redirection on input */ 

    if (input_fd > 0) {
      if (dup2(input_fd, 0) == -1) {
        perror("dup2 input_fd");
        exit(EXIT_FAILURE);
      }
      close(input_fd);
    }  
    close(new_fd[0]);
    
    /* check redirection on output */ 
    int output_target_fd;

    if (output_fd > 0) {
      output_target_fd = output_fd;
    } else if (check) {
      output_target_fd = old_fd[1];
    } else {
      output_target_fd = new_fd[1];
    }

    if (dup2(output_target_fd, 1) == -1) {
      perror("dup2 output_fd");
      exit(EXIT_FAILURE);
    }

    close(old_fd[0]);
    close(old_fd[1]);
    close(new_fd[1]);

    fflush(NULL);

    /* set process group and execution */
    if (setpgid(0, 0) == -1) {
      perror("setpgid");
      exit(EXIT_FAILURE);
    }
    
    if (execvp(argsArray[0], argsArray) == -1) {
      perror(argsArray[0]);
      exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);

  } else if (pid > 0) {  
    /* Parent process */
    /* Wait for the child process */
    wait(NULL);

    close(new_fd[1]);

    /* reset to original address */ 
    dup2(old_fd[0], 0);
    dup2(old_fd[1], 1);

    close(old_fd[0]);
    close(old_fd[1]);

    if (check == 0) return new_fd[0];
    close(new_fd[0]);
    return 0;
  }
}
/*----------------------------------------------------------------------------------*/
/* cd command execution */
void cd_exec(DynArray_T oTokens){
  int len = DynArray_getLength(oTokens);
  struct Token *t;
  if(len > 2) errorPrint("cd takes one parameter", FPRINTF);

  t = DynArray_get(oTokens, 1);
  if(chdir(t->pcValue) != 0) {
    errorPrint("No such file or directory", FPRINTF);
  }
}

/* setenv command execution */
void setenv_exec(DynArray_T oTokens){
  int len = DynArray_getLength(oTokens);
  struct Token *t;
  int set_success;

  if(len < 2 || len > 3) {
    errorPrint("setenv takes one or two parameters", FPRINTF);
  }
  if (len == 2) {
    set_success = setenv((t=DynArray_get(oTokens,1))->pcValue, "", 1);
  }
  if (len == 3) {
    set_success = setenv((t=DynArray_get(oTokens,1))->pcValue, 
      (t=DynArray_get(oTokens,2))->pcValue, 1);
  }
  if (set_success != 0) {
    errorPrint("Invalid argument", FPRINTF);
  }
}

/* unsetenv command execution */
void unsetenv_exec(DynArray_T oTokens){
  int len = DynArray_getLength(oTokens);
  struct Token *t;
  if(len != 2) errorPrint("unsetenv takes one parameter", FPRINTF);

  int unset_success = unsetenv((t=DynArray_get(oTokens,1))->pcValue);
  if(unset_success != 0) {
    errorPrint("Invalid argument", FPRINTF);
  }
}

/* normal command excution; according to token type */
void normal_exec(DynArray_T oTokens) {
  struct Token *token;
  struct Token *path;
  int input_fd = 0;
  int output_fd = 0;
  void **args;

  for (int i = 0; i < DynArray_getLength(oTokens); i++){
    token = DynArray_get(oTokens, i);

    /* Redirection: "<" or ">" */
    if (token->eType == TOKEN_REDIN || token->eType == TOKEN_REDOUT){   
        token = DynArray_removeAt(oTokens, i);      // delete "<" or ">" token
        path = DynArray_removeAt(oTokens, i);      // delete file path

        /* redirect "<" */
        if (token->eType == TOKEN_REDIN) {      
          input_fd = open(path->pcValue, O_RDONLY);
        }
        /* redirect ">" */
        else {                            
          output_fd = open(path->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        }
        freeToken(token, token);
        freeToken(path, path);
        if (input_fd == -1 || output_fd == -1) {
            errorPrint("No such file or directory", FPRINTF);
            return;
        }
        i--;
    }
    /* Pipe token: "|" */
    else if (token->eType == TOKEN_PIPE){            
        args = (void **)calloc(i + 1, sizeof(void *));
        assert(args != NULL);
        for(int k = 0; k < i; k++){
          args[k] = (struct Token*) DynArray_get(oTokens, i);
        }
        args[i] = NULL;

        input_fd = execution(args, i, 0, input_fd, 0);
        output_fd = 1;
        for (int j = i; j >= 0; j--){
            token = DynArray_removeAt(oTokens, j);
            freeToken(token, token);
        }
        i--;
        free(args);
    }
  }
  args = (void **)calloc(DynArray_getPhysLength(oTokens) + 1, sizeof(void *));
  DynArray_toArray(oTokens, args);
  execution(args, DynArray_getLength(oTokens), 1, input_fd, output_fd);

  free(args);
  return;
}

/*----------------------------------------------------------------------------------*/
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
        switch(btype){     
          case B_CD:
            cd_exec(oTokens);
            break;
          case B_FG:
            break;
          case B_EXIT:
            DynArray_map(oTokens, freeToken, NULL);
            DynArray_free(oTokens);
            exit(EXIT_SUCCESS); 
          case B_SETENV:
            setenv_exec(oTokens);
            break;
          case B_USETENV:
            unsetenv_exec(oTokens);
            break;
          case B_ALIAS:
            break;
          case NORMAL:
            normal_exec(oTokens);
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
      DynArray_map(oTokens, freeToken, NULL);
      DynArray_free(oTokens);
      exit(EXIT_FAILURE);
  }
  DynArray_map(oTokens, freeToken, NULL);
  DynArray_free(oTokens);
}

/*------------------------------------------------------------------------------------*/
/* Signal handler setting */
void SIGINT_handler(){   
  if(pid == 0)
    kill(-pid, SIGINT);
  return;
}

void SIGQUIT_handler(){    
  if(signal_quit == 1){
    if(pid == 0)
      kill(-pid, SIGQUIT);
    exit(0);
  }
  signal_quit = 1;
  printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
  alarm(5);
}

void SIGALRM_handler(){   // for handle SIGALRM
  signal_quit = 0;
}

/*------------------------------------------------------------------------------------*/
/* main function */
int main(int argc, char **argv) {
  /* signal handling */  
  signal(SIGINT, SIGINT_handler);
  signal(SIGQUIT, SIGQUIT_handler);
  signal(SIGALRM, SIGALRM_handler);

  sigset_t sSet;

  sigemptyset(&sSet);
  sigaddset(&sSet, SIGINT);
  sigaddset(&sSet, SIGQUIT);
  sigaddset(&sSet, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  const char* home = getenv("HOME");
  char* fname = "./ish";
  char* path = NULL;
  char command_buffer[MAX_LINE_SIZE + 2];
  FILE *fp = NULL;
  
  /* read and execute .ish file */
  if (home == NULL){
    errorPrint("Home variable not set", SETUP);
    errorPrint("No such file or directory", FPRINTF);
  } else {
    path = (char*) calloc(strlen(home) + strlen(fname), sizeof(char));
    assert(path != NULL);

    strcpy(path, home);
    strcat(path, "./ish");
    fp = fopen(path, "r");
    free(path);

    if (fp != NULL) {
      while(fgets(command_buffer, MAX_LINE_SIZE, fp)){
        fprintf(stdout, "%% %s", command_buffer);
        fflush(stdout);
        shellHelper(command_buffer);
        memset(command_buffer, 0, MAX_LINE_SIZE + 2);
      }
      fclose(fp);
    }
  }

  errorPrint(argv[0], SETUP);

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


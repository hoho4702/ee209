#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#ifndef SA_RESTART
#define SA_RESTART 0x10000000
#endif
#include "lexsyn.h"
#include "util.h"


/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static void realExit(){
  alarm(0);
  exit(0);
}

static void myExit(){
  write(STDOUT_FILENO, "\nType Ctrl-\\ again within 5 seconds to exit.\n", 44);
  struct sigaction sa;
  sa.sa_handler = realExit;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  if(sigaction(SIGQUIT, &sa, NULL) == -1) exit(EXIT_FAILURE);
  alarm(5);
  return;
}

static void notExit(){
  struct sigaction sa;
  sa.sa_handler = myExit;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  if(sigaction(SIGQUIT, &sa, NULL) == -1) exit(EXIT_FAILURE);
  alarm(0);
  return;
}

static void normal_operation(DynArray_T oTokens){
  int DynArrLen = DynArray_getLength(oTokens);
  char *argv[DynArrLen+1];
  int status;
  struct Token *t = DynArray_get(oTokens, 0);
  char *first_command = t->pcValue;
  int redirect_out = 0;
  int redirect_in = 0;
  int original_stdin = dup(STDIN_FILENO);
  int original_stdout = dup(STDOUT_FILENO);
  FILE *fp_red;
  int j = 0;
  for(int i = 0; i < DynArrLen; i++){
    t = DynArray_get(oTokens, i);
    if(t->eType == TOKEN_WORD && redirect_in == 0 && redirect_out == 0) {
      argv[j] = malloc(strlen(t->pcValue)+1);
      strcpy(argv[j], t->pcValue);
      j++;
    }

    if(redirect_in == 1){
      if(t->eType != TOKEN_WORD){
        fprintf(stderr, "./ish: Standard output redirection without file name\n");
        fflush(stderr);
        return;
      }
      fp_red = fopen(t->pcValue, "r");
      if(fp_red == NULL){
        fprintf(stderr, "%s: No such file or directory\n", t->pcValue);
        return;
      }
      dup2(fileno(fp_red), STDIN_FILENO);
      fclose(fp_red);
      redirect_in++;
    }
    else if(redirect_out == 1){
      if(t->eType != TOKEN_WORD){
        fprintf(stderr, "./ish: Standard output redirection without file name\n");
        fflush(stderr);
        return;
      }
      fp_red = fopen(t->pcValue, "w");
      dup2(fileno(fp_red), STDOUT_FILENO);
      fclose(fp_red);
      redirect_out++;
    }

    if(t->eType == TOKEN_REDIN) {
      redirect_in++;
    }
    else if(t->eType == TOKEN_REDOUT) {
      redirect_out++;
    }

    if(redirect_in > 2){
      fprintf(stderr, "./ish: Multiple redirection of standard input\n");
      fflush(stderr);
      return;
    }else if(redirect_out > 2){
      fprintf(stderr, "./ish: Multiple redirection of standard out\n");
      fflush(stderr);
      return;
    }
  }
  argv[j] = NULL;

  pid_t pid = fork();
  if(pid == 0){
    execvp(first_command, argv);
    fprintf(stderr, "%s: No such file or directory\n", first_command);
  }
  pid = wait(&status);

  for(int i = 0; i<DynArrLen; i++){
    free(argv[i]);
  }
  
  dup2(original_stdin, STDIN_FILENO);
  dup2(original_stdout, STDOUT_FILENO);
  close(original_stdin);
  close(original_stdout);

  return;  
}

static void stv(DynArray_T oTokens){
  struct Token *t;
  int dynLen = DynArray_getLength(oTokens);
  for(int i = 0; i < dynLen; i++){
    t = DynArray_get(oTokens, i);
    if(t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
      fprintf(stderr, "./ish: Standard input redirection without file name\n");
      return;
    }
    if(t->pcValue == NULL){
      fprintf(stderr, "./ish: setenv takes one or two parameters\n");
      return;
    }
  }

  if(dynLen>3 || dynLen<2){
    fprintf(stderr, "./ish: setenv takes one or two parameters\n");
    return;
  }
  
  t = DynArray_get(oTokens, 1);
  char *env_name = malloc(strlen(t->pcValue)+1);
  strcpy(env_name, t->pcValue);

  t = DynArray_get(oTokens, 2);
  int len = strlen(t->pcValue);
  char *env_value = malloc(len+1);
  strcpy(env_value, t->pcValue);

  if(setenv(env_name, env_value, 1) != 0) fprintf(stderr, "./ish: Can't set environment variable\n");
  free(env_name);
  free(env_value);
  return;
}

static void ustv(DynArray_T oTokens){
  struct Token *t;
  int dynLen = DynArray_getLength(oTokens);
  for(int i = 0; i < dynLen; i++){
    t = DynArray_get(oTokens, i);
    if(t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
      fprintf(stderr, "./ish: Standard input redirection without file name\n");
      return;
    }
    if(t->pcValue == NULL){
      fprintf(stderr, "./ish: unsetenv takes one parameter\n");
      return;
    }
  }

  if(dynLen != 2){
    fprintf(stderr, "./ish: unsetenv takes one parameter\n");
    return;
  }
  char *env_name = malloc(strlen(t->pcValue)+1);
  strcpy(env_name, t->pcValue);

  if(unsetenv(env_name) != 0) fprintf(stderr, "./ish: Can't destroy environment variable\n");
  free(env_name);
  return;
}

static void cd(DynArray_T oTokens){
  int arrayLen = DynArray_getLength(oTokens);
  struct Token *t;
  for(int i = 0; i<arrayLen; i++){
    t = DynArray_get(oTokens, i);
    if(t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
      fprintf(stderr, "./ish: Standard input redirection without file name\n");
      return;
    }
    if(t->pcValue == NULL){
      fprintf(stderr, "./ish: cd takes one parameter\n");
      return;
    }
  }
  if(arrayLen == 1){
    if(chdir(getenv("HOME")) != 0) fprintf(stderr, "./ish: fail to change directory\n");
    return;
  }
  if(arrayLen != 2){
    fprintf(stderr, "./ish: cd takes one parameter\n");
    return;
  }

  t = DynArray_get(oTokens, 1);
  char *dir = t->pcValue;
  if(chdir(dir) != 0) fprintf(stderr, "./ish: fail to change directory\n");
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
        switch(btype){
          case NORMAL:
            normal_operation(oTokens);
            break;
          case B_EXIT:
            exit(0);
            break;
          case B_SETENV:
            stv(oTokens);
            break;
          case B_USETENV:
            ustv(oTokens);
            break;
          case B_CD:
            cd(oTokens);
            break;
          default:
            printf("Default action\n"); 
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

int main() {
  /* TODO */
  struct sigaction sa;
  sa.sa_handler = myExit;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  if(sigaction(SIGQUIT, &sa, NULL) == -1) exit(EXIT_FAILURE);

  sa.sa_handler = notExit;
  sa.sa_flags = SA_RESTART;
  sigemptyset(&sa.sa_mask);
  if(sigaction(SIGALRM, &sa, NULL) == -1) exit(EXIT_FAILURE);

  const char *ishrc = "/.ishrc";
  char *homePath = malloc(strlen(getenv("HOME"))+strlen(ishrc)+1);
  strcpy(homePath, getenv("HOME"));
  strncat(homePath, ishrc, strlen(ishrc));
  FILE *fp1 = fopen(homePath, "r");

  char acLine[MAX_LINE_SIZE + 2];
  if(fp1){
    while(fgets(acLine, MAX_LINE_SIZE, fp1) != NULL){
      fprintf(stdout, "%% ");
      fprintf(stdout, "%s", acLine);
      fflush(stdout);

      shellHelper(acLine);
    }
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


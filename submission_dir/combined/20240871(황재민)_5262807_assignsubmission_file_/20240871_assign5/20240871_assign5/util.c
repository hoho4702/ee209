#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "dynarray.h"
#include "util.h"


void
errorPrint(char *input, enum PrintMode mode) {
  static char *ishname = NULL;

  if (mode == SETUP)
    ishname = input;
  else {
    if (ishname == NULL)
      fprintf(stderr, "[WARN] Shell name is not set. Please fix this bug in main function\n");
    if (mode == PERROR) {
      if (input == NULL)
        fprintf(stderr, "%s: %s\n", ishname, strerror(errno));
      else
        fprintf(stderr, "%s: %s\n", input, strerror(errno));
    }
    else if (mode == FPRINTF)
      fprintf(stderr, "%s: %s\n", ishname, input);
    else if( mode == ALIAS)
      fprintf(stderr, "%s: alias: %s: not found\n", ishname, input);
    else
      fprintf(stderr, "mode %d not supported in errorPrint\n", mode);
    }
}

enum BuiltinType
checkBuiltin(struct Token *t) {
  /* Check null input before using string functions  */
  assert(t);
  assert(t->pcValue);

  if (strncmp(t->pcValue, "cd", 2) == 0 && strlen(t->pcValue) == 2)
    return B_CD;
  if (strncmp(t->pcValue, "fg", 2) == 0 && strlen(t->pcValue) == 2)
    return B_FG;
  if (strncmp(t->pcValue, "exit", 4) == 0 && strlen(t->pcValue) == 4)
    return B_EXIT;
  else if (strncmp(t->pcValue, "setenv", 6) == 0 && strlen(t->pcValue) == 6)
    return B_SETENV;
  else if (strncmp(t->pcValue, "unsetenv", 8) == 0 && strlen(t->pcValue) == 8)
    return B_USETENV;
  else if (strncmp(t->pcValue, "alias" , 5) == 0 && strlen(t->pcValue) == 5) 
    return B_ALIAS;
  else
    return NORMAL;
}

int
countPipe(DynArray_T oTokens) {
  int cnt = 0, i;
  struct Token *t;

  for (i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_PIPE)
      cnt++;
  }

  return cnt;
}

/* Check background Command */
int
checkBG(DynArray_T oTokens) {
  int i;
  struct Token *t;

  for (i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    if (t->eType == TOKEN_BG)
      return 1;
  }
  return 0;
}

const char* specialTokenToStr(struct Token* psToken) {
  switch(psToken->eType) {
    case TOKEN_PIPE:
      return "TOKEN_PIPE(|)";
      break;
    case TOKEN_REDIN:
      return "TOKEN_REDIRECTION_IN(<)";
      break;
    case TOKEN_REDOUT:
      return "TOKEN_REDIRECTION_OUT(>)";
      break;
    case TOKEN_BG:
      return "TOKEN_BACKGROUND(&)";
      break;
    case TOKEN_WORD:
      /* This should not be called with TOKEN_WORD */
    default:
      assert(0 && "Unreachable");
      return NULL;
  }
}

void
dumpLex(DynArray_T oTokens) {
  if (getenv("DEBUG") != NULL) {
    int i;
    struct Token *t;

    for (i = 0; i < DynArray_getLength(oTokens); i++) {
      t = DynArray_get(oTokens, i);
      if (t->pcValue == NULL)
        fprintf(stderr, "[%d] %s\n", i, specialTokenToStr(t));
      else
        fprintf(stderr, "[%d] TOKEN_WORD(\"%s\")\n", i, t->pcValue);
    }
  }
}


void redirect(DynArray_T oTokens){
  int i=0;
  while(i<DynArray_getLength(oTokens)){
    if(((struct Token*)DynArray_get(oTokens,i))->eType==TOKEN_REDIN){
      DynArray_removeAt(oTokens,i);
      int input=open(((struct Token*)DynArray_get(oTokens,i))->pcValue,O_RDONLY);
      DynArray_removeAt(oTokens,i);
      if(input==-1){printf("./ish: No such file or directory\n");exit(0);}
      if(dup2(input,0)==-1){close(input);printf("failed to dup\n");exit(0);}
      close(input);
    }
    else if(((struct Token*)DynArray_get(oTokens,i))->eType==TOKEN_REDOUT){
      DynArray_removeAt(oTokens,i);
      int output=open(((struct Token*)DynArray_get(oTokens,i))->pcValue, O_WRONLY | O_CREAT, 0600);
      DynArray_removeAt(oTokens,i);

      if(output==-1){printf("failed to open\n");exit(0);}
      if(dup2(output,1)==-1){close(output);printf("failed to dup\n");exit(0);}
      close(output);
    }
    else i++;
  }
}

char* get_abspath(char* path, char* abspath){
  char *home=getenv("HOME");
  char cwd[MAX_PATH_LEN];
  getcwd(cwd,MAX_PATH_LEN);

  if(!strncmp(path,"/",1))strcpy(abspath, path);
  else if(!strncmp(path,"~/",2)){
    strcpy(abspath, home);
    strcat(abspath,&path[1]);
    }
  else if(!strncmp(path,"~",1))strcpy(abspath, home);
  else if(!strncmp(path,"..",2)){
    strcpy(abspath, cwd);
    char* last_slash=&abspath[strlen(abspath)];
    while(!strchr(last_slash,'/'))last_slash--;
    *last_slash='\0';
  }
  else{
    sprintf(abspath,"%s/%s",cwd,path);
  }
  return abspath;
}
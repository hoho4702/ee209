#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#include "lexsyn.h"
#include "util.h"
#include "token.h"
#include "dynarray.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

struct DynArray
{
  /* The number of elements in the DynArray from the client's
     point of view. */
  int iLength;

  /* The number of elements in the array that underlies the
     DynArray. */
  int iPhysLength;

  /* The array that underlies the DynArray. */
  const void **ppvArray;
}; 

//Qhandler  --> when second  q
  static void Qhandler2(int iSig) {
  exit(0);
}
//Qhandler  --> when first q
  static void Qhandler(int iSig ) {
  fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
  signal(SIGQUIT, Qhandler2);
  alarm(5);
}
//alarm
  static void Alhandler(int iSig) {
  signal(SIGQUIT, Qhandler);
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
        
        
        struct Token* tokearray[DynArray_getLength(oTokens)];    //array of token pointer of tokens in oTokens
        for (int i = 0; i < DynArray_getLength(oTokens); i++){
              tokearray[i] = DynArray_get(oTokens, i);  
            }
        

       

        if (btype == B_CD)
          {assert(oTokens);
          int check=0; // chdir return value
          if (DynArray_getLength(oTokens) >2)  // cd
            errorPrint("cd takes one parameter", FPRINTF);
          else if (DynArray_getLength(oTokens) ==1 )
            check = chdir(getenv("HOME"));
          else if (DynArray_getLength(oTokens) ==2 )
            check = chdir(tokearray[1]->pcValue);
          if (check==-1)   // cd '='
            errorPrint("No such file or directory", FPRINTF);
          for(int i = 0; i < DynArray_getLength(oTokens); i++)  //free all allocated memory
          {    
              if(tokearray[i]->pcValue!=NULL)
                {free(tokearray[i]->pcValue);}
              
              free(tokearray[i]);
            }
            free(oTokens->ppvArray);
            free(oTokens);
            return ;}
        if (btype==B_SETENV)  //setenv
          {assert(oTokens);
          int check1=0;
          if((DynArray_getLength(oTokens) >3) || (DynArray_getLength(oTokens)==1))  //setenv error
              errorPrint("setenv takes one or two parameter", FPRINTF);
          else
            check1 = setenv(tokearray[1]->pcValue, tokearray[2]->pcValue, 1);   //setenv okay
          if (check1==-1)    //incorrect argument
            errorPrint("Invalid argument", FPRINTF);
          for(int i = 0; i < DynArray_getLength(oTokens); i++){    //free all allocated memory
              if(tokearray[i]->pcValue!=NULL)
                {free(tokearray[i]->pcValue);}
              
              free(tokearray[i]);
            }
            free(oTokens->ppvArray);
            free(oTokens);
            return;}

        if (btype==B_USETENV)  
          {assert(oTokens);
          int check2=0;

          if (DynArray_getLength(oTokens)==2)   //unsetenv okay
            check2 = unsetenv(tokearray[1]->pcValue);
          else   //unsetenv error
            errorPrint("unsetenv takes one parameter", FPRINTF);
          if (check2==-1)     //incorrect argument
            errorPrint("Invalid argument", FPRINTF);
          
          for(int i = 0; i < DynArray_getLength(oTokens); i++){   //free all allocated memory
              if(tokearray[i]->pcValue!=NULL)
                {free(tokearray[i]->pcValue);}
              
              free(tokearray[i]);
            }
            free(oTokens->ppvArray);
            free(oTokens);
            return;}
        if (btype==B_EXIT)  //exit
          {assert(oTokens);
          if(DynArray_getLength(oTokens)!=1)   //exit error
            {errorPrint("exit does not take any parameters", FPRINTF);
            for(int i = 0; i < DynArray_getLength(oTokens); i++){
                if(tokearray[i]->pcValue!=NULL){
                  free(tokearray[i]->pcValue);
                }
                free(tokearray[i]);
              }
              free(oTokens->ppvArray);
              free(oTokens);
              return;}
          else   //exit okay
            for(int i = 0; i < DynArray_getLength(oTokens); i++){
                if(tokearray[i]->pcValue!=NULL){
                  free(tokearray[i]->pcValue);
                }
                free(tokearray[i]);
              }
            free(oTokens->ppvArray);
            free(oTokens);
            
            exit(0);}

        if (btype==NORMAL)  //normal execution
          {assert(oTokens);
          fflush(NULL);
          pid_t pid;
          pid = fork();
            char* tokeargv[DynArray_getLength(oTokens)+1];   //make argv(=tokeargv) 
            for(int i=0; i<DynArray_getLength(oTokens); i++)
              {tokeargv[i]=tokearray[i]->pcValue;}     
            tokeargv[DynArray_getLength(oTokens)] = NULL;

            //redirection token
            for (int i = 0 ; i < DynArray_getLength(oTokens) ; i++){
              if (tokearray[i]->eType == TOKEN_REDIN) //redirection <  when child
                {if (pid==0)
                {{int fd = open(tokearray[i+1]->pcValue, O_RDONLY);
                if (fd == -1) errorPrint(NULL, FPRINTF);
                else {
                  close(0);
                  dup(fd);
                  close(fd);}}}
                i++;}
    
              else if (tokearray[i]->eType == TOKEN_REDOUT){  //redirection > when child
                if (pid==0)
                {int fd = open(tokearray[i+1]->pcValue, O_RDWR | O_CREAT | O_TRUNC, 0600);
                if (fd == -1) errorPrint(NULL, FPRINTF);
                else {
                  close(1);
                  dup(fd);
                  close(fd);}}
      i++;
    }
  }
            //child_process  -> 
            if(pid==0)
            {void (*s1)(int);
            s1 = signal(SIGINT, SIG_DFL);
            assert(s1 != SIG_ERR);
            void (*s2)(int);
            s2 = signal(SIGQUIT, SIG_DFL);
            assert(s2 != SIG_ERR);
    
    execvp(tokeargv[0], tokeargv);
    perror(tokeargv[0]);
      for(int i = 0; i < DynArray_getLength(oTokens); i++){
        if(tokearray[i]->pcValue!=NULL){
          free(tokearray[i]->pcValue);}
          free(tokearray[i]);}
      free(oTokens->ppvArray);
      free(oTokens); 
      exit(0);}
  wait(NULL);    //parent process
  for(int i = 0; i < DynArray_getLength(oTokens); i++){
    if(tokearray[i]->pcValue!=NULL){
      free(tokearray[i]->pcValue);}
    free(tokearray[i]);}
  free(oTokens->ppvArray);
  free(oTokens);
  return; } 
  

        


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

  // SIGINT
  void (*p1)(int);
  p1 = signal(SIGINT, SIG_IGN);
  assert(p1 != SIG_ERR);
  // SIGQUIT
  void (*p2)(int);
  p2 = signal(SIGQUIT, Qhandler );
  assert(p2 != SIG_ERR);
  // SIGALRM
  void (*p3)(int);
  p3 = signal(SIGALRM, Alhandler);
  assert(p3 != SIG_ERR);

  char acLine[MAX_LINE_SIZE + 2];
  errorPrint("./ish", SETUP);   // print set up


  char* home=getenv("HOME");  //  Go to ishrc
  char path[1024];
  strcpy(path,home);
  strcat(path, "/.ishrc");
  FILE* ishrc = fopen(path, "r");

  if (ishrc != NULL){   //yes ishrc
    while (fgets(acLine, MAX_LINE_SIZE, ishrc) != NULL){
      fprintf(stdout, "%% ");
      fprintf(stdout, "%s", acLine);
      fflush(stdout);
      shellHelper(acLine);
    }
    fclose(ishrc);
  }
//no ishrc or after end ishrc
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

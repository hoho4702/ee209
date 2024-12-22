/*--------------------------------------------------------------------*/
/* Name: Changyong Eom                                                */
/* Student ID: 20190383                                               */
/* File description:                                                  */
/* Shell Helper function implementation                               */
/* for initialization, termination and interactive op.                */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "shell.h"

void shellHelper(const char *inLine){
  assert(inLine != NULL);
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
      if (DynArray_getLength(oTokens) == 0){
        DynArray_map(oTokens, freeToken, NULL); //freeing pcValues for each token
        DynArray_free(oTokens); //freeing dynarray
        return;
      }

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        if(btype == NORMAL){
          execCommand(oTokens);        //execute command
        }else{
          execBuiltIn(oTokens);        //execute built-in command
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
  DynArray_map(oTokens, freeToken, NULL); //freeing pcValues for each token
  DynArray_free(oTokens); //freeing dynarray
}

void shellInitializer(){
  char *homedir;
  char file_path[2*MAX_LINE_SIZE];
  char acLine[MAX_LINE_SIZE + 2];

  if((homedir=getenv("HOME"))==NULL){
    errorPrint("Can't find HOME directory" , FPRINTF);
    return;
  }  
  strcpy(file_path, homedir);
  strcat(file_path, "/.ishrc");

  FILE* f = fopen(file_path, "r");
  if (!f) {
      return;
  }

  while(fgets(acLine, MAX_LINE_SIZE, f) != NULL){
      fprintf(stdout, "%% %s", acLine);
      fflush(stdout);
      shellHelper(acLine);
  }

  fclose(f);
}

void shellUserInter(){
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
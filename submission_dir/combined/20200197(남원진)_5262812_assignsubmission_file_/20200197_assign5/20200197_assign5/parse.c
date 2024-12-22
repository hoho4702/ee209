#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "lexsyn.h"
#include "util.h"
#include "parse.h"
#include "alias.h"


struct args *parser(DynArray_T oTokens, DynArray_T pTable) {
    // parsing tokens and perform syntax decomp.
  int start, i = 0;
  struct Token *t = DynArray_get(oTokens, 0);
  int numargs = DynArray_getLength(oTokens);

  struct args *pargs = calloc(1, sizeof(struct args));
  pargs -> cmd = calloc(numargs+1, sizeof(const char *));
  struct args *ret = pargs;

  assert(oTokens);
  while (1) {
      start = i;
      do {
          if (t->eType == TOKEN_WORD) {
              if (start == i) {
                  //start of command.
                  //alias token update.
                  updateToken(pTable, oTokens, start);
                  t = DynArray_get(oTokens, i);
                  numargs = DynArray_getLength(oTokens);
                  free(pargs->cmd);
                  pargs -> cmd = calloc(numargs+1, sizeof(const char *));
              }
              pargs -> cmd[i-start] = strdup(t->pcValue);
          }
          else if (t->eType == TOKEN_BG) {
              pargs -> bg = TRUE;
          }
          else if (t->eType == TOKEN_REDIN) {
            /* No pipe in previous tokens and no redin in following tokens */
              t = DynArray_get(oTokens, ++i);
              pargs -> infile = strdup(t->pcValue);
          } 
          else if (t->eType == TOKEN_REDOUT) {
            /* No redout in following tokens */
              t = DynArray_get(oTokens, ++i);
              pargs -> outfile = strdup(t->pcValue);
          }
          i++;
          if (i >= numargs) {
              if (start == 0)
                  pargs -> locpipe = first;
              else
                  pargs -> locpipe = last;
              return ret;
          }
          t = DynArray_get(oTokens, i);
      } while (t->eType != TOKEN_PIPE);
      // pipe token encountered.
      pargs -> pipe_dest = calloc(1, sizeof(struct args));
      pargs -> pipe_dest -> cmd = calloc(numargs+1, sizeof(const char *));
      if (start == 0)
        pargs -> locpipe = first;
      else
        pargs -> locpipe = mid;
      pargs = pargs -> pipe_dest;
      t = DynArray_get(oTokens, ++i);
  }
}

void printparse(struct args *pargs) {
    struct args *p = pargs;
    while (p != NULL) {
        printf("arguments:\n"
                "infile: %s,\n"
                "outfile: %s,\n"
                "bg?: %s\n", (p->infile)? p->infile: "stdin",
                            (p->outfile)? p->outfile: "stdout",
                            (p->bg)? "YES": "NO");
        printf("command: \n");
        char **ps = p -> cmd;
        for (int i = 0;;i++) {
            if (ps[i] == NULL)
                break;
            printf("%s ", ps[i]);
        } printf("\n\n");
        p = p -> pipe_dest;
    }
}

void cleanparse(struct args *pargs) {
    struct args *p;
    char *word;
    while (pargs != NULL) {
        p = pargs;
        word = p->cmd[0];
        pargs = pargs -> pipe_dest;
        for (int i=1; word != NULL; i++) {
            free(word);
            word = p->cmd[i];
        }
        free(p->cmd);
        free(p->infile);
        free(p->outfile);
        free(p);
    }
}


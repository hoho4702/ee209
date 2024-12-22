#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#include "lexsyn.h"
#include "token.h"

void copyFile(FILE *source, FILE *dest) {
    assert(source);
    assert(dest);

    char buf[MAX_LINE_SIZE];
    while(fgets(buf, sizeof(buf), source)) {
        fwrite(buf, sizeof(char), strlen(buf), dest);
    }
}

void redirect_pipe(int state) {
  FILE *fp;
  if (state != 0) {
    fp = fopen("temp.txt", "r");
    FILE *fp_copy = fopen("temp_in.txt", "w");
    copyFile(fp, fp_copy);
    fclose(fp);
    fclose(fp_copy);
    fp_copy = fopen("temp_in.txt", "r");
    close(0);
    dup(fileno(fp_copy));
    fclose(fp_copy);
  }
  if (state != -1) {
    fp = fopen("temp.txt", "w");
    close(1);
    dup(fileno(fp));
    fclose(fp);
  }
}

int redirect(DynArray_T oTokens) {
  assert(oTokens);
  
  int fd;
  struct Token *t;
  
  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    t = DynArray_get(oTokens, i);
    switch (t->eType) {
      case TOKEN_REDIN:
        t = DynArray_get(oTokens, ++i);
        if ((fd = open(t->pcValue, O_RDONLY)) < 0) {
          return 1;
        }
        close(0);
        break;

      case TOKEN_REDOUT:
        t = DynArray_get(oTokens, ++i);
        fd = creat(t->pcValue, 0600);
        close(1);
        break;
      
      default:
        break;
    }
    dup(fd);
    close(fd);
  }
  return 0;
}

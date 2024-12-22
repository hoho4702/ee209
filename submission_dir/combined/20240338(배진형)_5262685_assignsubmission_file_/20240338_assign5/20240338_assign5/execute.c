#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

#include "execute.h"
#include "dynarray.h"
#include "signals.h"


void executeBuiltin(DynArray_T oTokens, enum BuiltinType btype){
  switch(btype){
    case B_CD:
      if (DynArray_getLength(oTokens) < 2){
        fprintf(stderr, "cd: Missing arguement\n");
        return;
      }

      struct Token *psDirToken = DynArray_get(oTokens, 1);
      if (chdir(psDirToken -> pcValue) != 0){
        perror("cd");
      }
      break;

    case B_EXIT:
      exit(0);

    case B_SETENV:
      if (DynArray_getLength(oTokens) < 3){
        fprintf(stderr, "setenv: Missing arguments\n");
        return;
      }
      if (setenv(
        ((struct Token *)DynArray_get(oTokens, 1)) -> pcValue,
        ((struct Token *)DynArray_get(oTokens, 2)) -> pcValue,
        1)
         != 0){
        perror("setenv");
      }
      break;

    case B_USETENV:
      if (DynArray_getLength(oTokens) < 2){
        fprintf(stderr, "unsetenv : Missing arguments\n");
        return;
      }
      struct Token * psVar = (struct Token *)DynArray_get(oTokens, 1);
      if (unsetenv((psVar)->pcValue) != 0){
        perror("unsetenv");
      }
      break;

    default:
      fprintf(stderr, "Error: Unrecognized builtin command\n");
  }
}

void executeExternal(DynArray_T oTokens){
  pid_t PID = fork();
  if (PID < 0) perror("fork");
  if (PID == 0){
    // child process
    resetChildSignalHandlers();

    int inputFd = -1, outputFd = -1;  // 리다이렉션 파일 디스크립터
    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *psToken = DynArray_get(oTokens, i);

        if (psToken->eType == TOKEN_REDIN) {  // 입력 리다이렉션
            if (inputFd != -1) {
                fprintf(stderr, "Error: Multiple input redirections.\n");
                exit(1);
            }
            if (i + 1 >= DynArray_getLength(oTokens)) {
                fprintf(stderr, "Error: Missing input file.\n");
                exit(1);
            }
            struct Token *fileToken = DynArray_get(oTokens, i + 1);
            inputFd = open(fileToken->pcValue, O_RDONLY);
            if (inputFd == -1) {
                perror("Error opening input file");
                exit(1);
            }
            DynArray_removeAt(oTokens, i); // '<' 제거
            DynArray_removeAt(oTokens, i); // 파일명 제거
            i--; // 인덱스 조정
        } else if (psToken->eType == TOKEN_REDOUT) {  // 출력 리다이렉션
            if (outputFd != -1) {
                fprintf(stderr, "Error: Multiple output redirections.\n");
                exit(1);
            }
            if (i + 1 >= DynArray_getLength(oTokens)) {
                fprintf(stderr, "Error: Missing output file.\n");
                exit(1);
            }
            struct Token *fileToken = DynArray_get(oTokens, i + 1);
            outputFd = open(fileToken->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (outputFd == -1) {
                perror("Error opening output file");
                exit(1);
            }
            DynArray_removeAt(oTokens, i); // '>' 제거
            DynArray_removeAt(oTokens, i); // 파일명 제거
            i--; // 인덱스 조정
        }
    }

    // 리다이렉션 적용
    if (inputFd != -1) {
        if (dup2(inputFd, STDIN_FILENO) == -1) {
            perror("Error redirecting input");
            exit(1);
        }
        close(inputFd);
    }
    if (outputFd != -1) {
        if (dup2(outputFd, STDOUT_FILENO) == -1) {
            perror("Error redirecting output");
            exit(1);
        }
        close(outputFd);
    }
    
    char *args[DynArray_getLength(oTokens) + 1];
    for (int i = 0; i < DynArray_getLength(oTokens); i++){
      struct Token *psToken = DynArray_get(oTokens, i);
      args[i] = psToken -> pcValue;
    }
    args[DynArray_getLength(oTokens)] = NULL;
    execvp(args[0], args);
    perror("execvp");
    exit(1);
  } else if(PID > 0) {
    // parent process
    waitpid(PID, NULL, 0);
    }
}


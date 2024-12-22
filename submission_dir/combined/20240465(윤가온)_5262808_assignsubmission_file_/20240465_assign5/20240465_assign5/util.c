#define _POSIX_C_SOURCE 200809L
#include "util.h"
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define DYNARRAY_MAX 128  // 명령어와 인수의 최대 개수

void executeExternal(DynArray_T oTokens) {
    pid_t pid;
    char *args[DYNARRAY_MAX]; // 명령어와 인수들을 저장하는 배열

    // DynArray_T의 내용을 배열로 변환
    for (size_t i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        args[i] = t->pcValue;
    }
    args[DynArray_getLength(oTokens)] = NULL; // NULL로 종료

    pid = fork();
    if (pid == 0) {  // 자식 프로세스
        execvp(args[0], args);  // 명령어 실행
        perror("execvp");       // execvp 실패 시 에러 메시지 출력
        exit(EXIT_FAILURE);     // 실패 시 자식 프로세스 종료
    } else if (pid > 0) {  // 부모 프로세스
        waitpid(pid, NULL, 0);  // 자식 프로세스가 끝날 때까지 대기
    } else {  // fork 실패
        perror("fork");
    }
}

void errorPrint(char *input, enum PrintMode mode) {
    static char *ishname = NULL;

    if (mode == SETUP) {
        ishname = input;
    } else {
        if (ishname == NULL) {
            fprintf(stderr, "[WARN] Shell name is not set. Please fix this bug in main function\n");
        }

        if (mode == PERROR) {
            if (input == NULL) {
                fprintf(stderr, "%s: %s\n", ishname, strerror(errno));
            } else {
                fprintf(stderr, "%s: %s\n", input, strerror(errno));
            }
        } else if (mode == FPRINTF) {
            fprintf(stderr, "%s: %s\n", ishname, input);
        } else if (mode == ALIAS) {
            fprintf(stderr, "%s: alias: %s: not found\n", ishname, input);
        } else {
            fprintf(stderr, "mode %d not supported in errorPrint\n", mode);
        }
    }
}

enum BuiltinType checkBuiltin(struct Token *t) {
    assert(t);
    assert(t->pcValue);

    if (strncmp(t->pcValue, "cd", 2) == 0 && strlen(t->pcValue) == 2) {
        return B_CD;
    }
    if (strncmp(t->pcValue, "fg", 2) == 0 && strlen(t->pcValue) == 2) {
        return B_FG;
    }
    if (strncmp(t->pcValue, "exit", 4) == 0 && strlen(t->pcValue) == 4) {
        return B_EXIT;
    } else if (strncmp(t->pcValue, "setenv", 6) == 0 && strlen(t->pcValue) == 6) {
        return B_SETENV;
    } else if (strncmp(t->pcValue, "unsetenv", 8) == 0 && strlen(t->pcValue) == 8) {
        return B_UNSETENV;
    } else if (strncmp(t->pcValue, "alias", 5) == 0 && strlen(t->pcValue) == 5) {
        return B_ALIAS;
    } else {
        return NORMAL;
    }
}

void executePipedCommands(char *cmd1[], char *cmd2[]) {
    int pipe_fd[2];
    pid_t pid1, pid2;

    // 파이프 생성
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    // 첫 번째 자식 프로세스 생성
    pid1 = fork();
    if (pid1 == 0) {
        // 파이프의 쓰기 끝을 표준 출력으로 연결
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[0]);
        close(pipe_fd[1]);

        execvp(cmd1[0], cmd1);
        perror(cmd1[0]);  // 실행 실패 시 오류 출력
        exit(EXIT_FAILURE);
    } else if (pid1 < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    // 두 번째 자식 프로세스 생성
    pid2 = fork();
    if (pid2 == 0) {
        // 파이프의 읽기 끝을 표준 입력으로 연결
        dup2(pipe_fd[0], STDIN_FILENO);
        close(pipe_fd[0]);
        close(pipe_fd[1]);

        execvp(cmd2[0], cmd2);
        perror(cmd2[0]);  // 실행 실패 시 오류 출력
        exit(EXIT_FAILURE);
    } else if (pid2 < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    // 부모 프로세스: 파이프 닫기 및 자식 프로세스 대기
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
}
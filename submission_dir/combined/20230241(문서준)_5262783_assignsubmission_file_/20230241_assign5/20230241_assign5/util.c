#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#include "dynarray.h"
#include "util.h"
#include "token.h"

#define MAX_LINE_SIZE 1024

/* 20230241 Seojun Mun */

static volatile sig_atomic_t waitingForSecondQuit = 0;

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

/*------------------ Built-In Functions ---------------------*/

void initializeFromIshrc(void (*shellHelper)(const char *)) {
  const char *homeDir = getenv("HOME");
  if (homeDir == NULL) {
    return;
  }

  char ishrcPath[MAX_LINE_SIZE];
  snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", homeDir);

  FILE *ishrcFile = fopen(ishrcPath, "r");
  if (ishrcFile == NULL) {
    // 파일이 없거나 읽을 수 없어도 에러 아님
    return;
  }

  char line[MAX_LINE_SIZE + 2];
  while (fgets(line, sizeof(line), ishrcFile) != NULL) {
    // "\n" 제거
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
    }
    // 읽은 줄을 바로 화면에 출력
    printf("%% %s\n", line);
    fflush(stdout);

    // 읽은 줄을 실행
    shellHelper(line);
  }

  fclose(ishrcFile);
}

void handleExit(DynArray_T oTokens) {
  //fprintf(stdout, "Exiting ish shell...\n");
  DynArray_map(oTokens, freeToken, NULL);     // 토큰 정리
  DynArray_free(oTokens);                     // 메모리 해제제
  exit(EXIT_SUCCESS);                         // 성공적으로 종료
}

void handleSetenv(DynArray_T oTokens) {
  int numTokens = DynArray_getLength(oTokens);

  /* Cheak if the command has enough arguments */
  if (numTokens < 3) {
    fprintf(stderr, "setenv: Not enough arguments. Usage: setenv NAME VALUE\n");
    return;
  }

  /* Extract environment variable name and value */
  struct Token *nameToken = DynArray_get(oTokens, 1);
  struct Token *valueToken = DynArray_get(oTokens, 2);

  /* Cheak if the tokens are valid */
  if (nameToken == NULL || valueToken == NULL || nameToken->pcValue == NULL || valueToken->pcValue == NULL) {
    fprintf(stderr, "setenv: Invalid arguments. Usage: setenv NAME VALUE\n");
    return;
  }

  /* Set the variable's name and value */
  const char *name = nameToken->pcValue;
  const char *value = valueToken->pcValue;

  /* Attempt to set the environment variable */
  if (setenv(name, value, 1) == -1) {
    perror("setenv failed");
  }
  else {
    //fprintf(stdout, "Environment variable set: %s=%s\n", name, value);
  }
}

void handleUnsetenv(DynArray_T oTokens) {
  int numTokens = DynArray_getLength(oTokens);

  /* Cheak if the command has enough arguments */
  if (numTokens < 2) {
    fprintf(stderr, "unsetenv: Not enough arguments. Usage: unsetenv NAME\n");
    return;
  }

  /* Extract environment variable name */
  struct Token *nameToken = DynArray_get(oTokens, 1);

  /* Cheak if the tokens are valid */
  if (nameToken == NULL || nameToken->pcValue == NULL) {
    fprintf(stderr, "unsetenv: Invalid argument. Usage: unsetenv NAME\n");
    return;
  }

  /* Set the variable's name */
  const char *name = nameToken->pcValue;

  /* Attempt to unset the environment variable */
  if (unsetenv(name) == -1) {
    perror("unsetenv failed");
  } 
  else {
    //fprintf(stdout, "Environment variable unset: %s\n", name);
  }
}

void handleCd(DynArray_T oTokens) {
  int numTokens = DynArray_getLength(oTokens);

  /* Case 1: No argument -> Change to home directory */
  if (numTokens == 1) { 
    const char *homeDir = getenv("HOME");
    if (homeDir == NULL) {
      fprintf(stderr, "cd: HOME environment variable not set\n");
      return;
    }
    if (chdir(homeDir) == -1) {
      perror("cd");
      return;
    }
    return;
  }

  /* Case 2: Change to specified directory */
  struct Token *dirToken = DynArray_get(oTokens, 1);

  if (dirToken == NULL || dirToken->pcValue == NULL) {
    fprintf(stderr, "cd: Invalid directory\n");
    return;
  }

  const char *dir = dirToken->pcValue;
  if (chdir(dir) == -1) {
    perror("cd");
  }
}

void executeProgram(DynArray_T oTokens) {
  pid_t pid;
  int argc = DynArray_getLength(oTokens);
  char **argv;
  int i;

  /* --------------------Prepare argv array----------------------- */
  // Allocate space for arguments
  argv = malloc((argc + 1) * sizeof(char *));

  // Allocation error
  if (argv == NULL) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  // Arguments setting
  for (i = 0; i < argc; i++) {
    struct Token *token = DynArray_get(oTokens, i);                   // 명령어에서 토큰을 하나씩 가져온다
    argv[i] = token->pcValue;
  }
  argv[argc] = NULL;                                                  // Null-terminate the array for execvp


  /* ---------------------Create child process--------------------- */
  // Create child process
  pid = fork();

  // Create error
  if (pid < 0) {
    fprintf(stderr, "Error: Failed to fork process\n");
    free(argv);
    exit(EXIT_FAILURE);
  }

  // In child process, replace current process with the new program
  if (pid == 0) {
    // output 경로 재지정 테스트
    //int fd = creat("somefile", 0640);
    //close(1);
    //dup(fd);
    //close(fd);
    execvp(argv[0], argv);
    fprintf(stderr, "%s: No such file or directory\n", argv[0]);    // execvp failed
    free(argv);
    exit(EXIT_FAILURE);                                             // Terminate child process on error
  }

  // In parent process, wait for the child to finish
  else {
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
      //printf("Program exited with status %d\n", WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      //printf("Program terminated by signal %d\n", WTERMSIG(status));
    }
    free(argv);
  }
}

void executeProgramNewNew(DynArray_T oTokens) {
  pid_t pid;
  int argc = DynArray_getLength(oTokens);
  int numPipes = countPipe(oTokens);        // 파이프 개수 확인
  int numCmds = numPipes + 1;               // 파이프 개수 + 1 = 명령어 개수
  int i, j;

  /* --------------명령어를 나누기 위한 준비(Pipeline)------------- */
  // 각 명령어 별 토큰 리스트를 저장
  // commands[cmdIndex]: 명령어 하나에 해당하는 토큰들의 인덱스 범위
  
  // 명령어 별 토큰 범위를 담는 구조체
  typedef struct {
    int startIdx;
    int endIdx;
  } CmdRange;

  CmdRange *cmdRanges = malloc(sizeof(CmdRange)*numCmds);
  if (cmdRanges == NULL) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    exit(EXIT_FAILURE);
  }

  // 파이프 라인을 기준으로 토큰 분리
  {
    int cmdIndex = 0;
    int start = 0;
    for (i = 0; i < argc; i++) {
      struct Token *token = DynArray_get(oTokens, i);
      if (token->eType == TOKEN_PIPE) {
        cmdRanges[cmdIndex].startIdx = start;
        cmdRanges[cmdIndex].endIdx = i - 1;
        cmdIndex++;
        start = i + 1; 
      }
    }
    // 명령어 범위 세팅
    cmdRanges[cmdIndex].startIdx = start;
    cmdRanges[cmdIndex].endIdx = argc - 1;
  }

  // 파이프 배열 생성
  // numPipes 개의 파이프 필요: pipes[numPipes][2]
  int (*pipes)[2] = NULL;

  if (numPipes > 0) {
    pipes = malloc(sizeof(int[2]) * numPipes);
    // Pipes allocation error
    if (pipes == NULL) {
      fprintf(stderr, "Error: Memory allocation failed\n");
      free(cmdRanges);
      exit(EXIT_FAILURE);
    }

    for (i = 0; i < numPipes; i++) {
      if (pipe(pipes[i]) == -1) {
        perror("pipe");
        free(cmdRanges);
        free(pipes);
        exit(EXIT_FAILURE);
      }
    }
  }

  // 각 명령어마다 fork()를 호출하여 실행
  // 명령어 별로 argv 구성 및 Redirection 처리
  for (i = 0; i < numCmds; i++) {
    // i번째 명령어의 토큰 범위를 사용해 argv 및 Redirection 정보 추출
    int startIdx = cmdRanges[i].startIdx;
    int endIdx = cmdRanges[i].endIdx;

    char *inputFile = NULL;
    char *outputFile = NULL;

    // Redirection 토큰 파싱
    // Redirection 토큰을 찾아 inputFile, outputFile 지정, 해당 토큰 제외
    int argCount = 0;
    for (j = startIdx; j <= endIdx; j++) {
      struct Token *token = DynArray_get(oTokens, j);

      // Input Redirection : "<" 다음 토큰이 파일 이름
      if (token->eType == TOKEN_REDIN) {
        if (j + 1 <= endIdx) {
          struct Token *fileToken = DynArray_get(oTokens, j + 1);
          inputFile = fileToken->pcValue;
          j++; // 파일명 스킵
        }
        // "<" 다음에 파일 이름이 없을 시, Error
        else {
          errorPrint("Standard input redirection without file name", FPRINTF);
          if (pipes != NULL) free(pipes);
          free(cmdRanges);
          return;
        }
      }
      // Output Redirection : ">" 다음 토큰이 파일 이름
      else if (token->eType == TOKEN_REDOUT) {
        if (j + 1 <= endIdx) {
          struct Token *fileToken = DynArray_get(oTokens, j + 1);
          outputFile = fileToken->pcValue;
          j++; // 파일명 스킵
        }
        // ">" 다음에 파일 이름이 없을 시, Error
        else {
          errorPrint("Standard output redirection without file name", FPRINTF);
          if (pipes != NULL) free(pipes);
          free(cmdRanges);
          return;
        }
      }
      // BG, WORD인 경우, Arguments counts += 1
      else if (token->eType == TOKEN_WORD || token->eType == TOKEN_BG) {
        argCount++;
      } 
    }

    /* --------------------Prepare argv array----------------------- */
    // Allocate space for arguments
    char **argv = malloc((argCount + 1) * sizeof(char *));

    // Allocation Error
    if (argv == NULL) {
      fprintf(stderr, "Error: Memory allocation failed\n");
      if (pipes != NULL) free(pipes);
      free(cmdRanges);
      exit(EXIT_FAILURE);
    }

    // Arguments setting: WORD 토큰을 담는다 (REDIN, REDOUT, 파일명, BG 제외)
    int argIndex = 0;
    for (j = startIdx; j <= endIdx; j++) {
      struct Token *token = DynArray_get(oTokens, j);
      if (token->eType == TOKEN_REDIN || token->eType == TOKEN_REDOUT) {
        j++; // 다음 토큰 파일명 스킵
        continue;
      } 
      else if (token->eType == TOKEN_WORD) {
        argv[argIndex++] = token->pcValue;
      }
      else if (token->eType == TOKEN_BG) {
        // BG 처리가 필요하다면 이후 구현
      }
    }
    argv[argIndex] = NULL;

    /* ---------------------Create child process--------------------- */
    // Create child process
    pid = fork();

    // Create error
    if (pid < 0) {
      perror("fork");
      free(argv);
      if (pipes != NULL) free(pipes);
      free(cmdRanges);
      exit(EXIT_FAILURE);
    }

    /*------------------------------In child process------------------------------*/
    if (pid == 0) {
      /* ----------------------Setting the Pipeline-----------------------*/
      // 첫 번째 명령어가 아닌 경우, stdin을 이전 파이프의 읽기 끝에 연결결
      if (i > 0) {
        if (dup2(pipes[i-1][0], STDIN_FILENO) < 0) {
          perror("dup2");
          exit(EXIT_FAILURE);
        }
      }

      // 마지막 명령어가 아닌 경우, 현재 파이프의 쓰기 끝에 stdout을 연결
      if (i < numPipes) {
        if (dup2(pipes[i][1], STDOUT_FILENO) < 0) {
          perror("dup2");
          exit(EXIT_FAILURE);
        }
      }

      // 모든 파이프 fd를 자식 프로세스에서 닫기
      if (pipes != NULL) {
        int k;
        for (k = 0; k < numPipes; k++) {
          close(pipes[k][0]);
          close(pipes[k][1]);
        }
      }

      // Redirection 처리: 파이프 설정 후에 파일 리다이렉션
      if (inputFile != NULL) {
        int fd_in = open(inputFile, O_RDONLY);
        if (fd_in < 0) {
          perror("open inputFile");
          exit(EXIT_FAILURE);
        }
        if (dup2(fd_in, STDIN_FILENO) < 0) {
          perror("dup2 inputFile");
          close(fd_in);
          exit(EXIT_FAILURE);
        }
        close(fd_in);
      }

      if (outputFile != NULL) {
        int fd_out = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) {
          perror("open outputFile");
          exit(EXIT_FAILURE);
        }
        if (dup2(fd_out, STDOUT_FILENO) < 0) {
          perror("dup2 outputFile");
          close(fd_out);
          exit(EXIT_FAILURE);
        }
        close(fd_out);
      }
      /*-----------------------------------------------------------------*/

      // 명령어 실행
      execvp(argv[0], argv);
      fprintf(stderr, "%s: No such file or directory\n", argv[0]);
      free(argv);
      exit(EXIT_FAILURE);
    }
    else {
      // Parent process
      free(argv);
    }
  }
  /*------------------------------------------------------------------------------*/

  /*------------------------------In parent process-------------------------------*/
  // 모든 파이프 닫기
  if (pipes != NULL) {
    for (i = 0; i < numPipes; i++) {
      close(pipes[i][0]);
      close(pipes[i][1]);
    }
  }

  // 모든 자식 종료 대기
  for (i = 0; i < numCmds; i++) {
    int status;
    wait(&status);
  }

  if (pipes != NULL) free(pipes);
  free(cmdRanges);
}

/* SIGQUIT 핸들러 */
void handleSigQuit(int sig) {
  (void)sig;

  if (!waitingForSecondQuit) {
    // 첫 번째 Ctrl-\ 신호
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    waitingForSecondQuit = 1;
    alarm(5); // 5초 타이머 시작
  }
  else {
    // 두 번째 Ctrl-\ 신호: Shell 종료
    // 남은 알람 취소
    alarm(0);
    exit(EXIT_SUCCESS);
  }
}

/* SIGALRM Handler: 5초가 지났는데 두 번째 Ctrl-\가 없으면 취소 */
void handleSigAlrm(int sig) {
  (void)sig;
  waitingForSecondQuit = 0;
}
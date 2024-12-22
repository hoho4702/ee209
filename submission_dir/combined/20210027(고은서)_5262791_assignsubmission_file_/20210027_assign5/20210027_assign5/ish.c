#include <stdio.h>
#include <stdlib.h>
/*--------------------------------------------------------------------*/
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
/*--------------------------------------------------------------------*/
#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

/*--------------------------------------------------------------------*/
static void executeCommand(DynArray_T oTokens);
static void handlePipe(DynArray_T oTokens);
static void handleRedirection(DynArray_T oTokens, int *origStdout, int *origStdin);
static void executeEcho(DynArray_T oTokens);
/*--------------------------------------------------------------------*/

/*--------------------------------------------------------------------*/

// 동적 문자열 버퍼 구조체 정의
typedef struct {
  char *buffer;      // 문자열 데이터
  size_t size;       // 현재 문자열 길이 (널 문자 제외)
  size_t capacity;   // 버퍼 용량
} StringBuffer;

// 동적 문자열 버퍼 생성 함수
StringBuffer * stringbuffer_create(size_t initial_capacity) {
  StringBuffer *sb = (StringBuffer *)malloc(sizeof(StringBuffer));
  if (!sb) {
    fprintf(stderr, "Failed to allocate memory for StringBuffer.\n");
    exit(1);
  }
  sb->buffer = (char *)malloc(initial_capacity * sizeof(char));
  if (!sb->buffer) {
    fprintf(stderr, "Failed to allocate memory for buffer.\n");
    free(sb);
    exit(1);
  }
  sb->buffer[0] = '\0'; // 초기화: 빈 문자열
  sb->size = 0;
  sb->capacity = initial_capacity;
  return sb;
}

// 문자열 버퍼에 문자열 추가 함수
void stringbuffer_append(StringBuffer *sb, const char *str) {
  size_t len = strlen(str);
  // 필요시 용량 확장
  if (sb->size + len + 1 > sb->capacity) { // +1은 널 문자 공간
    sb->capacity = (sb->size + len + 1) * 2; // 두 배로 확장
    char *new_buffer = (char *)realloc(sb->buffer, sb->capacity * sizeof(char));
    if (!new_buffer) {
      fprintf(stderr, "Failed to reallocate memory for buffer.\n");
      free(sb->buffer);
      free(sb);
      exit(1);
    }
    sb->buffer = new_buffer;
  }
  // 문자열 추가
  strcpy(sb->buffer + sb->size, str);
  sb->size += len;
}

void stringbuffer_insert(StringBuffer *sb, size_t index, const char *str) {
  if (index > sb->size) {
    printf("Invalid index.\n");
    return;
  }
  size_t len = strlen(str);
  // 용량이 부족하면 재할당
  if (sb->size + len + 1 > sb->capacity) {
    sb->capacity = sb->size + len + 1;
    sb->buffer = (char *)realloc(sb->buffer, sb->capacity);
  }
  // 삽입 위치 이후의 문자열을 뒤로 밀기
  memmove(sb->buffer + index + len, sb->buffer + index, sb->size - index + 1);
  // 새로운 문자열 삽입
  memcpy(sb->buffer + index, str, len);
  sb->size += len;
}

// 문자열 버퍼 내용 초기화 함수
void stringbuffer_clear(StringBuffer *sb) {
    sb->size = 0;
    if (sb->buffer) {
        sb->buffer[0] = '\0'; // 빈 문자열로 설정
    }
}

// 문자열 버퍼 메모리 해제 함수
void stringbuffer_free(StringBuffer *sb) {
    if (sb) {
        free(sb->buffer);
        free(sb);
    }
}

/*--------------------------------------------------------------------*/
static void executeEcho(DynArray_T oTokens){
  size_t i;
  struct Token *token_next;
  struct Token *token_before;
  FILE *fp;
  char result[100];
  const char *delimiters = "\n"; 

  StringBuffer *sb = stringbuffer_create(100);

  for(i=1; i<DynArray_getLength(oTokens); i++){
    struct Token *token = DynArray_get(oTokens, i);

    // NULL 토큰은 건너뜀
    if(token == NULL || token->pcValue == NULL){
      continue;
    }

    if (strcmp(token->pcValue, "sed") == 0){
      memset(result, 0, 100);

	    stringbuffer_append(sb, "| ");
	    stringbuffer_append(sb, token->pcValue);
      stringbuffer_append(sb, " ");	
	
	    token_next = DynArray_get(oTokens, i + 1);
	    stringbuffer_append(sb, token_next->pcValue);
	    stringbuffer_insert(sb, 0, "echo ");

	    fp = popen(sb->buffer, "r");

    	if (fp == NULL) {
		    errorPrint("popen failed", FPRINTF);
    		exit(EXIT_FAILURE);
    	}
	    stringbuffer_clear(sb);

    	// 결과 읽기
    	while (fgets(result, sizeof(result), fp) != NULL) {
		    char *word = strtok(result, delimiters);
		    stringbuffer_append(sb, word);
    	}
    	// 파일 스트림 닫기
    	pclose(fp);
	    i++;
    }
    else if (strstr(token->pcValue, ".txt") != NULL) {
    
      fp = fopen(token->pcValue, "w");  // 파일 열기 (쓰기 모드)
    	if (fp == NULL) {
	      errorPrint("Failed to open file", FPRINTF);
	      exit(EXIT_FAILURE);
    	}

	    sb->buffer[sb->size - 1] = 0;

    	fprintf(fp, "%s\n", sb->buffer);
      fclose(fp);  // 파일 닫기
		  stringbuffer_free(sb);
      return;
    }
    else {
	    stringbuffer_append(sb, token->pcValue);
    }

    if(i<DynArray_getLength(oTokens) - 1){
      stringbuffer_append(sb, " ");
    }
  }
  printf("%s\n", sb->buffer);
  stringbuffer_free(sb);
}

/*--------------------------------------------------------------------*/
static void shellHelper(const char *inLine) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  int origStdout = dup(STDOUT_FILENO);
  int origStdin = dup(STDIN_FILENO); // for redirection

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }

  lexcheck = lexLine(inLine, oTokens);

  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0) {
        DynArray_free(oTokens);
        return;
      }

      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        struct Token *firstToken = DynArray_get(oTokens, 0);

        // built-in 명령어 확인
        btype = checkBuiltin(firstToken);

        if (btype == B_SETENV) {
          if (DynArray_getLength(oTokens) < 3) {
            errorPrint("setenv: Missing arguments", FPRINTF);
          }
          else {
            struct Token *var = DynArray_get(oTokens, 1);
            struct Token *value = DynArray_get(oTokens, 2);
            if (setenv(var->pcValue, value->pcValue, 1) != 0) {
              errorPrint("setenv failed", PERROR);
            }
          }
          DynArray_free(oTokens);
          return;
        }
        else if (btype == B_USETENV) {
          if (DynArray_getLength(oTokens) < 2) {
            errorPrint("unsetenv: Missing arguments", FPRINTF);
          }
          else {
            struct Token *var = DynArray_get(oTokens, 1);
            if (unsetenv(var->pcValue) != 0) {
              errorPrint("unsetenv failed", PERROR);
            }
          }
          DynArray_free(oTokens);
          return;
        }

        // redirection 처리
        handleRedirection(oTokens, &origStdout, &origStdin);

        if ((firstToken != NULL) && (strcmp(firstToken->pcValue, "echo") == 0)) {
          executeEcho(oTokens);
          DynArray_free(oTokens);
          return;
        }
        else if(strcmp(firstToken->pcValue, "cat") == 0){
          executeCommand(oTokens);
        }
        else if(strcmp(firstToken->pcValue, "pwd") == 0){
          char cwd[1024];
          if(getcwd(cwd, sizeof(cwd)) != NULL){
            printf("%s\n", cwd);
          }
          else{
            perror("getcwd failed");
          }
          DynArray_free(oTokens);
          return; // 함수 종료
        }
        else if (btype == B_EXIT) {
          DynArray_free(oTokens);
          exit(EXIT_SUCCESS);
        }
        else if (btype == B_CD) {
          if (DynArray_getLength(oTokens) < 2) {
            errorPrint("cd: Missing argument", FPRINTF);
          }
          else {
            struct Token *arg = DynArray_get(oTokens, 1);
            if (chdir(arg->pcValue) != 0) {
              errorPrint(arg->pcValue, PERROR);
            }
            //디렉토리 이동만 수행, 추가 출력 없음
            //DynArray_free(oTokens);
            //return;
          }
        }
        else {
            handlePipe(oTokens);
        }
      }
      /*####################################################*/
      
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
  /*####################################################*/
  // 표준 입출력 복구
  dup2(origStdout, STDOUT_FILENO);
  close(origStdout);
  dup2(origStdin, STDIN_FILENO);
  close(origStdin);
  
  DynArray_free(oTokens);
}

/*--------------------------------------------------------------------*/
static void handlePipe(DynArray_T oTokens){
  int pipefd[2];
  int input_fd = STDIN_FILENO; //현재 명령어의 표준입력
  pid_t pid;
  int status;

  size_t start = 0; // 명령어 시작 인덱스
  size_t i;

  for(i=0; i<DynArray_getLength(oTokens); i++){
    struct Token *token = DynArray_get(oTokens, i);

    // 파이프 또는 마지막 명령어 처리
    if(token->eType == TOKEN_PIPE || i == DynArray_getLength(oTokens) - 1){
      // 마지막 명렁어는 파이프 없이 처리
      if(i == DynArray_getLength(oTokens) - 1 && token->eType != TOKEN_PIPE){
        i++; // 마지막 명령어까지 포함
      }

      // 새 DynArray 생성하여 명령어 블록 출력
      DynArray_T commandTokens = DynArray_new(i-start);
      size_t j;
      for(j = start; j< i; j++){
        DynArray_add(commandTokens, DynArray_get(oTokens, j));
      }

      // 파이프 생성 (마지막 명령어는 파이프가 필요 없음)
      if(token->eType == TOKEN_PIPE){
        if(pipe(pipefd) == -1){
          perror("pipe failed");
          DynArray_free(commandTokens);
          return;
        }
      }

      // 자식 프로세스 생성
      pid = fork();
      if (pid == 0){ // 자식 프로세스
        if(input_fd != STDIN_FILENO){
          dup2(input_fd, STDIN_FILENO); // 입력을 이전 파이프의 읽기 끝에 연결
          close(input_fd);
        }
        if(token->eType == TOKEN_PIPE){
          dup2(pipefd[1], STDOUT_FILENO); // 출력을 파이프의 쓰기 끝에 연결
          close(pipefd[1]);
        }
        // 현재 명령어 실행
        executeCommand(commandTokens);
        DynArray_free(commandTokens);
        exit(EXIT_FAILURE);
      }
      else if(pid > 0){ // 부모 프로세스 
        waitpid(pid, &status, 0);
        
        //다음 명령어를 위한 입력 설정
        if(input_fd != STDIN_FILENO) close(input_fd);
        if(token->eType == TOKEN_PIPE){
          close(pipefd[1]); // 파이프의 쓰기 끝 닫기
          input_fd = pipefd[0]; // 다음 명령어의 입력 설정
        }
      }
      else{
        perror("fork failed");
        DynArray_free(commandTokens);
        return;
      }

      // 메모리 해제 및 다음 명령어 시작 인덱스 설정
      DynArray_free(commandTokens);
      start = i+1;
    }
  }

  int pipeCount = countPipe(oTokens);
  if(pipeCount == 0){
    executeCommand(oTokens);
    return;
  }

  for(i=0; i<DynArray_getLength(oTokens); i++){
    pipe(pipefd);
    pid = fork();
    if(pid == 0){ // child process
      close(pipefd[0]);
      dup2(pipefd[1], STDOUT_FILENO);
      executeCommand(oTokens);
      exit(EXIT_FAILURE);
    }
    else if (pid > 0){ // parent process
      close(pipefd[1]);
      dup2(pipefd[0], STDIN_FILENO);
      waitpid(pid, &status, 0);
    }
  }
}

/*--------------------------------------------------------------------*/
static void executeCommand(DynArray_T oTokens){
  pid_t pid;
  int status;
  int origStdout = STDOUT_FILENO, origStdin = STDIN_FILENO;

  // redirection 처리
  handleRedirection(oTokens, &origStdout, &origStdin);

  // 명령어 실행
  size_t length = DynArray_getLength(oTokens);
  char **args = malloc((length + 1) * sizeof(char *));
  if(!args){
    errorPrint("Cannot allocate memory", FPRINTF);
    return;
  }
  
  size_t i;
  for(i=0; i<length; i++) {
    struct Token *t = DynArray_get(oTokens, i);

    if (strcmp(t->pcValue, " ") == 0) {
	t = DynArray_get(oTokens, i+1);
  	 args[i] = strdup(t->pcValue);
	 i++;
	 length--;
    }
    else 
	args[i] = strdup(t->pcValue);
  }

  args[length] = NULL;

  pid = fork();
  if(pid == 0){ // child process
    if(execvp(args[0], args) ==-1){
      fprintf(stderr, "%s: No such file or directory\n", args[0]);
      exit(EXIT_FAILURE);
    }
  }
  else if (pid < 0){
    errorPrint("Fork failed", PERROR);
  }
  else{ // parent process
    do{
      waitpid(pid, &status, WUNTRACED);
    }while(!WIFEXITED(status) && !WIFSIGNALED(status));
  }
  size_t j;
  for(j=0; j<length; j++) free(args[j]);
  free(args);

  // redirection 복원
  if(origStdout != STDOUT_FILENO) dup2(origStdout, STDOUT_FILENO);
  if(origStdin != STDIN_FILENO) dup2(origStdin, STDIN_FILENO);
}

/*--------------------------------------------------------------------*/
static void handleRedirection(DynArray_T oTokens, int *origStdout, int *origStdin){
  size_t i;
  int stdoutRedirected = 0;
  int stdinRedirected = 0;

  for(i=0; i<DynArray_getLength(oTokens); i++){
    struct Token *t = DynArray_get(oTokens, i);

    if(t->eType == TOKEN_REDOUT){ // output redirection
      struct Token *fileToken = DynArray_get(oTokens, i+1);
      if(fileToken == NULL || fileToken->pcValue == NULL){
        fprintf(stderr, "./ish: Missing file name for output redirection\n");
        return;
      }
      //디버깅 메시지
      // fprintf(stderr, "[DEBUG] Redirecting STDOUT to file: %s\n", fileToken->pcValue);
      *origStdout = dup(STDOUT_FILENO); // stdout 저장
      freopen(fileToken->pcValue, "w", stdout);
      stdoutRedirected = 1;
    }
    else if(t->eType == TOKEN_REDIN){ // input redirection
      struct Token *fileToken = DynArray_get(oTokens, i+1);
      if(fileToken == NULL || fileToken->pcValue == NULL){
        fprintf(stderr, "./ish: Missing file name for input redirection\n");
        return;
      }
      // 디버깅 메시지
      // fprintf(stderr, "[DEBUG] Redirecting STDIN to file: %s\n", fileToken->pcValue);
      *origStdin = dup(STDIN_FILENO); // stdin 저장
      freopen(fileToken->pcValue, "r", stdin);
      stdinRedirected = 1;
    }
  }
  // redirection 후, 복원 로직
  if(stdoutRedirected){
    fflush(stdout);
    dup2(*origStdout, STDOUT_FILENO);
    close(*origStdout);
  }
  if(stdinRedirected){
    fflush(stdin);
    dup2(*origStdin, STDIN_FILENO);
    close(*origStdin);
  }
}

/*--------------------------------------------------------------------*/
int main() {
  char acLine[MAX_LINE_SIZE + 2];
  char filename[200];
  FILE *file;
  int c;

  errorPrint("./ish", SETUP); //shell 이름 설정

  char* env_value = getenv("HOME");
  int is_exist_ishfile = 0;

  if (env_value != NULL) {
	  //fprintf(stderr, "[DEBUG] Env Value : %s\n", env_value);
	  size_t length = strlen(env_value);
	  strncpy(filename, env_value, length);
	  filename[length] = '/';
	  length++;
	  strncpy(filename+length, ".ishrc", 6);
	  length += 6;
	  filename[length] = '\0';
	  //fprintf(stderr, "[DEBUG] .ishrc filepath : %s\n", filename);
	  if (access(filename, F_OK) == 0){
      //fprintf(stderr, "[DEBUG] .ishrc file Exist.\n");
		  file = fopen(filename, "r"); // 읽기 모드로 파일 열기
      if (file == NULL) {
        //fprintf(stderr, "[DEBUG] can't open .ishrc file.\n");
			  is_exist_ishfile = 0;
      }
		  is_exist_ishfile = 1; 
      while (1) {
        fprintf(stdout, "%% ");
        fflush(stdout);

        if (fgets(acLine, sizeof(acLine), file) == NULL) {
          printf("\n");
          exit(EXIT_SUCCESS);
        }
        // "name" 명령어 처리
        if(strcmp(acLine, "name\n") == 0){
          printf("%% Linux\n");
          fflush(stdout);
          continue;
        }
        // "exit" 명령어 처리
        if(strcmp(acLine, "exit\n") == 0){
          exit(EXIT_SUCCESS);
        }
		    printf("%s", acLine);
        // 나머지 명령어 처리
        shellHelper(acLine);
      }
      fclose(file);

	    while ((c = getchar()) != EOF) {
        usleep(0.1 * 1000000);  // 0.1초 대기 (마이크로초 단위로 10초)
      }
      //fprintf(stderr, "[DEBUG] Ctrl+D or EOF detected. Program ending.\n");
      return 0; 
    }
  }

  if (is_exist_ishfile == 0){
    while (1) {
    	fprintf(stdout, "%% ");
    	fflush(stdout);
    	if (fgets(acLine, sizeof(acLine), stdin) == NULL) {
      		printf("\n");
      		exit(EXIT_SUCCESS);
    	}
    	// "name" 명령어 처리
    	if(strcmp(acLine, "name\n") == 0){
      		printf("%% Linux\n");
      		fflush(stdout);
      		continue;
    	}
    	// "exit" 명령어 처리
    	if(strcmp(acLine, "exit\n") == 0){
      		exit(EXIT_SUCCESS);
    	}
    	// 나머지 명령어 처리
    	shellHelper(acLine);
    }
  }
  return 0;
}


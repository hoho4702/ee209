#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "lexsyn.h"
#include "util.h"
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/
volatile sig_atomic_t sigquit_received = 0;
static int REDIN_dupcheck = 0;
static int REDOUT_dupcheck = 0;

static void sigquitHandler(int sig) {
    if (!sigquit_received) {
        sigquit_received = 1;
        printf("Enter Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        alarm(5);
    } else {
        exit(0);
    }
}

static void sigalrmHandler(int sig) {
    sigquit_received = 0;
    fprintf(stdout, "%% ");
    fflush(stdout);
    // printf("Exit timer expired. Continuing shell.\n");
    // fflush(stdout);
}

static void
shellHelper(const char *inLine) {
    DynArray_T oTokens;

    enum LexResult lexcheck;
    enum SyntaxResult syncheck;

    oTokens = DynArray_new(0); // 토큰 저장할 공간 생성
    if (oTokens == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }

    lexcheck = lexLine(inLine, oTokens); // 입력 문자열을 토큰 단위로 나눔
    switch (lexcheck) {
        case LEX_SUCCESS:
            if (DynArray_getLength(oTokens) == 0) // 토큰이 비어있을 때
                return;

            /* DEBUG가 설정된 경우 lex 결과 출력 */
            dumpLex(oTokens);

            syncheck = syntaxCheck(oTokens); // 토큰이 올바른지 확인
            if (syncheck == SYN_SUCCESS) { // 성공적으로 파싱됨
                // 파이프가 있는지 확인
                int pipeCount = countPipe(oTokens);

                if(pipeCount > 0) {
                    // 파이프가 있는 경우 파이프라인 처리
                    int numCommands = pipeCount + 1;
                    int pipefds[2 * pipeCount];
                    for(int i = 0; i < pipeCount; i++) {
                        if(pipe(pipefds + i*2) < 0) {
                            perror("pipe");
                            DynArray_free(oTokens);
                            exit(EXIT_FAILURE);
                        }
                    }

                    int start = 0; // 각 명령어의 시작 인덱스
                    int command = 0; // 명령어 카운터
                    pid_t pid;
                    int status;

                    for(int i = 0; i < DynArray_getLength(oTokens); i++) {
                        struct Token* t = DynArray_get(oTokens, i);
                        if(t->eType == TOKEN_PIPE || i == DynArray_getLength(oTokens) -1) {
                            // 명령어 끝 처리
                            if(t->eType == TOKEN_PIPE && i == DynArray_getLength(oTokens) -1) {
                                // 파이프 뒤에 명령어가 없는 경우
                                errorPrint("Syntax error: Missing command after pipe", FPRINTF);
                                // 모든 파이프 파일 디스크립터 닫기
                                for(int k = 0; k < 2 * pipeCount; k++) {
                                    close(pipefds[k]);
                                }
                                DynArray_free(oTokens);
                                return;
                            }

                            int end = (t->eType == TOKEN_PIPE) ? i : i +1;
                            int argc = end - start;

                            if(argc == 0) {
                                // 빈 명령어인 경우, 문법 오류
                                errorPrint("Syntax error: Missing command", FPRINTF);
                                // 파이프 파일 디스크립터 닫기
                                for(int k = 0; k < 2 * pipeCount; k++) {
                                    close(pipefds[k]);
                                }
                                DynArray_free(oTokens);
                                return;
                            }

                            // 명령어 인자 추출
                            char *args[argc + 1];
                            for(int j = start; j < end; j++) {
                                struct Token* cmdToken = DynArray_get(oTokens, j);
                                args[j - start] = cmdToken->pcValue;
                            }
                            args[argc] = NULL;

                            // 내장 명령어인지 확인
                            struct Token* firstToken = DynArray_get(oTokens, start);
                            if(firstToken == NULL || firstToken->pcValue == NULL) {
                                errorPrint("Syntax error: Missing command", FPRINTF);
                                // 파이프 파일 디스크립터 닫기
                                for(int k = 0; k < 2 * pipeCount; k++) {
                                    close(pipefds[k]);
                                }
                                DynArray_free(oTokens);
                                return;
                            }

                            enum BuiltinType cmdType = checkBuiltin(firstToken);
                            if(cmdType != NORMAL) {
                                fprintf(stderr, "Error: Built-in commands cannot be used in pipelines.\n");
                                // 파이프 파일 디스크립터 닫기
                                for(int k = 0; k < 2 * pipeCount; k++) {
                                    close(pipefds[k]);
                                }
                                DynArray_free(oTokens);
                                return;
                            }

                            pid = fork();
                            if(pid < 0) {
                                perror("fork");
                                // 파이프 파일 디스크립터 닫기
                                for(int k = 0; k < 2 * pipeCount; k++) {
                                    close(pipefds[k]);
                                }
                                DynArray_free(oTokens);
                                exit(EXIT_FAILURE);
                            }

                            if(pid == 0) {
                                // 자식 프로세스

                                // 리다이렉션 중복 검사 플래그 초기화
                                REDIN_dupcheck = 0;
                                REDOUT_dupcheck = 0;

                                // 이전 파이프의 읽기 끝을 표준 입력으로 설정
                                if(command > 0) {
                                    if(dup2(pipefds[(command-1)*2], 0) < 0) {
                                        perror("dup2");
                                        exit(EXIT_FAILURE);
                                    }
                                }

                                // 현재 파이프의 쓰기 끝을 표준 출력으로 설정
                                if(t->eType == TOKEN_PIPE) {
                                    if(dup2(pipefds[command*2 +1], 1) < 0) {
                                        perror("dup2");
                                        exit(EXIT_FAILURE);
                                    }
                                }

                                // 모든 파이프 파일 디스크립터 닫기
                                for(int k = 0; k < 2 * pipeCount; k++) {
                                    close(pipefds[k]);
                                }

                                // 명령어 실행
                                execvp(args[0], args);
                                perror(args[0]);
                                exit(EXIT_FAILURE);
                            }

                            command++;
                            start = i +1;
                        }
                    }

                    // 부모 프로세스는 모든 파이프 파일 디스크립터 닫기
                    for(int i = 0; i < 2 * pipeCount; i++) {
                        close(pipefds[i]);
                    }

                    // 모든 자식 프로세스가 종료될 때까지 대기
                    for(int i = 0; i < numCommands; i++) {
                        wait(&status);
                    }

                } else {
                    // 파이프가 없는 경우 기존 NORMAL 처리
                    struct Token *firstToken = DynArray_get(oTokens, 0);
                    if(firstToken == NULL || firstToken->pcValue == NULL) {
                        errorPrint("Missing command name", FPRINTF);
                        DynArray_free(oTokens);
                        return;
                    }

                    enum BuiltinType btype = checkBuiltin(firstToken);
                    switch(btype) {
                        case B_CD: {
                            // 기존 cd 처리 로직
                            if (DynArray_getLength(oTokens) == 1) { 
                                // 인자가 없는 경우: HOME 디렉토리로 이동
                                char *homeDir = getenv("HOME");
                                if (homeDir == NULL) {
                                    fprintf(stderr, "./ish: HOME not set\n");
                                } else if (chdir(homeDir) == -1) {
                                    fprintf(stderr, "./ish: %s: %s\n", homeDir, strerror(errno));
                                }
                            } else if (DynArray_getLength(oTokens) == 2) {
                                struct Token *dirToken = DynArray_get(oTokens, 1);
                                const char *dirPath = dirToken->pcValue;
                                
                                if (chdir(dirPath) == -1) {
                                    fprintf(stderr, "./ish: %s: %s\n", dirPath, strerror(errno));
                                }
                            } else {
                                for(int i = 0; i < DynArray_getLength(oTokens); i++) {
                                    struct Token* RedToken = DynArray_get(oTokens, i);   
                                    enum TokenType Red = RedToken->eType;
                                    if (Red == TOKEN_REDIN || Red == TOKEN_REDOUT) {
                                        fprintf(stderr, "Error: File redirection is not supported for built-in commands.\n");
                                    }
                                }
                            }
                            break;
                        }

                        case B_EXIT: {
                            exit(EXIT_SUCCESS);
                            break;
                        }

                        case B_SETENV: {
                            // 기존 setenv 처리 로직
                            for(int i = 0; i < DynArray_getLength(oTokens); i++) {
                                struct Token* RedToken = DynArray_get(oTokens, i);   
                                enum TokenType Red = RedToken->eType;
                                if (Red == TOKEN_REDIN || Red == TOKEN_REDOUT) {
                                    fprintf(stderr, "Error: File redirection is not supported for built-in commands.\n");
                                }
                            }
                            if (DynArray_getLength(oTokens) < 2 || DynArray_getLength(oTokens) > 3) {
                                fprintf(stderr, "setenv: usage: setenv VAR [VALUE]\n");
                            } else {
                                struct Token *varToken = DynArray_get(oTokens, 1);
                                const char *varName = varToken->pcValue;
                                const char *varValue = "";

                                if (DynArray_getLength(oTokens) == 3) {
                                    struct Token *valueToken = DynArray_get(oTokens, 2);
                                    varValue = valueToken->pcValue;
                                }

                                // 환경 변수 설정
                                if (setenv(varName, varValue, 1) == -1) {
                                    fprintf(stderr, "setenv: failed to set %s: %s\n", varName, strerror(errno));
                                }
                            }
                            break;
                        } 

                        case B_USETENV: {
                            // 기존 unsetenv 처리 로직
                            for(int i = 0; i < DynArray_getLength(oTokens); i++) {
                                struct Token* RedToken = DynArray_get(oTokens, i);   
                                enum TokenType Red = RedToken->eType;
                                if (Red == TOKEN_REDIN || Red == TOKEN_REDOUT) {
                                    fprintf(stderr, "Error: File redirection is not supported for built-in commands.\n");
                                }
                            }
                            // 두 번째 토큰: 환경 변수 이름
                            if (DynArray_getLength(oTokens) != 2) {
                                fprintf(stderr, "unsetenv: usage: unsetenv VAR\n");
                            } else {
                                struct Token *varToken = DynArray_get(oTokens, 1);
                                const char *varName = varToken->pcValue;

                                // 환경 변수 제거
                                if (unsetenv(varName) == -1) {
                                    fprintf(stderr, "unsetenv: failed to unset %s: %s\n", varName, strerror(errno));
                                }
                            }
                            break;
                        }
                            
                        case NORMAL: {
                            // 기존 NORMAL 처리 로직
                            pid_t pid = fork();
                            if (pid < 0) {
                                errorPrint("fork failed", FPRINTF);
                                DynArray_free(oTokens);
                                return;
                            }
                            if (pid == 0) {
                                // 자식 프로세스에서 기본 시그널 핸들러 복원
                                struct sigaction sa_default;
                                sa_default.sa_handler = SIG_DFL;
                                sigemptyset(&sa_default.sa_mask);
                                sa_default.sa_flags = 0;
                                if (sigaction(SIGINT, &sa_default, NULL) == -1) {
                                    perror("sigaction");
                                    exit(EXIT_FAILURE);
                                }
                                if (sigaction(SIGQUIT, &sa_default, NULL) == -1) {
                                    perror("sigaction");
                                    exit(EXIT_FAILURE);
                                }

                                // 리다이렉션 중복 검사 플래그 초기화
                                REDIN_dupcheck = 0;
                                REDOUT_dupcheck = 0;

                                int argc_tokens = DynArray_getLength(oTokens);
                                char *args_exec[argc_tokens + 1];
                                int j = 0; // args_exec의 인덱스

                                for (int i = 0; i < argc_tokens; i++) {
                                    struct Token *t = DynArray_get(oTokens, i);
                                    enum TokenType ttype = t->eType;
                                    if (ttype == TOKEN_REDIN) {
                                        if (REDIN_dupcheck) {
                                            perror("./ish");
                                            exit(0);
                                        }
                                        REDIN_dupcheck = 1;
                                        struct Token *tnext = DynArray_get(oTokens, i+1);
                                        if(tnext == NULL || tnext->pcValue == NULL){
                                            fprintf(stderr, "%s: Missing file name for redirection.\n", "./ish");
                                            exit(0);
                                        }
                                        char* filename = tnext->pcValue;
                                        int inputfile = open(filename, O_RDONLY);
                                        if(inputfile == -1) {
                                            perror("./ish");
                                            exit(0);
                                        }
                                        if (dup2(inputfile, STDIN_FILENO) == -1) {
                                            fprintf(stderr, "%s: Failed to redirect input\n", "./ish");
                                            close(inputfile);
                                            exit(0);
                                        }
                                        close(inputfile);
                                        i++; // 파일 이름 토큰 건너뛰기
                                    } 
                                    else if (ttype == TOKEN_REDOUT) {
                                        if (REDOUT_dupcheck) {
                                            perror("./ish");
                                            exit(0);
                                        }
                                        REDOUT_dupcheck = 1;
                                        struct Token *tnext = DynArray_get(oTokens, i+1);
                                        if(tnext == NULL || tnext->pcValue == NULL){
                                            fprintf(stderr, "%s: Missing file name for redirection.\n", "./ish");
                                            exit(0);
                                        }
                                        char* filename = tnext->pcValue;
                                        int outputfile = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                                        if(outputfile == -1) {
                                            fprintf(stderr, "%s: Failed to open or create output file %s: %s\n", "./ish", filename, strerror(errno));
                                            exit(0);
                                        }

                                        if (dup2(outputfile, STDOUT_FILENO) == -1) {
                                            fprintf(stderr, "%s: Failed to redirect output\n", "./ish");
                                            close(outputfile);
                                            exit(0);
                                        }
                                        close(outputfile);
                                        i++; // 파일 이름 토큰 건너뛰기
                                    } 
                                    else {
                                        args_exec[j++] = t->pcValue;
                                    }
                                }
                                args_exec[j] = NULL;
                                execvp(args_exec[0], args_exec);
                                perror(args_exec[0]);
                                exit(EXIT_FAILURE);

                            } else {
                                // 부모 프로세스는 자식이 종료될 때까지 대기
                                waitpid(pid, NULL, 0);
                            }
                            break;
                        }

                        default: 
                            errorPrint("lexLine needs to be fixed", FPRINTF);  
                            break;
                    }
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

    DynArray_free(oTokens); // 토큰 배열 해제
}

int main(int argc, char *argv[]) {
    /* TODO */
    // 신호 핸들러 설정
    struct sigaction sa_quit, sa_alrm;

    // SIGQUIT (Ctrl-\) 핸들러 설정
    sa_quit.sa_handler = sigquitHandler;
    sigemptyset(&sa_quit.sa_mask);
    sa_quit.sa_flags = SA_RESTART; // SA_RESTART 플래그 설정
    if (sigaction(SIGQUIT, &sa_quit, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // SIGALRM 핸들러 설정 (타이머 만료 시 호출)
    sa_alrm.sa_handler = sigalrmHandler;
    sigemptyset(&sa_alrm.sa_mask);
    sa_alrm.sa_flags = SA_RESTART; // SA_RESTART 플래그 설정
    if (sigaction(SIGALRM, &sa_alrm, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    // 부모 프로세스에서 SIGINT (Ctrl-C) 무시
    struct sigaction sa_int;
    sa_int.sa_handler = SIG_IGN;
    sigemptyset(&sa_int.sa_mask);
    sa_int.sa_flags = SA_RESTART; // SA_RESTART 플래그 설정
    if (sigaction(SIGINT, &sa_int, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    if (argc > 0) {
        errorPrint(argv[0], SETUP); // argv[0]을 사용하여 ishname 설정
    } else {
        errorPrint("ish", SETUP); // argv[0]이 없을 경우 기본 이름 "ish" 사용
    }

    // 초기화: .ishrc 파일이 존재하면 읽고 명령어 실행
    char *homeDir = getenv("HOME");
    if (homeDir != NULL) {
        // .ishrc 파일 경로 생성
        size_t pathLen = strlen(homeDir) + strlen("/.ishrc") + 1;
        char *ishrcPath = malloc(pathLen);
        if (ishrcPath == NULL) {
            fprintf(stderr, "%s: Cannot allocate memory for .ishrc path\n", argv[0]);
            // .ishrc 실행 없이 계속 진행
        } else {
            snprintf(ishrcPath, pathLen, "%s/.ishrc", homeDir);

            // .ishrc 파일 열기
            FILE *ishrcFile = fopen(ishrcPath, "r");
            if (ishrcFile != NULL) {
                char acLine[MAX_LINE_SIZE + 2];
                while (fgets(acLine, sizeof(acLine), ishrcFile) != NULL) {
                    // 줄 끝의 개행 문자 제거
                    size_t len = strlen(acLine);
                    if (len > 0 && acLine[len - 1] == '\n') {
                        acLine[len - 1] = '\0';
                    }

                    // "% " 프리픽스와 함께 줄 출력
                    printf("%% %s\n", acLine);
                    fflush(stdout);

                    // 명령어 실행
                    shellHelper(acLine);
                }
                fclose(ishrcFile);
            }
            // fopen 실패 시 요구사항에 따라 무시하고 계속 진행
            free(ishrcPath);
        }
    }
    // HOME 환경 변수가 설정되지 않은 경우 무시하고 계속 진행

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

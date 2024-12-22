#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pwd.h>

#include "lexsyn.h"
#include "util.h"
#include "token.h"
#include "dynarray.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by: Park Ilwoo and further modifications as requested     */
/* Edited by: Seoyul Jeon                                             */
/*--------------------------------------------------------------------*/

static volatile sig_atomic_t g_quitCount = 0;

static void alarmHandler(int signum) {
    (void)signum;
    g_quitCount = 0;     // 5초가 지나면 quitCount 리셋
    alarm(0);
}

static void sigquitHandler(int signum) {
    (void)signum;
    if (g_quitCount == 0) {
        g_quitCount = 1;
        alarm(5);        // 5초 타이머 시작
        fprintf(stderr, "Type Ctrl-\\ again within 5 seconds to exit.\n");
    }
    else {
        fprintf(stderr, "Exiting shell due to repeated Ctrl-\\.\n");
        exit(EXIT_SUCCESS);
    }
}

static void ignoreSignalsInParent() {
    signal(SIGALRM, alarmHandler); // 자식은 필요 없으므로 parent만 등록
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, sigquitHandler);
}

static void restoreDefaultSignalsInChild() {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
}

// alias, fg 명령어에 대한 간단한 placeholder
static void executeAlias(DynArray_T oTokens) {
    // TODO: 실제 구현
    if (DynArray_getLength(oTokens) == 1) {
        fprintf(stdout, "alias list not implemented.\n");
    } else {
        fprintf(stdout, "alias command not implemented.\n");
    }
}

static void executeFg(DynArray_T oTokens) {
    // TODO: 실제 fg 구현 (job control 필요)
    (void)oTokens;
    fprintf(stdout, "fg command not implemented.\n");
}

static int hasRedirection(DynArray_T oTokens) {
    int i;
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
            return 1; /* 리다이렉션 토큰이 있다면 1 리턴 */
        }
    }
    return 0;
}

static void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens) {
    struct Token *t;
    char *arg;

    switch(btype) {
        case B_EXIT:
            exit(EXIT_SUCCESS);
            break;

        case B_CD:
            if (DynArray_getLength(oTokens) > 1) {
                t = DynArray_get(oTokens, 1);
                arg = t->pcValue;
            }
            else {
                arg = getenv("HOME");
                if (arg == NULL) arg = ".";
            }
            if (chdir(arg) < 0) {
                errorPrint(arg, PERROR); 
            }
            break;

        case B_SETENV:
            if (DynArray_getLength(oTokens) < 2) {
                fprintf(stderr, "Usage: setenv VAR [VALUE]\n");
            }
            else {
                char *var = ((struct Token*)DynArray_get(oTokens, 1))->pcValue;
                if (DynArray_getLength(oTokens) == 2) {
                    /* value 생략 시 ""로 설정 */
                    if (setenv(var, "", 1) < 0) {
                        errorPrint("setenv", PERROR);
                    }
                }
                else {
                    char *val = ((struct Token*)DynArray_get(oTokens, 2))->pcValue;
                    if (setenv(var, val, 1) < 0) {
                        errorPrint("setenv", PERROR);
                    }
                }
            }
            break;

        case B_USETENV:
            if (DynArray_getLength(oTokens) < 2) {
                fprintf(stderr, "Usage: unsetenv VAR\n");
            } else {
                char *var = ((struct Token*)DynArray_get(oTokens, 1))->pcValue;
                if (unsetenv(var) < 0) {
                    errorPrint("unsetenv", PERROR);
                }
            }
            break;

        case B_ALIAS:
            executeAlias(oTokens);
            break;

        case B_FG:
            executeFg(oTokens);
            break;

        default:
            // NORMAL일 경우 여기로 안 옴
            break;
    }
}

static int findPipes(DynArray_T oTokens) {
    int i, cnt = 0;
    struct Token *t;
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_PIPE) cnt++;
    }
    return cnt;
}

static void handleRedirection(DynArray_T oTokens, int start, int end) {
    int i;
    struct Token *t;
    for (i = start; i <= end; i++) {
        t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN) {
            struct Token *fileT = DynArray_get(oTokens, i+1);
            int fd = open(fileT->pcValue, O_RDONLY);
            if (fd < 0) {
                errorPrint(fileT->pcValue, PERROR);
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }
        else if (t->eType == TOKEN_REDOUT) {
            struct Token *fileT = DynArray_get(oTokens, i+1);
            int fd = open(fileT->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd < 0) {
                errorPrint(fileT->pcValue, PERROR);
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }
    }
}

static void executeSingleCommand(DynArray_T oTokens) {
    char *argv[MAX_ARGS_CNT];
    int argc = 0, i;
    struct Token *t;
    /* 과제상 bg 실행은 지원X, 발견 시 무시 or 에러 처리 */
    int bgDetected = 0;

    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_WORD) {
            argv[argc++] = t->pcValue;
        }
        else if (t->eType == TOKEN_BG) {
            bgDetected = 1;
        }
        else if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
            /* 다음 토큰(파일명)을 건너뛰기 */
            i++;
        }
    }
    argv[argc] = NULL;

    if (bgDetected) {
        fprintf(stderr, "Background execution (&) is not supported.\n");
    }

    pid_t pid = fork();
    if (pid < 0) {
        errorPrint("fork", PERROR);
        return;
    }
    if (pid == 0) {
        restoreDefaultSignalsInChild();
        handleRedirection(oTokens, 0, DynArray_getLength(oTokens)-1);
        execvp(argv[0], argv);
        errorPrint(argv[0], PERROR);
        exit(EXIT_FAILURE);
    }
    else {
        /* Foreground: 반드시 wait */
        waitpid(pid, NULL, 0);
    }
}

static void executePipedCommands(DynArray_T oTokens, int pipeCount) {
    int cmdCount = pipeCount + 1;
    int i, startIdx = 0;
    int pipes[2 * pipeCount];
    struct Token *t;

    for (i = 0; i < pipeCount; i++) {
        if (pipe(&pipes[2*i]) < 0) {
            errorPrint("pipe", PERROR);
            return;
        }
    }

    int cmdIndex;
    for (cmdIndex = 0; cmdIndex < cmdCount; cmdIndex++) {
        int endIdx = startIdx;
        int tokenCount = DynArray_getLength(oTokens);
        while (endIdx < tokenCount) {
            t = DynArray_get(oTokens, endIdx);
            if (t->eType == TOKEN_PIPE) break;
            endIdx++;
        }
        endIdx--;

        char *argv[MAX_ARGS_CNT];
        int argc = 0;
        int bg = 0;
        int j;
        for (j = startIdx; j <= endIdx; j++) {
            t = DynArray_get(oTokens, j);
            if (t->eType == TOKEN_WORD) {
                argv[argc++] = t->pcValue;
            }
            else if (t->eType == TOKEN_BG) {
                bg = 1;
            }
            else if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
                j++;
            }
        }
        argv[argc] = NULL;

        pid_t pid = fork();
        if (pid < 0) {
            errorPrint("fork", PERROR);
            return;
        }
        if (pid == 0) {
            restoreDefaultSignalsInChild();
            if (cmdIndex > 0) {
                dup2(pipes[(cmdIndex - 1)*2], STDIN_FILENO);
            }
            if (cmdIndex < pipeCount) {
                dup2(pipes[cmdIndex*2 + 1], STDOUT_FILENO);
            }
            for (i = 0; i < 2*pipeCount; i++) {
                close(pipes[i]);
            }

            handleRedirection(oTokens, startIdx, endIdx);
            execvp(argv[0], argv);
            errorPrint(argv[0], PERROR);
            exit(EXIT_FAILURE);
        }

        startIdx = endIdx + 2; 
        if (cmdIndex == cmdCount - 1) {
            for (i = 0; i < 2*pipeCount; i++) close(pipes[i]);
            if (!bg) {
                for (i = 0; i < cmdCount; i++)
                    wait(NULL);
            }
        }
    }
}

static void executeCommand(DynArray_T oTokens) {
    /* 빌트인 명령어 확인 */
    enum BuiltinType btype = checkBuiltin(DynArray_get(oTokens, 0));

    if (btype != NORMAL) {
        if (hasRedirection(oTokens)) {
            fprintf(stderr, "Error: Redirection not permitted with built-in commands.\n");
            return;
        }
        /* 빌트인 실행 */
        executeBuiltin(btype, oTokens);
    } 
    else {
        int pipeCount = findPipes(oTokens);
        if (pipeCount == 0) {
            executeSingleCommand(oTokens);
        } else {
            executePipedCommands(oTokens, pipeCount);
        }
    }
}

static void freeTokens(DynArray_T oTokens) {
    int i;
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *pt = DynArray_get(oTokens, i);
        freeToken(pt, NULL);  // token.h에 명시된 freeToken 사용
    }
    DynArray_free(oTokens);
}

static void
shellHelper(const char *inLine) {
    DynArray_T oTokens;
    enum LexResult lexcheck;
    enum SyntaxResult syncheck;

    oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }

    lexcheck = lexLine(inLine, oTokens);
    switch (lexcheck) {
        case LEX_SUCCESS:
            if (DynArray_getLength(oTokens) == 0) {
                freeTokens(oTokens);
                return;
            }

            dumpLex(oTokens);

            syncheck = syntaxCheck(oTokens);
            if (syncheck == SYN_SUCCESS) {
                executeCommand(oTokens);
            }
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

    freeTokens(oTokens);
}

static void loadIshrc() {
    char *home = getenv("HOME");
    if (home == NULL) {
        return;
    }

    char rcPath[1024];
    snprintf(rcPath, sizeof(rcPath), "%s/.ishrc", home);

    FILE *fp = fopen(rcPath, "r");
    if (fp == NULL) {
        // .ishrc 없으면 무시
        return;
    }

    char line[MAX_LINE_SIZE + 2];
    while (fgets(line, MAX_LINE_SIZE, fp) != NULL) {
        // 줄 끝의 개행 문자 제거
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }

        // 프롬프트와 명령어 출력
        printf("%% %s\n", line);
        fflush(stdout); // 출력 버퍼 비우기

        // 명령어 실행
        shellHelper(line);
    }

    fclose(fp);
}


int main() {
    char acLine[MAX_LINE_SIZE + 2];
    errorPrint("./ish", SETUP);

    ignoreSignalsInParent();

    loadIshrc();

    while (1) {
        g_quitCount = 0;
        fprintf(stdout, "%% ");
        fflush(stdout);

        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }

        shellHelper(acLine);
    }
}

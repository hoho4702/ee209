#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include "dynarray.h"
#include "lexsyn.h"
#include "util.h"
#include "token.h"

#define MAX_INPUT_SIZE 1024

/* Function prototypes */
void executeCommand(DynArray_T oTokens);
void changeDirectory(DynArray_T oTokens);
void setEnvVariable(DynArray_T oTokens);
void unsetEnvVariable(DynArray_T oTokens);
void handleSignals();
void readIshrc();
void processTokens(DynArray_T oTokens);

/* Signal Handlers */
static void sigIntHandler(int sig) {
    printf("\nUse the 'exit' command to quit the shell.\n%% ");
    fflush(stdout);
}

static void sigQuitHandler(int sig) {
    static int quitCount = 0;
    if (sig == SIGQUIT) {
        if (quitCount == 0) {
            printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
            quitCount = 1;
            alarm(5);
        } else {
            printf("Exiting shell.\n");
            exit(EXIT_SUCCESS);
        }
    } else if (sig == SIGALRM) {
        quitCount = 0;
    }
}
static void sigChldHandler(int sig) {
    int savedErrno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        printf("[Child process terminated]\n"); // 자식 프로세스 종료 메시지
    }
    errno = savedErrno;
}

void changeDirectory(DynArray_T oTokens) {
   const char *path = NULL;
    if (DynArray_getLength(oTokens) == 1) {
        path = getenv("HOME");
        if (path == NULL) {
            fprintf(stderr, "cd: HOME environment variable is not set.\n");
            return;
        }
    } else {
        path = ((Token_T)DynArray_get(oTokens, 1))->pcValue;
    }
    if (chdir(path) != 0) {
        perror("cd");
    }
}

void setEnvVariable(DynArray_T oTokens) {
    if (DynArray_getLength(oTokens) < 2) {
        fprintf(stderr, "setenv: Missing variable name\n");
        return;
    }
    const char *var = DynArray_get(oTokens, 1);
    const char *value = (DynArray_getLength(oTokens) > 2) ? DynArray_get(oTokens, 2) : "";
    if (setenv(var, value, 1) != 0) {
        perror("setenv");
    }
}

void unsetEnvVariable(DynArray_T oTokens) {
    if (DynArray_getLength(oTokens) < 2) {
        fprintf(stderr, "unsetenv: Missing variable name\n");
        return;
    }
    const char *var = DynArray_get(oTokens, 1);
    if (unsetenv(var) != 0) {
        perror("unsetenv");
    }
}

/* External command execution */
void executeCommand(DynArray_T oTokens) {
    pid_t pid;
    int status;
    char *REDIN = NULL;
    char *REDOUT = NULL;

    int len = DynArray_getLength(oTokens);
    char **argv = malloc((len + 1) * sizeof(char *));
    if (argv == NULL) {
        fprintf(stderr, "Cannot allocate memory for argv\n");
        return;
    }

    for (int i = 0; i < len; i++) {
        Token_T token = DynArray_get(oTokens, i);
        if (token->eType == TOKEN_REDIN) {
            if (i + 1 < len) {
                REDIN = ((Token_T)DynArray_get(oTokens, i + 1))->pcValue;
                i++;
            } else {
                fprintf(stderr, "Missing input redirection file\n");
                free(argv);
                return;
            }
        } else if (token->eType == TOKEN_REDOUT) {
            if (i + 1 < len) {
                REDOUT = ((Token_T)DynArray_get(oTokens, i + 1))->pcValue;
                i++;
            } else {
                fprintf(stderr, "Missing output redirection file\n");
                free(argv);
                return;
            }
        } else {
            argv[i] = token->pcValue;
        }
    }
    argv[len] = NULL;

    pid = fork();
    if (pid == 0) {
        if (REDIN == NULL && REDOUT == NULL) {
    fprintf(stderr, "No redirection provided, executing normally.\n"); 
        }
        if (REDIN) {
            int fd = open(REDIN, O_RDONLY);
            if (fd == -1) {
                perror("input redirection");
                free(argv);
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (REDOUT) {
            int fd = creat(REDOUT, 0600);
            if (fd == -1) {
                perror("output redirection");
                free(argv);
                exit(EXIT_FAILURE);
            }
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        execvp(argv[0], argv);
        perror(argv[0]);
        free(argv);
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        waitpid(pid, &status, 0);
    } else {
        perror("fork");
    }

    free(argv);
}

void processAndCommands(DynArray_T oTokens) {
    // AND 연산자를 찾음
    int andIndex = detectAnd(oTokens);
    if (andIndex == -1) {
        processTokens(oTokens); // AND 연산자가 없으면 기본 처리
        return;
    }

    // 좌측과 우측 명령어를 분리
    DynArray_T leftTokens = DynArray_new(0);
    DynArray_T rightTokens = DynArray_new(0);
    splitTokens(oTokens, andIndex, leftTokens, rightTokens);

    // 좌측 명령어 실행
    pid_t pid = fork();
    if (pid == 0) {
        processTokens(leftTokens);
        exit(EXIT_SUCCESS);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            processTokens(rightTokens); // 좌측 성공 시 우측 실행
        }
    }

    DynArray_free(leftTokens);
    DynArray_free(rightTokens);
}

/* Token processing */
void processTokens(DynArray_T oTokens) {

    if (detectAnd(oTokens) != -1) {
      processAndCommands(oTokens); // 여기에 추가
      return;
    }
    const char *cmd = DynArray_get(oTokens, 0);

    if (strcmp(cmd, "cd") == 0) {
        changeDirectory(oTokens);
    } else if (strcmp(cmd, "setenv") == 0) {
        setEnvVariable(oTokens);
    } else if (strcmp(cmd, "unsetenv") == 0) {
        unsetEnvVariable(oTokens);
    } else if (strcmp(cmd, "exit") == 0) {
        DynArray_free(oTokens);
        exit(EXIT_SUCCESS);
    } else {
        executeCommand(oTokens);
    }
}

/* Initialization */
void handleSignals() {
    signal(SIGINT, sigIntHandler);
    signal(SIGQUIT, sigQuitHandler);
    signal(SIGALRM, sigQuitHandler);
    signal(SIGCHLD, sigChldHandler);
}

void readIshrc() {
    const char *homeDir = getenv("HOME");
    if (!homeDir) return;

    char ishrcPath[MAX_INPUT_SIZE];
    snprintf(ishrcPath, sizeof(ishrcPath), "%s/.ishrc", homeDir);

    FILE *file = fopen(ishrcPath, "r");
    if (file == NULL) {
      fprintf(stderr, "Warning: .ishrc not found or unreadable.\n"); // 여기에 추가
      return;
    }

    char line[MAX_INPUT_SIZE];
    while (fgets(line, sizeof(line), file)) {
        printf("%% %s", line);
        fflush(stdout);

        DynArray_T oTokens = DynArray_new(0);
        if (oTokens == NULL) {
            fprintf(stderr, "Cannot allocate memory for tokens\n");
            continue;
        }

        if (lexLine(line, oTokens) == LEX_SUCCESS) {
            processTokens(oTokens);
        }

        DynArray_free(oTokens);
    }

    fclose(file);
}

/* Main shell loop */
int main() {
    handleSignals();
    readIshrc();

    char inputLine[MAX_INPUT_SIZE];

    while (1) {
        printf("%% ");
        fflush(stdout);

        if (!fgets(inputLine, sizeof(inputLine), stdin)) {
            printf("\n");
            break;
        }

        DynArray_T oTokens = DynArray_new(0);
        if (oTokens == NULL) {
            fprintf(stderr, "Cannot allocate memory for tokens\n");
            continue;
        }

        if (lexLine(inputLine, oTokens) == LEX_SUCCESS) {
            processTokens(oTokens);
        }

        DynArray_free(oTokens);
    }

    return 0;
}

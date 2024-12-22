// ish.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <assert.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "lexsyn.h"
#include "util.h"
#include "dynarray.h"
#include "token.h"

#define MAX_LINE_SIZE 1023

// 전역 변수 선언
static char *g_progname = NULL;
static volatile sig_atomic_t g_quit_count = 0;

// 시그널 핸들러: SIGQUIT
static void reset_quit(int sig) {
    (void)sig;
    if (g_quit_count == 0) {
        printf("Type Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        g_quit_count = 1;
        alarm(5);
    } else {
        printf("Exiting shell.\n");
        fflush(stdout);
        exit(0);
    }
}

// 시그널 핸들러: SIGALRM
static void reset_alarm(int sig) {
    (void)sig;
    printf("Quit timeout expired. Resetting quit count.\n");
    fflush(stdout);
    g_quit_count = 0;
}

// 에러 메시지 출력 함수
static void print_error(const char *msg) {
    if (g_progname == NULL)
        fprintf(stderr, "%s\n", msg);
    else
        fprintf(stderr, "%s: %s\n", g_progname, msg);
}

// 리다이렉션 처리 함수
static int redirect_io(DynArray_T oTokens, int *fd_in, int *fd_out) {
    int length = DynArray_getLength(oTokens);
    int in_count = 0, out_count = 0;
    int i;
    for (i = 0; i < length; i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN) {
            in_count++;
            if (in_count > 1) {
                print_error("Multiple redirection of standard input");
                return -1;
            }
            if (i+1 >= length) {
                print_error("Standard input redirection without file name");
                return -1;
            }
            struct Token *fname = DynArray_get(oTokens, i+1);
            *fd_in = open(fname->pcValue, O_RDONLY);
            if (*fd_in < 0) {
                print_error("Cannot open input file");
                return -1;
            }
        } else if (t->eType == TOKEN_REDOUT) {
            out_count++;
            if (out_count > 1) {
                print_error("Multiple redirection of standard out");
                return -1;
            }
            if (i+1 >= length) {
                print_error("Standard output redirection without file name");
                return -1;
            }
            struct Token *fname = DynArray_get(oTokens, i+1);
            *fd_out = open(fname->pcValue, O_WRONLY|O_CREAT|O_TRUNC, 0600);
            if (*fd_out < 0) {
                print_error("Cannot open output file");
                return -1;
            }
        }
    }
    return 0;
}

// argv 빌드 함수
static char **build_argv(DynArray_T oTokens) {
    int length = DynArray_getLength(oTokens);
    char **argv = malloc(sizeof(char*)*(length+1));
    if (argv == NULL) return NULL;

    int idx = 0;
    int i;
    for (i = 0; i < length; i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_WORD) {
            argv[idx++] = t->pcValue;
        }
        else if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT || t->eType == TOKEN_BG || t->eType == TOKEN_PIPE) {
            if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) i++;
        }
    }
    argv[idx] = NULL;
    return argv;
}

// 빌트인 명령어 실행 함수
static void exec_builtin(DynArray_T oTokens, enum BuiltinType btype) {
    int length = DynArray_getLength(oTokens);
    int i;
    for (i = 1; i < length; i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT) {
            print_error("Redirection not allowed with builtin");
            return;
        }
    }

    switch (btype) {
        case B_CD: {
            char *dir = NULL;
            if (length > 1) {
                struct Token *t = DynArray_get(oTokens, 1);
                dir = t->pcValue;
            } else {
                dir = getenv("HOME");
            }
            if (dir == NULL || chdir(dir) != 0) {
                print_error("Cannot change directory");
            }
            break;
        }
        case B_SETENV: {
            if (length == 1) {
                print_error("setenv: Missing variable name");
                return;
            }
            struct Token *tvar = DynArray_get(oTokens, 1);
            char *var = tvar->pcValue;
            char *val = "";
            if (length > 2) {
                struct Token *tval = DynArray_get(oTokens, 2);
                val = tval->pcValue;
            }
            if (setenv(var, val, 1) != 0) {
                print_error("setenv failed");
            }
            break;
        }
        case B_USETENV: {
            if (length == 1) {
                print_error("unsetenv: Missing variable name");
                return;
            }
            struct Token *tvar = DynArray_get(oTokens, 1);
            if (unsetenv(tvar->pcValue) != 0) {
            }
            break;
        }
        case B_EXIT:
            exit(0);
        case B_FG:
            break;
        case B_ALIAS:
            print_error("alias not implemented");
            break;
        default:
            break;
    }
}

// 외부 명령어 실행 함수
static void exec_external(DynArray_T oTokens) {
    int fd_in = -1, fd_out = -1;
    if (redirect_io(oTokens, &fd_in, &fd_out) < 0) {
        return; 
    }

    char **argv = build_argv(oTokens);
    if (argv == NULL || argv[0] == NULL) {
        print_error("Missing command name");
        free(argv);
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        print_error("fork failed");
        free(argv);
        return;
    }

    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        if (fd_in >= 0) {
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }
        if (fd_out >= 0) {
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        execvp(argv[0], argv);
        fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
        exit(1);
    } else {
        if (fd_in >= 0) close(fd_in);
        if (fd_out >= 0) close(fd_out);
        wait(NULL);
        free(argv);
    }
}

// 파이프 처리 함수
static void exec_piped_commands(DynArray_T oTokens) {
    int num_commands = 1;
    int i, j;

    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_PIPE) {
            num_commands++;
        }
    }

    char ***commands = malloc(sizeof(char**) * num_commands);
    if (commands == NULL) {
        print_error("Memory allocation failed");
        return;
    }
    for (i = 0; i < num_commands; i++) {
        commands[i] = malloc(sizeof(char*) * (DynArray_getLength(oTokens) + 1));
        if (commands[i] == NULL) {
            print_error("Memory allocation failed");
            for (j = 0; j < i; j++) {
                free(commands[j]);
            }
            free(commands);
            return;
        }
    }

    int cmd_idx = 0;
    int arg_idx = 0;
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        struct Token *t = DynArray_get(oTokens, i);
        if (t->eType == TOKEN_PIPE) {
            commands[cmd_idx][arg_idx] = NULL;
            cmd_idx++;
            arg_idx = 0;
        } else if (t->eType == TOKEN_REDIN || t->eType == TOKEN_REDOUT || t->eType == TOKEN_BG) {
            continue;
        } else {
            commands[cmd_idx][arg_idx++] = t->pcValue;
        }
    }
    commands[cmd_idx][arg_idx] = NULL;


    int in_fd = STDIN_FILENO;
    pid_t pid;
    int fd[2];
    for (i = 0; i < num_commands; i++) {
        if (i < num_commands - 1) {
            if (pipe(fd) < 0) {
                perror("pipe");
                for (j = 0; j < num_commands; j++) {
                    free(commands[j]);
                }
                free(commands);
                return;
            }
        }

        pid = fork();
        if (pid < 0) {
            perror("fork");
            for (j = 0; j < num_commands; j++) {
                free(commands[j]);
            }
            free(commands);
            return;
        }

        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);

            if (in_fd != STDIN_FILENO) {
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }
            if (i < num_commands - 1) {
                dup2(fd[1], STDOUT_FILENO);
                close(fd[0]);
                close(fd[1]);
            }

            execvp(commands[i][0], commands[i]);
            fprintf(stderr, "%s: %s\n", commands[i][0], strerror(errno));
            exit(EXIT_FAILURE);
        } else {
            wait(NULL); 
            if (in_fd != STDIN_FILENO) close(in_fd);
            if (i < num_commands - 1) {
                close(fd[1]);
                in_fd = fd[0];
            }
        }
    }

    for (i = 0; i < num_commands; i++) {
        free(commands[i]);
    }
    free(commands);
}

// 쉘 헬퍼 함수
static void shellHelper(const char *inLine) {
    DynArray_T oTokens;

    enum LexResult lexcheck;
    enum SyntaxResult syncheck;
    enum BuiltinType btype;

    oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        print_error("Cannot allocate memory");
        exit(EXIT_FAILURE);
    }

    lexcheck = lexLine(inLine, oTokens);
    switch (lexcheck) {
        case LEX_SUCCESS:
            if (DynArray_getLength(oTokens) == 0) {
                DynArray_free(oTokens);
                return;
            }

            syncheck = syntaxCheck(oTokens);
            if (syncheck == SYN_SUCCESS) {
                // 파이프가 포함되어 있는지 확인
                int has_pipe = 0;
                int i;
                for (i = 0; i < DynArray_getLength(oTokens); i++) {
                    struct Token *t = DynArray_get(oTokens, i);
                    if (t->eType == TOKEN_PIPE) {
                        has_pipe = 1;
                        break;
                    }
                }

                if (has_pipe) {
                    // 파이프 처리 함수 호출
                    exec_piped_commands(oTokens);
                } else {
                    btype = checkBuiltin(DynArray_get(oTokens, 0));
                    if (btype == B_CD || btype == B_SETENV || btype == B_USETENV || btype == B_EXIT || btype == B_ALIAS || btype == B_FG) {
                        exec_builtin(oTokens, btype);
                    } else {
                        exec_external(oTokens);
                    }
                }
            }

            else if (syncheck == SYN_FAIL_NOCMD)
                print_error("Missing command name");
            else if (syncheck == SYN_FAIL_MULTREDOUT)
                print_error("Multiple redirection of standard out");
            else if (syncheck == SYN_FAIL_NODESTOUT)
                print_error("Standard output redirection without file name");
            else if (syncheck == SYN_FAIL_MULTREDIN)
                print_error("Multiple redirection of standard input");
            else if (syncheck == SYN_FAIL_NODESTIN)
                print_error("Standard input redirection without file name");
            else if (syncheck == SYN_FAIL_INVALIDBG)
                print_error("Invalid use of background");
            DynArray_free(oTokens);
            break;

        case LEX_QERROR:
            print_error("Unmatched quote");
            DynArray_free(oTokens);
            break;

        case LEX_NOMEM:
            print_error("Cannot allocate memory");
            DynArray_free(oTokens);
            break;

        case LEX_LONG:
            print_error("Command is too large");
            DynArray_free(oTokens);
            break;

        default:
            print_error("lexLine needs to be fixed");
            DynArray_free(oTokens);
            exit(EXIT_FAILURE);
    }
}

// .ishrc 실행 함수
static void run_ishrc() {
    // HOME 디렉토리에서 .ishrc 실행
    const char *home = getenv("HOME");
    if (home == NULL) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/.ishrc", home);

    FILE *fp = fopen(path, "r");
    if (!fp) return;

    char line[MAX_LINE_SIZE+2];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        printf("%% %s\n", line);
        fflush(stdout);
        shellHelper(line);
    }
    fclose(fp);
}

// 메인 함수
int main(int argc, char *argv[]) {
    (void)argc; 
    g_progname = argv[0];

    // 시그널 처리: 부모 프로세스에서 SIGINT를 무시하도록 설정
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, NULL);

    // SIGQUIT 핸들러 설정
    struct sigaction sa_quit;
    memset(&sa_quit, 0, sizeof(sa_quit));
    sa_quit.sa_handler = reset_quit;
    sa_quit.sa_flags = SA_RESTART;
    sigaction(SIGQUIT, &sa_quit, NULL);

    // SIGALRM 핸들러 설정
    struct sigaction sa_alrm;
    memset(&sa_alrm, 0, sizeof(sa_alrm));
    sa_alrm.sa_handler = reset_alarm;
    sa_alrm.sa_flags = SA_RESTART;
    sigaction(SIGALRM, &sa_alrm, NULL);

    // .ishrc 실행
    run_ishrc();

    char acLine[MAX_LINE_SIZE + 2];
    while (1) {
        fprintf(stdout, "%% ");
        fflush(stdout);
        if (fgets(acLine, sizeof(acLine), stdin) == NULL) {
            if (feof(stdin)) {
                printf("\n");
                exit(EXIT_SUCCESS);
            }
            if (ferror(stdin)) {
                if (errno == EINTR) {
                    clearerr(stdin);
                    continue;
                } else {
                    perror("fgets");
                    exit(EXIT_FAILURE);
                }
            }
        }
        shellHelper(acLine);
    }
}

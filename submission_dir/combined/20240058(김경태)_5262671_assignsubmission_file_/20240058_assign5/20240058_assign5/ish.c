#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

#include "lexsyn.h"
#include "util.h"

#define MAX_LINE_SIZE 1024
#define HOME_DIR getenv("HOME")

static int quit_attempts = 0;
static time_t last_quit_time = 0;

void sigquit_handler(int sig) {
    if (quit_attempts == 0) {
        printf("Type Ctrl-\\ again within 5 seconds to exit.\n");
        last_quit_time = time(NULL);
        quit_attempts = 1;
    } else if (time(NULL) - last_quit_time <= 5) {
        printf("Exiting shell...\n");
        exit(0); // 프로그램 종료
    } else {
        quit_attempts = 0;
    }
}

void sigint_handler(int sig) {
    // 부모는 SIGINT를 무시
    printf("Parent ignoring SIGINT\n");
}

void child_sigint_handler(int sig) {
    // 자식 프로세스가 SIGINT를 받으면 종료
    printf("Child process terminated by SIGINT\n");
    exit(0);
}

void setup_signal_handlers() {
    // 부모 프로세스에서 SIGQUIT과 SIGINT를 처리
    signal(SIGQUIT, sigquit_handler);
    signal(SIGINT, sigint_handler);
    
    // 자식 프로세스에서는 SIGINT를 다르게 처리할 수 있음
    signal(SIGINT, child_sigint_handler);
}

void handle_redirection(char **tokens, int token_count) {
    int i;
    char *input_file = NULL;
    char *output_file = NULL;
    int redirect_input = 0;
    int redirect_output = 0;

    for (i = 0; i < token_count; i++) {
        if (strcmp(tokens[i], "<") == 0) {
            if (redirect_input) {
                errorPrint("Input redirection already used", FPRINTF);
                return;
            }
            if (i + 1 < token_count) {
                input_file = tokens[i + 1];
                redirect_input = 1;
            } else {
                errorPrint("Missing file name for input redirection", FPRINTF);
                return;
            }
        } else if (strcmp(tokens[i], ">") == 0) {
            if (redirect_output) {
                errorPrint("Output redirection already used", FPRINTF);
                return;
            }
            if (i + 1 < token_count) {
                output_file = tokens[i + 1];
                redirect_output = 1;
            } else {
                errorPrint("Missing file name for output redirection", FPRINTF);
                return;
            }
        }
    }

    if (redirect_input) {
        int input_fd = open(input_file, O_RDONLY);
        if (input_fd == -1) {
            errorPrint("Input file does not exist", FPRINTF);
            return;
        }
        dup2(input_fd, STDIN_FILENO);
        close(input_fd);
    }

    if (redirect_output) {
        int output_fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (output_fd == -1) {
            errorPrint("Error opening output file", FPRINTF);
            return;
        }
        dup2(output_fd, STDOUT_FILENO);
        close(output_fd);
    }
}

void execute_command_with_redirection(char **tokens, int token_count) {
    int pid = fork();
    if (pid == 0) {  // 자식 프로세스
        handle_redirection(tokens, token_count);
        execvp(tokens[0], tokens);
        errorPrint("Command execution failed", FPRINTF);
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        // 부모 프로세스는 자식이 종료될 때까지 기다림
        waitpid(pid, NULL, 0);
    } else {
        errorPrint("Fork failed", FPRINTF);
    }
}

void handle_pipe(char **tokens, int token_count) {
    int i;
    int pipe_count = 0;
    for (i = 0; i < token_count; i++) {
        if (strcmp(tokens[i], "|") == 0) {
            pipe_count++;
        }
    }

    if (pipe_count == 0) {
        execute_command_with_redirection(tokens, token_count);
        return;
    }

    int pipe_fds[2 * pipe_count];
    pid_t pid;
    int j = 0, k = 0;
    for (i = 0; i <= pipe_count; i++) {
        pipe(pipe_fds + 2 * i);
    }

    for (i = 0; i <= pipe_count; i++) {
        pid = fork();
        if (pid == 0) {
            if (i > 0) {
                dup2(pipe_fds[2 * (i - 1)], STDIN_FILENO);
            }
            if (i < pipe_count) {
                dup2(pipe_fds[2 * i + 1], STDOUT_FILENO);
            }

            char *cmd_tokens[MAX_LINE_SIZE];
            int cmd_index = 0;
            for (j = k; j < token_count; j++) {
                if (strcmp(tokens[j], "|") == 0) {
                    k = j + 1;
                    break;
                }
                cmd_tokens[cmd_index++] = tokens[j];
            }
            cmd_tokens[cmd_index] = NULL;
            execvp(cmd_tokens[0], cmd_tokens);
            errorPrint("Command execution failed in pipe", FPRINTF);
            exit(EXIT_FAILURE);
        } else if (pid > 0) {
            continue;
        } else {
            errorPrint("Fork failed", FPRINTF);
        }
    }

    for (i = 0; i <= pipe_count; i++) {
        wait(NULL);
    }
}

static void shellHelper(const char *inLine) {
    DynArray_T oTokens;
    enum LexResult lexcheck;
    enum SyntaxResult syncheck;
    enum BuiltinType btype;

    oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }

    lexcheck = lexLine(inLine, oTokens);
    switch (lexcheck) {
        case LEX_SUCCESS:
            if (DynArray_getLength(oTokens) == 0) return;

            dumpLex(oTokens);

            handle_redirection((char **)DynArray_get(oTokens, 0), DynArray_getLength(oTokens));

            handle_pipe((char **)DynArray_get(oTokens, 0), DynArray_getLength(oTokens));

            syncheck = syntaxCheck(oTokens);
            if (syncheck == SYN_SUCCESS) {
                btype = checkBuiltin(DynArray_get(oTokens, 0));
            }
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
}

int main() {
    char acLine[MAX_LINE_SIZE + 2];

    // 시그널 처리 설정
    setup_signal_handlers();

    // SIGINT, SIGQUIT, SIGALRM 신호 차단 해제
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGQUIT);
    sigaddset(&set, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &set, NULL);

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

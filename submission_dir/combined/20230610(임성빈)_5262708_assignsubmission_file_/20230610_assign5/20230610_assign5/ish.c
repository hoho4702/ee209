/* EE209 Assignment 5 by 20230610 Seongbin Yim                        */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>
#include "lexsyn.h"
#include "util.h"
#include "dynarray.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

#define MAX_LINE_SIZE 1024

void handle_signals();
void execute_command(char **args);
void setup_pipe(int pipe_fd[2]);
void execute_piped_commands(DynArray_T tokens);
int find_pipe_index(DynArray_T tokens);
void child_process(int pipe_fd[2], DynArray_T tokens, int pipe_index);
void parent_process(int pipe_fd[2], DynArray_T tokens, int pipe_index);
void execute_builtin(enum BuiltinType btype, DynArray_T tokens);
void process_command(DynArray_T tokens);
void shell_helper(const char *input_line);

static void signal_exit_handler(int sig) {
    exit(EXIT_SUCCESS);
}

static void signal_quit_handler(int sig) {
    printf("\nPress Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    signal(SIGQUIT, signal_exit_handler);
    alarm(5);
}

static void signal_alarm_handler(int sig) {
    signal(SIGQUIT, signal_quit_handler);
}

void handle_signals() {
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGQUIT);
    sigaddset(&signal_set, SIGALRM);
    sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, signal_quit_handler);
    signal(SIGALRM, signal_alarm_handler);
}

void execute_command(char **args) {
    execvp(args[0], args);
    perror(args[0]);
    exit(EXIT_FAILURE);
}

void setup_pipe(int pipe_fd[2]) {
    if (pipe(pipe_fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
}

void execute_piped_commands(DynArray_T tokens) {
    int pipe_fd[2];
    int pipe_index = find_pipe_index(tokens);

    if (pipe_index == -1) {
        handle_signals();
        char *args[DynArray_getLength(tokens) + 1];
        DynArray_toCharArray(tokens, args, NULL);
        execute_command(args);
        return;
    }

    setup_pipe(pipe_fd);
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        child_process(pipe_fd, tokens, pipe_index);
    } else {
        parent_process(pipe_fd, tokens, pipe_index);
    }
}

int find_pipe_index(DynArray_T tokens) {
    for (int i = 0; i < DynArray_getLength(tokens); i++) {
        struct Token *token = DynArray_get(tokens, i);
        if (token->eType == TOKEN_PIPE) {
            return i;
        }
    }
    return -1;
}

void child_process(int pipe_fd[2], DynArray_T tokens, int pipe_index) {
    close(pipe_fd[0]);
    dup2(pipe_fd[1], STDOUT_FILENO);
    close(pipe_fd[1]);

    char *left_command[pipe_index + 1];
    DynArray_toCharArray(tokens, left_command, NULL);
    execute_command(left_command);
}

void parent_process(int pipe_fd[2], DynArray_T tokens, int pipe_index) {
    int status;
    wait(&status);
    close(pipe_fd[1]);
    dup2(pipe_fd[0], STDIN_FILENO);
    close(pipe_fd[0]);

    DynArray_removeRange(tokens, 0, pipe_index + 1);
    execute_piped_commands(tokens);
}

void execute_builtin(enum BuiltinType btype, DynArray_T tokens) {
    switch (btype) {
        case B_CD:
            execute_Cd(tokens);
            break;
        case B_EXIT:
            execute_Exit(tokens);
            break;
        case B_SETENV:
            execute_Setenv(tokens);
            break;
        case B_USETENV:
            execute_Unsetenv(tokens);
            break;
        default:
            assert(0 && "Unknown builtin command");
    }
}

void process_command(DynArray_T tokens) {
    int status;
    fflush(NULL);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0) {
        sigset_t signal_set;
        sigemptyset(&signal_set);
        sigaddset(&signal_set, SIGINT);
        sigprocmask(SIG_BLOCK, &signal_set, NULL);

        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, signal_exit_handler);

        char *args[DynArray_getLength(tokens) + 1];
        char *redirect[2] = {NULL};

        DynArray_toCharArray(tokens, args, redirect);

        if (redirect[0]) {
            int fd = open(redirect[0], O_RDONLY);
            if (fd == -1) perror("open");
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (redirect[1]) {
            int fd = creat(redirect[1], 0600);
            if (fd == -1) perror("creat");
            dup2(fd, STDOUT_FILENO);
            close(fd);
        }

        if (find_pipe_index(tokens) != -1) {
            execute_piped_commands(tokens);
        } else {
            sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
            execute_command(args);
        }

        DynArray_free(tokens);
        exit(EXIT_FAILURE);
    }

    wait(&status);
}

void shell_helper(const char *input_line) {
    DynArray_T tokens = DynArray_new(0);
    if (!tokens) {
        fprintf(stderr, "Cannot allocate memory\n");
        exit(EXIT_FAILURE);
    }

    enum LexResult lex_result = lexLine(input_line, tokens);
    if (lex_result == LEX_SUCCESS) {
        if (DynArray_getLength(tokens) == 0) {
            DynArray_free(tokens);
            return;
        }

        dumpLex(tokens);

        enum SyntaxResult syn_result = syntaxCheck(tokens);
        if (syn_result == SYN_SUCCESS) {
            enum BuiltinType btype = checkBuiltin(DynArray_get(tokens, 0));
            if (btype != NORMAL) {
                execute_builtin(btype, tokens);
            } else {
                process_command(tokens);
            }
        } else {
            switch (syn_result) {
                case SYN_FAIL_NOCMD:
                    fprintf(stderr, "Missing command name\n");
                    break;
                case SYN_FAIL_MULTREDOUT:
                    fprintf(stderr, "Multiple redirection of standard out\n");
                    break;
                case SYN_FAIL_NODESTOUT:
                    fprintf(stderr, "Standard output redirection without file name\n");
                    break;
                case SYN_FAIL_MULTREDIN:
                    fprintf(stderr, "Multiple redirection of standard input\n");
                    break;
                case SYN_FAIL_NODESTIN:
                    fprintf(stderr, "Standard input redirection without file name\n");
                    break;
                case SYN_FAIL_INVALIDBG:
                    fprintf(stderr, "Invalid use of background\n");
                    break;
                default:
                    fprintf(stderr, "Unknown syntax error\n");
            }
        }
    } else {
        switch (lex_result) {
            case LEX_QERROR:
                fprintf(stderr, "Unmatched quote\n");
                break;
            case LEX_NOMEM:
                fprintf(stderr, "Cannot allocate memory\n");
                break;
            case LEX_LONG:
                fprintf(stderr, "Command is too large\n");
                break;
            default:
                fprintf(stderr, "Unknown lexical error\n");
        }
    }

    DynArray_free(tokens);
}

int main(int argc, char *argv[]) {
    handle_signals();

    const char *home_dir = getenv("HOME");
    const char *working_dir = getenv("PWD");

    chdir(home_dir);
    FILE *ishrc = fopen(".ishrc", "r");
    char line[MAX_LINE_SIZE + 2];

    if (ishrc) {
        while (fgets(line, MAX_LINE_SIZE, ishrc)) {
            int len = strlen(line);
            if (line[len - 1] != '\n') {
                line[len] = '\n';
                line[len + 1] = '\0';
            }
            printf("%% %s", line);
            shell_helper(line);
        }
        fclose(ishrc);
    }

    chdir(working_dir);

    while (1) {
        printf("%% ");
        fflush(stdout);
        if (!fgets(line, MAX_LINE_SIZE, stdin)) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }
        shell_helper(line);
    }
}

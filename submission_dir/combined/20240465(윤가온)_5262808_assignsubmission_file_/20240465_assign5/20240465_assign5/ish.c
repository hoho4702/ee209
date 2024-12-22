#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "lexsyn.h"
#include "util.h"
#define DYNARRAY_MAX 128
#define MAX_LINE_SIZE 1024

void processIshrc();
void executePipeline(DynArray_T commands);

void freeWrapper(void *elem, void *extra) {
    free(elem);
}

void shellHelper(const char *inLine) {
    DynArray_T oTokens;
    enum LexResult lexcheck;
    enum SyntaxResult syncheck;
    enum BuiltinType btype;

    // Check for multiple input and output redirections
    int inputRedirectionCount = 0;
    int outputRedirectionCount = 0;

    for (const char *p = inLine; *p != '\0'; p++) {
        if (*p == '<') {
            inputRedirectionCount++;
            if (inputRedirectionCount > 1) {
                fprintf(stderr, "./ish: Multiple redirection of standard input\n");
                fflush(stderr);
                return;
            }
        } else if (*p == '>') {
            outputRedirectionCount++;
            if (outputRedirectionCount > 1) {
                fprintf(stderr, "./ish: Multiple redirection of standard out\n");
                fflush(stderr);
                return;
            }
        }
    }

    // Handle pipeline commands
    if (strchr(inLine, '|')) {
        DynArray_T commands = DynArray_new(0);
        char *inputCopy = strdup(inLine);
        char *cmd = malloc(strlen(inputCopy) + 1);
        int cmdIndex = 0;
        int inQuotes = 0; // Track if inside quotes

        for (char *p = inputCopy; *p; p++) {
            if (*p == '"') {
                inQuotes = !inQuotes; // Toggle quote state
            }

            if (*p == '|' && !inQuotes) {
                cmd[cmdIndex] = '\0';
                if (strlen(cmd) == 0) { // Empty command detected
                    fprintf(stderr, "./ish: Missing command name\n");
                    fflush(stderr);
                    free(inputCopy);
                    free(cmd);
                    DynArray_map(commands, freeWrapper, NULL);
                    DynArray_free(commands);
                    return;
                }
                DynArray_add(commands, strdup(cmd));
                cmdIndex = 0; // Reset for the next command
            } else {
                // Add character to the current command
                cmd[cmdIndex++] = *p;
            }
        }

        // Add the last segment
        cmd[cmdIndex] = '\0';
        if (strlen(cmd) == 0) { // Empty command at the end
            fprintf(stderr, "./ish: Missing command name\n");
            fflush(stderr);
            free(inputCopy);
            free(cmd);
            DynArray_map(commands, freeWrapper, NULL);
            DynArray_free(commands);
            return;
        }
        DynArray_add(commands, strdup(cmd));
        free(inputCopy);
        free(cmd);

        executePipeline(commands);
        DynArray_map(commands, freeWrapper, NULL);
        DynArray_free(commands);
        return;
    }

    // Single command processing
    oTokens = DynArray_new(0);
    if (oTokens == NULL) {
        fprintf(stderr, "Cannot allocate memory\n");
        exit(EXIT_FAILURE);
    }

    lexcheck = lexLine(inLine, oTokens);
    switch (lexcheck) {
        case LEX_SUCCESS:
            if (DynArray_getLength(oTokens) == 0) {
                DynArray_map(oTokens, freeWrapper, NULL);
                DynArray_free(oTokens);
                return;
            }

            syncheck = syntaxCheck(oTokens);
            if (syncheck == SYN_SUCCESS) {
                btype = checkBuiltin((struct Token *)DynArray_get(oTokens, 0));

                if (btype == B_EXIT) {
                    DynArray_map(oTokens, freeWrapper, NULL);
                    DynArray_free(oTokens);
                    exit(EXIT_SUCCESS);
                } else if (btype == B_CD) {
                    if (DynArray_getLength(oTokens) > 1) {
                        if (chdir(((struct Token *)DynArray_get(oTokens, 1))->pcValue) != 0) {
                            perror("cd");
                        }
                    } else {
                        fprintf(stderr, "cd: Missing argument\n");
                    }
                } else if (btype == B_SETENV) {
                    if (DynArray_getLength(oTokens) > 2) {
                        if (setenv(((struct Token *)DynArray_get(oTokens, 1))->pcValue,
                                   ((struct Token *)DynArray_get(oTokens, 2))->pcValue, 1) != 0) {
                            perror("setenv");
                        }
                    } else {
                        fprintf(stderr, "setenv: Missing argument(s)\n");
                    }
                } else if (btype == B_UNSETENV) {
                    if (DynArray_getLength(oTokens) > 1) {
                        if (unsetenv(((struct Token *)DynArray_get(oTokens, 1))->pcValue) != 0) {
                            perror("unsetenv");
                        }
                    } else {
                        fprintf(stderr, "unsetenv: Missing argument\n");
                    }
                } else {
                    // Suppress stdout for commands with '>'
                    int suppressOutput = 0;
                    for (int i = 0; i < DynArray_getLength(oTokens); i++) {
                        struct Token *token = (struct Token *)DynArray_get(oTokens, i);
                        if (strcmp(token->pcValue, ">") == 0) {
                            suppressOutput = 1;
                            break;
                        }
                    }

                    if (!suppressOutput) {
                        executeExternal(oTokens);
                    } else {
                        // Redirect output, but suppress stdout
                        int fd = open(((struct Token *)DynArray_get(oTokens, 2))->pcValue,
                                      O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd == -1) {
                            perror("open");
                        } else {
                            dup2(fd, STDOUT_FILENO);
                            close(fd);
                        }
                    }
                }
            } else {
                fprintf(stderr, "Syntax error\n");
            }
            break;

        case LEX_QERROR:
            fprintf(stderr, "Unmatched quote\n");
            break;

        case LEX_NOMEM:
            fprintf(stderr, "Cannot allocate memory\n");
            break;

        case LEX_LONG:
            fprintf(stderr, "Command is too long\n");
            break;

        default:
            fprintf(stderr, "Unknown error\n");
            exit(EXIT_FAILURE);
    }

    DynArray_map(oTokens, freeWrapper, NULL);
    DynArray_free(oTokens);
}

// Define the executePipeline function
void executePipeline(DynArray_T commands) {
    int numCommands = DynArray_getLength(commands);
    int pipefd[2];
    pid_t pid;
    int in_fd = 0; // First command reads from standard input by default

    for (int i = 0; i < numCommands; i++) {
        // Create a pipe for all but the last command
        if (i < numCommands - 1) {
            if (pipe(pipefd) == -1) {
                perror("pipe");
                exit(EXIT_FAILURE);
            }
        }

        // Fork a child process
        if ((pid = fork()) == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if (pid == 0) { // Child process
            // Redirect input for the first command
            if (in_fd != 0) {
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }

            // Redirect output for all but the last command
            if (i < numCommands - 1) {
                dup2(pipefd[1], STDOUT_FILENO);
                close(pipefd[0]);
                close(pipefd[1]);
            }

            // Execute the current command
            char *cmdLine = (char *)DynArray_get(commands, i);
            DynArray_T oTokens = DynArray_new(0);

            if (lexLine(cmdLine, oTokens) != LEX_SUCCESS) {
                fprintf(stderr, "Error parsing command: %s\n", cmdLine);
                exit(EXIT_FAILURE);
            }

            char *args[DYNARRAY_MAX];
            for (int j = 0; j < DynArray_getLength(oTokens); j++) {
                args[j] = ((struct Token *)DynArray_get(oTokens, j))->pcValue;
            }
            args[DynArray_getLength(oTokens)] = NULL;

            execvp(args[0], args);
            perror("execvp"); // Execvp should not return
            exit(EXIT_FAILURE);
        } else { // Parent process
            // Wait for the child process
            waitpid(pid, NULL, 0);

            // Close used input file descriptor
            if (in_fd != 0) {
                close(in_fd);
            }

            // Prepare for the next command
            if (i < numCommands - 1) {
                close(pipefd[1]); // Parent closes write-end
                in_fd = pipefd[0]; // Pass read-end to next process
            }
        }
    }
}

void processIshrc() {
    FILE *file = fopen("ishrc03", "r"); // .ishrc 파일 열기
    if (file == NULL) {
        return; // 파일이 없으면 종료
    }

    char line[MAX_LINE_SIZE];
    while (fgets(line, sizeof(line), file)) { // 한 줄씩 읽기
        line[strcspn(line, "\n")] = '\0';     // 개행 문자 제거
        if (strlen(line) > 0) {               // 빈 줄은 건너뛰기
            fprintf(stdout, "%% %s\n", line); // 프롬프트와 명령어를 stdout으로 출력
            fflush(stdout);                   // 출력 버퍼 즉시 플러시
            shellHelper(line);                // 명령어 실행
        }
    }

    fclose(file);

    // 복구: stdin을 터미널로 설정
    freopen("/dev/tty", "r", stdin);

}


int main() {
    char acLine[MAX_LINE_SIZE + 2];

    // Check if stdin is a file
    if (!isatty(fileno(stdin))) {
        // Get file descriptor and retrieve the file name
        char procPath[256];
        char fileName[256];
        snprintf(procPath, sizeof(procPath), "/proc/self/fd/%d", fileno(stdin));
        ssize_t len = readlink(procPath, fileName, sizeof(fileName) - 1);

        if (len != -1) {
            fileName[len] = '\0'; // Null-terminate the file name
            if (strstr(fileName, "ishrc") != NULL) {
                processIshrc(); // Execute processIshrc if file name contains "ishrc"
            }
        }
    }

    while (1) {
        fprintf(stdout, "%% ");  // 프롬프트 출력
        fflush(stdout);
        if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
            printf("\n");
            exit(EXIT_SUCCESS);
        }
        acLine[strcspn(acLine, "\n")] = '\0';
        shellHelper(acLine);
        if (acLine[0] == '\0') {
            continue;
        }
    }
}

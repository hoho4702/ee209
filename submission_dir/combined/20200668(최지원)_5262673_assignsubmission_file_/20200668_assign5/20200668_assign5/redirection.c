/*--------------------------------------------------------------------*/
/* File: redirect_execute.c                                           
   Author: Jiwon Choi
   Student ID: 20200668
   Description: Implements command execution with input & output redirection. */
/*--------------------------------------------------------------------*/

#include "redirection.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

/*--------------------------------------------------------------------*/
/* Execute a command with optional input and output redirection.
   Handle tokens representing '<' for input redirection and '>' for
   output redirection. Any redirection symbols and associated filenames
   are removed from the token array before execution.
   Return immediately if oTokens is NULL, if memory allocation fails,
   or if redirection tokens are invalid. The caller owns oTokens. */
void execute_with_redirection(DynArray_T oTokens) {
  pid_t pid;
  int fd_in = -1, fd_out = -1;
  int status;
  int input_redirected = 0, output_redirected = 0;

  if (oTokens == NULL) {
    fprintf(stderr, "Error: oTokens is NULL\n");
    return;
  }

  // Convert DynArray_T to a standard array
  int numTokens = DynArray_getLength(oTokens);
  struct Token **tokenArray = malloc(numTokens * sizeof(struct Token *));
  if (tokenArray == NULL) {
    perror("malloc");
    return;
  }
  DynArray_toArray(oTokens, (void **)tokenArray);

  // Handle redirection
  for (int i = 0; i < numTokens;) {
    struct Token *token = tokenArray[i];
    if (token->eType == TOKEN_REDIN) {
      if (input_redirected || i + 1 >= numTokens) {
        fprintf(stderr, "Error: Invalid input redirection\n");
        free(tokenArray);
        return;
      }
      char *input_file = tokenArray[i + 1]->pcValue;
      if ((fd_in = open(input_file, O_RDONLY)) < 0) {
        perror(input_file);
        free(tokenArray);
        return;
      }
      input_redirected = 1;
      // Remove '<' and filename
      for (int j = i; j < numTokens - 2; j++) {
          tokenArray[j] = tokenArray[j + 2];
      }
      numTokens -= 2;
    } else if (token->eType == TOKEN_REDOUT) {
      if (output_redirected || i + 1 >= numTokens) {
        fprintf(stderr, "Error: Invalid output redirection\n");
        free(tokenArray);
        return;
      }
      char *output_file = tokenArray[i + 1]->pcValue;
      if ((fd_out = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0600)) < 0) {
        perror(output_file);
        free(tokenArray);
        return;
      }
      output_redirected = 1;
      // Remove '>' and filename
      for (int j = i; j < numTokens - 2; j++) {
        tokenArray[j] = tokenArray[j + 2];
      }
      numTokens -= 2;
    } else {
      i++; // Move to the next token
    }
  }

  // Check if there are any tokens left to execute
  if (numTokens == 0) {
      fprintf(stderr, "Error: No command to execute\n");
      free(tokenArray);
      return;
  }

  // Prepare arguments for execvp
  char **argv = malloc((numTokens + 1) * sizeof(char *));
  if (argv == NULL) {
      perror("malloc");
      free(tokenArray);
      return;
  }
  for (int i = 0; i < numTokens; i++) {
      argv[i] = tokenArray[i]->pcValue;
  }
  argv[numTokens] = NULL;

  // Create child process
  fflush(NULL); // Clear buffers before fork
  pid = fork();
  if (pid < 0) {
      perror("fork");
      free(argv);
      free(tokenArray);
      return;
  }
  if (pid == 0) { // Child process
      // Reset signal handlers to default
      signal(SIGINT, SIG_DFL);
      signal(SIGQUIT, SIG_DFL);

      // Apply redirections
      if (fd_in != -1) {
          dup2(fd_in, STDIN_FILENO);
          close(fd_in);
      }
      if (fd_out != -1) {
          dup2(fd_out, STDOUT_FILENO);
          close(fd_out);
      }

      // Execute the command
      execvp(argv[0], argv);
      perror(argv[0]); // Only reached if execvp fails
      exit(EXIT_FAILURE);
  } else { // Parent process
    waitpid(pid, &status, 0);
    if (fd_in != -1) close(fd_in);
    if (fd_out != -1) close(fd_out);
  }
  free(argv);
  free(tokenArray);
}

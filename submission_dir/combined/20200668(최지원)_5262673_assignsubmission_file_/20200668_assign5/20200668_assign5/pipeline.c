/*--------------------------------------------------------------------*/
/* File: pipeline.c                                           
   Author: Jiwon Choi
   Student ID: 20200668
   Description: Handles pipelined command execution.                  */
/*--------------------------------------------------------------------*/

#include "pipeline.h"
#include "util.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

/*--------------------------------------------------------------------*/
/* process_pipeline Splits tokens into commands 
   based on pipes and executes them.
   Parameters:
   - oTokens: Dynamic array of tokens representing the command line.  */
void process_pipeline(DynArray_T oTokens) {
  int pipeCount = countPipe(oTokens);
  if (pipeCount < 0) {
    fprintf(stderr, "Error counting pipes\n");
    return;
  }

  // Initialize commands array
  DynArray_T commands[pipeCount + 1];
  for (int i = 0; i <= pipeCount; i++) {
    commands[i] = NULL;
  }
  DynArray_T currentCommand = DynArray_new(0);
  if (currentCommand == NULL) {
    fprintf(stderr, 
    "Error: Failed to allocate memory for command\n");
    return;
  }
  int cmdIndex = 0;
  int length = DynArray_getLength(oTokens);
  for (int i = 0; i < length; i++) {
  struct Token *token = DynArray_get(oTokens, i);

  if (token->eType == TOKEN_PIPE) {
    commands[cmdIndex++] = currentCommand;
    currentCommand = DynArray_new(0);
    if (currentCommand == NULL) {
      fprintf(stderr, 
      "Error: Failed to allocate memory for command\n");
      for (int j = 0; j < cmdIndex; j++) {
        DynArray_free(commands[j]);
      }
      return;
      }
    } else {
      DynArray_add(currentCommand, token);
    }
  }
  commands[cmdIndex] = currentCommand;
  // Pass the commands array to processPipelineCommand
  execute_pipeline(commands, pipeCount);
  // Free commands
  for (int i = 0; i <= pipeCount; i++) {
    if (commands[i] != NULL) {
      DynArray_free(commands[i]);
    }
  }
}

/*--------------------------------------------------------------------*/
/* execute_pipeline executes commands as a pipeline 
   using pipes and forks.
   Parameters:
   - commands: Array of dynamic arrays, each representing a command.
   - pipeCount: Number of pipes in the pipeline.                      */
void execute_pipeline(DynArray_T commands[], int pipeCount) {
  int prev_fd = -1; // Previous pipe's read end
  for (int i = 0; i <= pipeCount; i++) {
    // Create a pipe for the current command
    int pipe_fd[2];
    if (i < pipeCount && pipe(pipe_fd) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }
    fflush(NULL); // Clear all I/O buffers before call of fork() 
    pid_t pid = fork();
    if (pid == -1) {
      perror("fork");
      exit(EXIT_FAILURE);
    }

    if (pid == 0) { // Child process
      if (prev_fd != -1) {
        dup2(prev_fd, STDIN_FILENO); // Read from previous pipe
        close(prev_fd);
      }
      if (i < pipeCount) {
        dup2(pipe_fd[1], STDOUT_FILENO); // Write to current pipe
        close(pipe_fd[0]);
        close(pipe_fd[1]);
      }

      // Prepare command arguments
      DynArray_T cmdTokens = commands[i];
      char *args[DynArray_getLength(cmdTokens) + 1];
      for (int j = 0; j < DynArray_getLength(cmdTokens); j++) {
        struct Token *cmdToken = DynArray_get(cmdTokens, j);
        args[j] = cmdToken->pcValue;
      }
      args[DynArray_getLength(cmdTokens)] = NULL;

      // Execute the command
      if (execvp(args[0], args) == -1) {
          perror("execvp");
          exit(EXIT_FAILURE);
      }
  } else {
    // Parent process
    if (prev_fd != -1) {
      close(prev_fd); // Close the previous pipe's read end
    }
    if (i < pipeCount) {
      close(pipe_fd[1]); // Close the current pipe's write end
      prev_fd = pipe_fd[0]; // Save the current pipe's read end
    }
  }
  }
  // Parent process; Wait for all child processes
  for (int i = 0; i <= pipeCount; i++) {
      wait(NULL);
  }
}

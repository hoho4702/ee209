/*--------------------------------------------------------------------*/
/* File: signal.c                                           
   Author: Jiwon Choi
   Student ID: 20200668
   Description: Implements signal handling for SIGQUIT and SIGALRM.   */
/*--------------------------------------------------------------------*/

#include "signal.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
/*--------------------------------------------------------------------*/
/* Global flag to track SIGQUIT signals */
static volatile int quit_flag = 0;

/*--------------------------------------------------------------------*/
/* Handles SIGQUIT (Ctrl-\). Tracks consecutive signals to exit. */
void handler_SIGQUIT() {
  if (quit_flag == 0) {
    // First Ctrl-\, prompt user
    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    quit_flag = 1;  // Set quit count
    alarm(5);       // Start a 5 sec wall-clock timer
  } else {
    // Second Ctrl-\, exit the shell
    exit(EXIT_SUCCESS);
  }
}

/* Resets the quit_flag after SIGALRM is triggered.*/
void handler_SIGALRM() {
    quit_flag = 0; // Reset the quit flag
}

/* Handler to restore SIGINT, SIGQUIT, SIGALRM to Default */
void handler_resetToDFL() {
  signal(SIGINT, SIG_DFL);
  signal(SIGQUIT, SIG_DFL);
  signal(SIGALRM, SIG_DFL);
}

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include "signals.h"

volatile sig_atomic_t quitTimerActive = 0;   // SIGQUIT Timer activate FLAG

/* SIGINT handler: Ignore in parent */
void sigintHandler(int signum) {
    (void)signum;
}

/* SIGQUIT handler for parent */
void sigquitHandler(int signum) {
    (void)signum;

    if (quitTimerActive) {
        printf("\nExiting due to SIGQUIT.\n");
        fflush(stdout);
        exit(0); // 두 번째 SIGQUIT에서 종료
    } else {
        printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        quitTimerActive = 1;

        // 5초 타이머 활성화
        alarm(5);
    }
}

/* SIGALRM handler */
void sigalrmHandler(int signum) {
    (void)signum;
    quitTimerActive = 0; // 5초 타이머 종료
}

/* Set up signal handlers for the parent process */
void setupParentSignalHandlers(void) {
    signal(SIGINT, sigintHandler);       // Ignore SIGINT
    signal(SIGQUIT, sigquitHandler);    // Signal handling: SIGQUIT
    signal(SIGALRM, sigalrmHandler);    // Signal handling: SIGALRM
}

/* Reset signal handlers for the child process to default behavior */
void resetChildSignalHandlers(void) {
    signal(SIGINT, SIG_DFL);  // Original SIGINT
    signal(SIGQUIT, SIG_DFL); // Original SIGQUIT
}
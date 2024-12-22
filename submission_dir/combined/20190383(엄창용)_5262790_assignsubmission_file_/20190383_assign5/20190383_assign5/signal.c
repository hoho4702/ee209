/*--------------------------------------------------------------------*/
/* Name: Changyong Eom                                                */
/* Student ID: 20190383                                               */
/* File description:                                                  */
/* Signal handling Helper function implementation                     */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "signal.h"

volatile sig_atomic_t q = 0;

void installSignalHandler(){
    signal(SIGINT, SIG_IGN);    //parent process should ignore the SIGINT
    signal(SIGQUIT, quitHandler);
    signal(SIGALRM, alarmHandler);
}

void quitHandler(int isig){
    if(q){
        exit(0);
    }else{
        q = 1;
        printf("Type Ctrl-\\ again within 5 seconds to exit.\n");
        fflush(stdout);
        alarm(5);
    }
}

void alarmHandler(int isig){
    q = 0;
}

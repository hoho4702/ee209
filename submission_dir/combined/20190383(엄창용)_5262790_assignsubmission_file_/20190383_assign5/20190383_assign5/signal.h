/*--------------------------------------------------------------------*/
/* Name: Changyong Eom                                                */
/* Student ID: 20190383                                               */
/* File description:                                                  */
/* Signal handling Helper function header file                        */
/*--------------------------------------------------------------------*/

#ifndef SIGNAL_HANDLER_H
#define SIGNAL_HANDLER_H

#include <signal.h>

void installSignalHandler();
/* Install three signal handlers for SIGINT, 
   SIGQUIT, SIGALRM to acheive given task in
   this assignment.
   When the user types Ctrl-c, Linux sends a 
   SIGINT signal to the parent process and its
   children. Upon receiving a SIGINT signal, 
   the parent process should ignore the SIGINT 
   signal and the child process should terminate.
   No return value, no parameter.
*/
void quitHandler(int isig);
/* When the user types Ctrl-\, Linux sends a SIGQUIT
   signal to the parent process and its children. Upon
   receiving a SIGQUIT signal, the parent process should
   print some message to the standard output stream. 
   If and only if the user types Ctrl-\ again within 5 
   seconds of wall-clock time, then the parent process 
   should terminate. No return value.
*/
void alarmHandler(int isig);
/*  Helper function to control handling of SIGQUIT signals.
    Just change the value of 'volatile sig_atomic_t q' variable.
*/

#endif
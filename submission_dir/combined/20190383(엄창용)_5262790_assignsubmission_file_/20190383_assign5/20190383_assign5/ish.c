/*--------------------------------------------------------------------*/
/* Name: Changyong Eom                                                */
/* Student ID: 20190383                                               */
/* File description: main function for ish.c                          */
/*--------------------------------------------------------------------*/
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include "signal.h"
#include "shell.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

int main() {
  /* TODO */
  //to make sure that SIGINT, SIGQUIT, and SIGALRM are not blocked.
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGQUIT);
  sigaddset(&set, SIGALRM);
  sigprocmask(SIG_UNBLOCK, &set, NULL);
  errorPrint("./ish", SETUP); //to setup Shell name
  installSignalHandler();     //to setup signal handlers
  shellInitializer();         //to perform initialization with .ishrc file
  shellUserInter();           //to perform interactive operation with user
}


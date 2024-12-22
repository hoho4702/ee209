#ifndef SIGNAL_H
#define SIGNAL_H

#include <signal.h>

void handler_SIGQUIT();
void handler_SIGALRM();
void handler_resetToDFL();

#endif /* SIGNAL_H */

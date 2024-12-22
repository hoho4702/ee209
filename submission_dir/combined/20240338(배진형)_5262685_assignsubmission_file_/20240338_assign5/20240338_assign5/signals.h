#ifndef _SIGNALS_H
#define _SIGNALS_H

#include <signal.h>

/* FLAGS for signal handling */
extern volatile sig_atomic_t quitRequested;
extern volatile sig_atomic_t quitTimerActive;

/* Setup signal handlers */
void setupParentSignalHandlers(void);
void resetChildSignalHandlers(void);

#endif
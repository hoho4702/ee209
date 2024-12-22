#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef _WRAPPER_H_
#define _WRAPPER_H_

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>

typedef void handler_t(int);

void unix_error(char *msg);
void app_error(char *msg);

pid_t Fork(void);
int Open(const char *pathname, int flags, mode_t mode);
void Pipe(int pipefd[2]);

handler_t *Signal (int signum, handler_t *handler);
void Sigprocmask (int how, const sigset_t *set, sigset_t *oldset);
void Sigfillset (sigset_t *set);
void Sigemptyset (sigset_t *set);
void Sigaddset (sigset_t *set, int signum);
// int Sigsuspend (const sigset_t *set);

ssize_t Sio_puts (char s[]);
ssize_t Sio_putl (long v);


#endif  /* _WRAPPER_H_*/
#include "wrapper.h"

/*
 * unix_error - unix-style error routine
 */
void unix_error (char *msg) {
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error (char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(1);
}

pid_t Fork (void) {
    pid_t pid;

    if ((pid = fork()) < 0)
        unix_error("Fork error");

    return pid;
}

int Open (const char *pathname, int flags, mode_t mode) {
    int fd = open(pathname, flags, mode);
    if (fd < 0) 
        unix_error("Open error");
    
    return fd;
}

void Pipe (int pipefd[2]) {
    if (pipe(pipefd) < 0) 
        unix_error("Pipe error");
    return;
}

/* Signal routine wrappers */
/* $begin signalWrapper */
handler_t *Signal (int signum, handler_t *handler) {
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);   /* block sigs of type being handled */
    action.sa_flags = SA_RESTART;   /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0) 
        unix_error("Signal error");
    
    return (old_action.sa_handler);
}

void Sigprocmask (int how, const sigset_t *set, sigset_t *oldset) {
    if (sigprocmask(how, set, oldset) < 0)
        unix_error("Sigprocmask error");
    return;
}

void Sigfillset (sigset_t *set) {
    if (sigfillset(set) < 0)
        unix_error("Sigfillset error");
    return;
}

void Sigemptyset (sigset_t *set) {
    if (sigemptyset(set) < 0)
        unix_error("Sigaddset error");
    return;
}


void Sigaddset (sigset_t *set, int signum) {
    if (sigaddset(set, signum) < 0)
        unix_error("Sigaddset error");
    return;
}

/* 
 * You should not use Sigsuspend which doesn't ensure the atomicity 
 * Just use sigsuspend
 */
/*
int Sigsuspend (const sigset_t *set) {
    int rc;
    if ((rc = sigsuspend(set)) < 0)
        unix_error("Sigsuspend error");
    return rc;
}
*/
/* $end signalWrapper */

/* Private sio functions: don't use unless necessary */
/* $begin sioprivate */
/* sio_reverse - Reverse a string (from K&R) */
static void sio_reverse(char s[])
{
    int c, i, j;

    for (i = 0, j = strlen(s)-1; i < j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

/* sio_ltoa - Convert long to base b string (from K&R) */
static void sio_ltoa(long v, char s[], int b) 
{
    int c, i = 0;
    int neg = v < 0;

    if (neg)
	v = -v;

    do {  
        s[i++] = ((c = (v % b)) < 10)  ?  c + '0' : c - 10 + 'a';
    } while ((v /= b) > 0);

    if (neg)
	s[i++] = '-';

    s[i] = '\0';
    sio_reverse(s);
}

/* sio_strlen - Return length of string (from K&R) */
static size_t sio_strlen(char s[])
{
    int i = 0;

    while (s[i] != '\0')
        ++i;
    return i;
}
/* $end sioprivate */

/* Public sio functions */
/* $begin siopublic */
ssize_t sio_puts (char s[]) {   /* Put string */
    return write(STDOUT_FILENO, s, sio_strlen(s));
}

ssize_t sio_putl (long v) { /* Put long */
    char s[128];
    
    sio_ltoa(v, s, 10);
    return sio_puts(s);
}


void sio_error (char s[]) {
    sio_puts(s);
    _exit(1);
}
/* $end siopublic */

/* Wrappers for the SIO routines */
/* $begin Siowrapper */
ssize_t Sio_puts (char s[]) {
    ssize_t n;

    if ((n = sio_puts(s)) < 0)
        sio_error("Sio_puts error");

    return n;
}

ssize_t Sio_putl (long v) {
    ssize_t n;
    
    if ((n = sio_putl(v)) < 0)
        sio_error("Sio_putl error");
    
    return n;
}
/* $end Siowrapper */

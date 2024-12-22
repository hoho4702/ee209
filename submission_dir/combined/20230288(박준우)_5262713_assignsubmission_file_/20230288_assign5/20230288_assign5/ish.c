/*
 * EE209 Assignment 5: Shell Lab
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include "lexsyn.h"
#include "util.h"
#include "wrapper.h"
#include "job.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

/* Global variables */
DynArray_T oJobs;
volatile sig_atomic_t sigquit_flag = 0;

void runBuiltin(DynArray_T oTokens, enum BuiltinType btype);
void closeAllPipes(int (*pipes)[2], int numCommands);
int getArgv(DynArray_T oTokens, int *commandSt, int numTokens, char **argv);

void waitfg(pid_t pid);
int deletejob (DynArray_T oJobs, pid_t pid);
void sigchld_handler(int sig);
void sigquit_handler(int sig);
void sigalrm_handler(int sig);

void printArgv (char **argv);
void printJobs (DynArray_T oJobs);

static void
shellHelper(const char *inLine) {
  int debug = 0;

  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }


  lexcheck = lexLine(inLine, oTokens);
  // setenv("DEBUG", "", 1);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0)
        return;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        if (btype != NORMAL) {  // Built-in command
          runBuiltin(oTokens, btype);
        }
        else {  // Normal (not built-in) command
          int bgfg = checkBG(oTokens) ? BG:FG;
          if (bgfg == BG) {
            struct Token *t;
            int numTokens = DynArray_getLength(oTokens);
            t = DynArray_removeAt(oTokens, numTokens-1);
            free(t);
          }

          int saved_stdin  = dup(STDIN_FILENO);
          int saved_stdout = dup(STDOUT_FILENO);

          pid_t pid[MAX_ARGS_CNT];
          sigset_t mask_all, mask_chld, prev_chld;
          Sigfillset(&mask_all);
          Sigemptyset(&mask_chld);
          Sigaddset(&mask_chld, SIGCHLD);
          
          char *argv[MAX_ARGS_CNT+1];
          int numTokens = DynArray_getLength(oTokens);
          int numCommands = countPipe(oTokens) + 1;
          int commandSt = 0;

          int pipes[MAX_ARGS_CNT][2];
          for (int i=0; i<numCommands; i++) 
            Pipe(pipes[i]);

          for (int childIdx = 0; childIdx < numCommands; childIdx++ ) {
            /* Process redirection and extract the arguments */
            int retGetArgv = getArgv(oTokens, &commandSt, numTokens, argv);
            if (retGetArgv < 0) continue;
            if (debug) printArgv(argv);

            Sigprocmask(SIG_BLOCK, &mask_chld, &prev_chld);
            if ((pid[childIdx] = Fork()) == 0) {  /* Child runs the command */
              /* Unblock SIGCHLD for child process */
              Sigprocmask(SIG_SETMASK, &prev_chld, NULL);

              /* Set pipes */
              if (childIdx > 0) 
                dup2(pipes[childIdx-1][0], STDIN_FILENO);
              if (childIdx < numCommands - 1) 
                dup2(pipes[childIdx][1], STDOUT_FILENO);
              
              closeAllPipes(pipes, numCommands);
              close(saved_stdin);
              close(saved_stdout);
              if (execvp(argv[0], argv) < 0) {
                fprintf(stderr, "%s: No such file or directory\n", argv[0]);
                exit(0);
              }
            }
            else {  /* Parent area */
              /* Restore the redirection */
              dup2(saved_stdin, STDIN_FILENO);
              dup2(saved_stdout, STDOUT_FILENO);

              Job_T job = makeJob(pid[childIdx], bgfg);
              Sigprocmask(SIG_BLOCK, &mask_all, NULL);
              DynArray_add(oJobs, job);
              if (bgfg == BG) {
                Sio_puts("[");
                Sio_putl(pid[childIdx]);
                Sio_puts("] Background process is created\n");
                fflush(stdout);
              }
              Sigprocmask(SIG_SETMASK, &prev_chld, NULL);
            }
          }

          /* Parent area after executing every child process */
          closeAllPipes(pipes, numCommands);
          dup2(saved_stdin, STDIN_FILENO);
          dup2(saved_stdout, STDOUT_FILENO);
          close(saved_stdin);
          close(saved_stdout);
          /* If foreground, then wait for child */
          if (bgfg == FG) {
            for (int childIdx = 0; childIdx < numCommands; childIdx++) {
              waitfg(pid[childIdx]);
            }
          }
        }
      }
      /* syntax error cases */
      else if (syncheck == SYN_FAIL_NOCMD)
        errorPrint("Missing command name", FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDOUT)
        errorPrint("Multiple redirection of standard out", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTOUT)
        errorPrint("Standard output redirection without file name", FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDIN)
        errorPrint("Multiple redirection of standard input", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTIN)
        errorPrint("Standard input redirection without file name", FPRINTF);
      else if (syncheck == SYN_FAIL_INVALIDBG)
        errorPrint("Invalid use of background", FPRINTF);
      
      DynArray_free(oTokens);
      break;

    case LEX_QERROR:
      errorPrint("Unmatched quote", FPRINTF);
      DynArray_free(oTokens);
      break;

    case LEX_NOMEM:
      errorPrint("Cannot allocate memory", FPRINTF);
      DynArray_free(oTokens);
      break;

    case LEX_LONG:
      errorPrint("Command is too large", FPRINTF);
      DynArray_free(oTokens);
      break;

    default:
      errorPrint("lexLine needs to be fixed", FPRINTF);
      DynArray_free(oTokens);
      exit(EXIT_FAILURE);
  }
}

int main(int argc, char **argv) {
  /* TODO */
  errorPrint(argv[0], SETUP);

  oJobs = DynArray_new(0);

  sigset_t sSet;
  Sigemptyset(&sSet);
  Sigaddset(&sSet, SIGCHLD);
  Sigaddset(&sSet, SIGINT);
  Sigaddset(&sSet, SIGQUIT);
  Sigaddset(&sSet, SIGALRM);
  Sigprocmask(SIG_UNBLOCK, &sSet, NULL);

  Signal(SIGCHLD, sigchld_handler);
  Signal(SIGINT, SIG_IGN);
  Signal(SIGQUIT, sigquit_handler);
  Signal(SIGALRM, sigalrm_handler);

  char acLine[MAX_LINE_SIZE + 2];

  char *homePath = getenv("HOME");
  char ishrcPath[100];
  strcpy(ishrcPath, homePath);
  strcat(ishrcPath, "/.ishrc");
  FILE *ishrc = fopen(ishrcPath, "r");
  if (ishrc != NULL) {
    int lastNL = 0;
    while (1) {
      if (fgets(acLine, MAX_LINE_SIZE, ishrc) == NULL) {
        if (!lastNL)
          fprintf(stdout, "\n");
        fclose(ishrc);
        break;
      }
      fprintf(stdout, "%% ");
      fprintf(stdout, "%s", acLine);
      fflush(stdout);
      lastNL = (acLine[strlen(acLine)-1] == '\n');

      shellHelper(acLine);
    }
  }

  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      DynArray_free(oJobs);
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }

  exit(0);  /* control never reaches here */
}

static int onlyWordParameters(DynArray_T oTokens) {
  int numTokens = DynArray_getLength(oTokens);
  for (int i = 1; i < numTokens; i++) {
    struct Token *t = DynArray_get(oTokens, i);
    if (t->eType != TOKEN_WORD) return FALSE;
  }
  return TRUE;
}

void runBuiltin(DynArray_T oTokens, enum BuiltinType btype) {
  int numParms = DynArray_getLength(oTokens) - 1;
  // assert(numParms >= 0);
  switch(btype) {
    case B_SETENV:
      if (numParms == 0 || numParms > 2 || ! onlyWordParameters(oTokens)) {
        errorPrint("setenv takes one or two parameters", FPRINTF);
      }
      else if (numParms == 1) { /* 1 word parameter */
        struct Token *t = DynArray_get(oTokens, 1);
        if (strlen(t->pcValue) == 0) 
          errorPrint("Invalid argument", FPRINTF);
        else 
          setenv(t->pcValue, "", 1);
      }
      else {  /* 2 word parameters */
        struct Token *t1 = DynArray_get(oTokens, 1);
        struct Token *t2 = DynArray_get(oTokens, 2);
        if (strlen(t1->pcValue) == 0) 
          errorPrint("Invalid argument", FPRINTF);
        else
          setenv(t1->pcValue, t2->pcValue, 1);
      }
      break;

    case B_USETENV:
      if (numParms == 0 || numParms > 1 || ! onlyWordParameters(oTokens)) {
        errorPrint("unsetenv takes one parameter", FPRINTF);
      }
      else {  /* 1 word parameter */
        struct Token *t = DynArray_get(oTokens, 1);
        if (strlen(t->pcValue) == 0) 
          errorPrint("Invalid argument", FPRINTF);
        else
          unsetenv(t->pcValue);
      }
      break;

    case B_CD:
      if (numParms > 1 || ! onlyWordParameters(oTokens)) {
        errorPrint("cd takes one parameter", FPRINTF);
      }
      else if (numParms == 1) {  /* 1 word Parameter */
        struct Token *t = DynArray_get(oTokens, 1);
        if (chdir(t->pcValue) < 0) 
          errorPrint("No such file or directory", FPRINTF);
      }
      else {  /* No parameter */
        char *home = getenv("HOME");
        if (chdir(home) < 0)
          errorPrint("No such file or directory", FPRINTF);
      }
      break;
      
    case B_EXIT:
      if (numParms > 0) 
        errorPrint("exit does not take any parameters", FPRINTF);
      else 
        exit(0);
      break;

    default: 
      printf("Yet implemented.");
      break;
  }
}

void closeAllPipes(int (*pipes)[2], int numCommands) {
  for (int i=0; i<numCommands; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
  }
}

int getArgv(DynArray_T oTokens, int *commandSt, int numTokens, char **argv) {

  /* Assert did syntackCheck() */
  int tIdx = *commandSt;
  struct Token *t, *t2;
  int numArgv = 0;
  int success = 1;

  while (tIdx < numTokens) {
    t = DynArray_get(oTokens, tIdx);
    if (t->eType == TOKEN_PIPE) {
      tIdx++;
      break;
    }
    
    switch(t->eType) {
      case TOKEN_WORD:
        argv[numArgv++] = t->pcValue;
        tIdx++;
        break;

      case TOKEN_REDIN:
        t2 = DynArray_get(oTokens, tIdx+1);
        int fd_in = open(t2->pcValue, O_RDONLY);
        if (fd_in < 0) {
          errorPrint("No such file or directory", FPRINTF);
          success = -1;
        }
        else {
          dup2(fd_in, STDIN_FILENO);
          close(fd_in);
        }
        tIdx += 2;
        break;
      
      case TOKEN_REDOUT:
        t2 = DynArray_get(oTokens, tIdx+1);
        int fd_out = Open(t2->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        dup2(fd_out, STDOUT_FILENO);
        close(fd_out);
        tIdx += 2;
        break;
      
      default:
        errorPrint("getArgv: assertion fails", FPRINTF);
        exit(EXIT_FAILURE);
    }
  }
  argv[numArgv] = NULL;

  *commandSt = tIdx;

  return success;
}

void waitfg(pid_t pid) {
  int olderrno = errno;
  sigset_t mask_all, prev_all;

  Sigfillset(&mask_all);
  Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);
  
  struct Job tempJob;
  tempJob.pid = pid;
  while (DynArray_search(oJobs, &tempJob, Job_compare) != -1) {
    sigsuspend(&prev_all);  /* Don't use Sigsuspend (wrapper for sigsuspend).
                             * Unless, it can't assure the atomic property.
                             */
  }

  Sigprocmask(SIG_SETMASK, &prev_all, NULL);

  errno = olderrno;
  return;
}

int deletejob (DynArray_T oJobs, pid_t pid) {
  struct Job tempJob;
  tempJob.pid = pid;
  int jobIdx = DynArray_search(oJobs, &tempJob, Job_compare);
  
  if (jobIdx != -1) {
    Job_T jobp = DynArray_removeAt(oJobs, jobIdx);
    freeJob(jobp);
  }

  return jobIdx != -1;
}

/*
 * sigchld_handler
 */
void sigchld_handler(int sig) {
  int olderrno = errno;
  sigset_t mask_all, prev_all;
  pid_t pid;
  int status;

  Sigfillset(&mask_all);
  Sigprocmask(SIG_BLOCK, &mask_all, &prev_all);

  struct Job tempJob;
  Job_T jobp;
  int jobIdx;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    tempJob.pid = pid;
    jobIdx = DynArray_search(oJobs, &tempJob, Job_compare);

    if (jobIdx == -1) {
      printf("WTF: pid = %d\n", pid);
      printJobs(oJobs);
    }
    jobp = DynArray_get(oJobs, jobIdx);
    
    if (jobp->bgfg == BG) {
      Sio_puts("[");
      Sio_putl(jobp->pid);
      Sio_puts("] Background process is terminated\n");
    }

    deletejob(oJobs, jobp->pid);
  }

  errno = olderrno;
  Sigprocmask(SIG_SETMASK, &prev_all, NULL);
  return;
}


/*
 * sigquit_handler
 */
void sigquit_handler(int sig) {
  if (sigquit_flag == 1) exit(0);
  else {
    sigquit_flag = 1;
    Sio_puts("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    alarm(5);
  }
}

/* 
 * sigalrm_handler 
 */
void sigalrm_handler(int sig) {
  sigquit_flag = 0;
}

/* Debugging functions */
void printArgv (char **argv) {
  int i=0;
  while (argv[i] != NULL) {
    fprintf(stderr, "%s ", argv[i]);
    i++;
  }
  fprintf(stderr, "\n");
}

void printJobs (DynArray_T oJobs) {
  int numJobs = DynArray_getLength(oJobs);
  printf("numJobs = %d\n", numJobs);

  for (int i=0; i<numJobs; i++) {
    Job_T job = DynArray_get(oJobs, i);

    printf("pid = %d / bgfg = %s\n", job->pid, (job->bgfg==BG) ? "BG":"FG");
  }
  printf("\n");
}
#include <stdio.h>
#include <stdlib.h>
//
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include "parse.h"
#include "alias.h"
#include "idmanage.h"
//
#include "lexsyn.h"
#include "util.h"

#define MAX_CHAR 1024
/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/* 20200197 Nam Wonjin                                                */
/*--------------------------------------------------------------------*/

void catch_term(int sig) {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[%d] Background process is terminated\n", pid);
    }
}

void catch_quit(int sig) {
    static int quithit = 0;
    if (sig == SIGQUIT) {
        // C-\ invoked. set timer and return.
        if (quithit == 0) {
            quithit++;
            fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
            alarm(5);
        }
        // C-\ invoked again within timeout. terminate.
        else if (quithit == 1) {
            alarm(0);
            exit(EXIT_SUCCESS);
        }
    } else if (sig == SIGALRM) {
        // fail to invoke within timeout. reset quithit.
        quithit = 0;
    }
}


static void
shellHelper(const char *inLine, DynArray_T pTable, DynArray_T pids, const char *shell) {
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
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0)
        return;

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        int numtoken;
        pid_t pid;
        int status;

        /* TODO */
        // Execute builtin function on parent process.
        switch (btype) {
            case B_CD: {
                numtoken = DynArray_getLength(oTokens);
                if (numtoken == 1) { 
                    if (chdir(getenv("HOME")) != 0) 
                        perror(shell);
                } else if (numtoken == 2){
                    const char *dir = ((struct Token*) DynArray_get(oTokens, 1))->pcValue;
                    if (chdir(dir) != 0)
                        perror(shell);
                } else {
                    fprintf(stderr, "%s: cd takes one parameter\n", shell);
                }
                cleanoToken(oTokens);
                return; }
            case B_FG: {
                pid = idpop(pids);
                if (pid == 0) {
                    //No child.
                    fprintf(stderr, "%s: There is no background process.\n", shell);
                    cleanoToken(oTokens);
                    return;
                }
                fprintf(stdout, "[%d] Latest background process is running\n", pid);
                waitpid(pid, &status, WUNTRACED);
                cleanoToken(oTokens);
                return; }
            case B_EXIT: {
                cleanoToken(oTokens);
                cleanentry(pTable);
                cleanid(pids);
                exit(EXIT_SUCCESS); }
            case B_SETENV: {
                numtoken = DynArray_getLength(oTokens);
                if (numtoken == 2) { 
                    const char *var = ((struct Token*) DynArray_get(oTokens, 1))->pcValue;
                    if (setenv(var, "", 1) != 0) 
                        perror(shell);
                } else if (numtoken == 3){
                    const char *var = ((struct Token*) DynArray_get(oTokens, 1))->pcValue;
                    const char *val = ((struct Token*) DynArray_get(oTokens, 2))->pcValue;
                    if (setenv(var, val, 1) != 0)
                        perror(shell);
                } else {
                    fprintf(stderr, "%s : setenv takes one or two parameters\n", shell);
                }
                cleanoToken(oTokens);
                return; }
            case B_USETENV: {
                numtoken = DynArray_getLength(oTokens);
                if (numtoken == 2) { 
                    const char *var = ((struct Token*) DynArray_get(oTokens, 1))->pcValue;
                    if (unsetenv(var) != 0) 
                        perror(shell);
                } else {
                    fprintf(stderr, "%s: unsetenv takes one parameter\n", shell);
                }
                cleanoToken(oTokens);
                return; }
            case B_ALIAS: {
                //modify alias table (pTable).
                enum AliasResult result;
                DynArray_T alias_map = DynArray_new(0);
                numtoken = DynArray_getLength(oTokens);
                if (numtoken == 1) {
                    printentry(pTable);
                } else if (numtoken == 2){
                    const char *map = ((struct Token*) DynArray_get(oTokens, 1))->pcValue;
                    result = alias_lexLine(map, alias_map);
                    if (result != ALIAS_SUCCESS)
                        fprintf(stderr, "%s: alias invalid input\n", shell);
                    else {
                        //update alias table.
                        if (DynArray_getLength(alias_map) == 1) {
                            //find if there's anything on pTable.
                            char *name = ((struct Token *) 
                                    DynArray_get(alias_map, 0))->pcValue;
                            struct aliasentry *found;
                            if ((found = findEntry(pTable, name)) == NULL) {
                                //not found.
                                fprintf(stderr, "%s: alias: %s: not found\n", shell,
                                        name);
                            } else {
                                //found.
                                fprintf(stdout, "alias %s=\'%s\'\n", name, found->value);
                            }
                        } else if (DynArray_getLength(alias_map) == 2) {
                            char *name = ((struct Token*) 
                                    DynArray_get(alias_map, 0))->pcValue;
                            char *value = ((struct Token*) 
                                    DynArray_get(alias_map, 1))->pcValue;
                            updateEntry(pTable, name, value);
                        }
                        cleanoToken(alias_map);
                    }
                } else {
                    fprintf(stderr, "%s: alias takes one parameter.\n", shell);
                }
                cleanoToken(oTokens);
                return;
            }
            // Normal; Execute on child process.
            case NORMAL: {
                struct args *pargs, *ptr;
                const char *infile, *outfile;
                int infd, outfd;

                numtoken = DynArray_getLength(oTokens);
                pargs = parser(oTokens, pTable);
                /* dump lex result when DEBUG is set */
                dumpLex(oTokens);

                ptr = pargs;
                int filedes[2];
                
                fflush(NULL);
                pid = fork();
                if (pid == 0) {
                    // Child process.
                    // revert handling scheme.
                    signal(SIGINT, SIG_DFL);
                    signal(SIGQUIT, SIG_DFL);
                    signal(SIGALRM, SIG_DFL);
                    do {
                        // piping/IO redirect if necessary.
                        if (ptr -> pipe_dest != NULL) {
                            //install pipe.
                            if (pipe(filedes) == -1) {
                                //error
                                perror(shell);
                                cleanoToken(oTokens);
                                cleanparse(pargs);
                                exit(EXIT_FAILURE);
                            }
                        }
                        if ((infile = ptr->infile) != NULL) {
                            //input redirect.
                            if ((infd = open (infile, O_RDONLY)) == -1) {
                                //error
                                perror(shell);
                                cleanoToken(oTokens);
                                cleanparse(pargs);
                                exit(EXIT_FAILURE);
                            }
                            dup2(infd, STDIN_FILENO);
                            close (infd);
                        }
                        if ((outfile = ptr->outfile) != NULL) {
                            //output redirect.
                            outfd = open (outfile, O_WRONLY|O_CREAT|O_TRUNC,
                                    S_IRUSR|S_IWUSR);
                            if (outfd == -1) {
                                //error
                                perror(shell);
                                cleanoToken(oTokens);
                                cleanparse(pargs);
                                exit(EXIT_FAILURE);
                            }
                            dup2(outfd, STDOUT_FILENO);
                            close (outfd);
                        }

                        // create grandchild process and exec if pipe exists,
                        // exec on child if no piping.
                        if (ptr -> locpipe == last || ptr -> pipe_dest != NULL) {
                            //piped.
                            if (ptr -> locpipe == first ||
                                ptr -> locpipe == mid) {
                                // first and middle command.
                                // input = infd, output = pipedes[1]
                                fflush(NULL);
                                pid = fork();
                                if (pid < 0) {
                                    // error.
                                    perror(shell);
                                    cleanoToken(oTokens);
                                    cleanparse(pargs);
                                    exit(EXIT_FAILURE);
                                } else if (pid == 0) {
                                    // grandchild. write end to filedes[1].
                                    dup2(filedes[1], STDOUT_FILENO);
                                    close(filedes[0]);
                                    close(filedes[1]);
                                    if (execvp(ptr->cmd[0], ptr->cmd) == -1) {
                                        // Error.
                                        perror(ptr->cmd[0]);
                                        cleanoToken(oTokens);
                                        cleanparse(pargs);
                                        exit(EXIT_FAILURE);
                                    }
                                } else {
                                    // child. reserve read end filedes[0] for next cmd.
                                    close(filedes[1]);
                                    infd = filedes[0];
                                    dup2(infd, STDIN_FILENO);
                                    close(infd);
                                }
                            } else {
                                //last command. no more piping.
                                if (execvp(ptr->cmd[0], ptr->cmd) == -1) {
                                    // error.
                                    perror(ptr->cmd[0]);
                                    cleanoToken(oTokens);
                                    cleanparse(pargs);
                                    exit(EXIT_FAILURE);
                                }
                            }
                        } else {
                            //not piped. just exec.
                            if (execvp(ptr->cmd[0], ptr->cmd) == -1) {
                                // Error.
                                perror(ptr->cmd[0]);
                                cleanoToken(oTokens);
                                cleanparse(pargs);
                                exit(EXIT_FAILURE);
                            }
                        }
                        ptr = ptr -> pipe_dest;
                    } while (ptr != NULL);

                    // should not reach here.
                    // all child/grandchild are overwritten to exec.
                    cleanoToken(oTokens);
                    cleanparse(pargs);
                    exit(EXIT_FAILURE);

                } else if (pid < 0) {
                    // forking error.
                    perror(shell);
                } else {
                    // Parent process.
                    // if bg, no wait.
                    // TODO: NOCHILD signal handing implementation.
                    while (ptr -> pipe_dest != NULL)
                        ptr = ptr -> pipe_dest;
                    if (!ptr -> bg)
                        // wait child to terminate.
                        waitpid(pid, &status, WUNTRACED);
                    else {
                        //no wait. push pid to stack [pids] and
                        //continue.
                        idpush(pids, pid); 
                        printf("[%d] Background process is created\n", pid);
                    }
                }
                cleanparse(pargs);
                cleanoToken(oTokens);
                return;
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
      break;

    case LEX_QERROR:
      errorPrint("Unmatched quote", FPRINTF);
      break;

    case LEX_NOMEM:
      errorPrint("Cannot allocate memory", FPRINTF);
      break;

    case LEX_LONG:
      errorPrint("Command is too large", FPRINTF);
      break;

    default:
      errorPrint("lexLine needs to be fixed", FPRINTF);
      exit(EXIT_FAILURE);
  }
}

int main(int argc, char **argv) {
  /* TODO */
    char *shell = argv[0];
    // alias table set.
    DynArray_T pTable = DynArray_new(0);
    // pid history set.
    DynArray_T pids = DynArray_new(0);

    //sigprogmask to unblock required signals
    sigset_t mask, prev_mask;
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigaddset(&mask, SIGALRM);

    sigprocmask(SIG_UNBLOCK, &mask, &prev_mask);

    //set handler.
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, catch_term);
    signal(SIGQUIT, catch_quit);
    signal(SIGALRM, catch_quit);

    errorPrint(shell, SETUP);
  char acLine[MAX_LINE_SIZE + 2];
  // .ishrc file open.
  char *home = getenv("HOME");
  char rcdir[strlen(home)+strlen("/.ishrc")+1];
  strcpy(rcdir, home); strcat(rcdir, "/.ishrc");
  FILE *fp = fopen(rcdir, "r");
  if (!fp)
      //no rc file. ignore.
      ;
  else {
      while (fgets(acLine, MAX_LINE_SIZE, fp) != NULL) {
          fprintf(stdout, "%% ");
          fprintf(stdout, "%s", acLine);
          fflush(stdout);
          shellHelper(acLine, pTable, pids, shell);
      }
      fclose(fp);
  }

  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine, pTable, pids, shell);
  }
}


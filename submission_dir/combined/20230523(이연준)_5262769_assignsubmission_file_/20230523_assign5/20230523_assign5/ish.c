/*
  ish.c - 20230523 Yeonjun Lee
  co-worker : 20210629 진예환
  
  This code executes shell such as given "sampleish"
    input command, parse it, execute it
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>

#include "lexsyn.h"
#include "util.h"
#include "token.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

// Used for pipe
enum INOUT {IN, OUT};

typedef struct Token * Token_T;

// Structure for managing background process
#define MAX_BG 5

typedef struct backPID {
  int pid;
  struct backPID* next;
} backPID;

typedef struct backList {
  backPID * start;
  int count;
} backList;

backList * bgtrace = NULL;

// Used for managing SIGQUIT
int type_count = 0;

// Use for maintain backgroun Linked List
/*
  Add new back ground node to global back ground Linked List
  : This function make new Linked List node with given pid,
    and add it global variable, `bgtrace`
  
  parameters
  1. pid : pid of new back ground process
*/
void addBG(int pid) {
  if(bgtrace == NULL) exit(EXIT_FAILURE);
  if(bgtrace->count >= MAX_BG) {
    char *msg = "Program exceeds the maximum number\
 of background processes\n";
    errorPrint(msg, FPRINTF);
    exit(EXIT_FAILURE);
  }
  backPID * back = (backPID *)calloc(1, sizeof(backPID));
  back->pid = pid;
  back->next = bgtrace->start;
  bgtrace->start = back;
  bgtrace->count += 1;
}

/*
  Delete back ground node from global back ground Linked List
  : This function make delete Linked List node with given pid
    from global variable, `bgtrace`
  
  parameters
  1. pid : pid of terminated back ground process
*/
void deleteBG(int pid) {
  if(bgtrace == NULL) exit(EXIT_FAILURE);
  backPID * prev = NULL;
  for(backPID * cur = bgtrace->start; cur != NULL; cur = cur->next) {
    if(cur->pid == pid) {
      if(prev == NULL) bgtrace->start = cur->next;
      else prev->next = cur->next;
      free(cur);
      bgtrace->count -= 1;
      return;
    }
    prev = cur;
  }
  exit(EXIT_FAILURE);
}

// Signal Handlers
/*
  This function is signal handler for SIGCHLD
  This function finds the pid for terminated backg ground process,
  and delete it from back ground process Linked List
  
  parameters
  1. signum : signal number for SIGCHLD
*/
void sigchld_hander(int signum) {
  // When background child process is terminated
  // => delete it from backPID Linked List
  // and print the result state
  // => should block any signals

  //signal-handler-for-all-signal
  sigset_t sig_all, prev;
  sigfillset(&sig_all);

  int pid, status;

  // Get Terminated process
  while((pid = waitpid(-1, &status, WNOHANG|WUNTRACED)) > 0) {
    // Check output pid was pid of back ground
    for(backPID * cur = bgtrace->start; cur != NULL; cur = cur->next) {
      if(cur->pid != pid) continue;
      // If one of child proeccess whose pid is stored in backPID
      // is terminated
      // then manage it as terminated
      sigprocmask(SIG_BLOCK, &sig_all, &prev);
      fprintf(stdout, "[%d] Background process is terminated\n", pid);
      fflush(stdout);
      deleteBG(pid);
      sigprocmask(SIG_SETMASK, &prev, NULL);
      return;
    }
  }
}

/*
  This function is signal handler for SIGALRM
  This function is used to reset the sigquit state
  by resetting type_count global variable
  
  parameters
  1. signum : signal number for SIGALRM
*/
void sigalrm_handler(int signum){
  type_count = 0;
}

/*
  This function is signal handler for SIGQUIT
  This function manages quit when user types Ctrl-\

  At first this function is called,
  this function prints the message for quit,
  and save global value (type_count)
  which represent if the previous sigquit is occurs
  
  parameters
  1. signum : signal number for SIGQUIT
*/
void sigquit_handler(int signum){
  if(type_count == 0) {
    // 5 sec alaram
    // If user types Ctrl-\ with in 5 sec : exit
    // else : delete alaram
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n"); 
    fflush(stdout);
    type_count = 1;
    alarm(5);  
  } else if(type_count == 1) {
    exit(EXIT_SUCCESS);
  } 
}

/*
  This funcion make argument used to execvp from Tokens
  
  parameter
    1. oTokens : Used to access each tokens
    
    2. start : starting index of Tokens to change it to argument
    3. end : ending index of Tokens to change it to argument
    // start <= < end (0-indexing)

    4. array : the result array as a role of argument
      the address of string is stored here
    5. start_arr : start index of given array to copy

  return
    This functions returns the index of last NULL element
*/
int makecommand(DynArray_T oTokens, int start, int end,
  char **array, int start_arr) {
  // 1. Get # of tokens
  int iLen = end > DynArray_getLength(oTokens) ?
    DynArray_getLength(oTokens) : end;
  
  // if start >= length : Nothing to do => array[0] = NULL
  if (start >= iLen) { array[start_arr] = NULL; return start_arr; }
  if (start < 0) { array[start_arr] = NULL; return start_arr; }

  // 3. Store (start+i)th token to ith element in array
  int idx_arr = start_arr;
  for(int i = start; i < iLen; i++) {
    Token_T eachtoken = (Token_T)DynArray_get(oTokens, i);
    if(eachtoken->pcValue != NULL) array[idx_arr++]=eachtoken->pcValue;
  }

  // Last of array must NULL
  array[idx_arr] = NULL;
  return idx_arr;
}

/*
  This function get dynamic array oTokesn,
    and frees dynamic array
    after freeing every tokens stored at given dynamic array
  
  parameter
    1. oTokens : Dynamic array to free
*/
void freeTokens(DynArray_T oTokens) {
  int iLen = DynArray_getLength(oTokens);
  for(int i = 0; i < iLen; i++) {
    freeToken(DynArray_get(oTokens, i), NULL);
  }
  DynArray_free(oTokens);
}

/*
  This function is used to searching special tokens
    (such as redirection)
  
  parameter
    1. pvElement1 (should pointer of struct Token)
    2. pvElement2 (should pointer of struct Token)
  
  return
    if the token type of give two tokens are same
      => return 0
    else
      => return non-zero number
*/
int stringCompare(const void *pvElement1, const void *pvElement2) {
  Token_T token1 = (Token_T)pvElement1;
  Token_T token2 = (Token_T)pvElement2;
  return (token1->eType) - (token2->eType);
}

/*
  This function get dynamic array oTokesn,
    and get index of pipe commands
  
  parameter
    1. oTokens : Dynamic array to free
    2. pipe_idx : pipe index array
*/
int make_pipe_idx(DynArray_T oTokens, int *pipe_idx) {
              
  // 1. Get # of tokens
  int iLen = DynArray_getLength(oTokens);
  int start = 0;
  
  // 2. Check the index of pipe
  int pipe_num = 0;
  for(int i = start; i < iLen; i++) {
    Token_T eachtoken = (Token_T)DynArray_get(oTokens, i);

    if(eachtoken->eType == TOKEN_PIPE) {
      // If pipe is found Record the index of pipe
      pipe_idx[pipe_num++] = i;
    }
  }
  // Prevent Out Of Bound during pipe execution
  pipe_idx[pipe_num] = iLen;
  return 0;
}

/*
  This function analyze given command (parsing, ...),
  and execute it

  parameter
    1. inLine : pointer of given command string
*/
static void shellHelper(const char *inLine) {
  // Signal for SIGCHLD
  sigset_t sig_child;
  sigemptyset(&sig_child);
  sigaddset(&sig_child, SIGCHLD);

  // Signal handler for SIGCHLD
  signal(SIGCHLD, sigchld_hander);

  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  int pid;

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

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);
      
      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));

        if(btype == NORMAL) {
          // Check the number of pipe
          int pipe_num = countPipe(oTokens);
          if(pipe_num){
            // If pipe exists
            // Store next pipe index
            int cur_pipe_idx = 0;
  
            // get the index of all pipe
            int pipe_idx[MAX_ARGS_CNT];
            make_pipe_idx(oTokens, pipe_idx);

            // Generate pipe and Redirection then execute
            // fd[0] : pipeline input, fd[1] : pipeline output
            int pid, fd[2], fd_temp; 

            // Make pipes
            for(int command_idx = 0; command_idx <= pipe_num;
              command_idx++){

              pipe(fd);
              cur_pipe_idx = pipe_idx[command_idx];
              if((pid = fork()) == 0){
                // child process

                //signal handling : Make SIGINT to default
                void (*pf) (int);
                pf=signal(SIGINT, SIG_DFL);
                assert(pf!= SIG_ERR);

                // Make SIGQUIT to default
                pf=signal(SIGQUIT, SIG_DFL);
                assert(pf!= SIG_ERR);

                // Used for parameters of execvp
                char *argv[MAX_ARGS_CNT];

                if(command_idx == 0) {
                  close(fd[IN]);
                  dup2(fd[OUT], 1);
                  close(fd[OUT]);

                  makecommand(oTokens, 0, cur_pipe_idx, argv, 0);

                  execvp((const char *)argv[0], argv);
                  errorPrint(argv[0], PERROR);
                  exit(EXIT_FAILURE);
                } else if(command_idx == pipe_num) {
                  dup2(fd_temp, 0);
                  close(fd_temp);
                  // end of making pipeline

                  makecommand(oTokens, pipe_idx[command_idx-1],
                    DynArray_getLength(oTokens), argv, 0);

                  execvp((const char *)argv[0], argv);
                  errorPrint(argv[0], PERROR);
                  exit(EXIT_FAILURE); 
                } else {
                  // Intermediate pipe commands
                  close(fd[IN]);
                  dup2(fd_temp,0);
                  close(fd_temp);
                  dup2(fd[OUT], 1);
                  close(fd[OUT]);
                  
                  makecommand(oTokens, pipe_idx[command_idx-1],
                    cur_pipe_idx, argv, 0);

                  execvp((const char *)argv[0], argv);
                  errorPrint(argv[0], PERROR);
                  exit(EXIT_FAILURE); 

                }
              } else {
                // pipe for parent process
                if(checkBG(oTokens)) {
                  // Background
                  // Blocking SIGCHLD
                  sigprocmask(SIG_BLOCK, &sig_child, NULL);

                  fprintf(stdout,"[%d] Background process is created\n",
                    pid);
                  fflush(stdout);
                  addBG(pid);

                  sigprocmask(SIG_UNBLOCK, &sig_child, NULL);
                }
                
                if(command_idx != pipe_num) {
                  close(fd[OUT]);
                  fd_temp = dup(fd[IN]);
                  close(fd[IN]);
                }

                wait(NULL);
              }
            }
            freeTokens(oTokens);
          } else if(pipe_num == 0) {
            // Not pipe exist

            if((pid = fork()) == 0){
              // Child Process
              // Not builtin command => Run with execvp
              void (*pf) (int);
              pf=signal(SIGINT, SIG_DFL);
              assert(pf!= SIG_ERR);

              pf=signal(SIGQUIT, SIG_DFL);
              assert(pf!= SIG_ERR);

              char *argv[MAX_ARGS_CNT];
              // Check redirection out exist
              Token_T token_redout = makeToken(TOKEN_REDOUT, NULL);
              int redout_idx = DynArray_search(oTokens,
                (void *)token_redout, stringCompare);
              freeToken((void *)token_redout, NULL);

              // Check redirection in exist
              Token_T token_redin = makeToken(TOKEN_REDIN, NULL);
              int redin_idx = DynArray_search(oTokens,
                (void *)token_redin, stringCompare);
              freeToken((void *)token_redin, NULL);

              int total_len = DynArray_getLength(oTokens);
              if((redout_idx < 0) && (redin_idx < 0)) {
                // Not redirection : Normal execution
                makecommand(oTokens, 0, total_len, argv, 0);
                execvp((const char *)argv[0], argv);
                errorPrint(argv[0], PERROR);
                exit(EXIT_FAILURE);
              } else if ((redout_idx) && (redin_idx < 0)) {
                // Only redirection out exist : stdout->file
                Token_T file_token = (Token_T)DynArray_get(oTokens,
                  redout_idx+1);
                char *file_name = (char*)(file_token->pcValue);
                int file = creat(file_name, 0600);

                close(1);
                dup(file);
                close(file);

                // copy 0<= <redout_idx to argv from 0<= <=arr_idx(NULL)
                int arr_idx=makecommand(oTokens, 0, redout_idx, argv,0);
                makecommand(oTokens,redout_idx+2,
                  total_len,argv,arr_idx);

                execvp((const char *)argv[0], argv);
                errorPrint(argv[0], PERROR);
                exit(EXIT_FAILURE);
              } else if ((redout_idx < 0) && (redin_idx)) {
                // Only redirection in exist : stdin->file
                Token_T file_token = (Token_T)DynArray_get(oTokens,
                  redin_idx+1);
                char *file_name = (char*)(file_token->pcValue);
                int file = open(file_name, O_RDONLY, 0);

                close(0);
                dup(file);
                close(file);

                // copy 0<= <redin_idx to argv from 0<= <=arr_idx(NULL)
                int arr_idx=makecommand(oTokens, 0, redin_idx, argv,0);
                makecommand(oTokens,redin_idx+2,
                  total_len,argv,arr_idx);

                execvp((const char *)argv[0], argv);
                errorPrint(argv[0], PERROR);
                exit(EXIT_FAILURE);
              } else {
                // Both redirection input and redirection output
                // 1. Redirection output : stdout->file
                Token_T file_token_out = (Token_T)DynArray_get(oTokens,
                  redout_idx+1);
                char *file_name_out = (char*)(file_token_out->pcValue);
                int file_out = creat(file_name_out, 0600);

                close(1);
                dup(file_out);
                close(file_out);

                // 2. Redirection input : stdin->file
                Token_T file_token_in = (Token_T)DynArray_get(oTokens,
                  redin_idx+1);
                char *file_name_in = (char*)(file_token_in->pcValue);
                int file_in = open(file_name_in, O_RDONLY, 0);

                close(0);
                dup(file_in);
                close(file_in);

                // copy 0<= <redin_idx to argv from 0<= <=arr_idx(NULL)
                if(redin_idx < redout_idx) {
                  int arr_idx1=makecommand(oTokens,0,redin_idx,argv,0);
                  int arr_idx2 = makecommand(oTokens, redin_idx+2,
                    redout_idx, argv, arr_idx1);
                  makecommand(oTokens, redout_idx+2, total_len,
                    argv, arr_idx2);
                } else {
                  int arr_idx1=makecommand(oTokens,0,redout_idx,argv,0);
                  int arr_idx2 = makecommand(oTokens, redout_idx+2,
                    redin_idx, argv, arr_idx1);
                  makecommand(oTokens, redin_idx+2, total_len,
                    argv, arr_idx2);
                }

                execvp((const char *)argv[0], argv);
                errorPrint(argv[0], PERROR);
                exit(EXIT_FAILURE);
              }
            } else {
              // Parent Process
              if(checkBG(oTokens)) {
                // Background
                // Blocking SIGCHLD
                sigprocmask(SIG_BLOCK, &sig_child, NULL);

                fprintf(stdout, "[%d] Background process is created\n",
                  pid);
                fflush(stdout);
                addBG(pid);

                sigprocmask(SIG_UNBLOCK, &sig_child, NULL);
              } else {
                // Foreground
                // Wait for Child Process
                pid = wait(NULL);
              }

              // Common
              // Frees
              freeTokens(oTokens);
              
            }
          }
        } else {
          // Builtin Command => use C functions
          char *argv[MAX_ARGS_CNT];
          makecommand(oTokens, 0, DynArray_getLength(oTokens), argv, 0);

          if(btype == B_CD ) {
            if((DynArray_getLength(oTokens) > 2) ||
              ((DynArray_getLength(oTokens) == 2)
                && ((Token_T)DynArray_get(oTokens, 1))->pcValue == NULL)
            ) {
              fprintf(stderr, "./ish: cd takes one parameter\n");
            } else {
              if(argv[1]==NULL) {
                // cd without argument : cd
                chdir(getenv("HOME"));
              } else {
                // cd with argument : cd ~
                if(chdir(argv[1]) != 0) errorPrint("./ish", PERROR);
              }
            }
          } else if (btype == B_EXIT) {
            exit(0);
          } else if (btype == B_SETENV) {
            if (argv[1] == NULL) {
              fprintf(stderr,
                "./ish: setenv takes one or two parameters\n");
            } else if (argv[2] == NULL) setenv(argv[1], "\0", 1);
            else setenv(argv[1], argv[2], 1);
          } else if (btype == B_USETENV) {
            if (argv[2] != NULL) {
              fprintf(stderr,
                "./ish: unsetenv takes one parameter\n");
            } else unsetenv(argv[1]);
          }
          freeTokens(oTokens);
        }
      }

      /* syntax error cases */
      else if (syncheck == SYN_FAIL_NOCMD)
        errorPrint("Missing command name", FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDOUT)
        errorPrint("Multiple redirection of standard out", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTOUT)
        errorPrint("Standard output redirection without file name",
        FPRINTF);
      else if (syncheck == SYN_FAIL_MULTREDIN)
        errorPrint("Multiple redirection of standard input", FPRINTF);
      else if (syncheck == SYN_FAIL_NODESTIN)
        errorPrint("Standard input redirection without file name",
        FPRINTF);
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

/*
  This function gets command string, and execute it
  1. get command string from ~/.ishrc file
  2. executes commands
  3. User interaction
    3-1. get command from user
    3-2. execute it
*/
int main() {
  // Initialize backgroud process trace
  bgtrace = (backList *)calloc(1, sizeof(backList));
  bgtrace->start = NULL;
  bgtrace->count = 0;

  // Initialize shell name
  errorPrint("./ish", SETUP);

  // Signal Handlers
  signal(SIGCHLD, sigchld_hander);

  // Set SIGINT, SIGQUIT Handlers
  sigset_t sSet1, sSet2;

  sigemptyset(&sSet1);
  sigaddset(&sSet1, SIGINT);
  sigprocmask(SIG_UNBLOCK, &sSet1, NULL);

  sigemptyset(&sSet2);
  sigaddset(&sSet2, SIGQUIT);
  sigprocmask(SIG_UNBLOCK, &sSet2, NULL);
  
  // Ignore SIGINT
  void (*pf) (int);
  pf=signal(SIGINT, SIG_IGN);
  assert(pf != SIG_ERR);

  // If press ctrl+\ and press ctrl+\ again in 5 seconds to terminate
  pf=signal(SIGQUIT, sigquit_handler);
  assert(pf != SIG_ERR);

  // remove alarm message
  pf=signal(SIGALRM, sigalrm_handler);
  assert(pf != SIG_ERR);

  // 1. Open ~/.ishrc, and run command in this file
  // Get HOME path
  char* home_directory = getenv("HOME");
  if (home_directory == NULL) {
    exit(EXIT_FAILURE);
  }

  // Open ~/.ishrc file
  int path_len = strlen(home_directory) + strlen("/.ishrc");
  char *file_path = (char *)calloc(1, path_len + 1);
  sprintf(file_path, "%s/.ishrc", home_directory);

  FILE *fp1 = fopen(file_path,"r");
  free(file_path);
  char acLine[MAX_LINE_SIZE + 2];
  
  if (fp1 != NULL) {
    // Get commands from ~/.ishrc file, run each command
    while(fgets(acLine,MAX_LINE_SIZE,fp1) != NULL){
      // Print each command
      fprintf(stdout, "%% %s", acLine);
      fflush(stdout);
      // Run each command
      shellHelper(acLine);
    }

    fclose(fp1);
  }

  // 2. Interaction with user
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }
}

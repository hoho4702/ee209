#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "lexsyn.h"
#include "util.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

unsigned int sigquit_timeout=1;
unsigned int malloc_flag_dynarray=0;
unsigned int malloc_flag_token=0;
unsigned int malloc_flag_child_argv_dyn=0;

static void freeifmalloc(DynArray_T oTokens, DynArray_T child_argv_dyn){
  if(malloc_flag_token){
    for(int i=0; i < DynArray_getLength(oTokens);i++){
      freeToken(DynArray_get(oTokens, i));
    }
    malloc_flag_token=0;
  }
  if(malloc_flag_dynarray){
    DynArray_free(oTokens);
    malloc_flag_dynarray=0;
  }
  if(malloc_flag_child_argv_dyn){
    while(DynArray_getLength(child_argv_dyn)!=0){
      DynArray_free(DynArray_removeAt(child_argv_dyn, 0));
    }
    DynArray_free(child_argv_dyn);
    malloc_flag_child_argv_dyn=0;
  }
}

static void
shellHelper(const char *inLine) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    freeifmalloc(oTokens, NULL);
    exit(EXIT_FAILURE);
  }
  malloc_flag_dynarray=1;

  lexcheck = lexLine(inLine, oTokens);
  malloc_flag_token=1;

  struct Token* exec_token;
  unsigned int stdin_redir=0;
  unsigned int stdout_redir=0;
  DynArray_T child_argv_dyn;
  child_argv_dyn=DynArray_new(0);
  if (child_argv_dyn == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    freeifmalloc(oTokens, child_argv_dyn);
    exit(EXIT_FAILURE);
  }
  malloc_flag_child_argv_dyn=1;
  int new_stdin_fd;
  int new_stdout_fd;
  int old_stdin_fd;
  int old_stdout_fd;
  int process_num=1;
  int ex_process_out;

  DynArray_T child_argv_dyn_now;
  child_argv_dyn_now=DynArray_new(0);
  if (child_argv_dyn_now == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    freeifmalloc(oTokens, child_argv_dyn);
    exit(EXIT_FAILURE);
  }
  DynArray_add(child_argv_dyn, child_argv_dyn_now);

  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0)
        return;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        switch(btype){
          /* TODO : 여기다가 빌트인함수 동작 구현 */
          case NORMAL:
          for(int i=0;i<DynArray_getLength(oTokens);i++){
            exec_token=DynArray_get(oTokens, i);
            switch(exec_token->eType){
              case TOKEN_PIPE: 
              child_argv_dyn_now=DynArray_new(0);
              if (child_argv_dyn_now == NULL) {
                errorPrint("Cannot allocate memory", FPRINTF);
                freeifmalloc(oTokens, child_argv_dyn);
                exit(EXIT_FAILURE);
              }
              DynArray_add(child_argv_dyn, child_argv_dyn_now);
              process_num++;
              break;

              case TOKEN_REDIN:
              i++;
              exec_token=DynArray_get(oTokens, i);
              new_stdin_fd = open(exec_token->pcValue, O_RDONLY);
              if(new_stdin_fd==-1){
                errorPrint(NULL, PERROR);
                freeifmalloc(oTokens, child_argv_dyn);
                exit(EXIT_FAILURE);
                break;
              }
              old_stdin_fd = dup(STDIN_FILENO);
              if(old_stdin_fd==-1){
                errorPrint(NULL, PERROR);
                freeifmalloc(oTokens, child_argv_dyn);
                exit(EXIT_FAILURE);
                break;
              }
              stdin_redir+=1;
              break;

              case TOKEN_REDOUT:
              i++;
              exec_token=DynArray_get(oTokens, i);
              new_stdout_fd = open(exec_token->pcValue, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
              if(new_stdout_fd==-1){
                errorPrint(NULL, PERROR);
                freeifmalloc(oTokens, child_argv_dyn);
                exit(EXIT_FAILURE);
                break;
              }
              old_stdout_fd = dup(STDOUT_FILENO);
              if(old_stdout_fd==-1){
                errorPrint(NULL, PERROR);
                freeifmalloc(oTokens, child_argv_dyn);
                exit(EXIT_FAILURE);
                break;
              }
              stdout_redir+=1;
              break;

              case TOKEN_WORD:
              DynArray_add(child_argv_dyn_now, exec_token->pcValue);
              break;

              default:
              assert(FALSE);
              break;
            }
          }

          while(DynArray_getLength(child_argv_dyn)>0){
            //stdin, stdout redirection error로 탈출하면 여기로 옴
            if(stdin_redir && DynArray_getLength(child_argv_dyn)==process_num && dup2(new_stdin_fd, STDIN_FILENO)==-1){
              errorPrint(NULL, PERROR);
              freeifmalloc(oTokens, child_argv_dyn);
              exit(EXIT_FAILURE);
              break;
            }
            
            if(stdout_redir && DynArray_getLength(child_argv_dyn)==1 && dup2(new_stdout_fd, STDOUT_FILENO)==-1){
              errorPrint(NULL, PERROR);
              freeifmalloc(oTokens, child_argv_dyn);
              exit(EXIT_FAILURE);
              break;
            }

            //child에 넘겨줄 argv 만들기
            child_argv_dyn_now=DynArray_removeAt(child_argv_dyn, 0);
            char** child_argv=calloc(DynArray_getLength(child_argv_dyn_now)+1, sizeof(char*));
            if(child_argv==NULL){
              errorPrint(NULL, PERROR);
              freeifmalloc(oTokens, child_argv_dyn);
              exit(EXIT_FAILURE);
            }
            DynArray_toArray(child_argv_dyn_now, (void**)child_argv);

            // for(int i=0;i<DynArray_getLength(child_argv_dyn); i++){
            //     printf("%s\n", child_argv[i]);
            // }
            int pipe_chi_to_par[2];
            if(DynArray_getLength(child_argv_dyn)>0 && pipe(pipe_chi_to_par)==-1){
              errorPrint(NULL, PERROR);
              freeifmalloc(oTokens, child_argv_dyn);
              exit(EXIT_FAILURE);
            }

            //fork로 프로그램 실행
            fflush(NULL);
            pid_t pid=fork();
            if(pid==0){
              //child process
              //errorPrint(child_argv[0], SETUP); //ishname 설정
              if(DynArray_getLength(child_argv_dyn)>0){
                close(pipe_chi_to_par[0]);
                if(dup2(pipe_chi_to_par[1], STDOUT_FILENO)==-1){
                  errorPrint(NULL, PERROR);
                  freeifmalloc(oTokens, child_argv_dyn);
                  exit(EXIT_FAILURE);
                  break;
                }
              }
              if(DynArray_getLength(child_argv_dyn)<process_num-1){
                if(dup2(ex_process_out, STDIN_FILENO)==-1){
                  errorPrint(NULL, PERROR);
                  freeifmalloc(oTokens, child_argv_dyn);
                  exit(EXIT_FAILURE);
                  break;
                }
              }
              if((long)signal(SIGINT, SIG_DFL)==-1 || (long)signal(SIGALRM, SIG_DFL)==-1 || (long)signal(SIGQUIT, SIG_DFL)==-1){
                errorPrint(NULL, PERROR);
                exit(EXIT_FAILURE);
              }
              execvp(child_argv[0], child_argv);
              errorPrint(child_argv[0], PERROR);
              free(child_argv);
              freeifmalloc(oTokens, child_argv_dyn);
              exit(EXIT_FAILURE);
            }
            else{
              //parent process
              if(DynArray_getLength(child_argv_dyn)>0){
                close(pipe_chi_to_par[1]);
              }
              if(waitpid(pid, NULL, 0)==-1){
                errorPrint(NULL, PERROR);
                free(child_argv);
                freeifmalloc(oTokens, child_argv_dyn);
                exit(EXIT_FAILURE);
              }
              
              if(DynArray_getLength(child_argv_dyn)>0){
                //close(ex_process_out);
                ex_process_out=pipe_chi_to_par[0];
              }

              //stdio 복구
              if(stdin_redir==1 && DynArray_getLength(child_argv_dyn)==(process_num-1) && (close(new_stdin_fd)==-1 || dup2(old_stdin_fd, STDIN_FILENO)==-1)){
                errorPrint(NULL, PERROR);
                free(child_argv);
                freeifmalloc(oTokens, child_argv_dyn);
                exit(EXIT_FAILURE);
              }

              if(stdout_redir==1 && DynArray_getLength(child_argv_dyn)==0 && (close(new_stdout_fd)==-1 || dup2(old_stdout_fd, STDOUT_FILENO)==-1)){
                errorPrint(NULL, PERROR);
                free(child_argv);
                freeifmalloc(oTokens, child_argv_dyn);
                exit(EXIT_FAILURE);
              }

              //동적메모리할당 free
              free(child_argv);
              DynArray_free(child_argv_dyn_now);
            }
          }
          //stdin, stdout 다시 돌려놔야 함
          break;

          case B_EXIT:
          if(DynArray_getLength(oTokens)>1){
            errorPrint("exit does not take any parameters", FPRINTF);
          }
          else{
            freeifmalloc(oTokens, child_argv_dyn);
            exit(0);
          }
          break;

          case B_SETENV:
          if((DynArray_getLength(oTokens) !=2 && DynArray_getLength(oTokens) != 3) || ((struct Token*)DynArray_get(oTokens, 1))->eType != TOKEN_WORD){
            errorPrint("setenv takes one or two parameters", FPRINTF);
            break;
          }
          else if(DynArray_getLength(oTokens) == 2){
            if(setenv(((struct Token*)DynArray_get(oTokens, 1))->pcValue, "", 1)==-1){
              errorPrint(NULL, PERROR);
            }
          }
          else{
            if(setenv(((struct Token*)DynArray_get(oTokens, 1))->pcValue, ((struct Token*)DynArray_get(oTokens, 2))->pcValue, 1)==-1){
              errorPrint(NULL, PERROR);
            }
          }
          break;

          case B_USETENV:
          if(DynArray_getLength(oTokens) !=2){
            errorPrint("unsetenv takes one parameter", FPRINTF);
            break;
          }
          else{
            if(unsetenv(((struct Token*)DynArray_get(oTokens, 1))->pcValue)==-1){
              errorPrint(NULL, PERROR);
            }
          }
          break;

          case B_CD:
          if(DynArray_getLength(oTokens) !=1 && DynArray_getLength(oTokens) !=2){
            errorPrint("cd takes one parameter", FPRINTF);
            break;
          }
          else if(DynArray_getLength(oTokens) == 1){
            if(getenv("HOME")==NULL) break;
            if(chdir(getenv("HOME"))==-1){
              errorPrint(NULL, PERROR);
            }
          }
          else{
            if(chdir(((struct Token*)DynArray_get(oTokens, 1))->pcValue) == -1){
              errorPrint(NULL, PERROR);
            }
          }
          break;

          default:
          assert(FALSE);
          break;
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
      freeifmalloc(oTokens, child_argv_dyn);
      exit(EXIT_FAILURE);
  }
  freeifmalloc(oTokens, child_argv_dyn);
}

static void sigquit_handler(){
  if(sigquit_timeout){
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(NULL);
    sigquit_timeout=0;
    alarm(5);
  }
  else{
    exit(1);
  }
}
static void sigalrm_handler(){
  sigquit_timeout=1;
}

int main(int argc, char* argv[]) {
  char acLine[MAX_LINE_SIZE + 2];
  sigset_t sig_set;

  if(sigemptyset(&sig_set)==-1){
    errorPrint(NULL, PERROR);
    exit(EXIT_FAILURE);
  }

  if(sigaddset(&sig_set, SIGINT)==-1 || sigaddset(&sig_set, SIGQUIT)==-1 || sigaddset(&sig_set, SIGALRM)==-1){
    errorPrint(NULL, PERROR);
    exit(EXIT_FAILURE);
  }

  if(sigprocmask(SIG_UNBLOCK, &sig_set, NULL)==-1){
    errorPrint(NULL, PERROR);
    exit(EXIT_FAILURE);
  }

  if((long)signal(SIGINT, SIG_IGN)==-1 || (long)signal(SIGALRM, sigalrm_handler) || (long)signal(SIGQUIT, sigquit_handler)){
    errorPrint(NULL, PERROR);
    exit(EXIT_FAILURE);
  }

  errorPrint(argv[0], SETUP); //ishname 설정
  char ishrc_directory[100];
  strcpy(ishrc_directory, getenv("HOME"));
  strcat(ishrc_directory, (char*)"/.ishrc");

  FILE* file_ishrc = fopen(ishrc_directory, "r");
  if(file_ishrc!=NULL){
    /* ishrc 파일 열고 실행 */
    while(1){
      if (fgets(acLine, MAX_LINE_SIZE, file_ishrc) == NULL) {
        if(feof(file_ishrc)){
          break;
        }
        else{
          errorPrint(NULL, PERROR);
        }
      }
      printf("%% %s", acLine);
      if(acLine[strlen(acLine)-1]!='\n') printf("\n");
      shellHelper(acLine);
    }
    if(fclose(file_ishrc)==EOF){
      errorPrint(NULL, PERROR);
    }
  }

  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      if(feof(stdin)){
        printf("\n");
        exit(EXIT_SUCCESS);
      }
      else{
        errorPrint(NULL, PERROR);
      }
    }
    shellHelper(acLine);
  }
}
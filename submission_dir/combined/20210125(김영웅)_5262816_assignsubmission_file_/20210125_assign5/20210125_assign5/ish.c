// name : Kim Yeongung, student ID : 20210125
// This code file make some simple UNIX shell by using some function in variable header file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "lexsyn.h"
#include "util.h"
#include <fcntl.h>
#include <signal.h>

volatile sig_atomic_t sigquit_received = 0; // Uses it to handles SITQUIT signal.

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

static void
shellHelper(const char *inLine) {
  DynArray_T oTokens; // 명령어를 저장할 동적배열

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }


  lexcheck = lexLine(inLine, oTokens); // 입력 문자열 inLine을 해석하여 토큰 배열에 저장한다.
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0)
        return;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens); // 명령어의 구문이 올바른지 확인한다
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0)); // 내장 명령어인지 확인한다
        /* TODO */
        if(btype == B_CD) // when "cd" command is entered
        {
          /* cd command always use path value, and that value is located next to "cd"
          so, pick the path value by using DynArray_get function. */
          struct Token *pToken = (struct Token *)DynArray_get(oTokens, 1);

          if(chdir(pToken->pcValue)!=0)
          {
            perror("chdir failed");
          }
          break;
        }
        else if(btype == B_EXIT)
        /* exit command terminate the process.*/
        {
          exit(0);
        }
        else if(btype == B_SETENV)
        /* setenv function set the environment variable.*/
        {
          struct Token *pToken1 = (struct Token *)DynArray_get(oTokens, 1);
          struct Token *pToken2 = (struct Token *)DynArray_get(oTokens, 2);
          setenv(pToken1->pcValue, pToken2->pcValue, 1);
        }
        else if(btype == B_USETENV)
        /* unsetenv function destroy the environment variable.*/
        {
          struct Token *pToken = (struct Token *)DynArray_get(oTokens, 1);
          unsetenv(pToken->pcValue);
        }
        else
        /* Otherwise is normal case that use execvp.
        Determine the case of redirection and design a specific method.
        make Array variable that store pointer of pcValue of each token and used it to execute by using execvp
        Also make ttype(token type) variable that store enum TokenType value to execute redirection command
        Because redirection command character('<', '>')'s pcValue is NULL. We use token type to search redirection
        command.*/
        {
          pid_t pid = fork();
          if(pid == 0)
          {
            struct Token *pToken;
            enum TokenType *ttype; 
            char **Array;
            int i, fd;
            Array = (char **)malloc(sizeof(char *)*(DynArray_getLength(oTokens)+1));
            ttype = (enum TokenType *)malloc(sizeof(enum TokenType)*(DynArray_getLength(oTokens)+1));
            for(i = 0 ; i < DynArray_getLength(oTokens) ; i++)
            {
              pToken= (struct Token *)DynArray_get(oTokens, i);
              Array[i] = pToken->pcValue;
              ttype[i] = pToken->eType;
            }
            Array[DynArray_getLength(oTokens)] = NULL;
            ttype[DynArray_getLength(oTokens)] = TOKEN_WORD; 
            
            if(ttype[DynArray_getLength(oTokens)-2]==TOKEN_REDIN)
            {
              fd = open(Array[DynArray_getLength(oTokens)-1], O_RDONLY);
              dup2(fd, 0);
              close(fd);
              Array[DynArray_getLength(oTokens)-2] = NULL;
              Array[DynArray_getLength(oTokens)-1] = NULL;
              if(execvp(Array[0] , Array) < 0)
              {
                printf("%s: No such file or directory\n", Array[0]);
                exit(0);
              }
            }
            else if(ttype[DynArray_getLength(oTokens)-2]==TOKEN_REDOUT)
            {
              fd = open(Array[DynArray_getLength(oTokens)-1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
              close(1);
              dup(fd);
              close(fd);
              Array[DynArray_getLength(oTokens)-2] = NULL;
              if(execvp(Array[0] , Array) < 0)
              {
                printf("%s: No such file or directory\n", Array[0]);
                exit(0);
              }
            }
            else
            {
              if(execvp(Array[0] , Array) < 0)
              {
                printf("%s: No such file or directory\n", Array[0]);
                exit(0);
              } 
            }
            for(i = 0 ; i <= DynArray_getLength(oTokens) ; i++)
            {
              free(Array[i]);
            }
            free(Array);
          }
          wait(NULL);
          break;
        }
      }

      /* syntax error cases */
      else if (syncheck == SYN_FAIL_NOCMD)
      {
        errorPrint("./ish", SETUP);
        errorPrint("Missing command name", FPRINTF); 
      }
      else if (syncheck == SYN_FAIL_MULTREDOUT)
      {
        errorPrint("./ish", SETUP);
        errorPrint("Multiple redirection of standard out", FPRINTF);
      }
      else if (syncheck == SYN_FAIL_NODESTOUT)
      {
        errorPrint("./ish", SETUP);
        errorPrint("Standard output redirection without file name", FPRINTF);
      }
      else if (syncheck == SYN_FAIL_MULTREDIN)
      {
        errorPrint("./ish", SETUP);
        errorPrint("Multiple redirection of standard input", FPRINTF);
      }
      else if (syncheck == SYN_FAIL_NODESTIN)
      {
        errorPrint("./ish", SETUP);
        errorPrint("Standard input redirection without file name", FPRINTF);
      }
      else if (syncheck == SYN_FAIL_INVALIDBG)
      {
        errorPrint("./ish", SETUP);
        errorPrint("Invalid use of background", FPRINTF);
      }
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

/* SIGINT handler, parent process should ignore the SIGINT signal*/
void sigint_handler(int sig) 
{
    fflush(stdout);
}

/* SIGQUIT handler, the parent process should print the message "Type Ctrl+\ agin within 5 seconds to exit".*/
void sigquit_handler(int sig) 
{
    if (sigquit_received == 0) 
    {
      sigquit_received = 1;
      printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
      fflush(stdout);
      alarm(5);
    } 
    else
    {
      fflush(stdout);
      exit(EXIT_SUCCESS);
    }
}

/* SIGALARM handler, sigquit handler send alarm signal so SIGALARM handler is executing*/
void alarm_handler(int sig)
{
  sigquit_received = 0;
  fflush(stdout);
}

int main() {
  /* TODO */
  /* make signal set, and install signal handler by using signal function. */
  sigset_t mask;

  sigemptyset(&mask);
  sigprocmask(SIG_SETMASK, &mask, NULL);

  // SIGQUIT 핸들러 설정
  signal(SIGQUIT, sigquit_handler);

  // SIGINT 핸들러 설정
  signal(SIGINT, sigint_handler);

  // SIGALRM 핸들러 설정
  signal(SIGALRM, alarm_handler);
  
  char acLine[MAX_LINE_SIZE + 2]; 

  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) { // 명령어를 읽어온다.
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine); // 입력된 명령어를 shellHelper에 전달
  }
}


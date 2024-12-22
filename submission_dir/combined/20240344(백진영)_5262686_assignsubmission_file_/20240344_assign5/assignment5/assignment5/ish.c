#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>


#include "lexsyn.h"
#include "util.h"
#include "assert.h"
#include "signal.h"
#include "dynarray.h"
#include "token.h"

#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/wait.h>

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/



/*--------------------------------------------------------------------*/
/* SigquitHandler
SIGQUIT 신호를 처리합니다.
매개변수: iSig - 신호 번호(int).
반환: 공백
읽기: 없음.
쓰기: stdout에게 메시지를 출력합니다.
글로벌: SIGQUIT 신호의 동작을 수정합니다.*/
/*--------------------------------------------------------------------*/





/*--------------------------------------------------------------------*/
/* sigalrmHanler
SIGALRM 신호를 처리합니다.
매개변수: iSig - 신호 번호(int).
반환: void
읽기: 없음.
쓰기: 없음.
글로벌: SIGQUIT 신호의 동작을 수정합니다.*/
/*--------------------------------------------------------------------*/



/*--------------------------------------------------------------------*/
/* shellHelper
단일 명령줄 입력을 처리합니다.
매개변수: 인라인 - 명령줄 입력(const char*).
반환: void
읽기: inLine을 통해 표준 입력에서 간접적으로 읽기.
쓰기: 구문 오류에 대한 오류 메시지를 stderr에 출력합니다.
글로벌: DynArray_T oTokens를 사용하고 잠재적으로 수정합니다*/
/*--------------------------------------------------------------------*/

static void
shellHelper(const char *inLine) {
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

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
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

/*--------------------------------------------------------------------*/
/* pipe_preprocessing
파이프 토큰으로 명령줄을 처리하고 실행을 준비합니다.
매개변수: oTokens - 명령줄(DynArray_T)을 나타내는 토큰.
반환: void*
읽기: 없음.
쓰기: 메모리 할당 실패 시 stderr에 오류 메시지를 출력합니다.
전역: pipe_fd, pipe_fd2를 사용하고 잠재적으로 수정합니다.*/
/*--------------------------------------------------------------------*/




/*--------------------------------------------------------------------*/
/* non_builtin
oTokens로 표시되는 내장되지 않은 명령을 실행합니다.
매개변수: oTokens - 명령어(DynArray_T)를 나타내는 토큰.
            state - 파이프 상태(int)를 나타냅니다.
반환: void*
읽기: 표준 입력 및 잠재적으로 파일에서 읽을 수 있습니다.
쓰기: 표준 출력 및 표준 오류에 쓰기.
전역: pipe_fd, pipe_fd2 를 수정합니다.*/
/*--------------------------------------------------------------------*/







/*--------------------------------------------------------------------*/
/* main
셸 프로그램의 주요 기능.
매개변수: argc - 명령줄 인수(int) 수입니다.
argv - 명령줄 인수 배열(char*[]).
반환: 프로그램 종료 상태.
읽기: 표준 입력에서 읽기.
쓰기: 표준 출력 및 표준 오류에 쓰기.
글로벌: SIGINT, SIGQUIT, SIGALRM 신호의 동작을 수정합니다,
pipe_fd, pipe_fd2를 사용합니다.

이 기능은 신호 처리기를 초기화하고 환경을 설정합니다,
stdin 또는 지정된 파일의 명령을 처리하고 처리합니다
상호작용 셸 세션.*/
/*--------------------------------------------------------------------*/




int main() {
  /* TODO */
  char acLine[MAX_LINE_SIZE + 2];
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


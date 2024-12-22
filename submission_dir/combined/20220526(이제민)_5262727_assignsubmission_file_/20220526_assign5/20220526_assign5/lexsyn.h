#ifndef _LEXSYN_H_
#define _LEXSYN_H_

#include "dynarray.h"

enum {MAX_LINE_SIZE = 1024};
enum {MAX_ARGS_CNT = 64};

enum LexResult {LEX_SUCCESS, LEX_QERROR/*따옴표 제대로 안닫힘*/, LEX_NOMEM/*동적메모리할당 실패패*/, LEX_LONG/*1024자 넘어감*/};
enum AliasResult {ALIAS_SUCCESS, ALIAS_LONG, ALIAS_QERROR};
enum SyntaxResult {
  SYN_SUCCESS,
  SYN_FAIL_NOCMD, //명령이 빠짐짐
  SYN_FAIL_MULTREDIN, //
  SYN_FAIL_NODESTIN,
  SYN_FAIL_MULTREDOUT,
  SYN_FAIL_NODESTOUT,
  SYN_FAIL_INVALIDBG,
};

void command_lexLine(const char * pcLine, DynArray_T ctokens);
enum AliasResult alias_lexLine(const char *pcLine, DynArray_T oTokens);
enum LexResult lexLine_quote(const char *pcLine, DynArray_T oTokens);
enum LexResult lexLine(const char *pcLine, DynArray_T oTokens); //얘만쓰면 될듯? 얘가 주어진 dynarray에 토큰 다 넣어줌ㅋㅋ
enum SyntaxResult syntaxCheck(DynArray_T oTokens);

#endif /* _LEXSYN_H_ */

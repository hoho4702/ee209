#ifndef _TOKEN_H_
#define _TOKEN_H_

/*
얘가 "토큰". 이 라이브러리는 완전히 독립적임.
토큰 종류에 대한 해설은 아래 참고. 얘로 인해 lexically analyze 단계에서 토큰 종류 구분할 수 있음.
util.*가 얘에 의존함함
이 토큰은 외부에서 dynarray에 저장됨
*/

enum TokenType {
  TOKEN_PIPE, //기능하는 |
  TOKEN_REDIN, //기능하는 <
  TOKEN_REDOUT, //기능하는 >
  TOKEN_WORD, //기타 ("", '', 기능하지 않는 |, >, < 포함함)
  /*TOKEN_BG*/};

struct Token {
  /* The type of the token. */
  enum TokenType eType;

  /* The string which is the token's value. */
  char *pcValue; //eType!=TOKEN_WORD이면 NULL이어야 함함
};

void freeToken(void *pvItem);
struct Token *makeToken(enum TokenType eTokenType, char *pcValue);
#endif /* _TOKEN_H_ */
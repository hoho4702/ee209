/*--------------------------------------------------------------------*/
/* token.h - 20230523 Yeonjun Lee                                     */
/*This is header file to use functions related about tokens in token.c*/
/*--------------------------------------------------------------------*/

#ifndef _TOKEN_H_
#define _TOKEN_H_

enum TokenType {
  TOKEN_PIPE,
  TOKEN_REDIN,
  TOKEN_REDOUT,
  TOKEN_WORD,
  TOKEN_BG};

struct Token {
  /* The type of the token. */
  enum TokenType eType;

  /* The string which is the token's value. */
  char *pcValue;
};

void freeToken(void *pvItem, void *pvExtra);
struct Token *makeToken(enum TokenType eTokenType, char *pcValue);
#endif /* _TOKEN_H_ */

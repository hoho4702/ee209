/* Background */
/* Name: Seheon Kim */
/* Student ID: 20200098 */
/* token source code role: Contains code for creating and managing 
tokens. Tokens are the basic data structure used to analyze shell 
commands. */

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

#ifndef _UTIL_H_
#define _UTIL_H_

#include "token.h"
#include "dynarray.h"

enum {FALSE, TRUE};

enum BuiltinType {NORMAL, B_EXIT, B_SETENV, B_USETENV, B_CD/*, B_ALIAS, B_FG*/}; //빌트인 함수의 종류
enum PrintMode {SETUP, PERROR, FPRINTF, ALIAS};

void errorPrint(char *input, enum PrintMode mode); //SETUP으로 ishname 먼저 설정해주고 그다음에 PERROR 모드로 한번 써보자. input은 child process의 이름 넣어주기 (없으면 ishname로 자동으로 들어감감)
enum BuiltinType checkBuiltin(struct Token *t); //토큰이 빌트인 함수인지 여부 확인
int countPipe(DynArray_T oTokens); //Pipe (|) 토큰의 개수를 셈
//int checkBG(DynArray_T oTokens);
void dumpLex(DynArray_T oTokens); //dynarray에 들어있는 모든 토큰을 stderr로 프린트함, 환경변수 DEBUG 설정되어 있어야 함함

#endif /* _UTIL_H_ */

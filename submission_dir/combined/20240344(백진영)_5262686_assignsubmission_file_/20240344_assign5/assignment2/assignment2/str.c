#include <assert.h> /* to use assert() */
#include <stdio.h>
#include <stdlib.h> /* for strtol() */
#include <string.h>
#include <strings.h>
#include "str.h"

/* Your task is: 
   1. Rewrite the body of "Part 1" functions - remove the current
      body that simply calls the corresponding C standard library
      function.
   2. Write appropriate comment per each function
*/

/* Part 1 */
/*------------------------------------------------------------------------*/
size_t StrGetLength(const char* pcSrc)//글자길이 세는 함수
{
  const char *pcEnd;
  assert(pcSrc); /* NULL address, 0, and FALSE are identical. */
  pcEnd = pcSrc;
	
  while (*pcEnd) /* null character and FALSE are identical. */
    pcEnd++;

  return (size_t)(pcEnd - pcSrc);
}

/*------------------------------------------------------------------------*/
char *StrCopy(char *pcDest, const char* pcSrc)
{
  size_t i;
  assert(pcSrc);
  int n=StrGetLength(pcSrc);//인풋 길이 재기
  if(n == 0)
    pcDest[0] = '\0';

  for (i = 0; i < n && pcSrc[i] != '\0'; i++)
      pcDest[i] = pcSrc[i];// \0 지우고 삽입
  for ( ; i < n; i++)
      pcDest[i] = '\0';//나머지 0으로 채우기
  return pcDest;
}

/*------------------------------------------------------------------------*/
int StrCompare(const char* pcS1, const char* pcS2)
{
  assert(pcS1);//비어있는지 확인
  assert(pcS2);//위와 동문
  return StrGetLength(pcS1)-StrGetLength(pcS2) ;//크기 비교
}
/*------------------------------------------------------------------------*/
char *StrFindChr(const char* pcHaystack, int c)
{
  assert(pcHaystack);//빈지 확인
  int n = StrGetLength(pcHaystack);//전체 크기 확인
  for(int i=0; i<=n;i++){
      if(pcHaystack[i]==c){//겹치는 거 확인
          return (char *)&pcHaystack[i];
      }

  }
  return NULL;//없으면 null반환
}
/*------------------------------------------------------------------------*/
char *StrFindStr(const char* pcHaystack, const char *pcNeedle)
{
  assert(pcHaystack);//빈지 확인
  assert(pcNeedle);
  /* TODO: fill this function */
  int n=StrGetLength(pcHaystack);
  if(StrFindChr(pcHaystack, '\0')!=NULL)
    n+=1;
  int m=StrGetLength(pcNeedle);
  int cnt=0;
  for (int i=0;i<n-m;i++){
      for(int j=0;j<m;j++){//앞에와는 다르게 긴문장으로 확인
          if(pcHaystack[i+j]==pcNeedle[j]){
              cnt++;//그 특정길이에서 동일한 갯수 확인
          }
      }
      if (cnt==m){
          return (char*)&pcHaystack[i];//같으면 첫 위치 반환
      }
      else{
          cnt=0;//같지 않았으면 다시 카운트 0으로 만들기
      }
  }
  return NULL;
}
/*------------------------------------------------------------------------*/
char *StrConcat(char *pcDest, const char* pcSrc)
{
  /* TODO: fill this function */
  assert(pcSrc);
  int n= StrGetLength(pcDest)+StrGetLength(pcSrc)+1;//전체 크기 확인
  int m = StrGetLength(pcDest);//받은거 확인
  for(int i=0;i<n;i++){
      pcDest[m+i]=pcSrc[i];//추가하기
  }
  return pcDest;
}

/*------------------------------------------------------------------------*/
long int StrToLong(const char *nptr, char **endptr, int base)
{
   assert(nptr);//빈지 확인
    // handle only when base is 10
    if (base != 10) {
      return 0;}
    long int k=0;
    int sn;

    char* bgn = (char*)nptr;
    while (isspace(*bgn)) {//처음 사인 확인
        if (!*bgn){
            return 0;}
        bgn++;
    }
    if (*bgn == '-') {sn = -1; bgn++;}//부호 확인
    else if (*bgn == '+') {sn = 1; bgn++;}
    else sn = 1;

    char* ed = bgn;
    while (isdigit(*ed)) ed++;

    while (bgn != ed){
        k = k*10 + ((int)(*bgn) - '0');//값표현
        if (sn==-1 && k<0) return LONG_MIN;
        if (sn==1 && k<0) return LONG_MAX;
        bgn++;
    }
    k = k * (sn);
    if (endptr) *endptr = ed;
    return k;//long 값 반환
}

/*------------------------------------------------------------------------*/
int StrCaseCompare(const char *pcS1, const char *pcS2)
{
  assert(pcS1);//빈거 확인
  assert(pcS2);
  /* TODO: fill this function */
  int n= StrGetLength(pcS1)<StrGetLength(pcS2)?StrGetLength(pcS1):StrGetLength(pcS2);//작은 구하기
  int a = StrGetLength(pcS1);//각각 길이 저장
  int b = StrGetLength(pcS2);
  if (a!=b)// 다름을 확인
      return a-b;//차이 반환
  int q = 0;
  char* L = "[\\]^_`";// 알파벳사이에 있는 문자 확인
  char* l = "abcdefghijklmnopqrstuvwxyz";//알파벳 체크용
  for(int i=0;i<n;i++){
      if(pcS1[i]!=pcS2[i])
      {
          if(StrFindChr(L, pcS1[i])!=NULL || StrFindChr(L, pcS2[i])!=NULL)//겹침 확인
          {
              return pcS1[i] - pcS2[i];
          }
          else
          {
              if(StrFindChr(l, pcS1[i])!=NULL)
                  q -= 32;//알파벳 보정
              if(StrFindChr(l, pcS2[i])!=NULL)
                  q += -32;//알파벳 보정
              return pcS1[i] - pcS2[i] + q;
          }
      } 
  }
  return 0; //끝내깅
}
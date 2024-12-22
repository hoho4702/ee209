#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* for skeleton code */
#include <unistd.h> /* for getopt */
#include "str.h"

#define MAX_STR_LEN 1023

#define FALSE 0
#define TRUE  1

int check_pattern(const char *str, const char *pattern) {
  char* paat;
  char* tat;//택스트
  paat = (char*) pattern;
  tat = (char*) str;

  while (*paat) {
        if (*paat == '*') {//체크 용
            while (*tat) {
              if (check_pattern(tat, paat+1)) {
                return TRUE;
                }
                 tat+=1;
              }
            return FALSE;
          } 
        else {//다른 경우
            if (*tat == *paat) {
                if (check_pattern(tat+1, paat+1)) return TRUE;
              }
            if (!*tat) return FALSE;
            
            return FALSE;//안되면
          }
      }
    return TRUE;
  }

  /*--------------------------------------------------------------------*/
  /* PrintUsage()
    print out the usage of the Simple Grep Program                     */
  /*--------------------------------------------------------------------*/
  void PrintUsage(const char* argv0)//심플한 확인~
  {
    const static char *asd =
      "Simple Grep (sgrep) Usage:\n"
      "%s pattern [stdin]\n";

    printf(asd, argv0);
  }
  /*-------------------------------------------------------------------*/
  /* SearchPattern()
    Your task:
    1. Do argument validation
    - String or file argument length is no more than 1023
    - If you encounter a command-line argument that's too long,
    print out "Error: pattern is too long"

    2. Read the each line from standard input (stdin)
    - If you encounter a line larger than 1023 bytes,
    print out "Error: input line is too long"
    - Error message should be printed out to standard error (stderr)

    3. Check & print out the line contains a given string (search-string)

    Tips:
    - fgets() is an useful function to read characters from file. Note
    that the fget() reads until newline or the end-of-file is reached.
    - fprintf(sderr, ...) should be useful for printing out error
    message to standard error

    NOTE: If there is any problem, return FALSE; if not, return TRUE  */
  /*-------------------------------------------------------------------*/
  int SearchPattern(const char *pattern)
  {
    
    char buuf[MAX_STR_LEN + 2];

    if (StrGetLength(pattern)>MAX_STR_LEN){//최대길이 쳌
         
          fprintf(stderr, "Error: pattern is too long\n");
          return 0;
      }
    


    /* Read one line at a time from stdin, and process each line */
    while (fgets(buuf, sizeof(buuf), stdin)) {

      
      if (StrGetLength(buuf)>MAX_STR_LEN){//위와 동일한 체크
              fprintf(stderr, "Error: input line is too long\n");
              
              return 0;
          }
      
      int rees=FALSE;
          char* tmmp = buuf;
          
          while (*(tmmp+1)){
          
              rees = rees || check_pattern(tmmp, pattern);
              tmmp+=1;
          }
          
          if (rees) printf("%s", buuf);
    }

    return TRUE;
  }
  /*-------------------------------------------------------------------*/
  int main(const int argc, const char *argv[])
  {
    /* Do argument check and parsing */
    if (argc < 2) {
      fprintf(stderr, "Error: argument parsing error\n");
      PrintUsage(argv[0]);
      return (EXIT_FAILURE);
    }

    return SearchPattern(argv[1]) ? EXIT_SUCCESS:EXIT_FAILURE;
  }

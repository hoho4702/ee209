#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <wait.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>


#include "lexsyn.h"
#include "util.h"
#include "dynarray.h"

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/

struct ParsingResult {
    enum SyntaxResult ret;
    char **commandArray; // Command array in char form
    char *inputRd; // Input redirection file
    char *outputRd; // Output redirection file
};

char **convertArray(DynArray_T oTokens)

/* Convert the token values in oTokens to char */

{
  char *pcValue;
  char **charArray;
  DynArray_T oTemp;
  int arrayLength;

  oTemp = DynArray_new(0);

  for (int i = 0; i < DynArray_getLength(oTokens); i++) {
    struct Token *token = (struct Token *) DynArray_get(oTokens, i);

    if (token != NULL) {
        pcValue = token->pcValue; 
        DynArray_add(oTemp, pcValue);  
    }
  }
  arrayLength = DynArray_getLength(oTemp);

  charArray = (char **) calloc((size_t) arrayLength + 1, sizeof(char*));
  DynArray_toArray(oTemp, (void **) charArray);
  charArray[arrayLength] = NULL;
  DynArray_free(oTemp);

  return charArray;
  }

static struct ParsingResult parseTokens(DynArray_T oTokens, const char *progName) {
  int i;
    struct ParsingResult result = {SYN_SUCCESS, NULL, NULL, NULL};
    int riexist = FALSE, roexist = FALSE, pexist = FALSE;
    struct Token *t, *t1;

    assert(oTokens);

    /* Check the first token */
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
        t = DynArray_get(oTokens, i);
        if (i == 0) {
            if (t->eType != TOKEN_WORD) {
                /* Missing command name */
                result.ret = SYN_FAIL_NOCMD;
                return result;
            }
        } else {
            if (t->eType == TOKEN_PIPE) {
                if (roexist == TRUE) {
                    result.ret = SYN_FAIL_MULTREDOUT;
                    return result;
                } else {
                    if (i == DynArray_getLength(oTokens) - 1) {
                        result.ret = SYN_FAIL_NOCMD;
                        return result;
                    } else {
                        t1 = DynArray_get(oTokens, i + 1);
                        if (t1->eType != TOKEN_WORD) {
                            result.ret = SYN_FAIL_NOCMD;
                            return result;
                        }
                    }
                    pexist = TRUE;
                }
            } else if (t->eType == TOKEN_BG) {
                if (i != DynArray_getLength(oTokens) - 1) {
                    result.ret = SYN_FAIL_INVALIDBG;
                    return result;
                }
            } else if (t->eType == TOKEN_REDIN) {
                if ((pexist == TRUE) || (riexist == TRUE)) {
                    result.ret = SYN_FAIL_MULTREDIN;
                    return result;
                } else {
                    if (i == DynArray_getLength(oTokens) - 1) {
                        result.ret = SYN_FAIL_NODESTIN;
                        return result;
                    } else {
                        t1 = DynArray_get(oTokens, i + 1);
                        if (t1->eType != TOKEN_WORD) {
                            result.ret = SYN_FAIL_NODESTIN;
                            return result;
                        }
                        result.inputRd = t1->pcValue; // Save input redirection
                    }
                    riexist = TRUE;
                }
            } else if (t->eType == TOKEN_REDOUT) {
                if (roexist == TRUE) {
                    result.ret = SYN_FAIL_MULTREDOUT;
                    return result;
                } else {
                    if (i == DynArray_getLength(oTokens) - 1) {
                        result.ret = SYN_FAIL_NODESTOUT;
                        return result;
                    } else {
                        t1 = DynArray_get(oTokens, i + 1);
                        if (t1->eType != TOKEN_WORD) {
                            result.ret = SYN_FAIL_NODESTOUT;
                            return result;
                        }
                        result.outputRd = t1->pcValue; // Save output redirection
                    }
                    roexist = TRUE;
                }
            }
        }
    }

    /* Convert tokens to command array using convertArray */
    result.commandArray = convertArray(oTokens);

    return result;
}

static void
shellHelper(const char *inLine, const char *progName) {
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

        if (btype == B_CD){
          int noArg = DynArray_getLength(oTokens) - 1;
          char **commandArray = convertArray(oTokens);
          char *env;

          env = getenv("HOME");
          if(noArg > 1) fprintf(stderr, "%s: cd takes one parameter\n", progName);
          else if(noArg == 0 && env == NULL) fprintf(stderr, "%s: cd: HOME not set\n", progName);
          else if(noArg == 0 && env != NULL)
          {
            if(chdir(env) == -1)
            {
              fprintf(stderr, "%s: ", progName);
              perror(env);
            }
          }  
          else if(chdir(commandArray[1]) == -1)
          {
            fprintf(stderr, "%s: ", progName);
            perror(commandArray[1]);
          }
        }

        else if (btype == B_EXIT){
          int noArg = DynArray_getLength(oTokens) - 1;
          if(noArg > 0) fprintf(stderr, "%s: exit does not take any parameters\n", progName);
          else
          {
            exit(EXIT_SUCCESS);
          }
        }

        else if (btype == B_SETENV){
          int noArg = DynArray_getLength(oTokens) - 1;
          char **commandArray = convertArray(oTokens);
          if(noArg == 0)
            fprintf(stderr, "%s: setenv: setenv takes one or two parameters\n", progName);
          else if (noArg > 2) 
            fprintf(stderr, "%s: setenv: setenv takes one or two parameters\n", progName);
          else if(noArg == 1)
          {
            if(setenv(commandArray[1], "\0", 1) == -1)
            perror(progName);
          }
          else if(noArg == 2)
          {
            if(setenv(commandArray[1], commandArray[2], 1) == -1)
            perror(progName);
          }
        }

        else if (btype == B_USETENV){
          int noArg = DynArray_getLength(oTokens) - 1;
          char **commandArray = convertArray(oTokens);
          if(noArg == 0)
            fprintf(stderr, "%s: unsetenv takes one parameter\n", progName);
          else if(noArg > 1)
            fprintf(stderr, "%s: unsetenv takes one parameter\n", progName);
          else if(unsetenv(commandArray[1]) == -1)
            perror(progName);
        }
        else{
          char *pcStdin = parseTokens(oTokens, progName).inputRd;
          char *pcStdout = parseTokens(oTokens, progName).outputRd;
          void (*pfRet)(int);

          char **commandArray = convertArray(oTokens);

          fflush(NULL);
          pid_t iPid = fork();
          if (iPid == -1) {
            perror(progName);
            exit(EXIT_FAILURE);
        }

        if (iPid == 0) {
            // Child process
            int iFd;

            // Reset SIGINT to default in the child
            pfRet = signal(SIGINT, SIG_DFL);
            if (pfRet == SIG_ERR) {
                perror(progName);
                exit(EXIT_FAILURE);
            }

            // Handle input redirection
            if (pcStdin != NULL) {
                iFd = open(pcStdin, O_RDONLY);
                if (iFd == -1) {
                    fprintf(stderr, "%s: ", progName);
                    perror(pcStdin);
                    exit(EXIT_FAILURE);
                }
                dup2(iFd, STDIN_FILENO);
                close(iFd);
            }

            // Handle output redirection
            if (pcStdout != NULL) {
                iFd = creat(pcStdout, 0600);
                if (iFd == -1) {
                    fprintf(stderr, "%s: ", progName);
                    perror(pcStdout);
                    exit(EXIT_FAILURE);
                }
                dup2(iFd, STDOUT_FILENO);
                close(iFd);
            }

            // Execute the command
            execvp(commandArray[0], commandArray);
            // If execvp returns, an error occurred
            perror(commandArray[0]);
            exit(EXIT_FAILURE);
        }

        // Parent process
        if (wait(NULL) == -1) {
            perror(progName);
            exit(EXIT_FAILURE);
        }
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
      exit(EXIT_FAILURE);
  }
}

static char *findIshrc(void)

/* Find file location of .ishrc in HOME directory. */

{
   char *homeDir;
   char *Ishrc;

   homeDir = getenv("HOME");
   Ishrc = malloc(strlen(homeDir) + strlen("/.ishrc") + 1);

   strcpy(Ishrc, homeDir);
   strcat(Ishrc, "/.ishrc");
   return Ishrc;
}

int main(int argc, char *argv[]) {
  /* TODO */
  errorPrint(argv[0], SETUP);
  char acLine[MAX_LINE_SIZE + 2];
  char *temp;
  FILE *tempFile;
  void (*pfRet)(int);

  pfRet = signal(SIGINT, SIG_IGN);
  if(pfRet == SIG_ERR) {
    perror(argv[0]); 
    exit(EXIT_FAILURE); 
  }

  temp = findIshrc();
  tempFile = fopen(temp, "r");
  free(temp);

  while (tempFile != NULL && fgets(acLine, MAX_LINE_SIZE, tempFile) != NULL)
  {
    acLine[strcspn(acLine, "\n")] = '\0';
    printf("%% %s\n", acLine);
    fflush(stdout);
    
    shellHelper(acLine, argv[0]);
  }
  printf("%% ");
  fflush(stdout);

  while (1) {
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine, argv[0]);

    fprintf(stdout, "%% ");
    fflush(stdout); 
  }
}


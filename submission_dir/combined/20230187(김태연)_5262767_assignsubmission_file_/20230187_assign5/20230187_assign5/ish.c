#include <stdio.h>
#include <stdlib.h>

#include "lexsyn.h"
#include "util.h"
#include <sys/utsname.h>
#include <signal.h>
#include <string.h> 
#include <unistd.h>
#include <locale.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/wait.h>

/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/
int fds[2];    
int red;

volatile sig_atomic_t quit_flag = 0;



void sigint_handler(int sig) {
    signal(SIGINT, SIG_DFL); 
    fflush(stdout);
}

void sigquit_handler(int sig) {
    fflush(stdout);
    alarm(5);  
}

void sigalrm_handler(int sig) {
    fflush(stdout);
    exit(0);  
}

static void
shellHelper(const char *inLine) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;
  int length;

  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }

  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if ((length=DynArray_getLength(oTokens)) == 0)
        return;

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        // btype: NORMAL, B_EXIT, B_SETENV, B_USETENV, B_CD, B_ALIAS, B_FG
        int i;
        for (i=1; i<length-1; i++) {
          struct Token *t_i = (struct Token *)DynArray_get(oTokens, i);
          if (t_i->eType == TOKEN_PIPE) {
             /* PIPE */
              // printf("***in the pipe ***");  
              pipe(fds);                       
              // pid_t pid = 0;
              fflush(stdout);
              pid_t pid = fork();
              if (!pid) {
                /* child process */
                dup2(fds[0], STDIN_FILENO);
                close(fds[0]);    
                close(fds[1]); 
             
                char newLine[1024] = "";
                int j;
                int offset=0;
                for (j=i+1; j<length; j++) {
                  struct Token *t = (struct Token *)DynArray_get(oTokens, j);
                  char *token;
                  if (t->eType == TOKEN_PIPE) {
                    token = "|";
                  }
                  else if (t->eType == TOKEN_WORD) {
                    token = t->pcValue;
                  }
                  else {
                    fprintf(stderr, "inValid eType");
                  }
                  offset += snprintf(newLine + offset, sizeof(newLine) - offset, "%s ", token);
                  // printf("Token %d: %s\n", j, token);
                  // fflush(stdout);
                }       
                // printf(" in child i %d, length %d \n", i, length);
                // printf("line %s", &newLine[6]);
                // fflush(stdout);
                newLine[offset] = '\0';
                shellHelper(newLine); 
                fflush(stdout); 
                exit(0);
              }
              dup2(fds[1], STDOUT_FILENO);

              close(fds[0]);
              close(fds[1]);
              // printf("hello\n");
              // fflush(stdout);
              // write(STDOUT_FILENO, "this is write", 13);
              int j;
              char oldLine[1024] = "";
              for (j=0; j<i; j++) {
                char *token = ((struct Token *)DynArray_get(oTokens, j))->pcValue;
                strcat(oldLine,token);
                strcat(oldLine, " ");
                // printf("Token %d: %s\n", j, token->pcValue);
                // fflush(stdout);
              }
              // printf("line: %s\n ", newLine);
              // fflush(stdout);
              shellHelper(oldLine);
              //fflush(stdout);
              int status = -1;
              waitpid(pid, &status, WNOHANG);
              return;
              
              
          }
          else if(t_i ->eType == TOKEN_REDIN) {
            assert(i+1==length-1); // prev token should be first token.
            struct Token *t = (struct Token *)DynArray_get(oTokens, i+1);
            char *filename = t->pcValue;
            int fd = open(filename, O_RDONLY, 0600); 
            if (fd == -1) {
                fprintf(stderr, "open error");
                exit(EXIT_FAILURE);
            }

            if (dup2(STDIN_FILENO, red) == -1) {
                fprintf(stderr, "dup2 error");
                exit(EXIT_FAILURE);
            }
            if (dup2(fd, STDIN_FILENO) == -1) {
                fprintf(stderr, "dup2 error");
                exit(EXIT_FAILURE);
            }

            close(fd);
          }
          else if(t_i ->eType == TOKEN_REDOUT) {
            assert(i+1==length-1); // prev token should be first token.
            struct Token *t = (struct Token *)DynArray_get(oTokens, i+1);
            char *filename = t->pcValue;
      
            // char *filename = "./output.txt";
            //int fd;
            int fd = creat(filename, 0600); // I used *chmod 0600 output.txt

           if (fd == -1) {
              fprintf(stderr, "open error");
              exit(EXIT_FAILURE);
            }
            if (dup2(STDOUT_FILENO, red) == -1) {
                fprintf(stderr, "dup2 error");
                exit(EXIT_FAILURE);
            }
  
            if (dup2(fd, STDOUT_FILENO) == -1) {
                fprintf(stderr, "dup2 error");
                exit(EXIT_FAILURE);
            }
            close(fd);
          }
        }
        if (btype == NORMAL){
            struct Token *t = (struct Token *)DynArray_get(oTokens, 0);
            char *cmd = t->pcValue;

            if (strncmp(cmd, "pwd", 3) == 0) {
              char buf[1000];
              if (getcwd(buf, sizeof(buf))) {
                  printf("%s\n", buf);
              } else {
                  fprintf(stderr, "getcwd failed");
              }
            }
            else if (strncmp(cmd, "cat", 3) == 0) {
              struct Token *t = (struct Token *)DynArray_get(oTokens, 1);
              if (t->eType != TOKEN_WORD) {
                t = (struct Token *)DynArray_get(oTokens, 2);
              }
              char *filename = t->pcValue;
              FILE *file = fopen(filename, "r");
              if (file == NULL) {
                  perror(filename);
                  return;
              }

              char buffer[1024];
              size_t bytes_read;

              while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                  fwrite(buffer, 1, bytes_read, stdout);
              }

              if (ferror(file)) {
                  perror("Error reading file");
              }

              fclose(file);
            }
            else if (strncmp(cmd, "rm", 2) == 0) {
              int i;
              for (i=1; i<length; i++) {
                struct Token *t = (struct Token *)DynArray_get(oTokens, i);
                char *filename = t->pcValue;
                if (filename[0]=='-') continue;
                if (remove(filename) != 0) {
                  fprintf(stderr, "Error deleting the file");
                }
                return;
              }
            }
            else if (strncmp(cmd, "echo", 4) == 0) {
              int i;
              // printf("length: %d", length);
              
             char newLine[1024] = "";  // Buffer to store the accumulated string
              int offset = 0;  // Offset to track the current position in the buffer

              // Loop through the tokens and append them to the newLine buffer
              for (i = 1; i < length - 1; i++) {
                  struct Token *t_i = (struct Token *)DynArray_get(oTokens, i);
                  if (t_i->eType == TOKEN_WORD) {
                      char *buf = t_i->pcValue;
                      offset += snprintf(newLine + offset, sizeof(newLine) - offset, "%s ", buf);
                      // printf("this is token %s", buf);

                  } else {
                      newLine[offset-1] = '\0'; // remove last space
                      fflush(stdout);
                      printf("%s\n", newLine);
                      fflush(stdout);
                      dup2(red, STDOUT_FILENO);
                      close(red);
                      return;
                      // fprintf(stderr, "Not TOKEN_WORD\n");
                  }
              }

              // Add the last token with a newline
              
              char *lastBuf = ((struct Token *)DynArray_get(oTokens, length - 1))->pcValue;
              offset += snprintf(newLine + offset, sizeof(newLine) - offset, "%s", lastBuf);
              newLine[offset] = '\0';
              //setlocale(LC_ALL, "en_US.UTF-8"); 
              // Print the whole line at once
              printf("%s\n", newLine);
              fflush(stdout);
            }
            else if (strncmp(cmd, "sed", 3) == 0) {
              char *rule = ((struct Token *)DynArray_get(oTokens, 1))->pcValue; // s/Hello/Hi/g
              char buf[1024];
              size_t bytesRead = read(STDIN_FILENO, buf, sizeof(buf));
              char **argv = malloc(4 * sizeof(char*)); 
              //printf("read: %s", buf);
    
              char *delimiter = "/";
              char *token = strtok(rule, delimiter);
              int i = 0;
              
              while (token != NULL && i < 4) {
                  argv[i] = token;
                  token = strtok(NULL, delimiter); 
                  i++;
              }

              char *pattern = argv[1];
              char *replacement = argv[2];
              char *pos;
              size_t pattern_len;
              size_t replacement_len;
              while ((pos = strstr(buf, pattern))!=NULL){
                pattern_len = strlen(pattern);
                replacement_len = strlen(replacement);
                memmove(pos + replacement_len, pos + pattern_len, bytesRead - (pos - buf) - pattern_len);
                memcpy(pos, replacement, replacement_len);
              }
              buf[bytesRead - (pattern_len - replacement_len)] = '\0';
              printf("%s", buf); // buf arleady includes \n
            }
            else if (strncmp(cmd, "printenv", 8) == 0) {
              char *name = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
              char *value = getenv(name);
              if (value) {
                printf("%s\n", getenv(name));
              }
            }
            else if (strncmp(cmd, "uname", 5) == 0) {
              struct utsname systemInfo;
              if (uname(&systemInfo) == -1) {
                  fprintf(stderr, "uname error");
                  return;
              }
              printf("%s\n", systemInfo.sysname);
            }
            else {
              fprintf(stderr, "%s: No such file or directory\n", cmd);
            }
        }
        else if (btype == B_EXIT) {
          exit(0);
        }
        else if (btype == B_SETENV) {
          char *name = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
          char *value = ((struct Token *)DynArray_get(oTokens, 2))->pcValue;
          if(setenv(name, value, 0)!=0) fprintf(stderr, "Cannot make the environment value\n");
        }
        else if (btype == B_USETENV) {
          char *name = ((struct Token *)DynArray_get(oTokens, 1))->pcValue;
          if(unsetenv(name)!=0) fprintf(stderr, "Cannot remove the environment value\n");
        }
        else if (btype == B_CD) {
          char* path;
          if(length>1) path=((struct Token *)DynArray_get(oTokens, 1))->pcValue;
          else if((path=(char*)getenv("HOME"))==NULL) path=".";

          if(chdir(path)!=0) fprintf(stderr, "Cannot change directory\n");
        }
        else if (btype == B_ALIAS) {
          
        }
        else if (btype == B_FG) {
          
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

int main() {
  /* TODO */

    signal(SIGQUIT, sigquit_handler);
    signal(SIGALRM, sigalrm_handler);
    signal(SIGINT, sigint_handler);


  char acLine[MAX_LINE_SIZE + 2];
  char *home = getenv("HOME");
  if (home) {
    char ishrc_path[256];
    snprintf(ishrc_path, sizeof(ishrc_path), "%s/.ishrc", home);

    FILE *file = fopen(ishrc_path, "r");
    if (file) {
        // Read from .ishrc
          while (1) {
            if (fgets(acLine, MAX_LINE_SIZE, file) == NULL) {
              // printf("\n");
              fflush(stdout);
              break;
            }
            fprintf(stdout, "%% ");
            fflush(stdout);
            printf("%s", acLine);
            shellHelper(acLine);
          }
        fclose(file);
    }
    
  }
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);

    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      printf("\n");
      fflush(stdout);
      exit(EXIT_SUCCESS);
    }

    shellHelper(acLine);
  }
  
 
}
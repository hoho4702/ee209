#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "lexsyn.h"
#include "util.h"
#include "dynarray.h"
#include "token.h" 

#define MAX_LINE_SIZE 1023

void do_pipe(DynArray_T oTokens) 
{
    int num_pipe = 0;
    int i = 0, j = 0;


    for (i = 0; i < DynArray_getLength(oTokens); i++) 
    {
      struct Token *token = DynArray_get(oTokens, i);
      if (token->eType == TOKEN_PIPE) {num_pipe++;}
    } 
    

    int **pipes = malloc(num_pipe * sizeof(int *));
    if (pipes == NULL) 
    {
      perror("malloc");
      exit(EXIT_FAILURE);
    }
    
    for (i = 0; i < num_pipe; i++) 
    {
      pipes[i] = malloc(2 * sizeof(int));  
      if (pipes[i] == NULL || pipe(pipes[i]) < 0) 
      {
        perror("pipe");
        exit(EXIT_FAILURE);
      }
    }

    int curr_command = 0;  
    int curr_pipe = 0;     

    for (i = 0; i <= DynArray_getLength(oTokens); i++) 
    {
      struct Token *token;
      if (i < DynArray_getLength(oTokens)) {token = DynArray_get(oTokens, i);} 
      else {token = NULL;}

      if (token == NULL || token->eType == TOKEN_PIPE)//null을 만나거나 pipe를 만나면 새로운 것을 처리
      {
        pid_t pid = fork();
        if (pid < 0) 
        {
          perror("fork");
          exit(EXIT_FAILURE);
        } 
        
        else if (pid == 0) 
        {
          if (curr_pipe > 0) {dup2(pipes[curr_pipe - 1][0], STDIN_FILENO);} //이전 출력을 현재의 인풋으로(첫번째 파이프는 필요없음)
          if (curr_pipe < num_pipe) {dup2(pipes[curr_pipe][1], STDOUT_FILENO);} //현재 출력(마지막 파이프는 필요가 없음)

          for (j = 0; j < num_pipe; j++) 
          {
            close(pipes[j][0]);
            close(pipes[j][1]);
          }

          int len_command = i - curr_command;//명령 + 인자 개수 count
          char **argv = malloc((len_command + 1) * sizeof(char *));
          for (j = 0; j < len_command; j++) 
          {
            struct Token *cmdToken = DynArray_get(oTokens, curr_command + j);
            argv[j] = cmdToken->pcValue;
          }

          argv[len_command] = NULL;
          execvp(argv[0], argv);
          perror("execvp");
          exit(EXIT_FAILURE);
        }

        else
        { 
        curr_command = i + 1;
        if (curr_pipe > 0) close(pipes[curr_pipe - 1][0]);
        if (curr_pipe < num_pipe) close(pipes[curr_pipe][1]);
        curr_pipe++;
        } 
      }
    }

    for (i = 0; i <= num_pipe; i++) {wait(NULL);}
    for (i = 0; i < num_pipe; i++) {free(pipes[i]);}
    free(pipes);
}

int check_redir(DynArray_T oTokens) 
{
  int i = 0;
  for (i = 0; i < DynArray_getLength(oTokens); i++) 
  {
    struct Token *token = DynArray_get(oTokens, i);
    if (token->eType == TOKEN_REDIN || token->eType == TOKEN_REDOUT) {return 1;}
  }
  return 0; 
}

static void shellHelper(const char *inLine) 
{
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  oTokens = DynArray_new(0);
  if (oTokens == NULL) 
  {
    errorPrint("Cannot allocate memory", FPRINTF);
    exit(EXIT_FAILURE);
  }

  lexcheck = lexLine(inLine, oTokens);  
  switch (lexcheck) 
  {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0) {return;}

      /* dump lex result when DEBUG is set */
      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) 
      {
        int hasPipe = 0;
        int i = 0;
        for (i = 0; i < DynArray_getLength(oTokens); i++) 
        {
          struct Token *token = DynArray_get(oTokens, i);
          if (token->eType == TOKEN_PIPE) 
          {
            hasPipe = 1;
            break;
          }
        }
        
        if (hasPipe) {do_pipe(oTokens);} 
        
        else 
        {
          btype = checkBuiltin(DynArray_get(oTokens, 0));
          /* TODO */
          switch (btype)
          {
            case B_CD: 
            {
              if (check_redir(oTokens)) 
              {
                fprintf(stderr, "./ish: cd takes one parameter\n");
                break;
              }
              
              int nTokens = DynArray_getLength(oTokens);
              if (nTokens < 2) 
              {
                const char *home = getenv("HOME"); 
                if (!home) {home = "/";}
                if (chdir(home) != 0) {fprintf(stderr, "./ish: %s\n", strerror(errno));}
              }
              else
              {
                struct Token* dirToken = DynArray_get(oTokens, 1);
                if (chdir(dirToken->pcValue) != 0) {fprintf(stderr, "./ish: %s\n", strerror(errno));}
              }
              break;
            }

            case B_SETENV:
            {
              /*if (check_redir(oTokens)) 
              {
                fprintf();
                break;
              }*/              
              
              int token_count = DynArray_getLength(oTokens);
              if (token_count < 2) 
              {
                fprintf(stderr, "./ish: setenv takes one or two parameters\n");
              }
              else if (token_count > 3) 
              {
                fprintf(stderr, "./ish: setenv takes one or two parameters\n");
              }
              else 
              {
                struct Token* key_token = DynArray_get(oTokens, 1);
                const char* setenv_key = key_token->pcValue;
                const char* setenv_val = "";

                if (token_count == 3) 
                {
                    struct Token* setenv_val_token = DynArray_get(oTokens, 2);
                    setenv_val = setenv_val_token->pcValue;
                }

                if (setenv(setenv_key, setenv_val, 1) != 0) 
                {
                    fprintf(stderr, "./ish: %s\n", strerror(errno));
                }
              }
              break;
            }

            case B_USETENV:
            {
              /*if (check_redir(oTokens)) 
              {
                fprintf();
                break;
              }*/

              int token_count = DynArray_getLength(oTokens);
              if (token_count != 2) 
              {
                  fprintf(stderr, "./ish: unsetenv takes one parameter\n");
              }
              else 
              {
                  struct Token* varToken = DynArray_get(oTokens, 1);
                  const char* var_name = varToken->pcValue;

                  if (unsetenv(var_name) != 0) 
                  {
                      fprintf(stderr, "./ish: %s\n", strerror(errno));
                  }
              }
              break;
            }

            case B_EXIT:
            {
              if (DynArray_getLength(oTokens) > 1) 
              {
                  fprintf(stderr, "./ish: exit does not take any parameters\n");
                  break;
              }
              exit(EXIT_SUCCESS);
            }

            case B_ALIAS:
            case B_FG:
            case NORMAL:
            
            default: 
            {              
              pid_t pid = fork();
              if (pid < 0) 
              {
                fprintf(stderr, "ish: %s\n", strerror(errno));
                break;
              }

              else if (pid == 0) 
              {
                signal(SIGINT, SIG_DFL);
                int i = 0;
                int input_file = -1;
                int output_file = -1; 

                for (i = 0; i < DynArray_getLength(oTokens); i++) 
                {
                  struct Token *token = DynArray_get(oTokens, i);
                  if (token->eType == TOKEN_REDIN) 
                  {
                    struct Token *file_name = DynArray_get(oTokens, ++i);
                    input_file = open(file_name->pcValue, O_RDONLY);
                    if (input_file < 0) 
                    {
                      fprintf(stderr, "./ish: %s\n",strerror(errno));
                      exit(EXIT_FAILURE);
                    }
                    dup2(input_file, STDIN_FILENO);
                    close(input_file);
                  }

                  else if (token->eType == TOKEN_REDOUT) 
                  {
                    struct Token *file_name = DynArray_get(oTokens, ++i);
                    output_file = open(file_name->pcValue, O_WRONLY | O_CREAT | O_TRUNC, 0600);
                    if (output_file < 0) 
                    {
                      fprintf(stderr, "./ish: %s\n", strerror(errno));
                      exit(EXIT_FAILURE);
                    }
                    dup2(output_file, STDOUT_FILENO);
                    close(output_file);
                  }
                }

                int start_point = 0;
                int token_count = DynArray_getLength(oTokens) - start_point;
                char **argv = malloc(sizeof(char*) * (token_count + 1));

                if (!argv) {exit(EXIT_FAILURE);}
                for (i = 0; i < token_count; i++) 
                {
                  struct Token* tk = DynArray_get(oTokens, start_point + i);
                  argv[i] = (char*)tk->pcValue;
                }

                argv[token_count] = NULL;
                execvp(argv[0], argv);
                fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
                free(argv);
                exit(EXIT_FAILURE);

              }
    
              else 
              {
                int error_check;
                waitpid(pid, &error_check, 0);
              }              
              break;
            }
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

static void ishrc_file() 
{
    char *home = getenv("HOME");
    char ishrc_filepath[1024];
    const char *ishrc_suffix = "/.ishrc";

    strncpy(ishrc_filepath, home, sizeof(ishrc_filepath) - 1);
    ishrc_filepath[sizeof(ishrc_filepath) - 1] = '\0';  
    strncat(ishrc_filepath, ishrc_suffix, sizeof(ishrc_filepath) - strlen(ishrc_filepath) - 1);

    FILE *file_pointer = fopen(ishrc_filepath, "r");
    if (file_pointer == NULL) {return;}

    char line[MAX_LINE_SIZE + 1];
    while (fgets(line, sizeof(line), file_pointer) != NULL) 
    {
        line[strcspn(line, "\n")] = '\0';
        printf("%% %s\n", line); 
        fflush(stdout); 
        shellHelper(line);
    }

    fclose(file_pointer);
}

int timer_count = 0;

void ctrl_slash(int sig) 
{
  if (timer_count == 0) 
  {
    timer_count = 1;
    printf("\nType Ctrl-\\ again within 5 seconds to exit.\n");
    fflush(stdout);
    alarm(5); 
  } 
  else 
  {
    exit(EXIT_SUCCESS);
  }
}

void reset_timer(int sig) 
{
  timer_count = 0; 
}

int main() 
{
  /* TODO */
  signal(SIGINT, SIG_IGN);
  signal(SIGQUIT, ctrl_slash); 
  signal(SIGALRM, reset_timer);

  errorPrint("./ish", SETUP);
  ishrc_file();

  /*-----*/

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


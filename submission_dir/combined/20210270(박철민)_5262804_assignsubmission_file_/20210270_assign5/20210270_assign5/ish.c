
#include "lexsyn.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "lexsyn.h"
#include "util.h"
#include <sys/types.h>  
#include <unistd.h>    
#include <sys/wait.h>   
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <errno.h> 

#define ERROR_HANDLER(msg) \
    do { \
        perror(msg); \
        exit(EXIT_FAILURE); \
    } while (0)



/*--------------------------------------------------------------------*/
/* ish.c                                                              */
/* Original Author: Bob Dondero                                       */
/* Modified by : Park Ilwoo                                           */
/* Illustrate lexical analysis using a deterministic finite state     */
/* automaton (DFA)                                                    */
/*--------------------------------------------------------------------*/
int missing_command_check(char *input);
static void cut_pipeline(char *input, DynArray_T pipeline_command); 
int exe_builtin( DynArray_T oTokens , enum BuiltinType btype);
static void exe_command(DynArray_T oTokens);
static void pipeline(char *commands[], int num_command);
static void start_ishrc();
static void setupSignalHandlers();
static void handleSIGQUIT(int signum);
static void cleanup_pipeline(int *fd_pipeli, int pipe_count, char **commands, int dup_command_count);

static void executeCD(DynArray_T oTokens);
static char *progran_title = NULL;
static void freeWrapper(void *pvElement, void *pvExtra) {
    (void)pvExtra; // 사용하지 않는 인자 무시
    free(pvElement);
}

static void
shellHelper(const char *inLine) {
  DynArray_T oTokens;

  enum LexResult lexcheck;
  enum SyntaxResult syncheck;
  enum BuiltinType btype;

  char* dup_line; 
  

  if ((dup_line = strdup(inLine)) == NULL) exit(EXIT_FAILURE); // line 
  if(*(dup_line)=='\0' ) {free(dup_line); return;}   // 예외처리

  char* end;
  for (end = dup_line; *(end)!= '\0'; end++); // end가 문자열 끝'\0'을 가르키게함
  for (end--; end > dup_line && isspace((unsigned char)*end); end--) {  
        *end = '\0';
    }

  if(*(dup_line)=='\0' ) {free(dup_line); return;}   // 예외처리


  int num_pipeline = 0;
  DynArray_T pipeline_command = DynArray_new(0);
  if (pipeline_command == NULL) exit(EXIT_FAILURE);
    if (missing_command_check(dup_line) != 1) {
    free(dup_line); DynArray_free(pipeline_command);
    return;} 

  cut_pipeline(dup_line, pipeline_command);
  char** commands; 
  if (DynArray_getLength(pipeline_command) != 1) {
    ;
    if ((commands =(char**)malloc(sizeof(char *)* DynArray_getLength(pipeline_command))) == NULL) {
        DynArray_map(pipeline_command,freeWrapper, NULL);
        DynArray_free(pipeline_command);
        free(dup_line);
        return;}
  int i;
  for (i = 0; i < DynArray_getLength(pipeline_command); i++) commands[i] = (char *)DynArray_get(pipeline_command, i);

  pipeline(commands, DynArray_getLength(pipeline_command));
  DynArray_map(pipeline_command, freeWrapper, NULL); DynArray_free(pipeline_command); free(dup_line); free(commands); 
  return; }


  oTokens = DynArray_new(0);
  if (oTokens == NULL) {
    errorPrint("Cannot allocate memory", FPRINTF);
    DynArray_map(pipeline_command, freeWrapper, NULL);  //memory leak 방지
    DynArray_free(pipeline_command);
    free(dup_line);
    exit(EXIT_FAILURE);
  }


  lexcheck = lexLine(inLine, oTokens);
  switch (lexcheck) {
    case LEX_SUCCESS:
      if (DynArray_getLength(oTokens) == 0){
        DynArray_map(pipeline_command, freeWrapper, NULL);  //memory leak 방지
        DynArray_free(pipeline_command);
        free(dup_line);
        DynArray_free(oTokens);
        return;}

      dumpLex(oTokens);

      syncheck = syntaxCheck(oTokens);
      if (syncheck == SYN_SUCCESS) {
        btype = checkBuiltin(DynArray_get(oTokens, 0));
        /* TODO */
        if (btype == NORMAL) exe_command(oTokens);
        else if (exe_builtin(oTokens, btype) ==0) {
        DynArray_map(pipeline_command, freeWrapper, NULL);  //memory leak 방지
        DynArray_free(pipeline_command);
        free(dup_line);
        DynArray_free(oTokens);
        //printf("\n");
        exit(EXIT_SUCCESS);}
 
      }

      /* syntax error cases */
      else if (syncheck == SYN_FAIL_NOCMD)
        //errorPrint("./ish: Missing command name", FPRINTF);
        fprintf(stderr, "%s: Missing command name\n", progran_title);
      else if (syncheck == SYN_FAIL_MULTREDOUT)
        //errorPrint("Multiple redirection of standard out", FPRINTF);
        fprintf(stderr, "%s: Multiple redirection of standard out\n", progran_title);
      else if (syncheck == SYN_FAIL_NODESTOUT)
        //errorPrint("Standard output redirection without file name", FPRINTF);
        fprintf(stderr, "%s: Standard output redirection without file name\n", progran_title);
      else if (syncheck == SYN_FAIL_MULTREDIN)
        //errorPrint("Multiple redirection of standard input", FPRINTF);
        fprintf(stderr, "%s: Multiple redirection of standard input\n", progran_title);
      else if (syncheck == SYN_FAIL_NODESTIN)
        //errorPrint("Standard input redirection without file name", FPRINTF);
        fprintf(stderr, "%s: Standard input redirection without file name\n", progran_title);
        
      else if (syncheck == SYN_FAIL_INVALIDBG)
        //errorPrint("Invalid use of background", FPRINTF);
        fprintf(stderr, "%s: Invalid use of background\n", progran_title);
      break;

    case LEX_QERROR:
      //errorPrint("Unmatched quote", FPRINTF);
      fprintf(stderr, "%s: Unmatched quote\n", progran_title);
      break;

    case LEX_NOMEM:
      //errorPrint("Cannot allocate memory", FPRINTF);
      fprintf(stderr, "%s: Cannot allocate memory\n", progran_title);
      break;

    case LEX_LONG:
      //errorPrint("Command is too large", FPRINTF);
      fprintf(stderr, "%s: Command is too large\n", progran_title);
      break;

    default:
      //errorPrint("lexLine needs to be fixed", FPRINTF);
      fprintf(stderr, "%s: lexLine needs to be fixed\n", progran_title);
      exit(EXIT_FAILURE);
  }
  DynArray_map(pipeline_command, freeWrapper, NULL);  //memory leak 방지
  DynArray_free(pipeline_command);
  free(dup_line);
  DynArray_free(oTokens);
}

int missing_command_check(char *dup_line) {
    char *start = dup_line;
    int return_value = 1;
    
    // 시작 부분에 `|`, `<`, `>`가 있는지 
    while (isspace((unsigned char)*start)) start++; 
    if (*start == '|' || *start == '<' || *start == '>') {
        return_value = 0;
    }

    char *prev = start;
    while (*start != '\0') {
        // `|`, `<`, `>` 전후에 명령어가 없는지 
        if ((*start == '|' || *start == '<' || *start == '>' || *start == '&') && isspace((unsigned char)*(start + 1))) {
            // 뒤에 명령어가 없는지 
            char *next = start + 1;
            while (isspace((unsigned char)*next)) next++;
            if (*next == '\0' || *next == '|' || *next == '<' || *next == '>') {
                return_value = 0;
            }
        }

        // 명령어 없이 `|`, `<`, `>`가 연속적
        if ((*prev == '|' || *prev == '<' || *prev == '>' || *prev == '&') && 
            (*start == '|' || *start == '<' || *start == '>' || *start == '&')) {
            if (*prev == '&' || *start == '&') {
                fprintf(stderr, "%s: Invalid use of background\n", progran_title);
            } else {
                fprintf(stderr, "%s: Missing command name\n", progran_title);
            }
            return 0;
        }

        prev = start;
        start++;
    }

    if (return_value == 0) {
        fprintf(stderr, "%s: Missing command name\n", progran_title);
    }
    return return_value;
}

//수정 해야함//

static void cut_pipeline(char *input, DynArray_T pipeline_command) {
    char *start = input;
    char *end;

    while (*start != '\0') {
        while (isspace(*start)) {
            start++;
        }

        int inside_quotes = 0;
        int has_pipe = 0;
        int has_redirection_out = 0;
        end = start;
        
        while (*end != '\0') {
            if (*end == '"') {
                inside_quotes = !inside_quotes;
            }
            if (!inside_quotes) {
                if (*end == '|') {
                    has_pipe = 1;
                    break;
                }
                if (*end == '>') {
                    has_redirection_out = 1;
                }
            }
            end++;
        }

        if (has_pipe && has_redirection_out) {
            fprintf(stderr, "%s: Multiple redirection of standard out\n", progran_title);
            return;  // 메모리 해제 없이 바로 리턴
        }

        char *trimmedEnd = end - 1;
        while (trimmedEnd >= start && isspace(*trimmedEnd)) {
            *trimmedEnd-- = '\0';
        }

        if (*start != '\0') {
            char *token = strdup(start);
            if (!token) {
                perror("cut_pipeline: Memory allocation failed");
                return;
            }
            DynArray_add(pipeline_command, token);
        }

        if (*end == '\0') break;
        start = end + 1;
    }
}

//shell helper 수정해야함
int exe_builtin(DynArray_T oTokens , enum BuiltinType btype){
  switch(btype) { 
    case B_CD:
      executeCD(oTokens);
      return 1;
    
    case B_SETENV:{
    const char *value;
      if (DynArray_getLength(oTokens) < 2) {
        fprintf(stderr, "./ish: setenv: Missing variable name\n"); 
        return 1;}

      else if (DynArray_getLength(oTokens) == 2) {value = "";}
      else {
        struct Token* t_value =DynArray_get(oTokens, 2);
         value =  t_value->pcValue;;}
      struct Token* t_var = DynArray_get(oTokens, 1);
      const char *var = t_var->pcValue;
      if (setenv(var, value, 1) != 0) { perror("./ish: setenv");}
      else {

        const char *check = getenv(var);
        //printf("Verification - %s = %s\n", var, check ? check : "not set"); 
      }
        return 1;}
        
    
    case B_USETENV:
      if (DynArray_getLength(oTokens) < 2) fprintf(stderr, "%s: unsetenv: Missing variable name\n", progran_title);
      else if (DynArray_getLength(oTokens) > 2) fprintf(stderr, "%s: unsetenv takes one parameter\n", progran_title);
      else { 
        struct Token* t_var = DynArray_get(oTokens, 1);
        const char *var = t_var->pcValue;
        if (unsetenv(var) != 0) perror("./ish: unsetenv");}  
        return 1;

      
    case B_EXIT:
        return 0;

    default:
      errorPrint("Wrong built-in command\n", FPRINTF);
    }
    return 0;
    }


static void exe_command(DynArray_T oTokens) {
  if (DynArray_getLength(oTokens) == 0) {
    fprintf(stderr, "%s: Missing command name\n", progran_title);
    return;}

  struct Token *dup_command_token = DynArray_get(oTokens, 0);
  char **env;
 if (strcmp(dup_command_token->pcValue, "printenv") == 0) {
    if (DynArray_getLength(oTokens) == 1) {
      
      extern char **environ;
      for (env = environ; *env != NULL; env++) printf("%s\n", *env);} 
      else {
      struct Token *var_token = DynArray_get(oTokens, 1);
      const char *value = getenv(var_token->pcValue);
      if (value != NULL) printf("%s\n", value); }
    return;
  }


  fflush(NULL);
  pid_t pid = fork();
  if(pid<0) {perror("Failed to fork"); return ;}
  
  else if (pid ==0){
    int fd_in = -1, fd_out = -1 , argc = 0;
    char *argv[DynArray_getLength(oTokens) + 1];
    int failure = 1;
    int i;
    for (i = 0; i < DynArray_getLength(oTokens); i++) {
      struct Token *t = DynArray_get(oTokens, i);

      if (t->eType == TOKEN_REDOUT) { 
        if (i == DynArray_getLength(oTokens) -1 ) break; 
        struct Token* t_next = DynArray_get(oTokens, ++i);
        if (t_next->eType != TOKEN_WORD) break; 
        if ((fd_out = open(t_next->pcValue, (O_WRONLY | O_CREAT | O_TRUNC), 0644)) < 0) {
        fprintf(stderr, "%s: %s\n", progran_title, strerror(errno));
        exit(EXIT_FAILURE);}
        failure = 0 ;}

      else if (t->eType == TOKEN_REDIN) { 
        if (i == DynArray_getLength(oTokens) -1 ) break; 
        struct Token* t_next = DynArray_get(oTokens, ++i);
        if (t_next->eType != TOKEN_WORD) break; 
        if ((fd_in = open(t_next->pcValue, O_RDONLY)) < 0) {
        fprintf(stderr, "%s: %s\n", progran_title, strerror(errno));
        exit(EXIT_FAILURE);}
        failure = 0 ;}
      //else if (strcmp(t->pcValue, "|") == 0) break;

      else  {argv[argc++] = t->pcValue; failure = 0 ;} }

      argv[argc] = NULL;
      if (failure) {
        if (fd_in != -1) close(fd_in);
        if (fd_out != -1) close(fd_out);
        exit(EXIT_FAILURE);}

      if (fd_in != -1 && dup2(fd_in, STDIN_FILENO) == -1) {
          if (fd_in != -1) close(fd_in);
          if (fd_out != -1) close(fd_out);

          exit(EXIT_FAILURE);}
      if (fd_out != -1 && dup2(fd_out, STDOUT_FILENO) == -1) {
          if (fd_in != -1) close(fd_in);
          if (fd_out != -1) close(fd_out);
          exit(EXIT_FAILURE);}

      execvp(argv[0], argv);
      perror(argv[0]);
      if (fd_in != -1) close(fd_in);
      if (fd_out != -1) close(fd_out);
      exit(EXIT_FAILURE);}
    
      else { 
        if (wait(NULL) == -1) perror("Failed to wait for child process");
    }
}


void handleSIGQUIT(int signum) {
    static time_t lastSIGQUIT = 0;
    time_t current_time = time(NULL);

    if (current_time - lastSIGQUIT <= 5) exit(EXIT_SUCCESS);

    fprintf(stdout, "\nType Ctrl-\\ again within 5 seconds to exit.\n");
    alarm(5);  
    lastSIGQUIT = current_time;
}



void setupSignalHandlers() {

    if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
        ERROR_HANDLER("Failed to set SIGINT handler");
    }


    if (signal(SIGQUIT, handleSIGQUIT) == SIG_ERR) {
        ERROR_HANDLER("Failed to set SIGQUIT handler");
    }


    if (signal(SIGALRM, SIG_IGN) == SIG_ERR) {
        ERROR_HANDLER("Failed to set SIGALRM handler");
    }
}



/*--------------------------------------------------------------------*/
static void start_ishrc(void) {
    char path[1024];
    char line[MAX_LINE_SIZE];
    char *home = getenv("HOME");
    FILE *file;
    
    if (!home) return;
    snprintf(path, sizeof(path), "%s/.ishrc", home);
    
    if (!(file = fopen(path, "r"))) return;

    while (fgets(line, sizeof(line), file)) {
        char *command = line;
        while (isspace(*command)) command++;
        if (*command == '\0') continue;
        
        if (command[strlen(command) - 1] == '\n')
            command[strlen(command) - 1] = '\0';
            
        printf("%% %s\n", command);
        shellHelper(command);
    }
    
    fclose(file);
}

void pipeline(char *commands[], int num_command) {
  int i;
    int fd_pipeline[2 * (num_command - 1)];
    pid_t *pids = malloc(num_command * sizeof(pid_t));
    if (!pids) exit(1);

    // Create pipes
    for (i = 0; i < num_command - 1; i++) {
        if (pipe(fd_pipeline + i * 2) < 0) {
            perror("pipe");
            exit(1);
        }
    }

    for (i = 0; i < num_command; i++) {
        pids[i] = fork();
        if (pids[i] < 0) {
            perror("fork");
            exit(1);
        }
        
        if (pids[i] == 0) {
            // 파이프 설정
            if (i > 0) {
                dup2(fd_pipeline[(i - 1) * 2], STDIN_FILENO);
            }
            if (i < num_command - 1) {
                dup2(fd_pipeline[i * 2 + 1], STDOUT_FILENO);
            }
            
            // Close all pipe fds
            int j;
            for (j = 0; j < 2 * (num_command - 1); j++) {
                close(fd_pipeline[j]);
            }

            // Parse command and handle redirections
            char *command = strdup(commands[i]);
            char *args[MAX_LINE_SIZE];
            int arg_count = 0;
            char *token = strtok(command, " ");
            
            int fd_in = -1, fd_out = -1;
            while (token) {
                if (strcmp(token, ">") == 0) {
                    token = strtok(NULL, " ");
                    if (token) {
                        fd_out = open(token, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (fd_out < 0) {
                            perror("open");
                            exit(1);
                        }
                        dup2(fd_out, STDOUT_FILENO);
                        close(fd_out);
                    }
                } else if (strcmp(token, "<") == 0) {
                    token = strtok(NULL, " ");
                    if (token) {
                        fd_in = open(token, O_RDONLY);
                        if (fd_in < 0) {
                            perror("open");
                            exit(1);
                        }
                        dup2(fd_in, STDIN_FILENO);
                        close(fd_in);
                    }
                } else {
                    args[arg_count++] = token;
                }
                token = strtok(NULL, " ");
            }
            args[arg_count] = NULL;

            execvp(args[0], args);
            perror(args[0]);
            free(command);
            exit(1);
        }
    }

    // Parent process
    for (i = 0; i < 2 * (num_command - 1); i++) {
        close(fd_pipeline[i]);
    }

    for (i = 0; i < num_command; i++) {
        waitpid(pids[i], NULL, 0);
    }

    free(pids);
}


static void cleanup_pipeline(int *fd_pipeline, int pipe_count, char **commands, int dup_command_count) {
    int i;
    for (i = 0; i < pipe_count * 2; i++) {
        close(fd_pipeline[i]);
    }
    for (i = 0; i < dup_command_count; i++) {
        free(commands[i]);
    }
}

static void executeCD(DynArray_T oTokens) {
    const char *dir;

    if (DynArray_getLength(oTokens) == 1) {
        dir = getenv("HOME"); 
        if (dir == NULL) {
            fprintf(stderr, "%s: cd: HOME environment variable not set\n", progran_title);
            return;
        }
    }
    else if (DynArray_getLength(oTokens) == 2) {
        struct Token* t = DynArray_get(oTokens, 1);
        dir = t->pcValue;
    }
    else {
        fprintf(stderr, "%s: cd: too many arguments\n", progran_title);
        return;
    }

    if (chdir(dir) != 0) {
        // perror("ish: cd") 
        fprintf(stderr, "%s: %s\n", progran_title, strerror(errno));
    }
}


int main(int argc, char *argv[]) {
  /* TODO */

    if (argc > 0) {
      progran_title = strdup(argv[0]); // argv[0]은 실행 중인 프로그램의 이름
      if (!progran_title) {
        fprintf(stderr, "Failed to allocate memory for program name\n");
          exit(EXIT_FAILURE);
        }
    } else {
        progran_title = "ish"; // 안전 장치로 기본값 설정
    }
    sigset_t sSet;
    sigemptyset(&sSet);
    sigaddset(&sSet, SIGINT);
    sigaddset(&sSet, SIGQUIT);
    sigaddset(&sSet, SIGALRM);
    if (sigprocmask(SIG_UNBLOCK, &sSet, NULL) == -1) {
        ERROR_HANDLER("Failed to unblock signals");
    }

  setupSignalHandlers();
  start_ishrc();
  char acLine[MAX_LINE_SIZE + 2];
  while (1) {
    fprintf(stdout, "%% ");
    fflush(stdout);
    if (fgets(acLine, MAX_LINE_SIZE, stdin) == NULL) {
      fprintf(stdout, "\n");
      exit(EXIT_SUCCESS);
    }
    shellHelper(acLine);
  }

  if (progran_title != NULL && progran_title != "unknown") free(progran_title);

  return EXIT_SUCCESS;  
}


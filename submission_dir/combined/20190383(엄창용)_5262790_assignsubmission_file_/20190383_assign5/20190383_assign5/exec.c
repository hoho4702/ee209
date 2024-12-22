/*--------------------------------------------------------------------*/
/* Name: Changyong Eom                                                */
/* Student ID: 20190383                                               */
/* File description:                                                  */
/* Command execution Helper function implementation                   */
/* for redirection, piplining command and so on...                    */
/*--------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <assert.h>
#include <string.h>
#include "signal.h"
#include "exec.h"
#include "dynarray.h"

void procRedir(DynArray_T oTokens){
    int fd; //file descriptor
    char *fname; //file name to be read
    struct Token *t;
    struct Token *f_token;

    for(int i = 0; i < DynArray_getLength(oTokens); ++i){
        t = DynArray_get(oTokens, i);
        if(t->eType == TOKEN_REDIN){
            //the following token is a name of a file
            f_token = DynArray_get(oTokens, i + 1);
            fname = f_token->pcValue;

            //stdin redirection
            //redirect the command's standard input to the file
            if((fd = open(fname, O_RDONLY)) < 0){   //open fails
                errorPrint("Redirected file cannot open", PERROR);
                return;
            }
            close(0);
            dup(fd);
            close(fd);
        }
        if(t->eType == TOKEN_REDOUT){
            //the following token is a name of a file
            f_token = DynArray_get(oTokens, i + 1);
            fname = f_token->pcValue;

            //stdout redirection
            //redirect the command's standard output to the file
            if((fd=open(fname, O_CREAT | O_TRUNC | O_WRONLY, 0600))<0){
                errorPrint("Redirected file cannot open", PERROR);
                return;
            }
            close(1);
            dup(fd);
            close(fd);
        }
    }
}

void procPipe(DynArray_T oTokens){
    assert(oTokens != NULL);
    int cnt = 0;    //to count the number of pipes
    int cmdIndex = 0;
    int in_fd = 0;
    int pipefd[2];
    struct Token *tmp;

    //counting # of pipefd
    for(int i = 0; i < DynArray_getLength(oTokens); ++i){
        tmp = DynArray_get(oTokens, i);
        if(tmp->eType == TOKEN_PIPE){
            cnt++;
        }
    }
    //handle pipe
    for (int i = 0; i <= cnt; ++i) {
        if(i < cnt){
            if(pipe(pipefd)){
                errorPrint("pipe creation failed", PERROR);
            }
        }
        //extracting the current command tokens
        DynArray_T curTokens = DynArray_new(0);
        for(int j = cmdIndex; j < DynArray_getLength(oTokens); ++j){
            tmp = DynArray_get(oTokens, j);
            if(tmp->eType != TOKEN_PIPE){
                DynArray_add(curTokens, tmp);
            }else{
                cmdIndex = j + 1;
                break;
            }
        }

        //fork a child process
        fflush(NULL);
        pid_t pid = fork();
        if(pid == 0){
            //child should not ignore the SIGINT signal.
            signal(SIGINT, SIG_DFL);
            //child should not ignore the SIGQUIT signal.
            signal(SIGQUIT, SIG_DFL);
            //redirect input
            if(i > 0){
                close(0);
                dup(in_fd); 
                close(in_fd);
            }
            //redirect output
            if (i < cnt) {
                close(1);
                dup(pipefd[1]);
                close(pipefd[1]);
            }
            close(pipefd[0]);
            //execute the command for current tokens
            execCommand(curTokens);
            //freeing temporary dynarray
            DynArray_free(curTokens); 
            exit(0);
        }else if(pid > 0){
            wait(NULL); //wait for the child that has been created
            close(pipefd[1]);
            in_fd = pipefd[0];
            //freeing temporary dynarray
            DynArray_free(curTokens);
        }else{  //fork failed
            errorPrint("execCmd fails fork()", PERROR);
        }
    }
}

void execCommand(DynArray_T oTokens){
    int cnt;
    char **args;
    struct Token *tmp;
    assert(oTokens != NULL);
    if((cnt = DynArray_getLength(oTokens)) == 0){
        errorPrint("execCmd has no command", FPRINTF);
        return;
    }
    //handle pipe
    for(int i = 0; i < DynArray_getLength(oTokens); ++i){
        tmp = DynArray_get(oTokens, i);
        if(tmp->eType == TOKEN_PIPE){
            procPipe(oTokens);
            return;
        }
    }
    //copy token into argument array
    if((args=(char**)malloc((cnt + 1)*sizeof(char*))) == NULL){
        errorPrint("execCmd cannot allocate memory", PERROR);
        return;
    }
    for(int i = 0; i < cnt; ++i){
        tmp = DynArray_get(oTokens, i);
        args[i] = tmp->pcValue;
        if(i==cnt-1){
            args[cnt] = '\0'; 
        }
    }

    //fork a child process
    fflush(NULL);
    pid_t pid = fork();
    if(pid == 0){   //execute given command with redirection handling
        //child should not ignore the SIGINT signal.
        signal(SIGINT, SIG_DFL);
        //child should not ignore the SIGQUIT signal.
        signal(SIGQUIT, SIG_DFL);
        procRedir(oTokens); //handle redirection
        execvp(args[0], args);
        errorPrint("nosuchcommand", PERROR);
        exit(EXIT_FAILURE);
    }else if(pid > 0){ //wait for the child that has been created
        wait(NULL);
    }else{  //fork failed
        errorPrint("execCmd fails fork()", PERROR);
    }
    free(args);
}

void execBuiltIn(DynArray_T oTokens){
    assert(oTokens != NULL);
    assert(DynArray_getLength(oTokens) > 0);
    
    struct Token *t_arg;

    if(DynArray_getLength(oTokens) == 1){   //there's no argument
        t_arg = NULL;
    }else{
        t_arg = DynArray_get(oTokens, 1);
        if(t_arg == NULL || t_arg->pcValue == NULL){
            errorPrint("Built-in command has invalid argument",
             FPRINTF);
            return;
        }
    }
    //if there is any file redirection with those built-in commands,
    //print an error message
    for(int i = 1; i < DynArray_getLength(oTokens); ++i){ 
        struct Token *tmp = DynArray_get(oTokens, i);
        if(tmp->eType == TOKEN_PIPE|| tmp->eType == TOKEN_REDIN 
         || tmp->eType == TOKEN_REDOUT){
            errorPrint("Built-in commands has illeagal redirection",
             FPRINTF);
            return;
        }
    }

    enum BuiltinType btype = checkBuiltin(DynArray_get(oTokens, 0));
    switch(btype){
        case B_SETENV:
            if(t_arg == NULL || strcmp(t_arg->pcValue, "") == 0){
                errorPrint("Please specify the environment for setenv",
                 FPRINTF);
            }else{
                struct Token *t_value;
                if(DynArray_getLength(oTokens) == 2){
                    if(setenv(t_arg->pcValue, "", 1)){
                        errorPrint("setenv failed", PERROR);
                    }
                }else{
                    t_value = DynArray_get(oTokens, 2);
                    if(t_value == NULL || t_arg->pcValue == NULL){
                        errorPrint("setenv has invalid value",
                         FPRINTF);
                    }else{
                        if(setenv(t_arg->pcValue, t_value->pcValue, 1)){
                            errorPrint("setenv has invalid value",
                            FPRINTF);
                        }
                    }
                }
            }
            break;

        case B_USETENV:
            if(t_arg == NULL || strcmp(t_arg->pcValue, "") == 0){
                errorPrint("Please specify the environment for unsetenv"
                , FPRINTF);
            }else{
                //destroy the environment variable var
                //If the environment variable does not exist, just ignore.
                if(getenv(t_arg->pcValue) != NULL){
                    if(unsetenv(t_arg->pcValue) != 0){
                        errorPrint("unsetenv failed", PERROR);
                    }
                }
            }
            break;

        case B_CD:
            //cd to the HOME directory if argument directory is omitted
            if(t_arg == NULL || strcmp(t_arg->pcValue, "") == 0){
                char *homedir;
                if((homedir=getenv("HOME"))==NULL){
                    errorPrint("Can't find HOME directory" , FPRINTF);
                }
                if(chdir(homedir)){
                    errorPrint("cd failed", PERROR);
                }
            }else{  //change its working directory to argument directory
                if(chdir(t_arg->pcValue)){
                    errorPrint("cd failed", PERROR);
                }
            }
            break;

        case B_EXIT:
            exit(0);

        default:
            errorPrint("Unknown built-in command", FPRINTF);
            break;
    }
}
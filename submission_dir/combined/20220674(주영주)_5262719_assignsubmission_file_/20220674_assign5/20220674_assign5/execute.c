/*--------------------------------------------------------------------*/
/* 20220674 주영주                                                     */
/* Assignment5                                                        */
/* execute.c                                                          */
/* execute the commands from lexical dynamic array                    */
/*--------------------------------------------------------------------*/

#include "execute.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include "dynarray.h"
#include "util.h"
#include "token.h"


extern char *programname;

/* signal handler for child process */
void exit_handler(int sig){
    fprintf(stdout, "\n");
    exit(0);
}

/* print error in stderr stream */
void builtin_error(const char *errormessage){

    fprintf(stderr, "%s: %s", programname, errormessage);
    return;
}

/*--------------------------------------------------------------------*/
/* make argument array functions                                      */

/*-----remove NULL in args-----*/
void remove_null(char **args, int idx, int len){
    if(args[idx] == NULL){
        int i;
        for(i = idx; i < len - 1; i++){
            args[i] = args[i + 1];
        }
        args[len - 1] = NULL;
    }
    return;
}

/*-----make argument array-----*/
/*---array dosen't contain information about
 'redirection symbols', 'redirection files'---*/
void dynarr_to_argarr(DynArray_T oTokens, char **args){
    int i;
    int dynlen = DynArray_getLength(oTokens);

    /* put all value of token to array(with NULL) */
    for(i = 0; i < dynlen; i++){
        args[i] = ((struct Token *)DynArray_get(oTokens, i))->pcValue;
    }

    /* if there are redirection symbol(s), remove redirection file(s) in array */
    for(i = dynlen - 1; i >= 0; i--){
        if(args[i] == NULL) args[i + 1] = NULL;
    }

    /* remove all NULL is the array */
    for(i = dynlen - 1; i >= 0; i--){
        remove_null(args, i, dynlen);
    }
}

/*--------------------------------------------------------------------*/

/*--------------------------------------------------------------------*/
/* redirection helper functions                                       */

/*if '<' exist, return redirection file. else, return NULL*/
char *redin_file(DynArray_T oTokens){
    int dynlen = DynArray_getLength(oTokens);
    int i;
    enum TokenType ttype;
    for(i = 0; i < dynlen; i++){
        ttype = ((struct Token *)DynArray_get(oTokens, i))->eType;
        if(ttype == TOKEN_REDIN){
            return ((struct Token *)DynArray_get(oTokens, i + 1))->pcValue;
        }
    }
    return NULL;
}

/*if '>' exist, return redirection file. else, return NULL*/
char *redout_file(DynArray_T oTokens){
    int dynlen = DynArray_getLength(oTokens);
    int i;
    enum TokenType ttype;
    for(i = 0; i < dynlen; i++){
        ttype = ((struct Token *)DynArray_get(oTokens, i))->eType;
        if(ttype == TOKEN_REDOUT){
            return ((struct Token *)DynArray_get(oTokens, i + 1))->pcValue;
        }
    }
    return NULL;
}

/*handle redirection with input, output files*/
int redir_handler(char *rif, char *rof){
    if(rif != NULL){
        int in = open(rif, O_RDONLY);
        if(in < 0){
            return -1;
        }
        if(dup2(in, STDIN_FILENO) < 0){
            close(in);
            return -1;
        }
        close(in);
    }

    if (rof != NULL) {
        int out = open(rof, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (out < 0) {
            return -1;
        }
        if (dup2(out, STDOUT_FILENO) < 0) { 
            close(out);
            return -1;
        }
        close(out);
    }
    return 0;
}

/*--------------------------------------------------------------------*/

/*-----execute the command-----*/
int execute(DynArray_T oTokens){

    int l = DynArray_getLength(oTokens);
    pid_t pid;
    char *cd_dir;
    int status;

    /*-----check the builtin type of command-----*/
    enum BuiltinType btype;
    btype = checkBuiltin(DynArray_get(oTokens, 0));
    
    /*-----convert the dynamic array to argument array-----*/
    char **args = (char **)calloc(l + 1, sizeof(char *));
    dynarr_to_argarr(oTokens, args);     // make argument array to execute
    args[l] = NULL;     // last element of array should be NULL

    /*-----put redirection input file to rif-----*/
    /*-----put redirection output file to rof-----*/
    char *rif = redin_file(oTokens);
    char *rof = redout_file(oTokens);

    /*-----execute by each cases-----*/
    switch (btype){
        case B_SETENV:
            /*-----don't redirect-----*/
            if(rif != NULL || rof != NULL){
                builtin_error("setenv takes one or two parameters\n");
                free(args);
                return EXIT_FAILURE;
            }

            if(l == 2){     // set environment to ""
                if(setenv(args[1], "", 0) == -1){
                    builtin_error("execution failed\n");
                    free(args);
                    return EXIT_FAILURE;
                }
                free(args);
                return EXIT_SUCCESS;
            }
            else if(l == 3){     // set environment
                if(setenv(args[1], args[2], 0) == -1){
                    builtin_error("execution failed\n");
                    free(args);
                    return EXIT_FAILURE;
                }
                free(args);
                return EXIT_SUCCESS;
            }
            else{     // should take one or two parameters
                builtin_error("setenv takes one or two parameters\n");
                free(args);
                return EXIT_FAILURE;
            }
            break;

        case B_USETENV:
            /*-----don't redirect-----*/
            if(rif != NULL || rof != NULL){
                builtin_error("unsetenv takes one parameter\n");
                free(args);
                return EXIT_FAILURE;
            }

            if(l == 2){     // unset environment variable
                if(unsetenv(args[1]) == -1){
                    builtin_error("execution failed\n");
                    free(args);
                    return EXIT_FAILURE;
                }
                free(args);
                return EXIT_SUCCESS;
            }
            else{     // should take one parameter
                builtin_error("unsetenv takes one parameter\n");
                free(args);
                return EXIT_FAILURE;
            }
            break;

        case B_CD:
            /*-----don't redirect-----*/
            if(rif != NULL || rof != NULL){
                builtin_error("cd takes one parameter\n");
                free(args);
                return EXIT_FAILURE;
            }

            /*-----put cd path in cd_dir-----*/
            if(l == 1) cd_dir = getenv("HOME");
            else if(l == 2) cd_dir = args[1];
            else{
                builtin_error("cd takes one parameter\n");
                free(args);
                return EXIT_FAILURE;
            }

            /*-----move directory-----*/
            if(chdir(cd_dir) == -1){
                builtin_error("No such file or directory\n");
                free(args);
                return EXIT_FAILURE;
            }
            free(args);
            return EXIT_SUCCESS;
            break;

        case B_EXIT:
            /*-----don't redirect-----*/
            if(rif != NULL || rof != NULL){
                builtin_error("exit does not take any parameters\n");
                free(args);
                return EXIT_FAILURE;
            }

            if(l == 1){
                free(args);
                exit(EXIT_SUCCESS);
            }
            else{
                builtin_error("exit does not take any parameters\n");
                free(args);
                return EXIT_FAILURE;
            }
            break;

        case NORMAL:
            pid = fork();     // make child process

            if(pid < 0){     // fork is failed
                builtin_error("fork failed\n");
                free(args);
                return EXIT_FAILURE;
            }
            else if(pid == 0){ // in child process
                /*-----signal handling in child process-----*/
                signal(SIGINT, exit_handler);
                signal(SIGQUIT, exit_handler);

                /*-----redirection handling in child process-----*/
                if(redir_handler(rif, rof) < 0){
                    exit(EXIT_FAILURE);
                }

                /*-----execute command-----*/
                execvp(args[0], args);
                fprintf(stderr, "%s: No such file or directory\n", args[0]);
                exit(EXIT_FAILURE);
            }
            else{ // in parent process
                pid = wait(&status);
            }
            free(args);
            return EXIT_SUCCESS;
            break;

        default:
            free(args);
            return EXIT_FAILURE;
            break;
    }
}

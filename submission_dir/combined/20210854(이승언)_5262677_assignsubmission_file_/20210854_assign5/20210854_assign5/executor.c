/*
EE209 Assignment 5: A Unix Shell
Submitted by SeungEon Lee, 20210854
This file implements the execution of builtin commands and input lines.
*/


#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <assert.h>
#include <fcntl.h>

#include "lexsyn.h"
#include "util.h"
#include "token.h"
#include "dynarray.h"
#include "executor.h"

/**
 * execute_exit
 * : executes the built-in exit command.
 * 
 * Parameters:
 *      DynArray_T oTokens - the array of processed tokens
 * 
 * Return:
 *      no return value
 */
void execute_exit(DynArray_T oTokens) {
    /* Check that input is not NULL. */
    assert(oTokens != NULL);

    /* Check the length of the array: exit takes no parameters */
    int len = DynArray_getLength(oTokens);
    int lenLimit = (len == 1);

    /* If input does not match length limit, print an error message. */
    if (!lenLimit) {
        errorPrint("exit does not take any parameters", FPRINTF);
    }
    /* If input matches length limit, free array and exit. */
    else {
        DynArray_map(oTokens, freeToken, NULL); /* Call freeToken function for elements in array. */
        DynArray_free(oTokens);                 /* Free the array. */
        exit(EXIT_SUCCESS);                     /* Exit with EXIT_SUCCESS. */
    }
}

/**
 * execute_setenv
 * : executes the built-in setenv command.
 * 
 * Parameters:
 *      DynArray_T oTokens - the array of processed tokens
 * 
 * Return:
 *      no return value
 */
void execute_setenv(DynArray_T oTokens) {
    /* Check that input is not NULL. */
    assert(oTokens != NULL);

    /* Check the length of the array: setenv takes 2 input, with the second possibly ommited. */
    int len = DynArray_getLength(oTokens);
    int lenLimit = ((len == 2) | (len == 3));

    /* If input does not match length limit, print an error message. */
    if (!lenLimit) {
        errorPrint("setenv takes one or two parameters", FPRINTF);
    }

    /* If input matches length limit, set environment variable using setenv. */
    else {
        char *env_name = ((struct Token *) DynArray_get(oTokens, 1))->pcValue;
        char *env_val = "";     /* Default(omitted): set value as empty string. */
        if (len == 3) {         /* On input: set value to input. */
            env_val = ((struct Token *) DynArray_get(oTokens, 2))->pcValue;
        }
        if (setenv(env_name, env_val, 1) == -1) errorPrint(NULL, PERROR);
    }
}

/**
 * execute_unsetenv
 * : executes the built-in unsetenv command.
 * 
 * Parameters:
 *      DynArray_T oTokens - the array of processed tokens
 * 
 * Return:
 *      no return value
 */
void execute_unsetenv(DynArray_T oTokens) {
    /* Check that input is not NULL. */
    assert(oTokens != NULL);

    /* Check the length of the array: setenv takes 1 input. */
    int len = DynArray_getLength(oTokens);
    int lenLimit = (len == 2);

    /* If input does not match length limit, print an error message. */
    if (!lenLimit) {
        errorPrint("unsetenv takes one parameter", FPRINTF);
    }

    /* If input matches length limit, unset environment variable using unsetenv. */
    else {
        char *env_name = ((struct Token *) DynArray_get(oTokens, 1))->pcValue;
        if (unsetenv(env_name) == -1) errorPrint(NULL, PERROR);
    }
}

/**
 * execute_cd
 * : executes the built-in cd command.
 * 
 * Parameters:
 *      DynArray_T oTokens - the array of processed tokens
 * 
 * Return:
 *      no return value
 */
void execute_cd(DynArray_T oTokens) {
    /* Check that input is not NULL. */
    assert(oTokens != NULL);

    /* Check the length of the array: cd takes 1 input, which is possibly ommited. */
    int len = DynArray_getLength(oTokens);
    int lenLimit = ((len == 1) | (len == 2));

    /* If input does not match length limit, print an error message. */
    if (!lenLimit) {
        errorPrint("cd takes one parameter", FPRINTF);
    }

    /* If input matches length limit, change directory using unsetenv. */
    else {
        char *dir = getenv("HOME");     /* Default(omitted): set directory to home directory. */
        if (len == 2) {                 /* On input: set directory to input. */
            dir = ((struct Token *) DynArray_get(oTokens, 1))->pcValue;
        }
        if (chdir(dir) == -1) errorPrint("No such file or directory", FPRINTF);
    }
}

/**
 * redirection_in
 * : executes redirection.
 * 
 * Parameters:
 *      DynArray_T oTokens - the array of processed tokens
 *      int index - index of <
 * 
 * Return:
 *      no return value
 */
void redirection_in(DynArray_T oTokens, int index) {
    assert(oTokens != NULL);
    
    int len = DynArray_getLength(oTokens);
    assert((index >=0) && (index <= len - 2));
    
    struct Token *t = (struct Token *)DynArray_get(oTokens, index + 1);
    if (t == NULL || t->pcValue == NULL) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
    }

    const char *filename = t->pcValue;

    if (access(filename, F_OK) == 0) {
        int fd = open(filename, O_RDONLY);
        if (fd == -1) {
            errorPrint(NULL, PERROR);
            exit(EXIT_FAILURE);
        }

        if (dup2(fd, STDIN_FILENO) == -1) {
            close(fd);
            errorPrint(NULL, PERROR);
            exit(EXIT_FAILURE);
        }

        close(fd);
    }
    else {
        errorPrint("No such file or directory", FPRINTF);
        exit(EXIT_FAILURE);
    }
}

/**
 * redirection_out
 * : executes redirection.
 * 
 * Parameters:
 *      DynArray_T oTokens - the array of processed tokens
 *      int index - index of >
 * 
 * Return:
 *      no return value
 */
void redirection_out(DynArray_T oTokens, int index) {
    assert(oTokens != NULL);
    
    int len = DynArray_getLength(oTokens);
    assert((index >=0) && (index <= len - 2));
    
    struct Token *t = (struct Token *)DynArray_get(oTokens, index + 1);
    if (t == NULL || t->pcValue == NULL) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
    }

    const char *filename = t->pcValue;

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
    }

    if (dup2(fd, STDOUT_FILENO) == -1) {
        close(fd);
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
    }

    close(fd);
}

/**
 * execute_normal
 * : executes normal command.
 * 
 * Parameters:
 *      DynArray_T oTokens - the array of processed tokens
 * 
 * Return:
 *      no return value
 */
void execute_normal(DynArray_T oTokens) {
    assert(oTokens != NULL);
    pid_t pid;
    int i;
    int redin_index = checkRedirectionIn(oTokens);
    int redout_index = checkRedirectionOut(oTokens);
    int len = DynArray_getLength(oTokens);
    
    char **argv = malloc((len + 1) * sizeof(char *));

    if (argv == NULL) {
        errorPrint("Cannot allocate memory", FPRINTF);
        exit(EXIT_FAILURE);
    }

    /* Fork */    
    fflush(NULL);
    if ((pid = fork()) < 0) {
        errorPrint(NULL, PERROR);
        exit(EXIT_FAILURE);
    }
    
    /* Child process */
    else if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        for (i = 0; i < len; i++) {
            struct Token* t = (struct Token *) DynArray_get(oTokens, i);
            argv[i] = t->pcValue;
        }
        argv[len] = NULL;
        
        if (redin_index != 0) redirection_in(oTokens, redin_index);
        if (redout_index != 0) redirection_out(oTokens, redout_index);
        
        errorPrint(argv[0], SETUP);
        execvp(argv[0], argv);
        
        errorPrint("No such file or directory", FPRINTF);
        free(argv);
        exit(EXIT_FAILURE);
    }

    /* Parent process */
    else {
        wait(NULL);
        free(argv);
    }
    return;
}
void execute_pipe(DynArray_T oTokens, int numPipes) {
    assert(oTokens != NULL);
    assert((numPipes) && (numPipes > 0));

    int pipefd[2 * numPipes];
    pid_t pid;
    DynArray_T pipeTokens;
    int i, j;
    int start = 0;

    for (i = 0; i < numPipes; i++) {
        if (pipe(pipefd + (2 * i)) == -1) {
            errorPrint(NULL, PERROR);
            exit(EXIT_FAILURE);
        }
    }

    for (i = 0; i <= numPipes; i++) {
        fflush(NULL);
        if ((pid = fork()) < 0) {
            errorPrint(NULL, PERROR);
            exit(EXIT_FAILURE);
        }
        else if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            if (i > 0) {
                if (dup2(pipefd[(i - 1) * 2], STDIN_FILENO) == -1) {
                    errorPrint(NULL, PERROR);
                    exit(EXIT_FAILURE);
                }
            }
            if (i < numPipes) {
                if (dup2(pipefd[(i * 2) + 1], STDOUT_FILENO) == -1) {
                    errorPrint(NULL, PERROR);
                    exit(EXIT_FAILURE);
                }
            }

            for (j = 0; j < 2 * numPipes; j++) {
                if (pipefd[j] > 0) close(pipefd[j]);
            }
            pipeTokens = DynArray_new(0);
            if (pipeTokens == NULL) {
                errorPrint("Cannot allocate memory", FPRINTF);
                exit(EXIT_FAILURE);
            }

            for (j = start; j < DynArray_getLength(oTokens); j++) {
                struct Token *t = (struct Token *) DynArray_get(oTokens, j);
                if (t->eType == TOKEN_PIPE) {
                    start = j + 1;
                    break;
                }
                DynArray_add(pipeTokens, t);
            }

            int len = DynArray_getLength(pipeTokens);
            char **argv = malloc((len + 1) * sizeof(char *));

            for (j = 0; j < len; j++) {
                struct Token* t = (struct Token *) DynArray_get(pipeTokens, j);
                argv[j] = t->pcValue;
            }
            argv[len] = NULL;
            
            errorPrint(argv[0], SETUP);
            execvp(argv[0], argv);
            
            errorPrint("No such file or directory", FPRINTF);
            free(argv);

            DynArray_map(pipeTokens, freeToken, NULL); /* Call freeToken function for elements. */
            DynArray_free(pipeTokens);                 /* Free the array. */
            exit(EXIT_SUCCESS);                     /* Exit with EXIT_SUCCESS. */
        }
    }
    for (i = 0; i< 2 * numPipes; i++) {
        close(pipefd[i]);
    }
    for (i = 0; i <= numPipes; i++) {
        wait(NULL);
    }
}

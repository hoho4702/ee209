/*
ish.h
Name: 조웅래
Student ID: 20240699
Description: 
Header file for the 'ish' shell. Declares helper functions
for command processing, execution, redirection, pipelines,
and signal handling.
*/

#ifndef ISH_H
#define ISH_H
    
#include "dynarray.h"
#include "token.h"

/* Function Declarations */

/*
shellHelper
Processes a single command line: tokenizes, checks syntax, and executes
built-in or external commands.
*/
void shellHelper(const char *inLine);

/*
setupSignals
Sets up signal handlers for SIGINT, SIGQUIT, and SIGALRM.
*/
void setupSignals(void);

/*
readIshrc
Reads and executes commands from the .ishrc file in the user's home directory.
*/
void readIshrc(void);

/*
executeBuiltin
Executes built-in commands like cd, setenv, unsetenv, and exit.
*/
void executeBuiltin(enum BuiltinType btype, DynArray_T oTokens);

/*
executeExternal
Executes external (non-built-in) commands with possible I/O redirection.
*/
void executeExternal(DynArray_T oTokens);

/*
processRedirections
Handles input and output redirections by opening files and updating file descriptors.
Returns 1 on success, 0 on failure.
*/
int processRedirections(DynArray_T oTokens, int *inFd, int *outFd);

/*
hasRedirection
Checks if the command includes input or output redirection tokens.
Returns 1 if present, 0 otherwise.
*/
int hasRedirection(DynArray_T oTokens);

/*
sigquitHandler
Handles SIGQUIT signal: prompts user to confirm exit on first press.
*/
void sigquitHandler(int sig);

/*
sigalrmHandler
Resets the quitPressed flag after the timeout period.
*/
void sigalrmHandler(int sig);

/*
processPipeline
Splits the command tokens at pipe symbols and executes the pipeline.
*/
void processPipeline(DynArray_T oTokens);

/*
splitPipeline
Splits tokens into separate commands based on pipes.
Returns an array of DynArray_T pointers and sets the count.
*/
DynArray_T *splitPipeline(DynArray_T oTokens, int *count);

/*
executePipeline
Executes a series of commands connected by pipes.
*/
void executePipeline(DynArray_T *cmds, int count);

#endif /* ISH_H */
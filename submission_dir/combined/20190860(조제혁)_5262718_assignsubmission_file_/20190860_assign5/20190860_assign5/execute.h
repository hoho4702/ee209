#ifndef _EXECUTE_H_
#define _EXECUTE_H_

#include "dynarray.h"
#include "token.h"

/* return values */
enum ExecResult {
    EXEC_SUCCESS,
    EXEC_FAIL_PERMISSION,
    EXEC_FAIL_NOT_FOUND,
    EXEC_FAIL_NO_MEM,
    EXEC_FAIL_IO_ERROR,
    EXEC_FAIL_FORK,
    EXEC_FAIL_PIPE,
    EXEC_FAIL_DUP,
    EXEC_FAIL_INVALID_ARGS
};

/* exec builtin command, return EXEC_SUCCESS if cmd was exec,
   or appropriate error code otherwise */
enum ExecResult executeBuiltin(DynArray_T oTokens);

struct CommandInfo {
    int startIndex;     /* start of cmd in token arr */
    int endIndex;       /* end of command in token arr */
    int inputFd;        /* input fd */
    int outputFd;       /* output fd */
    int hasRedirection; /* flag for redirect */
};

/* exec cmd, returns EXEC_SUCCESS if cmd was executed successfully, 
   or appropriate error code otherwise. */
enum ExecResult executeCommand(DynArray_T oTokens);

/* exec a pipeline of cmds, returns EXEC_SUCCESS if all commands
   were executed successfully, or appropriate error code otherwise. */
enum ExecResult executePipeline(DynArray_T oTokens);

/* file redirect, returns 0 on success, -1 on failure. */
int handleRedirection(DynArray_T oTokens, int* inFd, int* outFd);

/* clearn up fd's and other resources */
void cleanupExecution(int inFd, int outFd);

#endif /* _EXECUTE_H_ */

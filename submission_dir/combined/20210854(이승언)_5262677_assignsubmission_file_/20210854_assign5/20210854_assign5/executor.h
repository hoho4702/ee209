#ifndef _EXECUTOR_H_
#define _EXECUTOR_H_

void execute_exit(DynArray_T oTokens);

void execute_setenv(DynArray_T oTokens);

void execute_unsetenv(DynArray_T oTokens);

void execute_cd(DynArray_T oTokens);

void redirection_in(DynArray_T oTokens, int index);

void redirection_out(DynArray_T oTokens, int index);

void execute_normal(DynArray_T oTokens);

void execute_pipe(DynArray_T oTokens, int numPipes);

#endif /* _EXECUTOR_H_ */

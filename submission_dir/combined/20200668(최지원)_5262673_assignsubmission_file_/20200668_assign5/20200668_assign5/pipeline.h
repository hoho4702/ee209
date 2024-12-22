#ifndef PIPELINE_H
#define PIPELINE_H

#include "dynarray.h"
#include "token.h"

void process_pipeline(DynArray_T oTokens);
void execute_pipeline(DynArray_T commands[], int pipeCount);

#endif /* PIPELINE_H */

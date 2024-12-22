#ifndef _IDMANAGE_H_
#define _IDMANAGE_H_
#include <unistd.h>
void idpush(DynArray_T pids, pid_t id);
pid_t idpop(DynArray_T pids);
void cleanid(DynArray_T pids);
#endif

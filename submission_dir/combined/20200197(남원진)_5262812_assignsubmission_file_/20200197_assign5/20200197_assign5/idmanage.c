#include <stdlib.h>
#include "dynarray.h"
#include "idmanage.h"

void idpush(DynArray_T pids, pid_t id) {
    pid_t *ptr = malloc(sizeof(id));
    *ptr = id;
    DynArray_add(pids, ptr);
}

pid_t idpop(DynArray_T pids) {
    int numids = DynArray_getLength(pids);
    if (numids == 0) return 0;
    pid_t *pret = (pid_t *) DynArray_removeAt(pids, numids-1);
    pid_t ret = *pret; free(pret);
    return ret;
}

void cleanid(DynArray_T pids) {
    int numids = DynArray_getLength(pids);
    for (int i=0; i<numids; i++) {
        free(DynArray_get(pids, i));
    }
    DynArray_free(pids);
}

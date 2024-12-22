#ifndef _JOB_H_
#define _JOB_H_

#include <sys/types.h>
#include <stdlib.h>

enum BGFG {BG, FG};

struct Job {
    pid_t pid;
    enum BGFG bgfg;
};
typedef struct Job *Job_T;

Job_T makeJob(pid_t pid, enum BGFG bgfg);
void freeJob(Job_T job);
int Job_compare (const void *a, const void *b);

#endif  /* _JOB_H_ */
#include "job.h"

Job_T makeJob(pid_t pid, enum BGFG bgfg) {
    Job_T job;

    job = (Job_T)malloc(sizeof(struct Job));
    if (job == NULL)
        return NULL;

    job->pid = pid;
    job->bgfg = bgfg;

    return job;
}

void freeJob(Job_T job) {
   if (job != NULL) free(job);
}

int Job_compare (const void *a, const void *b) {
    Job_T job1 = (Job_T)a;
    Job_T job2 = (Job_T)b;

    return (job1->pid == job2->pid) ? 0:1;
}
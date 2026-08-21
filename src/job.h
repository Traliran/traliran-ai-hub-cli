/*
 * job.h - minimal background thread runner (worker does no ncurses).
 */
#ifndef HUB_JOB_H
#define HUB_JOB_H

#include <pthread.h>
#include <stdbool.h>

typedef struct {
    pthread_t      tid;
    volatile int   done;
    void        *(*fn)(void *);
    void          *arg;
} job_t;

void job_start(job_t *j, void *(*fn)(void *), void *arg);
bool job_is_done(job_t *j);
void job_join(job_t *j);

#endif /* HUB_JOB_H */
/*
 * job.c - minimal background thread runner.
 */
#include <stdlib.h>
#include "job.h"

static void *job_trampoline(void *arg) {
    job_t *j = arg;
    j->fn(j->arg);
    j->done = 1;
    return NULL;
}

void job_start(job_t *j, void *(*fn)(void *), void *arg) {
    j->fn = fn;
    j->arg = arg;
    j->done = 0;
    pthread_create(&j->tid, NULL, job_trampoline, j);
}

bool job_is_done(job_t *j) { return j->done != 0; }

void job_join(job_t *j) {
    pthread_join(j->tid, NULL);
    j->done = 1;
}
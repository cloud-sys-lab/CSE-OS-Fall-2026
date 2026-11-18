#ifndef UTHREAD_INTERNAL_H
#define UTHREAD_INTERNAL_H

#include <ucontext.h>
#include "uthread.h"

#define MAX_THREADS 128

typedef enum { READY, RUNNING, BLOCKED, DONE } tstate_t;

typedef struct tcb {
    int tid;
    tstate_t state;
    ucontext_t ctx;
    void *stack;
    int expected_ms;
    long wake_at;
} tcb_t;

typedef struct {
    void (*enqueue)(tcb_t *t);
    tcb_t *(*pick_next)(void);
    int (*should_preempt)(tcb_t *running);
} sched_policy_t;

extern sched_policy_t fcfs_policy;
extern sched_policy_t sjf_policy;
extern sched_policy_t stcf_policy;
extern sched_policy_t rr_policy;
extern sched_policy_t mlfq_policy;

#endif

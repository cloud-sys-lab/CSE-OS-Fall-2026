#ifndef UTHREAD_INTERNAL_H
#define UTHREAD_INTERNAL_H

#include <ucontext.h>
#include "uthread.h"

#define MAX_THREADS 128

typedef enum
{
    READY,
    RUNNING,
    BLOCKED,
    DONE
} tstate_t;

typedef struct tcb
{
    int tid;
    tstate_t state;
    ucontext_t ctx;
    void *stack;
    void (*fn)(void *);
    void *arg;
    int expected_ms;
    long remaining_us;
    long dispatched_at;
    int level;
    long level_used_us;
    int slice_ticks;
    unsigned long boost_epoch;
    long wake_at;
} tcb_t;

typedef struct
{
    void (*enqueue)(tcb_t *t);
    tcb_t *(*pick_next)(void);
    int (*should_preempt)(tcb_t *running);
    void (*init)(int quantum_ms);
} sched_policy_t;

long now_us(void);

extern sched_policy_t fcfs_policy;
extern sched_policy_t sjf_policy;
extern sched_policy_t stcf_policy;
extern sched_policy_t rr_policy;
extern sched_policy_t mlfq_policy;

#endif

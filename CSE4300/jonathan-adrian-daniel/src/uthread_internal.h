#ifndef UTHREAD_INTERNAL_H
#define UTHREAD_INTERNAL_H

#include <pthread.h>
#include <time.h>
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
    void (*enqueue)(void* instance, tcb_t *t);
    tcb_t *(*pick_next)(void* instance);
    int (*should_preempt)(void* instance, tcb_t *running);
    void* (*init)(int quantum_ms);
    void* instance;
} sched_policy_t;


typedef struct scheduler{
    pthread_mutex_t* policyAccessLock;
    sched_policy_t *policy;
    tcb_t *running;
    int quantum_ms;
    ucontext_t main_ctx;

    tcb_t *threads[MAX_THREADS];
    int thread_count;

    tcb_t *sleeping[MAX_THREADS];
    int sleeping_count;

    timer_t timer;
    int timer_created;

    uthread_stats_t stats[MAX_THREADS];
    int stats_count;
    long switch_count;
    tcb_t *last_dispatched;

}scheduler_t;

long now_us(void);

extern sched_policy_t fcfs_policy;
extern sched_policy_t sjf_policy;
extern sched_policy_t stcf_policy;
extern sched_policy_t rr_policy;
extern sched_policy_t mlfq_policy;

#endif

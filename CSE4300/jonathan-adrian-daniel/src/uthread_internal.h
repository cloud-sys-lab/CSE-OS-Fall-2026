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
    void (*enqueue)(void *state, tcb_t *t);
    tcb_t *(*pick_next)(void *state);
    int (*should_preempt)(void *state, tcb_t *running);
    void *(*init)(int quantum_ms);
    void (*destroy)(void *state);
} sched_policy_t;

typedef struct core
{
    struct scheduler *scheduler;
    tcb_t *running;
    ucontext_t sched_ctx;
#ifdef __linux__
    timer_t timer;
#endif
    int timer_created;
    tcb_t *last_dispatched;
    int id;
} core_t;

typedef struct scheduler
{
    pthread_mutex_t lock;
    const sched_policy_t *policy;
    void *policy_state;
    int quantum_ms;

    tcb_t *threads[MAX_THREADS];
    int thread_count;
    int live_threads;

    tcb_t *sleeping[MAX_THREADS];
    int sleeping_count;

    int active_cores;
    int next_core_id;

    uthread_stats_t stats[MAX_THREADS];
    int stats_count;
    long switch_count;
} scheduler_t;

long now_us(void);
long now_mono_us(void);

extern const sched_policy_t fcfs_policy;
extern const sched_policy_t sjf_policy;
extern const sched_policy_t stcf_policy;
extern const sched_policy_t rr_policy;
extern const sched_policy_t mlfq_policy;

#endif

#ifndef UTHREAD_H
#define UTHREAD_H

//extern def, included here for public calls, users do not need to know what a scheduler object is, however
struct scheduler;
typedef struct scheduler scheduler_t;


typedef enum {
    POLICY_FCFS,
    POLICY_SJF,
    POLICY_STCF,
    POLICY_RR,
    POLICY_MLFQ
} policy_t;

typedef struct {
    int expected_ms;
    int start_delay_ms;
} uthread_attr_t;

typedef struct {
    long arrived_us;
    long first_run_us;
    long finished_us;
    long cpu_us;
} uthread_stats_t;



scheduler_t *uthread_init(policy_t policy, int quantum_ms);

int uthread_create(scheduler_t *scheduler, void (*fn)(void *), void *arg, const uthread_attr_t *attr);

void uthread_yield(void);

void uthread_sleep(int ms);

void uthread_run(scheduler_t *scheduler);

int uthread_get_stats(scheduler_t *scheduler, int tid, uthread_stats_t *out);

long uthread_context_switches(scheduler_t *scheduler);

#endif

#ifdef MULTITHREAD_H
#include <pthread.h>
void uthread_set_policy_lock(scheduler_t *scheduler, pthread_mutex_t *lock);
#endif
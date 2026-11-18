#include <stddef.h>
#include "uthread_internal.h"

static sched_policy_t *policy;
static tcb_t *running;
static int quantum_ms;

static tcb_t *sleeping[MAX_THREADS];
static int sleeping_count;

static void schedule(void)
{
}

static void on_timer(int sig)
{
}

int uthread_init(policy_t which, int q)
{
    switch (which) {
    case POLICY_FCFS: policy = &fcfs_policy; break;
    case POLICY_SJF:  policy = &sjf_policy;  break;
    case POLICY_STCF: policy = &stcf_policy; break;
    case POLICY_RR:   policy = &rr_policy;   break;
    case POLICY_MLFQ: policy = &mlfq_policy; break;
    default: return -1;
    }
    quantum_ms = q;
    return 0;
}

int uthread_create(void (*fn)(void *), void *arg, const uthread_attr_t *attr)
{
    return -1;
}

void uthread_yield(void)
{
}

void uthread_sleep(int ms)
{
}

void uthread_run(void)
{
}

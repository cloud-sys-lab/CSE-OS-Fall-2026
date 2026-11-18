#include <stddef.h>
#include "uthread_internal.h"

static tcb_t *queue[MAX_THREADS];
static int count;

static void sjf_enqueue(tcb_t *t)
{
}

static tcb_t *sjf_pick_next(void)
{
    return NULL;
}

static int sjf_should_preempt(tcb_t *running)
{
    return 0;
}

sched_policy_t sjf_policy = {
    .enqueue = sjf_enqueue,
    .pick_next = sjf_pick_next,
    .should_preempt = sjf_should_preempt,
};

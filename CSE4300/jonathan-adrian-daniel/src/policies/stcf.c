#include <stddef.h>
#include "uthread_internal.h"

static tcb_t *queue[MAX_THREADS];
static int count;

static void stcf_enqueue(tcb_t *t)
{
}

static tcb_t *stcf_pick_next(void)
{
    return NULL;
}

static int stcf_should_preempt(tcb_t *running)
{
    return 0;
}

sched_policy_t stcf_policy = {
    .enqueue = stcf_enqueue,
    .pick_next = stcf_pick_next,
    .should_preempt = stcf_should_preempt,
};

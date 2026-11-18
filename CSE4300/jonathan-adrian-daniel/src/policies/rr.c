#include <stddef.h>
#include "uthread_internal.h"

static tcb_t *queue[MAX_THREADS];
static int count;

static void rr_enqueue(tcb_t *t)
{
}

static tcb_t *rr_pick_next(void)
{
    return NULL;
}

static int rr_should_preempt(tcb_t *running)
{
    return 0;
}

sched_policy_t rr_policy = {
    .enqueue = rr_enqueue,
    .pick_next = rr_pick_next,
    .should_preempt = rr_should_preempt,
};

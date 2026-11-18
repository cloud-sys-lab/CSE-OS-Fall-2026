#include <stddef.h>
#include "uthread_internal.h"

#define MLFQ_LEVELS 3

static tcb_t *queues[MLFQ_LEVELS][MAX_THREADS];
static int counts[MLFQ_LEVELS];

static void mlfq_enqueue(tcb_t *t)
{
}

static tcb_t *mlfq_pick_next(void)
{
    return NULL;
}

static int mlfq_should_preempt(tcb_t *running)
{
    return 0;
}

sched_policy_t mlfq_policy = {
    .enqueue = mlfq_enqueue,
    .pick_next = mlfq_pick_next,
    .should_preempt = mlfq_should_preempt,
};

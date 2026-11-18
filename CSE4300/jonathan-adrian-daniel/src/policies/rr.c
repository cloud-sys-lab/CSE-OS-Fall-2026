#include <stddef.h>
#include "uthread_internal.h"
#include "queue.h"

static queue_t ready;

static void rr_enqueue(tcb_t *t)
{
    queue_push(&ready, t);
}

static tcb_t *rr_pick_next(void)
{
    return queue_pop(&ready);
}

static int rr_should_preempt(tcb_t *running)
{
    return 1;
}

sched_policy_t rr_policy = {
    .enqueue = rr_enqueue,
    .pick_next = rr_pick_next,
    .should_preempt = rr_should_preempt,
};

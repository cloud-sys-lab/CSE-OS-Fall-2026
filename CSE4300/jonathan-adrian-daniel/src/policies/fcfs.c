#include <stddef.h>
#include "uthread_internal.h"
#include "queue.h"

static queue_t ready;

static void fcfs_enqueue(tcb_t *t)
{
    queue_push(&ready, t);
}

static tcb_t *fcfs_pick_next(void)
{
    return queue_pop(&ready);
}

static int fcfs_should_preempt(tcb_t *running)
{
    return 0;
}

sched_policy_t fcfs_policy = {
    .enqueue = fcfs_enqueue,
    .pick_next = fcfs_pick_next,
    .should_preempt = fcfs_should_preempt,
};

#include <stddef.h>
#include "uthread_internal.h"
#include "heap.h"

static heap_t ready;

static void sjf_enqueue(tcb_t *t)
{
    heap_push(&ready, t);
}

static tcb_t *sjf_pick_next(void)
{
    return heap_pop(&ready);
}

static int sjf_should_preempt(tcb_t *running)
{
    (void) running;
    return 0;
}

sched_policy_t sjf_policy = {
    .enqueue = sjf_enqueue,
    .pick_next = sjf_pick_next,
    .should_preempt = sjf_should_preempt,
};

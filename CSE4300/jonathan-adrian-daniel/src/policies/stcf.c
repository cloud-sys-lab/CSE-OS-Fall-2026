#include <stddef.h>
#include "uthread_internal.h"
#include "heap.h"

static int by_remaining(const tcb_t *a, const tcb_t *b)
{
    return a->remaining_us < b->remaining_us;
}

static heap_t ready = { .less = by_remaining };

static void stcf_enqueue(tcb_t *t)
{
    heap_push(&ready, t);
}

static tcb_t *stcf_pick_next(void)
{
    return heap_pop(&ready);
}

static int stcf_should_preempt(tcb_t *running)
{
    return 1;
}

sched_policy_t stcf_policy = {
    .enqueue = stcf_enqueue,
    .pick_next = stcf_pick_next,
    .should_preempt = stcf_should_preempt,
};

#include <stddef.h>
#include <stdlib.h>
#include "uthread_internal.h"
#include "heap.h"

typedef struct {
    heap_t ready;
} stcf_instance;

static int by_remaining(const tcb_t *a, const tcb_t *b)
{
    return a->remaining_us < b->remaining_us;
}

static void stcf_enqueue(void *inst, tcb_t *t)
{
    stcf_instance *instance = inst;
    heap_push(&instance->ready, t);
}

static tcb_t *stcf_pick_next(void *inst)
{
    stcf_instance *instance = inst;
    return heap_pop(&instance->ready);
}

static int stcf_should_preempt(void *inst, tcb_t *running)
{
    (void)inst;
    (void)running;
    return 1;
}

static void *stcf_init(int q)
{
    (void)q;
    stcf_instance *instance = calloc(1, sizeof *instance);
    if (instance)
        instance->ready.less = by_remaining;
    return instance;
}

static void stcf_destroy(void *inst)
{
    free(inst);
}

const sched_policy_t stcf_policy = {
    .enqueue = stcf_enqueue,
    .pick_next = stcf_pick_next,
    .should_preempt = stcf_should_preempt,
    .init = stcf_init,
    .destroy = stcf_destroy,
};

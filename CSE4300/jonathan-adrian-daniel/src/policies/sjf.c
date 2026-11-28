#include <stddef.h>
#include <stdlib.h>
#include "uthread_internal.h"
#include "heap.h"

typedef struct {
    heap_t ready;
} sjf_instance;

static int by_expected(const tcb_t *a, const tcb_t *b)
{
    return a->expected_ms < b->expected_ms;
}

static void sjf_enqueue(void *inst, tcb_t *t)
{
    sjf_instance *instance = inst;
    heap_push(&instance->ready, t);
}

static tcb_t *sjf_pick_next(void *inst)
{
    sjf_instance *instance = inst;
    return heap_pop(&instance->ready);
}

static int sjf_should_preempt(void *inst, tcb_t *running)
{
    (void)inst;
    (void)running;
    return 0;
}

static void *sjf_init(int q)
{
    (void)q;
    sjf_instance *instance = calloc(1, sizeof *instance);
    if (instance)
        instance->ready.less = by_expected;
    return instance;
}

static void sjf_destroy(void *inst)
{
    free(inst);
}

const sched_policy_t sjf_policy = {
    .enqueue = sjf_enqueue,
    .pick_next = sjf_pick_next,
    .should_preempt = sjf_should_preempt,
    .init = sjf_init,
    .destroy = sjf_destroy,
};

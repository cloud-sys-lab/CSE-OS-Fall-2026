#include <stddef.h>
#include <stdlib.h>
#include "uthread_internal.h"
#include "heap.h"

typedef struct {
    heap_t *ready;
} heap_instance;

static int by_remaining(const tcb_t *a, const tcb_t *b)
{
    return a->remaining_us < b->remaining_us;
}

static void stcf_enqueue(void* inst, tcb_t *t)
{
    heap_instance *instance = (heap_instance*) inst;
    heap_push(instance->ready, t);
}

static tcb_t *stcf_pick_next(void* inst)
{
    heap_instance *instance = (heap_instance*) inst;
    return heap_pop(instance->ready);
}

static int stcf_should_preempt(void* inst, tcb_t *running)
{
    (void) inst;
    return 1;
}
static void* stcf_init(int q){
    heap_instance *newInstance = malloc(sizeof(heap_instance));
    newInstance->ready = calloc(1, sizeof(heap_t));
    newInstance->ready->less = by_remaining;
    
    return newInstance;
}
#warning NEED TO IMPLEMENT A CLEANUP FUNCTION TO FREE EACH INSTANCE FOR ALL IMPLEMENTATIONS!!

sched_policy_t stcf_policy = {
    .enqueue = stcf_enqueue,
    .pick_next = stcf_pick_next,
    .should_preempt = stcf_should_preempt,
    .init = stcf_init
};

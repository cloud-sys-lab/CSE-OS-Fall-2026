#include <stddef.h>
#include <stdlib.h>
#include "uthread_internal.h"
#include "queue.h"

typedef struct {
    queue_t ready;
} rr_instance;

static void rr_enqueue(void *inst, tcb_t *t)
{
    rr_instance *instance = inst;
    queue_push(&instance->ready, t);
}

static tcb_t *rr_pick_next(void *inst)
{
    rr_instance *instance = inst;
    return queue_pop(&instance->ready);
}

static int rr_should_preempt(void *inst, tcb_t *running)
{
    (void)inst;
    (void)running;
    return 1;
}

static void *rr_init(int q)
{
    (void)q;   //this rr implementation does not use a time quantum internally in the policy specific handling, can therefore safely ignore q.
    return calloc(1, sizeof(rr_instance));
}

static void rr_destroy(void *inst)
{
    free(inst);
}

const sched_policy_t rr_policy = {
    .enqueue = rr_enqueue,
    .pick_next = rr_pick_next,
    .should_preempt = rr_should_preempt,
    .init = rr_init,
    .destroy = rr_destroy,
};

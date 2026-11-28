#include <stddef.h>
#include <stdlib.h>
#include "uthread_internal.h"
#include "queue.h"

typedef struct {
    queue_t ready;
} fcfs_instance;

static void fcfs_enqueue(void *inst, tcb_t *t)
{
    fcfs_instance *instance = inst;
    queue_push(&instance->ready, t);
}

static tcb_t *fcfs_pick_next(void *inst)
{
    fcfs_instance *instance = inst;
    return queue_pop(&instance->ready);
}

static int fcfs_should_preempt(void *inst, tcb_t *running)
{
    (void)inst;
    (void)running;
    return 0;
}

static void *fcfs_init(int q)
{
    (void)q;
    return calloc(1, sizeof(fcfs_instance));
}

static void fcfs_destroy(void *inst)
{
    free(inst);
}

const sched_policy_t fcfs_policy = {
    .enqueue = fcfs_enqueue,
    .pick_next = fcfs_pick_next,
    .should_preempt = fcfs_should_preempt,
    .init = fcfs_init,
    .destroy = fcfs_destroy,
};

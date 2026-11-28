#include <stddef.h>
#include <stdlib.h>
#include "uthread_internal.h"
#include "queue.h"

#define MLFQ_LEVELS 3
#define ALLOTMENT_SLICES 2
#define BOOST_QUANTA 50

typedef struct {
    queue_t levels[MLFQ_LEVELS];
    long quantum_us;
    long last_boost_us;
    unsigned long boost_epoch;
} mlfq_instance;

static int slice_ticks(int level)
{
    return 1 << level;
}

static long allotment_us(const mlfq_instance *instance, int level)
{
    return ALLOTMENT_SLICES * slice_ticks(level) * instance->quantum_us - instance->quantum_us / 10;
}

static void reset_to_top(const mlfq_instance *instance, tcb_t *t)
{
    t->level = 0;
    t->level_used_us = 0;
    t->boost_epoch = instance->boost_epoch;
}

static void boost(mlfq_instance *instance)
{
    instance->boost_epoch++;
    for (int i = 0; i < MLFQ_LEVELS; i++) {
        int n = instance->levels[i].count;
        for (int j = 0; j < n; j++) {
            tcb_t *t = queue_pop(&instance->levels[i]);
            reset_to_top(instance, t);
            queue_push(&instance->levels[0], t);
        }
    }
}

static void *mlfq_init(int quantum_ms)
{
    mlfq_instance *instance = calloc(1, sizeof *instance);
    if (!instance)
        return NULL;
    instance->quantum_us = quantum_ms * 1000L;
    instance->last_boost_us = now_mono_us();
    return instance;
}

static void mlfq_destroy(void *inst)
{
    free(inst);
}

static void mlfq_enqueue(void *inst, tcb_t *t)
{
    mlfq_instance *instance = inst;
    if (t->boost_epoch != instance->boost_epoch) {
        reset_to_top(instance, t);
    } else if (t->level_used_us >= allotment_us(instance, t->level)) {
        if (t->level < MLFQ_LEVELS - 1)
            t->level++;
        t->level_used_us = 0;
    }
    queue_push(&instance->levels[t->level], t);
}

static tcb_t *mlfq_pick_next(void *inst)
{
    mlfq_instance *instance = inst;
    long now = now_mono_us();
    if (now - instance->last_boost_us >= BOOST_QUANTA * instance->quantum_us) {
        boost(instance);
        instance->last_boost_us = now;
    }

    for (int i = 0; i < MLFQ_LEVELS; i++) {
        tcb_t *next = queue_pop(&instance->levels[i]);
        if (next != NULL) {
            next->slice_ticks = 0;
            return next;
        }
    }
    return NULL;
}

static int mlfq_should_preempt(void *inst, tcb_t *running)
{
    mlfq_instance *instance = inst;
    for (int i = 0; i < running->level; i++) {
        if (instance->levels[i].count > 0)
            return 1;
    }
    return ++running->slice_ticks >= slice_ticks(running->level);
}

const sched_policy_t mlfq_policy = {
    .enqueue = mlfq_enqueue,
    .pick_next = mlfq_pick_next,
    .should_preempt = mlfq_should_preempt,
    .init = mlfq_init,
    .destroy = mlfq_destroy,
};

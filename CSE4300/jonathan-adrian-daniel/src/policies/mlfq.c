#include <stddef.h>
#include "uthread_internal.h"
#include "queue.h"

#define MLFQ_LEVELS 3
#define ALLOTMENT_SLICES 2
#define BOOST_QUANTA 50

static queue_t levels[MLFQ_LEVELS];
static long quantum_us;
static long last_boost_us;
static unsigned long boost_epoch;

static int slice_ticks(int level)
{
    return 1 << level;
}

static long allotment_us(int level)
{
    return ALLOTMENT_SLICES * slice_ticks(level) * quantum_us - quantum_us / 10;
}

static void reset_to_top(tcb_t *t)
{
    t->level = 0;
    t->level_used_us = 0;
    t->boost_epoch = boost_epoch;
}

static void boost(void)
{
    boost_epoch++;
    for (int i = 0; i < MLFQ_LEVELS; i++) {
        int n = levels[i].count;
        for (int j = 0; j < n; j++) {
            tcb_t *t = queue_pop(&levels[i]);
            reset_to_top(t);
            queue_push(&levels[0], t);
        }
    }
}

static void mlfq_init(int quantum_ms)
{
    quantum_us = quantum_ms * 1000L;
    last_boost_us = now_us();
}

static void mlfq_enqueue(tcb_t *t)
{
    if (t->boost_epoch != boost_epoch) {
        reset_to_top(t);
    } else if (t->level_used_us >= allotment_us(t->level)) {
        if (t->level < MLFQ_LEVELS - 1)
            t->level++;
        t->level_used_us = 0;
    }
    queue_push(&levels[t->level], t);
}

static tcb_t *mlfq_pick_next(void)
{
    long now = now_us();
    if (now - last_boost_us >= BOOST_QUANTA * quantum_us) {
        boost();
        last_boost_us = now;
    }

    for (int i = 0; i < MLFQ_LEVELS; i++) {
        tcb_t *next = queue_pop(&levels[i]);
        if (next != NULL) {
            next->slice_ticks = 0;
            return next;
        }
    }
    return NULL;
}

static int mlfq_should_preempt(tcb_t *running)
{
    for (int i = 0; i < running->level; i++) {
        if (levels[i].count > 0)
            return 1;
    }
    return ++running->slice_ticks >= slice_ticks(running->level);
}

sched_policy_t mlfq_policy = {
    .enqueue = mlfq_enqueue,
    .pick_next = mlfq_pick_next,
    .should_preempt = mlfq_should_preempt,
    .init = mlfq_init,
};

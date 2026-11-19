#include <stddef.h>
#include "uthread_internal.h"
#include "queue.h"

#define MLFQ_LEVELS 3

static queue_t levels[MLFQ_LEVELS];
static tcb_t* previous_pick = NULL;
static int previous_level = -1;
static void mlfq_enqueue(tcb_t *t)
{
    int level = 0;
    if (t == previous_pick){
        //We know that the thread had been pulled from the queues before, if its being re-enqueued,
        //then we know that it had not finished its work in one time slice, move down a level if possible
        level = previous_level >= MLFQ_LEVELS-1 ? previous_level: previous_level+ 1;
    }
    queue_push(&levels[level], t);
}

static tcb_t *mlfq_pick_next(void)
{
    //Iterate through all levels
    for (int i = 0; i < MLFQ_LEVELS; i++){
        //Try to get element in current level of the queue.
        tcb_t* next = queue_pop(&levels[i]);

        //if there is an element to be retrieved from this upper level, return it, otherwise try the next level
        if (next != NULL){
            return next;
        }
    }
    //None of the levels contain any threads, return NULL.
    return NULL;
}

static int mlfq_should_preempt(tcb_t *running)
{
    return 1;
}

sched_policy_t mlfq_policy = {
    .enqueue = mlfq_enqueue,
    .pick_next = mlfq_pick_next,
    .should_preempt = mlfq_should_preempt,
};

#include <stddef.h>
#include "queue.h"

int queue_push(queue_t *q, tcb_t *t)
{
    if (q->count == MAX_THREADS)
        return -1;

    q->items[q->tail] = t;
    q->tail = (q->tail + 1) % MAX_THREADS;
    q->count++;
    return 0;
}

tcb_t *queue_pop(queue_t *q)
{
    if (q->count == 0)
        return NULL;

    tcb_t *t = q->items[q->head];
    q->head = (q->head + 1) % MAX_THREADS;
    q->count--;
    return t;
}

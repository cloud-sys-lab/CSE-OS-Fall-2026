#include <stddef.h>
#include "uthread_internal.h"

/*
 * Min-heap keyed on expected_ms. Ties are broken by insertion order
 * (seq) so jobs with equal expected runtimes run FIFO.
 */
typedef struct {
    tcb_t *t;
    unsigned long seq;
} heap_entry_t;

static heap_entry_t heap[MAX_THREADS];
static int count;
static unsigned long next_seq;

static int entry_less(const heap_entry_t *a, const heap_entry_t *b)
{
    if (a->t->expected_ms != b->t->expected_ms) {
        return a->t->expected_ms < b->t->expected_ms;
    }
    return a->seq < b->seq;
}

static void swap(int i, int j)
{
    heap_entry_t tmp = heap[i];
    heap[i] = heap[j];
    heap[j] = tmp;
}

static void sift_up(int i)
{
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!entry_less(&heap[i], &heap[parent])) {
            break;
        }
        swap(i, parent);
        i = parent;
    }
}

static void sift_down(int i)
{
    for (;;) {
        int left = 2 * i + 1;
        int right = left + 1;
        int smallest = i;

        if (left < count && entry_less(&heap[left], &heap[smallest])) {
            smallest = left;
        }
        if (right < count && entry_less(&heap[right], &heap[smallest])) {
            smallest = right;
        }
        if (smallest == i) {
            break;
        }
        swap(i, smallest);
        i = smallest;
    }
}

static void sjf_enqueue(tcb_t *t)
{
    if (count >= MAX_THREADS) {
        return -1;
    }
    heap[count].t = t;
    heap[count].seq = next_seq++;
    sift_up(count);
    count++;
}

static tcb_t *sjf_pick_next(void)
{
    if (count == 0) {
        return NULL;
    }

    tcb_t *t = heap[0].t;
    count--;
    if (count > 0) {
        heap[0] = heap[count];
        sift_down(0);
    }
    return t;
}

static int sjf_should_preempt(tcb_t *running)
{
    return 0;
}

sched_policy_t sjf_policy = {
    .enqueue = sjf_enqueue,
    .pick_next = sjf_pick_next,
    .should_preempt = sjf_should_preempt,
};

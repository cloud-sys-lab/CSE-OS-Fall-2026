#include <stddef.h>
#include "heap.h"

static int entry_less(const heap_entry_t *a, const heap_entry_t *b)
{
    if (a->t->expected_ms != b->t->expected_ms)
        return a->t->expected_ms < b->t->expected_ms;
    return a->seq < b->seq;
}

static void swap(heap_t *h, int i, int j)
{
    heap_entry_t tmp = h->items[i];
    h->items[i] = h->items[j];
    h->items[j] = tmp;
}

static void sift_up(heap_t *h, int i)
{
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (!entry_less(&h->items[i], &h->items[parent]))
            break;
        swap(h, i, parent);
        i = parent;
    }
}

static void sift_down(heap_t *h, int i)
{
    for (;;) {
        int left = 2 * i + 1;
        int right = left + 1;
        int smallest = i;

        if (left < h->count && entry_less(&h->items[left], &h->items[smallest]))
            smallest = left;
        if (right < h->count && entry_less(&h->items[right], &h->items[smallest]))
            smallest = right;
        if (smallest == i)
            break;
        swap(h, i, smallest);
        i = smallest;
    }
}

int heap_push(heap_t *h, tcb_t *t)
{
    if (h->count == MAX_THREADS)
        return -1;

    h->items[h->count].t = t;
    h->items[h->count].seq = h->next_seq++;
    sift_up(h, h->count);
    h->count++;
    return 0;
}

tcb_t *heap_pop(heap_t *h)
{
    if (h->count == 0)
        return NULL;

    tcb_t *t = h->items[0].t;
    h->count--;
    if (h->count > 0) {
        h->items[0] = h->items[h->count];
        sift_down(h, 0);
    }
    return t;
}

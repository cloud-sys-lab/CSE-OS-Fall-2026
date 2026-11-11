#include <stdio.h>

#include "../../include/queue.h"
#include "../../include/scheduler.h"

static void sort_by_arrival(Process procs[], int n)
{
    int i;

    for (i = 1; i < n; i++) {
        Process key = procs[i];
        int j = i - 1;

        while (j >= 0 && procs[j].arrival_time > key.arrival_time) {
            procs[j + 1] = procs[j];
            j--;
        }
        procs[j + 1] = key;
    }
}

static void admit_arrivals(Process procs[], int n, int admitted[],
                           Queue *ready, int clock)
{
    int i;

    for (i = 0; i < n; i++) {
        if (!admitted[i] && procs[i].arrival_time <= clock) {
            admitted[i] = 1;
            queue_enqueue(ready, &procs[i]);
        }
    }
}

static int next_unadmitted(Process procs[], int n, const int admitted[])
{
    int i;
    int next = -1;

    for (i = 0; i < n; i++) {
        if (!admitted[i] &&
            (next == -1 || procs[i].arrival_time < procs[next].arrival_time)) {
            next = i;
        }
    }
    return next;
}

void run_rr(Process procs[], int n, int quantum)
{
    Queue ready;
    int admitted[n];
    int clock = 0;
    int completed = 0;
    int i;

    if (n <= 0) {
        return;
    }
    if (quantum <= 0) {
        fprintf(stderr, "[rr] quantum must be greater than 0\n");
        return;
    }

    /* keep same-arrival jobs in stable arrival order when admitted. */
    sort_by_arrival(procs, n);

    if (queue_init(&ready, n) != 0) {
        fprintf(stderr, "[rr] failed to init ready queue\n");
        return;
    }

    for (i = 0; i < n; i++) {
        admitted[i] = 0;
    }

    admit_arrivals(procs, n, admitted, &ready, clock);

    while (completed < n) {
        Process *p;
        int slice;

        if (queue_is_empty(&ready)) {
            int next = next_unadmitted(procs, n, admitted);

            if (next < 0) {
                break;
            }
            clock = procs[next].arrival_time;
            admit_arrivals(procs, n, admitted, &ready, clock);
            continue;
        }

        p = queue_dequeue(&ready);

        if (p->start_time == -1) {
            p->start_time = clock;
        }

        slice = (p->remaining_time < quantum) ? p->remaining_time : quantum;
        clock += slice;
        p->remaining_time -= slice;

        /* new arrivals go before the preempted job is re-queued. */
        admit_arrivals(procs, n, admitted, &ready, clock);

        if (p->remaining_time > 0) {
            queue_enqueue(&ready, p);
        } else {
            p->finish_time = clock;
            completed++;
        }
    }

    queue_destroy(&ready);
}

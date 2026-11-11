#include <stdio.h>

#include "../../include/scheduler.h"

/*Non-preemptive SJF: once a job starts, it runs until it is completed, 
choosing the job with the shortest burst time, instead of running 1 tick 
at a time and repicking the shortest remianing job each tick*/
void run_sjf(Process procs[], int n, int quantum)
{
    (void)quantum;
 
    if (n <= 0)
        return;
 
    int clock = 0;
    int completed = 0;
 
    while (completed < n) {
        int best = -1;
        int next_arrival = -1;
 
        for (int i = 0; i < n; i++) {
            if (procs[i].remaining_time <= 0)
                continue;
 
            if (procs[i].arrival_time > clock) {
                if (next_arrival == -1 || procs[i].arrival_time < next_arrival)
                    next_arrival = procs[i].arrival_time;
                continue;
            }
 
            if (best == -1 ||
                procs[i].burst_time < procs[best].burst_time ||
                (procs[i].burst_time == procs[best].burst_time &&
                 procs[i].arrival_time < procs[best].arrival_time))
                best = i;
        }
 
        if (best == -1) {
            // CPU idle: jump to the next arrival
            clock = next_arrival;
            continue;
        }
 
        procs[best].start_time = clock;
        clock += procs[best].burst_time;
        procs[best].remaining_time = 0;
        procs[best].finish_time = clock;
        completed++;
    }
}
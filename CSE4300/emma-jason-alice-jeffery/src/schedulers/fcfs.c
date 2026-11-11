#include <stdio.h>

#include "../../include/scheduler.h"

void run_fcfs(Process procs[], int n, int quantum)
{
    (void)quantum;    
    
    if (n <= 0)
	    return;

    for (int i = 1; i < n; i++) {
	    Process key = procs[i];
	    int j = i - 1;
	    while (j >= 0 && procs[j].arrival_time > key.arrival_time) {
		    procs[j + 1] = procs[j];
		    j--;
	    }
	    procs[j + 1] = key;
    }
    int clock = 0;
    for (int i = 0; i < n; i++) {
	    if (clock < procs[i].arrival_time)
		    clock = procs[i].arrival_time;
	    procs[i].start_time = clock;
	    clock += procs[i].burst_time;
	    procs[i].remaining_time = 0;
	    procs[i].finish_time = clock;
    }
}

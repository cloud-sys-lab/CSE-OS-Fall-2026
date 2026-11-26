#include <stdio.h>

#include "jobs.h"
#include "queue.h"
//----------------------------------------------------------
/* Phase 1: Verify that jobs can be created, stored in a FIFO queue, and retrieved correctly
    - Verify the queue logic first before implementing  worker threads, mutexes, semaphores, or condition variables */
//----------------------------------------------------------

int main(){
    // Create an empty queue
    JobQueue queue;
    init_queue(&queue);

    // Create and insert jobs
    enqueue(&queue, create_job(1, 1000));
    enqueue(&queue, create_job(2, 2000));
    enqueue(&queue, create_job(3, 3000));

    Job job;
    // Repeatedly remove jobs until no jobs remain in the queue
    while (dequeue(&queue, &job)){
        print_job(&job);
    }

    printf("Phase 1: Test complete.\n");

    return 0;
}
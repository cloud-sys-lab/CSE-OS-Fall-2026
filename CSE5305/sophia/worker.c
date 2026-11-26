#include <stdio.h>
#include <unistd.h>

#include "worker.h"
#include "queue.h"
#include "jobs.h"
//----------------------------------------------------------

// Worker_thread main loop
/* Continuously checks the shared queue for available jobs, executes any jobs
        it receives, and terminates when the scheduler signals shutdown */
void *worker_thread(void *arg){
    // Convert the generic thread argument back into a WorkerArgs structure
    WorkerArgs *args = (WorkerArgs *)arg;

    int worker_id = args->worker_id;

    // Local pointer to the shared job queue
    JobQueue *queue = args->queue;
    Job job;

    printf("Worker %d started.\n", worker_id);

    // Continue running until the main thread requests shutdown
    while (!(*args->shutdown)){
        // Attempt to retrieve the next available job from the shared queue
        if (dequeue(queue, &job)){
            // Print confirmation that the worker thread has successfully been created
            printf("Worker %d started Job %d (size=%d)\n", worker_id, job.job_id, job.work_size);

            // Perform  CPU work associated with current job
            execute_job(&job);

            printf("Worker %d completed Job %d\n", worker_id, job.job_id);
        }
        
        else{
            // No job currently available
            //  - Sleep briefly before retrying; reduces CPU usage instead of continuously checking the queue
            usleep(1000);
        }
    }

    printf("Worker %d shutting down.\n", worker_id);

    return NULL;
}
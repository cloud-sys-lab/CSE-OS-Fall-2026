#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include "worker.h"
#include "queue.h"
#include "jobs.h"
//----------------------------------------------------------
// SEMAPHORE-Version
//----------------------------------------------------------
/* Worker Synchronization Strategy:
    - Workers block on a semaphore when no jobs are available
    - enqueue() performs sem_post() whenever a new job is added
    - sem_wait() wakes a worker when work becomes available
    - A mutex protects queue modifications during dequeue operations
    - Jobs are executed outside the mutex to maximize concurrency
    - Workers terminate when the shutdown flag is set */

// Semaphore behavior:
//  - enqueue() increases the semaphore count via sem_post()
//  - sem_wait() decreases the count/grants permission to process one available job
//----------------------------------------------------------

// Worker_thread main loop
/* Continuously checks the shared queue for available jobs, executes any jobs
        it receives, and terminates when the scheduler signals shutdown */
void *worker_thread(void *arg){
    // Convert the generic thread argument back into a WorkerArgs structure
    WorkerArgs *args = (WorkerArgs *)arg;

    int worker_id = args->worker_id;

    // Local reference to the shared queue used by all worker threads
    JobQueue *queue = args->queue;
    Job job;

    printf("Worker %d started.\n", worker_id);

    // Continue running until the main thread requests shutdown
    while (!(*args->shutdown)){
        // Wait until the semaphore indicates a job is available
        // The worker sleeps here instead of continuously polling the queue
        sem_wait(&queue->jobs_available);

        // The worker may have been awakened solely for shutdown
        // Exit without attempting to process another job
        if (*args->shutdown){
            break;
        }

        pthread_mutex_lock(&queue->mutex);

        // Safely remove a job while already holding the queue mutex
        // If no work is available, release the mutex and continue
        if (!dequeue_locked(queue, &job)){
            pthread_mutex_unlock(&queue->mutex);
            // No job was available after acquiring the mutex
            continue;
        }

        // Release the queue mutex before executing the job so other workers can access the queue concurrently
        pthread_mutex_unlock(&queue->mutex);

        printf("Worker %d started Job %d\n", worker_id, job.job_id);

        // Capture the timestamp immediately before execution begins
        struct timeval start;
        struct timeval end;
        gettimeofday(&start, NULL);

        execute_job(&job);

        // Capture the timestamp after job completion
        gettimeofday(&end, NULL);

        // Compute job execution time in milliseconds
        long elapsed_ms = (end.tv_sec - start.tv_sec) * 1000 + (end.tv_usec - start.tv_usec) / 1000;
        // Update shared scheduler statistics with this job's runtime
        record_job_completion(args->stats, elapsed_ms); // Save time

        printf("Worker %d completed Job %d\n", worker_id, job.job_id);
    }

    // Worker has exited its processing loop and is terminating cleanly
    printf("Worker %d shutting down.\n", worker_id);

    return NULL;
}
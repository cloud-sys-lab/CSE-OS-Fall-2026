#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

#include "../shared/jobs.h"
#include "../shared/statistics.h"

#include "worker.h"
#include "queue.h"
//----------------------------------------------------------
// CV-Version
//----------------------------------------------------------
/* Worker Synchronization Strategy:
    - Workers lock the queue mutex before inspecting queue state
    - If no jobs are available, workers sleep on a condition variable
    - enqueue() wakes workers whenever new jobs arrive
    - Jobs are processed outside the mutex to maximize concurrency
    - Workers terminate when the shared shutdown flag is set */

// Condition-variable implementation uses mutex protects the queue, not the job execution itself
// Release the queue mutex before executing the job so other workers can continue dequeuing/processing work
// Holding the mutex during execution forces tasks to run one at a time, eliminating speed benefits of running things in parallel
//----------------------------------------------------------

// WorkerArgs Data Structure
//  - Bundles together all shared resources and configuration required by a worker thread
//  - Passed to worker_thread() when the thread is created
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
        pthread_mutex_lock(&queue->mutex);

        // If no jobs are available, release mutex lock and sleep
        // pthread_cond_wait() automatically reacquires mutex lock before returning
        while (queue->count == 0 && !(*args->shutdown)){
            pthread_cond_wait(&queue->jobs_available, &queue->mutex);
        }

        // The worker may have been awakened solely for shutdown
        // Exit without attempting to process another job
        if (*args->shutdown){
            pthread_mutex_unlock(&queue->mutex);
            break;
        }

        // Safely remove a job while the mutex is already held
        // If no job is available, release the mutex and retry
        if (!dequeue_locked(queue, &job)){
            pthread_mutex_unlock(&queue->mutex);
            continue;
        }

        // Release the queue mutex before executing the job so other workers can access the queue concurrently
        pthread_mutex_unlock(&queue->mutex);

        // Begin processing retrieved job
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
        record_job_completion(args->stats, elapsed_ms);

        printf("Worker %d completed Job %d\n", worker_id, job.job_id);
    }

    // Worker has exited its processing loop and is terminating cleanly
    printf("Worker %d shutting down.\n", worker_id);

    return NULL;
}
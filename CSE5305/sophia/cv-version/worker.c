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
        pthread_mutex_lock(&queue->mutex);

        while (queue->count == 0 && !(*args->shutdown)){
            pthread_cond_wait(&queue->jobs_available, &queue->mutex);
        }

        // Shutdown may have occurred while this worker was sleeping.
        if (*args->shutdown){
            pthread_mutex_unlock(&queue->mutex);
            break;
        }

        // dequeue_locked(queue, &job);
        // pthread_mutex_unlock(&queue->mutex);

        // Protective measure ???
        if (!dequeue_locked(queue, &job)){
            pthread_mutex_unlock(&queue->mutex);
            continue;
        }

        pthread_mutex_unlock(&queue->mutex);
        
        printf("Worker %d started Job %d\n", worker_id, job.job_id);

        execute_job(&job);

        printf("Worker %d completed Job %d\n", worker_id, job.job_id);
    }

    printf("Worker %d shutting down.\n", worker_id);

    return NULL;
}
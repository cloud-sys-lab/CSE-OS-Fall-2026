#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "jobs.h"
#include "queue.h"
#include "worker.h"
//----------------------------------------------------------
/* Phase 4: 
    - Implement a semaphore-based thread pool
    - Worker threads retrieve jobs from a shared queue
    - Mutexes protect shared queue data
    - Semaphores block workers when no jobs are available and wake workers when new jobs are submitted */
//----------------------------------------------------------

int main()
{
    JobQueue queue;

    init_queue(&queue);

    // Create and insert jobs into queue
    enqueue(&queue, create_job(1, 5000000));
    enqueue(&queue, create_job(2, 10000000));
    enqueue(&queue, create_job(3, 15000000));
    enqueue(&queue, create_job(4, 20000000));
    enqueue(&queue, create_job(5, 25000000));

    // Shutdown flag shared by workers
    int shutdown = 0;

    // Thread handling
    pthread_t worker1;
    pthread_t worker2;

    // Arguments passed to workers
    WorkerArgs args1;
    WorkerArgs args2;

    args1.worker_id = 1;
    args1.queue = &queue;
    args1.shutdown = &shutdown;

    args2.worker_id = 2;
    args2.queue = &queue;
    args2.shutdown = &shutdown;

    // Create worker threads
    pthread_create(
        &worker1,
        NULL,
        worker_thread,
        &args1
    );

    pthread_create(
        &worker2,
        NULL,
        worker_thread,
        &args2
    );

    // Allow workers time to process jobs before shutting them down
    //  - This is temporary and will be replaced by proper synchronization in later phases
    sleep(2);

    // Signal workers to stop/shutdown
    shutdown = 1;

    // Wake all sleeping workers so they can notice shutdown and exit
    pthread_mutex_lock(&queue.mutex);

    sem_post(&queue.jobs_available);
    sem_post(&queue.jobs_available);

    pthread_mutex_unlock(&queue.mutex);

    // Wait for workers to exit.
    pthread_join(worker1, NULL);
    pthread_join(worker2, NULL);

    printf("All jobs complete.\n");

    return 0;
}
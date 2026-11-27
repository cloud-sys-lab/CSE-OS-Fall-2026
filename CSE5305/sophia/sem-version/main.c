#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "jobs.h"
#include "queue.h"
#include "worker.h"
//----------------------------------------------------------
// SEMAPHORE-Version
//----------------------------------------------------------
/* Program Flow:
    1. Initialize statistics and shared queue
    2. Enqueue jobs
    3. Create worker threads
    4. Workers process jobs concurrently
    5. Main thread waits for work completion
    6. Signal worker shutdown
    7. Join worker threads
    8. Print performance statistics */
//----------------------------------------------------------

int main()
{
    // Create stats object
    Statistics stats;
    init_statistics(&stats);

    // Create queue object
    JobQueue queue;
    init_queue(&queue);

    // Create sample jobs and place them into the shared queue
    // Jobs have increasing workloads to simulate varying execution times
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
    // Both workers share the same statistics object so completed-job metrics can be collected across all threads
    args1.stats = &stats;

    args2.worker_id = 2;
    args2.queue = &queue;
    args2.shutdown = &shutdown;
    // Both workers share the same statistics object so completed-job metrics can be collected across all threads
    args2.stats = &stats;

    // Create worker threads (2)
    pthread_create(&worker1, NULL, worker_thread, &args1);
    pthread_create(&worker2, NULL, worker_thread, &args2);

    // Wait until all jobs have been dequeued by worker threads
    // Simple polling loop is used here instead of an additional synchronization mechanism to keep this experiment simple
    while (queue.count > 0){ // queue.count = zero means all jobs have been dequeued, but worker threads may still be executing their final jobs
        usleep(10000);
    }

    // Set the shared shutdown flag so workers exit their processing loops when all work has been dispatched
    shutdown = 1;

    // Wake all sleeping workers so they can notice shutdown and exit
    pthread_mutex_lock(&queue.mutex);

    // Wake both worker threads
    // Each sem_post() releases one worker blocked in sem_wait() so it can notice the shutdown flag and exit
    sem_post(&queue.jobs_available);
    sem_post(&queue.jobs_available);

    pthread_mutex_unlock(&queue.mutex);

    // Wait for workers to exit
    pthread_join(worker1, NULL);
    pthread_join(worker2, NULL);

    // Print statistics report at shutdown
    print_statistics(&stats);

    printf("All jobs complete.\n");

    return 0;
}
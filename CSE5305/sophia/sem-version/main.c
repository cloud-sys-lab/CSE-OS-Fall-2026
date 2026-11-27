#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#include "../shared/workloads.h"
#include "../shared/jobs.h"
#include "../shared/statistics.h"

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
    WorkloadJob jobs[MAX_WORKLOAD_JOBS];
    int count = create_workload(jobs, WORKLOAD_BALANCED);
    for(int i = 0; i < count; i++){
        enqueue(&queue, create_job(jobs[i].id, jobs[i].size));
    }

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

    // Timestamps used to measure overall scheduler execution time (wall-clock time from scheduler start until all workers terminate)
    struct timeval scheduler_start;
    struct timeval scheduler_end;
    // Start wall-clock timer immediately before worker threads begin processing jobs
    // This captures the total elapsed time experienced by the scheduler
    gettimeofday(&scheduler_start, NULL);

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

    // Stop the wall-clock timer after all worker threads have finished and scheduler has completely shut down
    gettimeofday(&scheduler_end, NULL);
    // Compute total scheduler wall-clock runtime in milliseconds
    // Unlike total_job_runtime, this measures actual elapsed time from scheduler startup to scheduler shutdown, 
    //      including all thread execution and synchronization overhead
    stats.wall_clock_time_ms =
        (scheduler_end.tv_sec - scheduler_start.tv_sec) * 1000 +
        (scheduler_end.tv_usec - scheduler_start.tv_usec) / 1000;

    // Print statistics report at shutdown
    print_statistics(&stats);

    printf("All jobs complete.\n");

    return 0;
}
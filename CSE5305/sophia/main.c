#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "jobs.h"
#include "queue.h"
#include "worker.h"
//----------------------------------------------------------
/* Phase 2: 
    - Create worker threads and allow them to retrieve jobs from the shared queue. 
    - Add Mutex Lock protection for when threads access/modify shared queue data.
    - No synchronization primitives have been added yet. The goal is simply to verify the worker-thread workflow. */
//----------------------------------------------------------

int main()
{
    JobQueue queue;

    init_queue(&queue);

    // Create and insert jobs into queue
    enqueue(&queue, create_job(1, 1000));
    enqueue(&queue, create_job(2, 2000));
    enqueue(&queue, create_job(3, 3000));
    enqueue(&queue, create_job(4, 4000));
    enqueue(&queue, create_job(5, 5000));

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

    // Wait for workers to exit.
    pthread_join(worker1, NULL);
    pthread_join(worker2, NULL);

    printf("Phase 2A complete.\n");

    return 0;
}
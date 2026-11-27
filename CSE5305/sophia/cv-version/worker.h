#ifndef WORKER_H
#define WORKER_H

#include <pthread.h>

#include "queue.h"
#include "jobs.h"
#include "statistics.h"
//----------------------------------------------------------
// CV-Version
//----------------------------------------------------------

// WorkerArgs Data Structure
//  - Bundles together all shared resources and configuration required by a worker thread
//  - Passed to worker_thread() when the thread is created
typedef struct{
    int worker_id; // Unique identifier for the worker thread
    JobQueue *queue; // Shared job queue accessed by all workers

    // Shared shutdown flag set by the main thread when workers should exit
    int *shutdown;

    // Shared statistics object used to track completed jobs and execution times
    Statistics *stats;
} WorkerArgs;

//----------------------------------------------------------

// Main execution routine for a worker thread in the thread pool
// Continuously waits for work, processes jobs, updates statistics, and exits when shutdown is requested
void *worker_thread(void *arg);

#endif

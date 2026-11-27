#ifndef WORKER_H
#define WORKER_H

#include <pthread.h>

#include "queue.h"
#include "jobs.h"
//----------------------------------------------------------

// WorkerArgs Data Structure
//  - Stores information that must be passed to each worker thread when the thread is created
//  - Bundle of arguments passed to a worker thread
typedef struct{
    int worker_id;
    JobQueue *queue;

    // Shared flag used to tell workers when program should terminate
    int *shutdown;

} WorkerArgs;

//----------------------------------------------------------

// Main execution loop for a worker thread in the thread pool
void *worker_thread(void *arg);

#endif

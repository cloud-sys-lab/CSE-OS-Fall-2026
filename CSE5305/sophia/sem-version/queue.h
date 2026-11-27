#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include <semaphore.h>

#include "../shared/jobs.h"

// Maximum number of jobs that can be stored in the queue
#define MAX_QUEUE_SIZE 100
//----------------------------------------------------------
// SEMAPHORE-Version
//----------------------------------------------------------

// JobQueue Data Structure
/* Circular Job Queue:
        - A fixed-capacity FIFO queue where front/rear wrap around to the beginning of the array when they reach the end
        - Existing jobs are not overwritten
        - If the queue becomes full, enqueue() fails and returns 0. */

typedef struct {
    Job jobs[MAX_QUEUE_SIZE]; // Fixed-size array storing queued jobs

    int front; // Index of next job to remove
    int rear; // Index of next position to insert
    int count; // Current number of jobs stored

    // Mutex Lock: Protects shared queue data from concurrent access by multiple threads
    pthread_mutex_t mutex;

    // Semaphore: Tracks the number of jobs currently available in the queue.
    //  - Workers block on sem_wait() when no jobs are available and wake when enqueue() performs sem_post().
    sem_t jobs_available;

} JobQueue;

//----------------------------------------------------------

// Initialize queue
void init_queue(JobQueue *queue);

// Add a job to the queue
/* Returns:
    - 1 = success
    - 0 = queue full */
int enqueue(JobQueue *queue, Job job);

// Remove a job from the queue
/* Returns:
    - 1 = success
    - 0 = queue empty */
int dequeue(JobQueue *queue, Job *job);

// Check if the queue is empty: Returns 1 if queue is empty
int queue_empty(JobQueue *queue);

// Checks if queue is full: Returns 1 if queue is full
int queue_full(JobQueue *queue);

// Remove a job while assuming the caller already holds the queue mutex
// Used by worker threads after waking from the condition variable
int dequeue_locked(JobQueue *queue, Job *job);

#endif
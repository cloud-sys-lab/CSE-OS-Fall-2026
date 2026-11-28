#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>
#include "jobs.h"

// Maximum number of jobs that can be stored in the queue
#define MAX_QUEUE_SIZE 100
//----------------------------------------------------------

// JobQueue Data Structure
/* Circular Job Queue:
        - A fixed-capacity FIFO queue where front/rear wrap around to the beginning of the array when they reach the end
        - Existing jobs are not overwritten
        - If the queue becomes full, enqueue() fails and returns 0. */

typedef struct {
    Job jobs[MAX_QUEUE_SIZE];

    int front; // Index of next job to remove
    int rear; // Index of next position to insert
    int count; // Current number of jobs stored

    // Mutex Lock: Protects shared queue data from concurrent access by multiple threads
    pthread_mutex_t mutex;

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

#endif
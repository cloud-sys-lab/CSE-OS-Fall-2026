#include "queue.h"
//----------------------------------------------------------
// SEMAPHORE-Version
//----------------------------------------------------------
/* Queue Synchronization Strategy:
    - A mutex protects all shared queue data from concurrent access
    - A counting semaphore tracks how many jobs are currently available
    - enqueue() performs sem_post() whenever a new job is added
    - Worker threads call sem_wait() and sleep until work becomes available
    - dequeue_locked() removes jobs while assuming the caller already owns the mutex */
//----------------------------------------------------------

// Initialize an empty queue
//  - front = index of next item to remove
//  - rear = index of next position to insert a new item
//  - count = current number of jobs stored in queue
void init_queue(JobQueue *queue){
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;

    // Mutex:
    //  - Ensures only one thread can modify queue state at a time via aqcuisition of this lock
    //  - Protects front, rear, count, and job array contents
    pthread_mutex_init(&queue->mutex, NULL);

    // Semaphore starts at 0, as no worker should proceed until a job has been added to the queue via enqueue().
    sem_init(&queue->jobs_available, 0, 0);
}

// Helper function used when the queue mutex is already locked
// Avoids locking the mutex a second time inside worker_thread()
int dequeue_locked(JobQueue *queue, Job *job){
    // Defensive safety check: a worker may wake up and discover no work is available
    // Under normal semaphore operation, a successful sem_wait() should correspond to an available job, 
    //      but this prevents invalid removal if the queue state becomes inconsistent
    if (queue->count == 0){
        return 0;
    }

    *job = queue->jobs[queue->front];

    queue->front = (queue->front + 1) % MAX_QUEUE_SIZE;
    queue->count--;

    return 1;
}

// Check if queue is empty
/*  - Return 1 if no jobs are currently stored in the queue, 
    - otherwise return 0. */
int queue_empty(JobQueue *queue){
    return queue->count == 0;
}

// Check if queue is full
/*  - Return 1 if the queue has reached its max capacity, 
    - otherwise return 0. */
int queue_full(JobQueue *queue){
    return queue->count == MAX_QUEUE_SIZE;
}

// Insert a job at the rear of the queue
/* Returns:
    - 1 = success
    - 0 = queue is already full; failure */
int enqueue(JobQueue *queue, Job job){
    // Lock queue by acquiring mutex before checking or modifying shared queue state
    pthread_mutex_lock(&queue->mutex);
    if (queue_full(queue)){
        // Release Lock upon return
        pthread_mutex_unlock(&queue->mutex); 
        return 0;
    }

    queue->jobs[queue->rear] = job;
    // Move rear forward
    // Modulo allows the queue to wrap around as a circular buffer
    queue->rear = (queue->rear + 1) % MAX_QUEUE_SIZE;
    queue->count++; // Queue count tracks the number of jobs currently waiting for execution

    // Increment semaphore count to indicate a new job is available
    // If a worker is blocked in sem_wait(), one worker will be awakened
    sem_post(&queue->jobs_available);
    // Semaphore value conceptually matches the number of jobs available for workers to process. 
    // Each enqueue() increases the count, and each worker's sem_wait() decreases it

    // Release Lock upon return
    pthread_mutex_unlock(&queue->mutex);

    return 1;
}

// Remove the oldest job from the front of the queue (FIFO order)
/* Returns:
    - 1 = success
    - 0 = queue is empty; failure */
int dequeue(JobQueue *queue, Job *job){
    // Acquire Lock
    pthread_mutex_lock(&queue->mutex);
    if (queue_empty(queue)){
        // Release Lock upon return
        pthread_mutex_unlock(&queue->mutex); 
        return 0;
    }

    // Copy the job at the front of the queue into the caller's Job variable
    *job = queue->jobs[queue->front];
    queue->front = (queue->front + 1) % MAX_QUEUE_SIZE;
    queue->count--;

    // Release Lock upon return
    pthread_mutex_unlock(&queue->mutex); 
    return 1;
}
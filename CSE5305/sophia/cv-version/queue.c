#include "queue.h"
//----------------------------------------------------------
// CV-Version
//----------------------------------------------------------
/* Queue Synchronization Strategy:
    - A mutex protects all shared queue data
    - Workers wait on a condition variable when no jobs are available
    - enqueue() signals the condition variable whenever new work arrives
    - dequeue_locked() is used when the caller already owns the mutex */
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

    // Condition Variable:
    //  - Worker threads sleep here when the queue is empty
    //  - enqueue() signals waiting workers whenever a new job arrives
    pthread_cond_init(&queue->jobs_available, NULL);
}

// Helper function used when the queue mutex is already locked
// Avoids locking the mutex a second time inside worker_thread()
int dequeue_locked(JobQueue *queue, Job *job){
    // Defensive Safety check: a worker may wake up and discover no work is available
    // Return failure instead of removing an invalid job
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

    // Wake one waiting worker via CV signal so the newly added job can be processed
    pthread_cond_signal(&queue->jobs_available);

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
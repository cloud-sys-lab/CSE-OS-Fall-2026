#include "queue.h"
//----------------------------------------------------------

// Initialize an empty queue
/*  - front = index of next item to remove
    - rear = index of next position to insert a new item
    - count = current number of jobs stored in queue */

void init_queue(JobQueue *queue){
    queue->front = 0;
    queue->rear = 0;
    queue->count = 0;

    // Mutex Lock: Prevent multiple threads from accessing/modyfying the shared queue data at the same time
    pthread_mutex_init(&queue->mutex, NULL);
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
    // Acquire Lock
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
    queue->count++;

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
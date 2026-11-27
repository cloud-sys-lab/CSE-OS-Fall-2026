#include <stdio.h>
#include "statistics.h"
//----------------------------------------------------------
// Initializes shared statistics object
void init_statistics(Statistics *stats){
    stats->jobs_completed = 0; // No jobs have been completed yet
    stats->total_execution_time_ms = 0;
    stats->total_execution_time_ms = 0; // Total runtime starts at zero milliseconds

    // Initialize the mutex so multiple threads can safely update statistics without race conditions
    pthread_mutex_init(&stats->mutex, NULL);
}

// Records information about a completed job
void record_job_completion(Statistics *stats, long execution_time_ms){
    // Lock statistics structure before modifying it
    // Only one thread may enter this critical section at a time via ownership of the lock
    pthread_mutex_lock(&stats->mutex);

    stats->jobs_completed++; // Increase completed-job count
    stats->total_execution_time_ms += execution_time_ms; // Add this job's runtime to the running total

    // Release the lock so other worker threads may update the statistics structure
    pthread_mutex_unlock(&stats->mutex);
}

// Prints a summary of scheduler performance
void print_statistics(Statistics *stats){
    printf("\n===== Scheduler Statistics =====\n");
    printf("Jobs Completed: %d\n", stats->jobs_completed); // Total number of completed jobs
    printf("Total Job Runtime: %ld ms\n", stats->total_execution_time_ms); // Cumulative runtime of all jobs
    printf("Wall Clock Runtime: %ld ms\n", stats->wall_clock_time_ms);

    // Avoid division by zero
    if (stats->jobs_completed > 0){
        // Compute average runtime per job
        double avg = (double)stats->total_execution_time_ms / stats->jobs_completed;
        printf("Average Job Runtime: %.2f ms\n", avg);
    }

    printf("================================\n\n");
}
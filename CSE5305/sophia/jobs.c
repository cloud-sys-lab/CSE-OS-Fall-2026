#include <stdio.h>
#include "jobs.h"
//----------------------------------------------------------

// Create/initialize a new Job structure with a unique ID and amount of work to perform
Job create_job(int job_id, int work_size){
    Job job;

    job.job_id = job_id;
    job.work_size = work_size;

    return job;
}

//----------------------------------------------------------

// Simulates computational (CPU-bound) work via a loop and without using I/O
//  - The larger the work_size value, the longer the worker thread spends processing the job (more CPU time)
void execute_job(Job *job){
    
    volatile unsigned long result = 0; // Volatile prevents the compiler from removing the loop as an optimization.
    for (int i = 0; i < job->work_size; i++){
        result += (i * i) % 97;
    }

    (void)result; // prevent compiler optimizations (???)
}


// Print job information ("job ID | work size")
void print_job(const Job *job){
    printf("Job ID: %d | Work Size: %d\n", job->job_id, job->work_size);
}

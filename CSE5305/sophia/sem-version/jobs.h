#ifndef JOBS_H
#define JOBS_H
//----------------------------------------------------------

// Job Data Structure
//  - Represents a single unit of work that can be executed by a worker
typedef struct{
    int job_id;
    int work_size;
} Job;

//----------------------------------------------------------

// Create and initialize a Job structure with a unique ID and work size
Job create_job(int job_id, int work_size);

// Executes work associated with a job; simulate CPU work associated with the specified job.
void execute_job(Job *job);

// Print job info (Job ID and Work Size)
void print_job(const Job *job);

#endif
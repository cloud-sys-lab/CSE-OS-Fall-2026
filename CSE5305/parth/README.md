# CSE 5305 Project: Thread Visualizer
Parth Jalgaonkar

## Overview
The Thread Visualizer serves as a configurable testing ground for theoretical multi-threaded applications. It is a visual representation of how application-level threads work, and can demonstrate concepts such as mutual exclusion, race conditions, and sleeping. 

## Execution
Run:  
`python visualizer.py`  
  
No external packages are needed.

## Settings
- Threads: Number of worker threads
- Tasks: Number of tasks to complete
- Enable Locking: Uses a mutex lock to protect critical sections
- Sleep Inside Lock: Controls whether delay happens during critical section or not
- Shared Access %: Probability that a task requires a critical section (access the shared counter)
- Sleep %: Probability that a task voluntarily sleeps
- Shared Delay: Duration of the delay operation (in seconds)
- Seed: Allows reproducible "randomized" testing

## Files
`worker.py` - thread creation, workload generation, task execution, and synchronization  
`visualizer.py` - GUI creation, inputs, timeline visualization, and performance statistics
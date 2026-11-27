# User-level thread scheduler

CSE 4300 group project, Fall 2026. Jonathan, Adrian, and Dan.

Uthread is a small threading library written in C. A program creates threads, picks a scheduling policy, and the library runs those threads on a single CPU, switching between them on a timer. Everything happens inside one ordinary process. The kernel never knows our threads exist; it only sees one process that it interrupts every few milliseconds.

We implemented five scheduling policies behind one interface so the same program can be run under each of them and compared:

| Policy | Preemptive | Needs to know run times | Idea |
|---|---|---|---|
| FCFS | no | no | Run threads in the order they arrived |
| SJF | no | yes | Run the shortest job first |
| STCF | yes | yes | Always run the job with the least time left |
| RR | yes | no | Give everyone one time slice in turn |
| MLFQ | yes | no | Learn which threads are interactive and favor them |


## Building and running

Works on macOS and Linux. You need `cc` and `make`.

```
make          # build everything into build/
make test     # build and run every test in tests/
make clean
```


## Using the library

The whole public API is in `include/uthread.h`:

```c
int  uthread_init(policy_t policy, int quantum_ms);
int  uthread_create(void (*fn)(void *), void *arg, const uthread_attr_t *attr);
void uthread_yield(void);
void uthread_sleep(int ms);
void uthread_run(void);
```

For example:

```c
uthread_attr_t short_job = { .expected_ms = 50 };

uthread_init(POLICY_STCF, 10);
uthread_create(work, "a", NULL);
uthread_create(work, "b", &short_job);
uthread_run();
```

`uthread_init` picks the policy and the length of a time slice. `uthread_create` adds a thread, which ends when its function returns. `uthread_run` runs everything and returns once all the threads are done.

`expected_ms` is how much CPU time a thread is expected to need. Only SJF and STCF use it, and passing `NULL` is fine for the others.


## Project layout

```
include/uthread.h         public API
src/uthread_internal.h    thread control block and the policy interface
src/uthread.c             the runtime: threads, switching, timer, sleep
src/queue.c               FIFO queue
src/heap.c                min-heap used by SJF and STCF
src/policies/             one file per scheduling policy
tests/                    one program per test
```


## Design

### Threads

Each thread has a thread control block (`tcb_t`) holding its saved CPU context, its own 64 KB stack, the function it runs, and its state, plus a few fields that specific policies need.

```
            create
              |
              v
  +------> READY ------ picked ------> RUNNING ------ returns -----> DONE
  |          ^                            |
  |          +--- yield / preempted ------+
  |                                       |
  +------ wakes up ---- BLOCKED <--- uthread_sleep
```

### Switching between threads

Switching uses `swapcontext`, which saves the current registers and stack pointer for one thread and loads another's. It's ordinary user-space code, not a system call, so the kernel never sees the switch. All switching goes through one function, `schedule()`, which asks the policy who's next and switches to them.

### Preemption

At startup we ask the kernel to send our process a `SIGALRM` every quantum. The signal handler asks the policy whether the running thread should give up the CPU, and if so, puts it back in the ready queue and calls `schedule()`. When that thread gets picked again later, it picks up exactly where it was interrupted.

Since the timer can fire at any moment, including in the middle of the library's own code, the library blocks the signal while it's changing its queues and unblocks it afterward. A blocked signal isn't lost, just delayed until it's safe.

### Mechanism vs. policy

The runtime handles everything that works the same no matter which scheduler is running: creating threads, switching, the timer, sleeping, and tracking CPU time. A policy only decides which ready thread runs next. Each policy is a struct of function pointers, C's version of an abstract class:

```c
typedef struct {
    void   (*enqueue)(tcb_t *t);
    tcb_t *(*pick_next)(void);
    int    (*should_preempt)(tcb_t *running);
    void   (*init)(int quantum_ms);
} sched_policy_t;
```

Each policy keeps its own ready threads privately, so each one can use whatever data structure fits: a FIFO queue for FCFS and RR, a min-heap for SJF and STCF, and several queues for MLFQ.

### Sleeping

Sleeping threads go into a list kept by the runtime rather than the policy, since when a thread wakes up doesn't depend on the scheduler. Each timer tick moves threads whose time is up back to the policy. If every thread is asleep, the scheduler waits for the next tick.

### CPU time

When a thread stops running, the runtime charges it for the CPU time it used. STCF uses this to know how much time each thread has left, and MLFQ uses it to decide when a thread has used up its time on a level.


## The policies

**FCFS.** One FIFO queue, never preempts.

**SJF.** A min-heap ordered by `expected_ms`, never preempts. Ties go to whoever arrived first.

**STCF.** A min-heap ordered by time remaining. On every tick, the running thread goes back into the heap and whoever has the least time left runs next, so a shorter job that arrives later takes over.

**Round robin.** One FIFO queue. Every tick, the running thread goes to the back.

**MLFQ.** Three levels, each one a FIFO queue:

1. Higher levels always run first, and a thread arriving on a higher level preempts a lower one.
2. Threads on the same level take turns.
3. New threads start at the top.
4. Time slices get longer further down: 1, 2, and 4 quanta.
5. Once a thread has used a set amount of CPU time on a level, it moves down. Yielding or sleeping doesn't reset that, so a thread can't game its way into staying at the top.
6. Every 50 quanta, everything is boosted back to the top so long-running threads don't starve.


## Tests

| Test | What it checks |
|---|---|
| `test_basic` | Threads run and finish, and control returns to `main` |
| `test_yield` | Two yielding threads take turns |
| `test_preempt` | The timer takes the CPU away from a thread that never yields |
| `test_sleep` | Sleeping threads wake up on time, and the scheduler idles when everyone is asleep |
| `test_sjf` | Shortest job runs first, ties in arrival order |
| `test_stcf` | A short job arriving late preempts a long one, unless the long one is nearly done |
| `test_mlfq` | A new job runs ahead of a CPU-bound thread that's already been moved down |


## Benchmarks



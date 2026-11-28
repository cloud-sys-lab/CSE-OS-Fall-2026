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
make bench    # build and run the benchmarks
make clean
```


## Using the library

The public API is shared between `include/uthread.h` and `include/multi_core.h`:
##### include/uthread.h
```c
scheduler_t *uthread_init(policy_t policy, int quantum_ms);
void uthread_destroy(scheduler_t *s);
int  uthread_create(scheduler_t *s, void (*fn)(void *), void *arg, const uthread_attr_t *attr);
void uthread_yield(void);
void uthread_sleep(int ms);
void uthread_run(scheduler_t *s);
int  uthread_core_id(void);

int  uthread_get_stats(scheduler_t *s, int tid, uthread_stats_t *out);
long uthread_context_switches(scheduler_t *s);

```
##### include/multi_core.h
```c
int initialize_multi_core(policy_m_t policy);
int setCorePolicy(int coreID, policy_t newPolicy, int quantum);
int coreRun(int coreID);
int policyAddProcess(int coreID, void (*proc)(void *arg), void* arg, const uthread_attr_t *attr);
int multi_core_destroy();

```

### For example:

```c
uthread_attr_t short_job = { .expected_ms = 50 };

scheduler_t *s = uthread_init(POLICY_STCF, 10);
uthread_create(s, work, "a", NULL);
uthread_create(s, work, "b", &short_job);
uthread_run(s);
uthread_destroy(s);
```

`uthread_init` picks the policy and the length of a time slice, and returns a scheduler. `uthread_create` adds a thread to it, which ends when its function returns. `uthread_run` runs everything and returns once all the threads are done, and `uthread_destroy` frees the scheduler afterward.

`uthread_attr_t` has two optional settings. `expected_ms` is how much CPU time a thread is expected to need; only SJF and STCF use it. `start_delay_ms` holds a thread back for that long before it becomes ready, which is how the benchmarks simulate jobs arriving at different times. Passing `NULL` means no estimate and no delay.

After a run, `uthread_get_stats` gives each thread's arrival time, first run, finish time, and CPU time used, and `uthread_context_switches` gives the total number of switches.

To use more than one core, call `uthread_run` on the same scheduler from several pthreads. Each pthread acts as a core, and they all pull from the same ready threads. `uthread_core_id` tells a thread which core it's running on. Multi-core needs Linux (see [Multi-core](#multi-core)).


## Project layout

```
include/uthread.h         public API
src/uthread_internal.h    thread control block and the policy interface
src/uthread.c             the runtime: threads, switching, timer, sleep
src/queue.c               FIFO queue
src/heap.c                min-heap used by SJF and STCF
src/policies/             one file per scheduling policy
src/multi_core/           early multi-core wrapper, unused (see Multi-core)
bench/bench.c             benchmark program
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

Switching uses `swapcontext`, which saves the current registers and stack pointer for one thread and loads another's. It's ordinary user-space code, not a system call, so the kernel never sees the switch.

Threads never switch directly to each other. Each core has a scheduler loop with its own saved context, and a thread that yields, sleeps, finishes, or gets preempted switches back to that loop. The loop files the thread away (back in the ready queue, into the sleeping list, or done), asks the policy who's next, and switches to them. It's the same idea as a kernel running its scheduler between processes, and it keeps every scheduling decision in one place.

### Preemption

Each core asks the kernel to send a `SIGALRM` every quantum to the pthread it runs on. On Linux that's a per-thread timer from `timer_create`. macOS doesn't have those, so there we fall back to `setitimer`, which is a single timer for the whole process. That's fine with one core, but it's why multi-core needs Linux. The signal handler asks the policy whether the running thread should give up the CPU, and if so, switches back to the scheduler loop. When that thread gets picked again later, it picks up exactly where it was interrupted.

Since the timer can fire at any moment, including in the middle of the library's own code, the library blocks the signal while it's changing its queues and unblocks it afterward. A blocked signal isn't lost, just delayed until it's safe.

### Mechanism vs. policy

The runtime handles everything that works the same no matter which scheduler is running: creating threads, switching, the timer, sleeping, and tracking CPU time. A policy only decides which ready thread runs next. Each policy is a struct of function pointers, C's version of an abstract class:

```c
typedef struct {
    void   (*enqueue)(void *state, tcb_t *t);
    tcb_t *(*pick_next)(void *state);
    int    (*should_preempt)(void *state, tcb_t *running);
    void  *(*init)(int quantum_ms);
    void   (*destroy)(void *state);
} sched_policy_t;
```

`init` creates the policy's state, which holds its ready threads, and the other functions get it passed in, the same way a method gets `this`. `destroy` frees it. Each policy can use whatever data structure fits: a FIFO queue for FCFS and RR, a min-heap for SJF and STCF, and several queues for MLFQ.

The runtime keeps no globals. Its state is split in two:

- `scheduler_t` holds what's shared: the policy and its state, every thread, the sleeping list, the stats, and a lock.
- `core_t` holds what belongs to one core: the thread it's running, its scheduler loop's context, and its timer. Each pthread that calls `uthread_run` gets its own.

### Sleeping

Sleeping threads go into a list kept by the runtime rather than the policy, since when a thread wakes up doesn't depend on the scheduler. Each timer tick moves threads whose time is up back to the policy. If every thread is asleep, the scheduler waits for the next tick.

### CPU time

When a thread stops running, the runtime charges it for the CPU time it used, measured with the kernel thread's own CPU clock. STCF uses this to know how much time each thread has left, and MLFQ uses it to decide when a thread has used up its time on a level.

### Multi-core

Each pthread that calls `uthread_run` acts as one CPU core, with its own scheduler loop and its own timer. Two setups work:

- **Global scheduling.** Several pthreads call `uthread_run` on the same scheduler. They all pull from one shared ready queue, so a thread can run on a different core each time it's picked. The scheduler's lock is held whenever a core touches the queue or the sleeping list.
- **Partitioned scheduling.** Each pthread creates and runs its own scheduler, possibly with a different policy. Threads never move between cores, and the cores share nothing.

This needs a separate timer for each core, which only Linux has. On macOS all the cores would share one process-wide timer, and when one core finished and turned it off, the others a layer of abstraction to simplify multiple core scheduling.  `src/multi_core/` holds a layer of abstraction to simplify multi-core scheduling.  the layer is light as calling `uthread_run` from several pthreads is all that's needed to simulate multiple cores.  It is not necessary in order to perform global or partitioned multi-core schedulers, however it helps keep track of the policy, cores, and set schedulers.


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
| `test_multicore` | Four cores, both partitioned and global, with sleeping, yielding, and a thread created mid-run. Partitioned threads must stay on their own core. Linux only. |


## Benchmarks

`make bench` runs every workload under every policy with a 10 ms quantum, then runs the interactive workload under RR and MLFQ with quanta of 1, 5, 10, and 50 ms. Each row is repeated three times and shows averages, with the standard deviation for turnaround and response.

```
./build/bench/bench            # readable table
./build/bench/bench -r 10      # 10 repetitions per row
./build/bench/bench --csv      # one CSV line per repetition, for graphs
```

The workloads:

| Workload | Jobs | What it shows |
|---|---|---|
| convoy | one 200 ms job and four 10 ms jobs, all at once | Short jobs stuck behind a long one under FCFS |
| staggered | a 200 ms job, then 20 ms jobs arriving every 40 ms | Whether a late short job can jump ahead (STCF yes, SJF no) |
| interactive | two 300 ms CPU-bound jobs and two jobs that compute 2 ms then sleep 20 ms, ten times | Whether interactive threads get the CPU quickly when they wake up |
| bad-estimates | the convoy jobs, but the long job claims 10 ms and the short ones claim 200 ms | How badly SJF and STCF do when the estimates are wrong |

Jobs burn CPU with a busy loop calibrated at startup, so a "10 ms" job really needs about 10 ms of CPU.

For each run it reports average turnaround (finish minus arrival), average response (first run minus arrival), average wait (time spent ready but not running), the worst turnaround, the number of context switches, and for sleeping jobs, how late they woke up on average.

### Keeping the numbers honest

Our threads all run on one kernel thread, so the whole benchmark uses one CPU core. On Linux, the benchmark pins itself to whichever core it starts on so that the kernel doesn't move it around mid-run. macOS has no way to pin a thread to a core, so results there are noisier, and the benchmark says so when it starts.

The catch is that pinning doesn't keep anything else off that core. Turnaround and response are measured in real time, so if another program gets scheduled on the same core, our threads sit there waiting and the numbers come out higher than they should. The CPU time we charge each thread is less affected, since that clock doesn't advance while we're not running. The timer still ticks in real time, though, so a thread that loses part of its slice to another program still gets preempted on schedule.

To get clean results:

- Close anything heavy (browsers, IDEs, builds) before running.
- Use more repetitions (`-r 10`) and look at the standard deviation. A large one usually means something else was competing for the core.
- Compare policies against each other within the same run rather than reading too much into the exact milliseconds.



# CPU Scheduler Simulator (`SchedulerCPU.py`)

A discrete-time CPU scheduling simulator written in Python to evaluate trade-offs among **First-Come, First-Served (FCFS)**, **Round Robin (RR)** across time quanta, and **Preemptive Priority Scheduling with Aging** under deterministic workloads with explicit context-switch overhead.

---

## Project Overview & Motivation

CPU scheduling policies balance competing system goals: minimizing turnaround and waiting times, keeping interactive response snappy, controlling context-switch overhead, and preventing low-priority tasks from starving.

This simulator models a discrete tick clock where switching between distinct processes incurs a configurable context-switch cost (`ctx_cost = 1` tick by default). Rather than relying on randomized workloads, it evaluates three deterministic scenarios designed to isolate specific operating system behaviors:

1. **Mixed Interactive & Batch:** Compares batch compilation/logging tasks against interactive UI, database, and audio tasks.
2. **Convoy Effect:** Demonstrates how a long initial CPU burst (`video_enc`, burst `18`) delays shorter interactive commands (`shell_cmd1..3`) under non-preemptive scheduling.
3. **Starvation Stress (Detailed):** Tests how a low-priority background job (`backup_bg`, base priority `4`) fares against recurring high-priority tasks (`rt_sensor1..4`, priority `0`), and verifies that aging prevents indefinite starvation while printing per-process execution breakdowns.

---

## Implemented Scheduling Policies

* **FCFS (`run_fcfs`):** Non-preemptive scheduler selecting the earliest-arriving ready process (tie-broken by `pid`). Simple and low-overhead for uniform jobs, but vulnerable to convoy delays.
* **Round Robin (`run_rr`):** Preemptive time-sliced scheduler using a fixed time quantum (`q=2` and `q=5` evaluated). Smaller quanta improve responsiveness at the cost of higher context-switch counts; larger quanta reduce switching overhead at the cost of higher response time.
* **Preemptive Priority with Aging (`run_priority_aging`):** Tick-by-tick preemptive scheduler where lower numeric priority values represent higher priority (`0` is highest). Ready processes that are not currently running increment `wait_since_run` each tick and improve their `current_priority` (decrementing toward `0`) every `aging_step` ticks (`a=4` in the scenario evaluation) to prevent starvation.

---

## Metrics Tracked

* **Average Waiting Time (`AvgWait`):** Mean time processes spend waiting in the ready queue (excluding arrival-to-first-run response if preempted/delayed, computed as `turnaround_time - burst_time`).
* **Average Turnaround Time (`AvgTAT`):** Mean elapsed time from arrival to completion (`completion_time - arrival_time`).
* **Average Response Time (`AvgResp`):** Mean elapsed time from arrival until a process first starts executing on the CPU (`start_time - arrival_time`).
* **Context Switches (`CtxSw`):** Number of times the CPU transitions between different processes (`last_pid != chosen.pid`), each adding `ctx_cost` ticks to the simulation clock.
* **Completion Time (`EndT`):** Total simulation time elapsed when all processes finish.

---

## How to Run

### Run the Simulator
From the project directory in your terminal:

```bash
python SchedulerCPU.py
```

### Run the Unit Tests
Execute the test suite in `test_scheduler_cpu.py`:

```bash
python -m unittest -v test_scheduler_cpu.py
```

---

## Repository & Submission Structure

Place your project files inside your designated course submission folder (e.g., `<course>/<firstname>/`, such as `CSE4300/<firstname>/` or `CSE5305/<firstname>/`):

```text
<course>/<firstname>/
├── SchedulerCPU.py
├── test_scheduler_cpu.py
├── README.md
└── presentation/          # Slides / presentation materials (6-7 minute walkthrough)
```

---

## Key Takeaways for Presentation

* **Convoy Effect Mitigation:** Preemption and time-slicing (`RR (q=2)`) or priority/aging (`Prio+Aging (a=4)`) substantially reduce waiting and response times compared to `FCFS` when a long job arrives first.
* **Quantum Trade-Offs:** Comparing `q=2` vs. `q=5` highlights the balance between faster response/lower waiting in convoy-heavy workloads and fewer context switches in mixed workloads.
* **Starvation Prevention:** Periodic priority promotion (`aging_step`) ensures background work eventually advances and completes instead of being permanently locked out by recurring high-priority arrivals.

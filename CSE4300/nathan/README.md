# CPU Scheduler Simulator (`SchedulerCPU.py`)

A discrete-time CPU scheduling simulator written in Python to evaluate trade-offs among **First-Come, First-Served (FCFS)**, **Round Robin (RR)** across time quanta, and **Preemptive Priority Scheduling with Aging** under larger deterministic workloads (16–20 tasks per scenario) with explicit context-switch overhead.

## Project Overview & Motivation

CPU scheduling policies balance competing system goals: minimizing turnaround and waiting times, keeping interactive response snappy, controlling context-switch overhead, and preventing low-priority tasks from starving.

This simulator models a discrete tick clock where switching between distinct processes incurs a configurable context-switch cost (`ctx_cost = 1` tick by default). Rather than relying on small toy workloads, `build_large_workloads()` evaluates three scaled deterministic scenarios designed to isolate operating system behaviors under sustained contention:

1. **Mixed Interactive & Batch (Large — 20 Tasks):** Interleaves long CPU-bound jobs (`compiler_1`, `linker_1`, `gc_sweep`, `indexer_1`) with frequent interactive UI, database, cache, network, and audio packet bursts.

2. **Convoy Effect (Large — 16 Tasks):** Demonstrates how two heavy initial CPU bursts (`video_enc_1` burst `24` and `video_enc_2` burst `20`) bottleneck 14 short interactive commands (`shell_cmd_1..14`, bursts `2`–`3`) under non-preemptive scheduling.

3. **Starvation Stress (Large — 18 Tasks):** Tests how three low-priority background maintenance jobs (`backup_bg_1`, `archive_bg_2`, `fsck_bg_3`, base priority `4`) fare against 15 frequent real-time sensor bursts (`rt_sensor_1..15`, priority `0` arriving every 3 ticks), and verifies that aging prevents indefinite starvation while printing per-process execution breakdowns.

## Implemented Scheduling Policies

* **FCFS (`run_fcfs`):** Non-preemptive scheduler selecting the earliest-arriving ready process (tie-broken by `pid`). Simple and low-overhead for uniform jobs, but vulnerable to severe convoy delays.

* **Round Robin (`run_rr`):** Preemptive time-sliced scheduler using a fixed time quantum (`q=2` and `q=5` evaluated). Smaller quanta improve responsiveness and cut waiting time under heavy convoy loads at the cost of higher context-switch counts; larger quanta reduce switching overhead.

* **Preemptive Priority with Aging (`run_priority_aging`):** Tick-by-tick preemptive scheduler where lower numeric priority values represent higher priority (`0` is highest). Ready processes that are not currently running increment `wait_since_run` each tick and improve their `current_priority` (decrementing toward `0`) every `aging_step` ticks (`a=4` in the scenario evaluation) to prevent starvation.

## Benchmark Results (Large Workloads, `ctx_cost = 1`)

| Scenario | Policy | AvgWait | AvgTAT | AvgResp | CtxSw | EndT |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **Mixed Interactive & Batch (20 tasks)** | `FCFS` | 47.55 | 52.40 | 47.55 | 19 | 116 |
| **Mixed Interactive & Batch (20 tasks)** | `RR (q=2)` | **30.35** | **32.50** | **34.15** | 35 | **96** |
| **Mixed Interactive & Batch (20 tasks)** | `RR (q=5)` | 38.30 | 41.00 | 34.60 | 24 | 98 |
| **Mixed Interactive & Batch (20 tasks)** | `Prio+Aging (a=4)` | 48.25 | 53.10 | 46.90 | 21 | 118 |
| **Convoy Effect (16 tasks)** | `FCFS` | 54.00 | 58.94 | 54.00 | 15 | 94 |
| **Convoy Effect (16 tasks)** | `RR (q=2)` | **37.12** | **38.56** | **28.62** | 30 | **93** |
| **Convoy Effect (16 tasks)** | `RR (q=5)` | 44.56 | 48.25 | 38.06 | 20 | 94 |
| **Convoy Effect (16 tasks)** | `Prio+Aging (a=4)` | 52.56 | 57.50 | 47.19 | 18 | 97 |
| **Starvation Stress (18 tasks)** | `FCFS` | 30.39 | 34.39 | 30.39 | 17 | 89 |
| **Starvation Stress (18 tasks)** | `RR (q=2)` | **19.33** | **21.00** | 21.39 | 35 | 87 |
| **Starvation Stress (18 tasks)** | `RR (q=5)` | 26.56 | 29.72 | **18.33** | 18 | **75** |
| **Starvation Stress (18 tasks)** | `Prio+Aging (a=4)` | 28.50 | 32.50 | 25.56 | 19 | 91 |

## Metrics Tracked

* **Average Waiting Time (`AvgWait`):** Mean time processes spend waiting in the ready queue (`turnaround_time - burst_time`).
* **Average Turnaround Time (`AvgTAT`):** Mean elapsed time from arrival to completion (`completion_time - arrival_time`).
* **Average Response Time (`AvgResp`):** Mean elapsed time from arrival until a process first starts executing on the CPU (`start_time - arrival_time`).
* **Context Switches (`CtxSw`):** Number of times the CPU transitions between different processes (`last_pid != chosen.pid`), each adding `ctx_cost` ticks to the simulation clock.
* **Completion Time (`EndT`):** Total simulation time elapsed when all processes finish.

## How to Run

### Run the Simulator
```bash
python SchedulerCPU.py
```

### Run the Unit Tests
```bash
python -m unittest -v test_scheduler_cpu.py
```

## Repository & Submission Structure

```text
<CSE4300>/<nathan>/
├── SchedulerCPU.py
├── test_scheduler_cpu.py
├── README.md
└── presentation          # Slides / presentation materials
```

## Key Takeaways for Presentation

* **Scale Reveals Preemption's Advantage:** With 16–20 concurrent tasks, `RR (q=2)` consistently achieves the lowest average waiting and turnaround times across all three workloads despite higher context-switch counts (`30–35` switches).
* **Convoy Effect Mitigation:** When two long video encoders (`24` and `20` ticks) arrive first, `FCFS` traps 14 short commands at `AvgWait = 54.00`, whereas `RR (q=2)` drops `AvgWait` to `37.12` and `AvgResp` to `28.62`.
* **Starvation Prevention:** Under 15 recurring high-priority sensor bursts, `Prio+Aging (a=4)` promotes all three background jobs (`backup_bg_1`, `archive_bg_2`, `fsck_bg_3`) so 100% complete (`EndT = 33, 42, 52`) with a lower overall `AvgWait` (`28.50`) than `FCFS` (`30.39`).
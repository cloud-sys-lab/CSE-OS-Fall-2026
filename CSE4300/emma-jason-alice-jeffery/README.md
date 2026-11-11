# CPU Scheduler Simulator
A program that simulates CPU scheduling algorithms on a list of jobs loaded from a text file. Algorithms share one function signature so `main.c` can choose which to run at runtime.

```text
./scheduler <datafile> <fcfs|sjf|rr> [quantum]
```

Example:

```bash
make
./scheduler data/processes1.txt fcfs
./scheduler data/processes1.txt rr 4
```

Smoke tests for completed work:

```bash
python3 tests/run_tests.py
```

## Phases

### Phase 1 — Process data model 

**Goal:** Define the job / PCB-like struct every scheduler will use.

**Deliverable:** `include/process.h`

| Field | Purpose |
| --- | --- |
| `pid` | Process ID from the input file |
| `arrival_time` | When the job enters the system |
| `burst_time` | Total CPU time needed |
| `priority` | Priority value (for later priority scheduling) |
| `remaining_time` | Time left; starts equal to `burst_time` |
| `start_time` | First time on CPU; `-1` until first scheduled |
| `finish_time` | When the job completes |

---

### Phase 2 — Shared scheduler interface 

**Goal:** Give every algorithm the same signature so `main` can pick one with a function pointer.

**Deliverable:** `include/scheduler.h`

- `Scheduler` struct: algorithm `name` + `run` function pointer
- Common signature: `void run_*(Process procs[], int n, int quantum)`
- Declarations for `run_fcfs`, `run_sjf`, and `run_rr`
- `quantum` is used by Round Robin; ignored by FCFS and SJF

---

### Phase 3 — Build plumbing and stubs 

**Goal:** Compile, load jobs, and dispatch to a scheduler (even if the algorithm is still a stub).

**Deliverables:**

- `Makefile` — builds `src/*.c` and `src/schedulers/*.c` into `./scheduler`
- `data/processes1.txt` — sample input (`pid arrival burst priority`)
- `src/main.c` — parse args, load processes, init fields, map name → function, call `run`
- Stub `fcfs.c` / `sjf.c` / `rr.c` — print that the algorithm is not implemented yet
- `.gitignore` — ignore `build/` and the binary

**Checkpoint:** `make` succeeds and `./scheduler data/processes1.txt fcfs` runs without crashing.

---

### Phase 4 — Ready queue

**Goal:** Shared queue helpers for “who is ready right now.”

**Planned deliverables:** `include/queue.h`, `src/queue.c`

Useful operations: enqueue, dequeue, is_empty, and helpers to pick shortest job or highest priority among ready jobs. Needed especially for Round Robin and SJF.

---

### Phase 5 — Metrics 

**Goal:** Compute and print performance stats after a scheduler fills `start_time` / `finish_time`.

**Planned deliverables:** `include/metrics.h`, `src/metrics.c`

- Turnaround = finish − arrival  
- Waiting = turnaround − burst  
- Response = start − arrival  

Print a per-process table and averages, shared by all algorithms.

---

### Phase 6 — Scheduling algorithms

**Goal:** Replace stubs with real simulations.

| Order | Algorithm | Idea |
| --- | --- | --- |
| 1 | FCFS | Run jobs in arrival order to completion |
| 2 | SJF | Among arrived jobs, pick shortest burst (non-preemptive first) |
| 3 | RR | Ready queue + time quantum; preempt and re-enqueue if still remaining |

Only the “pick next job” and “how long to run” steps change between algorithms. Validate FCFS by hand against the sample data before moving on.

---

### Phase 7 — Tests 

**Goal:** Automated checks for correctness.

**Current:** `tests/run_tests.py` covers Phase 1–3 smoke tests (files, headers, build, load, dispatch, basic CLI errors).

**Still to add:** Expected waiting / turnaround / response numbers per algorithm once Phase 6 is done.

---


## Status summary

| Phase | Status |
| --- | --- |
| 1 Process model | Done |
| 2 Scheduler interface | Done |
| 3 Makefile, main, stubs | Done |
| 4 Ready queue | Done |
| 5 Metrics | Done |
| 6 Algorithms (FCFS / SJF / RR) | Done |
| 7 Full tests | Done (smoke tests and scheduler algo test |

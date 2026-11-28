import copy
import random
from dataclasses import dataclass


@dataclass
class Process:
    pid: int
    arrival_time: int
    burst_time: int
    remaining_time: int = 0
    start_time: int = -1  # -1 until the job reaches the CPU
    completion_time: int = 0
    waiting_time: int = 0
    turnaround_time: int = 0
    response_time: int = -1

    def __post_init__(self):
        # 0 means the caller didn't set it, so the job still has its full burst.
        if self.remaining_time == 0:
            self.remaining_time = self.burst_time


NUM_PROCESSES = 5
CONTEXT_SWITCH_TIME = 1  # ticks lost when the CPU swaps to a different job
TIME_QUANTUM = 2         # longest turn round robin gives a job
MAX_ARRIVAL_TIME = 10
MAX_BURST_TIME = 8
RANDOM_SEED = 42         # None for a new workload each run


def generate_processes(count: int) -> list[Process]:
    """Make count random jobs, earliest arrival first."""
    if RANDOM_SEED is not None:
        random.seed(RANDOM_SEED)

    processes = []
    for pid in range(1, count + 1):
        arrival = random.randint(0, MAX_ARRIVAL_TIME)
        burst = random.randint(1, MAX_BURST_TIME)
        processes.append(Process(pid=pid, arrival_time=arrival, burst_time=burst))

    processes.sort(key=lambda p: p.arrival_time)
    return processes


def calculate_metrics(processes: list[Process], total_time: int, total_context_switches: int, context_switch_cost: int):
    """Averages for one finished run and how busy the CPU was."""
    n = len(processes)
    avg_response = sum(p.response_time for p in processes) / n
    avg_waiting = sum(p.waiting_time for p in processes) / n
    avg_turnaround = sum(p.turnaround_time for p in processes) / n

    total_burst = sum(p.burst_time for p in processes)
    # Idle ticks and switches sit inside total_time, so they lower this.
    cpu_utilization = (total_burst / total_time * 100) if total_time > 0 else 0.0

    return {
        "avg_response": avg_response,
        "avg_waiting": avg_waiting,
        "avg_turnaround": avg_turnaround,
        "cpu_utilization": cpu_utilization,
        "total_time": total_time,
        "context_switches": total_context_switches,
    }


def run_fifo(procs: list[Process], cs_time: int):
    """First in, first run. Once a job starts, it runs until it finishes."""
    processes = copy.deepcopy(procs)
    current_time = 0
    gantt = []
    last_pid = None
    context_switches = 0

    completed = 0
    n = len(processes)

    while completed < n:
        ready = [p for p in processes if p.arrival_time <= current_time and p.remaining_time > 0]

        if not ready:
            gantt.append(f"[{current_time}]--IDLE--")
            current_time += 1
            continue

        # List is sorted by arrival, so the 0th elemet is next.
        current = ready[0]

        if last_pid is not None and last_pid != current.pid and cs_time > 0:
            for _ in range(cs_time):
                gantt.append(f"[{current_time}]--CS--")
                current_time += 1
            context_switches += 1

        if current.start_time == -1:
            current.start_time = current_time
            current.response_time = current.start_time - current.arrival_time

        # the whole burst runs in one stretch
        run_duration = current.remaining_time
        for _ in range(run_duration):
            gantt.append(f"[{current_time}]--P{current.pid}--")
            current_time += 1

        current.remaining_time = 0
        current.completion_time = current_time
        current.turnaround_time = current.completion_time - current.arrival_time
        current.waiting_time = current.turnaround_time - current.burst_time
        completed += 1
        last_pid = current.pid

    metrics = calculate_metrics(processes, current_time, context_switches, cs_time)
    return gantt, metrics


def run_sjcf(procs: list[Process], cs_time: int):
    """When the CPU is free, run the shortest job until it finishes."""
    processes = copy.deepcopy(procs)
    current_time = 0
    gantt = []
    last_pid = None
    context_switches = 0

    completed = 0
    n = len(processes)

    while completed < n:
        ready = [p for p in processes if p.arrival_time <= current_time and p.remaining_time > 0]

        if not ready:
            gantt.append(f"[{current_time}]--IDLE--")
            current_time += 1
            continue

        current = min(ready, key=lambda p: p.burst_time)

        if last_pid is not None and last_pid != current.pid and cs_time > 0:
            for _ in range(cs_time):
                gantt.append(f"[{current_time}]--CS--")
                current_time += 1
            context_switches += 1

        if current.start_time == -1:
            current.start_time = current_time
            current.response_time = current.start_time - current.arrival_time

        run_duration = current.remaining_time
        for _ in range(run_duration):
            gantt.append(f"[{current_time}]--P{current.pid}--")
            current_time += 1

        current.remaining_time = 0
        current.completion_time = current_time
        current.turnaround_time = current.completion_time - current.arrival_time
        current.waiting_time = current.turnaround_time - current.burst_time
        completed += 1
        last_pid = current.pid

    metrics = calculate_metrics(processes, current_time, context_switches, cs_time)
    return gantt, metrics


def run_srtcf(procs: list[Process], cs_time: int):
    """Run whichever arrived job has the least time left. A shorter one can interupt."""
    processes = copy.deepcopy(procs)
    current_time = 0
    gantt = []
    last_pid = None
    context_switches = 0

    completed = 0
    n = len(processes)

    while completed < n:
        ready = [p for p in processes if p.arrival_time <= current_time and p.remaining_time > 0]

        if not ready:
            gantt.append(f"[{current_time}]--IDLE--")
            current_time += 1
            continue

        current = min(ready, key=lambda p: p.remaining_time)

        if last_pid is not None and last_pid != current.pid and cs_time > 0:
            for _ in range(cs_time):
                gantt.append(f"[{current_time}]--CS--")
                current_time += 1
            context_switches += 1

        if current.start_time == -1:
            current.start_time = current_time
            current.response_time = current.start_time - current.arrival_time

        gantt.append(f"[{current_time}]--P{current.pid}--")
        current.remaining_time -= 1
        current_time += 1

        if current.remaining_time == 0:
            current.completion_time = current_time
            current.turnaround_time = current.completion_time - current.arrival_time
            current.waiting_time = current.turnaround_time - current.burst_time
            completed += 1

        last_pid = current.pid

    metrics = calculate_metrics(processes, current_time, context_switches, cs_time)
    return gantt, metrics


def run_round_robin(procs: list[Process], quantum: int, cs_time: int):
    """Same line as FIFO, but a job only keeps the CPU for quantum ticks."""
    processes = copy.deepcopy(procs)
    current_time = 0
    gantt = []
    last_pid = None
    context_switches = 0

    ready_queue = []
    arrived_pids = set()
    completed = 0
    n = len(processes)

    def check_new_arrivals(t):
        for p in processes:
            if p.arrival_time <= t and p.pid not in arrived_pids and p.remaining_time > 0:
                ready_queue.append(p)
                arrived_pids.add(p.pid)

    check_new_arrivals(current_time)

    while completed < n:
        if not ready_queue:
            gantt.append(f"[{current_time}]--IDLE--")
            current_time += 1
            check_new_arrivals(current_time)
            continue

        current = ready_queue.pop(0)

        if last_pid is not None and last_pid != current.pid and cs_time > 0:
            for _ in range(cs_time):
                gantt.append(f"[{current_time}]--CS--")
                current_time += 1
                check_new_arrivals(current_time)
            context_switches += 1

        if current.start_time == -1:
            current.start_time = current_time
            current.response_time = current.start_time - current.arrival_time

        exec_time = min(quantum, current.remaining_time)
        for _ in range(exec_time):
            gantt.append(f"[{current_time}]--P{current.pid}--")
            current.remaining_time -= 1
            current_time += 1
            check_new_arrivals(current_time)

        if current.remaining_time == 0:
            current.completion_time = current_time
            current.turnaround_time = current.completion_time - current.arrival_time
            current.waiting_time = current.turnaround_time - current.burst_time
            completed += 1
        else:
            # Arrivals during this turn were already queued, so this job goes behind them.
            ready_queue.append(current)

        last_pid = current.pid

    metrics = calculate_metrics(processes, current_time, context_switches, cs_time)
    return gantt, metrics

# Printing tools

def read_tick(entry: str) -> tuple[int, str]:
    bracket, label = entry.split("]--", 1)
    return int(bracket[1:]), label[:-2]


def collapse_gantt(gantt: list[str]) -> list[tuple[int, int, str]]:
    blocks: list[tuple[int, int, str]] = []
    for entry in gantt:
        time, label = read_tick(entry)
        if blocks and blocks[-1][2] == label and blocks[-1][1] == time:
            start, _, name = blocks[-1]
            blocks[-1] = (start, time + 1, name)
        else:
            blocks.append((time, time + 1, label))
    return blocks


def block_name(label: str) -> str:
    if label == "IDLE":
        return "idle"
    return label


def print_gantt(gantt: list[str]) -> None:
    blocks = collapse_gantt(gantt)
    if not blocks:
        print("  (no chart)")
        return

    # Context switches stay in the timeline as unlabeled gaps.
    rows = []
    for start, finish, label in blocks:
        if label == "CS":
            continue
        rows.append((block_name(label), start, finish, finish - start))

    task_w = max(len("Task"), *(len(task) for task, *_ in rows))
    print(f"  {'Task':<{task_w}}  {'Start':>6}  {'Finish':>6}  {'Length':>6}")
    print(f"  {'-' * task_w}  ------  ------  ------")
    for task, start, finish, length in rows:
        print(f"  {task:<{task_w}}  {start:6}  {finish:6}  {length:6}")

    pieces = []
    for start, finish, label in blocks:
        if label == "CS":
            # Wide enough that the leaving time and the next start both fit.
            width = max(finish - start, len(str(start)))
            pieces.append(" " * width)
            continue
        name = block_name(label)
        width = max(finish - start, len(name) + 2)
        pieces.append(name.center(width))

    bar = "|" + "|".join(pieces) + "|"
    col = 0
    marks = [(0, blocks[0][0])]
    for piece, block in zip(pieces, blocks):
        col += 1 + len(piece)
        marks.append((col, block[1]))

    width = max(len(bar), marks[-1][0] + len(str(marks[-1][1])))
    ruler = [" "] * width
    taken = [False] * width

    def place(at: int, time: int) -> None:
        text = str(time)
        end = at + len(text)
        if end > width or any(taken[at:end]):
            return
        for offset, char in enumerate(text):
            ruler[at + offset] = char
            taken[at + offset] = True

    place(marks[0][0], marks[0][1])
    place(marks[-1][0], marks[-1][1])
    for at, time in marks[1:-1]:
        place(at, time)

    print()
    print("  " + "".join(ruler).rstrip())
    print("  " + bar)


# A busier machine than the five-job demo. Most jobs are short, a few are
# long enough to block the queue, and arrivals keep coming.
REALISTIC_COUNT = 100
REALISTIC_SEED = 4300
REALISTIC_MEAN_GAP = 4  # average time between arrivals


def burst_class(burst_time: int) -> str:
    if burst_time <= 5:
        return "short"
    if burst_time <= 25:
        return "medium"
    return "long"


def generate_realistic_processes(
    count: int = REALISTIC_COUNT,
    seed: int = REALISTIC_SEED,
    mean_gap: int = REALISTIC_MEAN_GAP,
) -> list[Process]:
    """Build count jobs with staggered arrivals and a heavy tail of bursts.

    About 70% have a burst of 1 to 5, 20% a burst of 8 to 25, and 10% a
    burst of 40 to 90. Gaps between arrivals are exponential, so some jobs
    land together and some land while a long job would still be running.
    """
    rng = random.Random(seed)
    processes = []
    clock = 0
    for pid in range(1, count + 1):
        clock += int(rng.expovariate(1 / mean_gap))
        roll = rng.random()
        if roll < 0.70:
            burst = rng.randint(1, 5)
        elif roll < 0.90:
            burst = rng.randint(8, 25)
        else:
            burst = rng.randint(40, 90)
        processes.append(Process(pid=pid, arrival_time=clock, burst_time=burst))
    return processes


def run_realistic(
    count: int = REALISTIC_COUNT,
    seed: int = REALISTIC_SEED,
    mean_gap: int = REALISTIC_MEAN_GAP,
    cs_time: int = CONTEXT_SWITCH_TIME,
    quantum: int = TIME_QUANTUM,
):
    """Run all four schedulers on the larger workload.

    Switch cost and quantum stay at the simulator defaults, so the only
    change from the five-job run is the jobs themselves.
    """
    workload = generate_realistic_processes(count, seed, mean_gap)
    schedulers = [
        ("FIFO", lambda: run_fifo(workload, cs_time)),
        ("SJCF (SJF)", lambda: run_sjcf(workload, cs_time)),
        ("SRTCF (SRTF)", lambda: run_srtcf(workload, cs_time)),
        ("Round Robin", lambda: run_round_robin(workload, quantum, cs_time)),
    ]
    results = {}
    for name, func in schedulers:
        _, metrics = func()
        results[name] = metrics
    return workload, results


def print_realistic(workload: list[Process], results: dict) -> None:
    counts = {"short": 0, "medium": 0, "long": 0}
    for process in workload:
        counts[burst_class(process.burst_time)] += 1
    bursts = [process.burst_time for process in workload]

    print("=========================================================================")
    print("                    LARGER, MORE REALISTIC WORKLOAD                      ")
    print("=========================================================================")
    print(f"Jobs: {len(workload)}    Seed: {REALISTIC_SEED}")
    print(f"Short (burst 1-5): {counts['short']}")
    print(f"Medium (burst 8-25): {counts['medium']}")
    print(f"Long (burst 40-90): {counts['long']}")
    print(f"Median burst: {sorted(bursts)[len(bursts) // 2]}")
    print(f"Longest burst: {max(bursts)}")
    print(f"Last arrival: {workload[-1].arrival_time}")
    print(f"Total CPU work: {sum(bursts)}")
    print(f"Context switch: {CONTEXT_SWITCH_TIME} unit    Quantum: {TIME_QUANTUM}")
    print()
    print("FIRST EIGHT JOBS:")
    print(f"{'PID':<6}{'Arrival Time':<15}{'Burst Time':<12}{'Class'}")
    print("-" * 46)
    for process in workload[:8]:
        print(
            f"P{process.pid:<5}{process.arrival_time:<15}{process.burst_time:<12}"
            f"{burst_class(process.burst_time)}"
        )
    print()
    header = f"{'Algorithm':<15} | {'Avg Reaction':<12} | {'Avg Waiting':<12} | {'Avg Turnaround':<14} | {'CPU Util %':<10} | {'Switches':<8}"
    print(header)
    print("-" * len(header))
    for name, metrics in results.items():
        print(
            f"{name:<15} | "
            f"{metrics['avg_response']:<12.2f} | "
            f"{metrics['avg_waiting']:<12.2f} | "
            f"{metrics['avg_turnaround']:<14.2f} | "
            f"{metrics['cpu_utilization']:<10.1f} | "
            f"{metrics['context_switches']:<8}"
        )
    print("=========================================================================\n")


def main():
    print("=========================================================================")
    print("               CPU SCHEDULING ALGORITHM SIMULATOR                         ")
    print("=========================================================================\n")

    workload = generate_processes(NUM_PROCESSES)

    print("GENERATED PROCESS WORKLOAD:")
    print(f"{'PID':<6}{'Arrival Time':<15}{'Burst Time':<12}")
    print("-" * 35)
    for p in workload:
        print(f"P{p.pid:<5}{p.arrival_time:<15}{p.burst_time:<12}")
    print("\nSimulation Settings:")
    print(f" - Context Switch Penalty : {CONTEXT_SWITCH_TIME} unit(s)")
    print(f" - Round Robin Quantum   : {TIME_QUANTUM} unit(s)\n")

    results = {}
    schedulers = [
        ("FIFO", lambda: run_fifo(workload, CONTEXT_SWITCH_TIME)),
        ("SJCF (SJF)", lambda: run_sjcf(workload, CONTEXT_SWITCH_TIME)),
        ("SRTCF (SRTF)", lambda: run_srtcf(workload, CONTEXT_SWITCH_TIME)),
        ("Round Robin", lambda: run_round_robin(workload, TIME_QUANTUM, CONTEXT_SWITCH_TIME)),
    ]

    for name, func in schedulers:
        gantt, metrics = func()
        results[name] = metrics

        print(f"--- GANTT CHART: {name} ---")
        print_gantt(gantt)
        print()

    print("=========================================================================")
    print("                        COMPARATIVE PERFORMANCE                          ")
    print("=========================================================================")
    header = f"{'Algorithm':<15} | {'Avg Reaction':<12} | {'Avg Waiting':<12} | {'Avg Turnaround':<14} | {'CPU Util %':<10}"
    print(header)
    print("-" * len(header))

    for name, m in results.items():
        print(
            f"{name:<15} | "
            f"{m['avg_response']:<12.2f} | "
            f"{m['avg_waiting']:<12.2f} | "
            f"{m['avg_turnaround']:<14.2f} | "
            f"{m['cpu_utilization']:<10.1f}"
        )
    print("=========================================================================\n")

    realistic_workload, realistic_results = run_realistic()
    print_realistic(realistic_workload, realistic_results)


if __name__ == '__main__':
    main()
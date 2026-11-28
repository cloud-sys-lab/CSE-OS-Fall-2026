from dataclasses import dataclass
import math


@dataclass
class Process:
    pid: int
    name: str
    arrival_time: int
    burst_time: int
    base_priority: int  # Lower number = higher priority (0 is highest)

    remaining_time: int = 0
    current_priority: int = 0
    wait_since_run: int = 0
    start_time: int = -1
    completion_time: int = 0
    waiting_time: int = 0
    turnaround_time: int = 0
    response_time: int = -1
    started: bool = False
    completed: bool = False

    def reset(self) -> None:
        self.remaining_time = self.burst_time
        self.current_priority = self.base_priority
        self.wait_since_run = 0
        self.start_time = -1
        self.completion_time = 0
        self.waiting_time = 0
        self.turnaround_time = 0
        self.response_time = -1
        self.started = False
        self.completed = False


@dataclass
class SimResult:
    policy_name: str
    avg_waiting: float
    avg_turnaround: float
    avg_response: float
    context_switches: int
    total_time: int
    processes: list[Process]


def clone_processes(procs: list[Process]) -> list[Process]:
    copied = [
        Process(
            pid=p.pid,
            name=p.name,
            arrival_time=p.arrival_time,
            burst_time=p.burst_time,
            base_priority=p.base_priority,
        )
        for p in procs
    ]
    for p in copied:
        p.reset()
    return copied


def next_arrival_time(procs: list[Process], now: int) -> int:
    earliest = math.inf
    for p in procs:
        if not p.completed and p.arrival_time > now and p.arrival_time < earliest:
            earliest = p.arrival_time
    return int(earliest) if earliest != math.inf else -1


def build_result(
    policy_name: str, procs: list[Process], ctx_switches: int, total_time: int
) -> SimResult:
    n = len(procs)
    if n == 0:
        return SimResult(policy_name, 0.0, 0.0, 0.0, ctx_switches, total_time, procs)

    avg_wait = sum(p.waiting_time for p in procs) / n
    avg_tat = sum(p.turnaround_time for p in procs) / n
    avg_resp = sum(p.response_time if p.response_time >= 0 else 0 for p in procs) / n

    return SimResult(
        policy_name=policy_name,
        avg_waiting=avg_wait,
        avg_turnaround=avg_tat,
        avg_response=avg_resp,
        context_switches=ctx_switches,
        total_time=total_time,
        processes=procs,
    )


def run_fcfs(base_procs: list[Process], ctx_cost: int = 1) -> SimResult:
    """Non-preemptive First-Come, First-Served."""
    procs = clone_processes(base_procs)
    n = len(procs)
    time = 0
    completed = 0
    ctx_switches = 0
    last_pid = -1

    while completed < n:
        ready = [p for p in procs if not p.completed and p.arrival_time <= time]
        if not ready:
            nxt = next_arrival_time(procs, time)
            if nxt == -1:
                break
            time = nxt
            continue

        chosen = min(ready, key=lambda p: (p.arrival_time, p.pid))

        if last_pid != -1 and last_pid != chosen.pid:
            time += ctx_cost
            ctx_switches += 1

        chosen.started = True
        chosen.start_time = time
        chosen.response_time = time - chosen.arrival_time

        time += chosen.burst_time
        chosen.remaining_time = 0
        chosen.completion_time = time
        chosen.turnaround_time = time - chosen.arrival_time
        chosen.waiting_time = chosen.turnaround_time - chosen.burst_time
        chosen.completed = True
        last_pid = chosen.pid
        completed += 1

    return build_result("FCFS", procs, ctx_switches, time)


def run_rr(base_procs: list[Process], quantum: int = 2, ctx_cost: int = 1) -> SimResult:
    """Preemptive Round Robin with fixed time quantum."""
    quantum = max(1, quantum)
    procs = clone_processes(base_procs)
    n = len(procs)

    ready_queue: list[Process] = []
    queued_pids: set[int] = set()

    def enqueue_arrivals(now: int) -> None:
        arrivals = sorted(
            (p for p in procs if not p.completed and p.pid not in queued_pids and p.arrival_time <= now),
            key=lambda p: (p.arrival_time, p.pid),
        )
        for p in arrivals:
            ready_queue.append(p)
            queued_pids.add(p.pid)

    time = 0
    completed = 0
    ctx_switches = 0
    last_pid = -1
    enqueue_arrivals(time)

    while completed < n:
        if not ready_queue:
            nxt = next_arrival_time(procs, time)
            if nxt == -1:
                break
            time = nxt
            enqueue_arrivals(time)
            continue

        cur = ready_queue.pop(0)
        queued_pids.discard(cur.pid)

        if last_pid != -1 and last_pid != cur.pid:
            time += ctx_cost
            ctx_switches += 1
            enqueue_arrivals(time)

        if not cur.started:
            cur.started = True
            cur.start_time = time
            cur.response_time = time - cur.arrival_time

        slice_len = min(quantum, cur.remaining_time)
        time += slice_len
        cur.remaining_time -= slice_len
        last_pid = cur.pid

        enqueue_arrivals(time)

        if cur.remaining_time == 0:
            cur.completion_time = time
            cur.turnaround_time = time - cur.arrival_time
            cur.waiting_time = cur.turnaround_time - cur.burst_time
            cur.completed = True
            completed += 1
        else:
            ready_queue.append(cur)
            queued_pids.add(cur.pid)

    return build_result(f"RR (q={quantum})", procs, ctx_switches, time)


def run_priority_aging(
    base_procs: list[Process], aging_step: int = 4, ctx_cost: int = 1
) -> SimResult:
    """Preemptive priority scheduler (lower number = higher priority) with aging."""
    aging_step = max(1, aging_step)
    procs = clone_processes(base_procs)
    n = len(procs)

    time = 0
    completed = 0
    ctx_switches = 0
    running_pid = -1
    last_pid = -1

    while completed < n:
        for p in procs:
            if not p.completed and p.arrival_time <= time and p.pid != running_pid:
                p.wait_since_run += 1
                if p.wait_since_run >= aging_step and p.current_priority > 0:
                    p.current_priority -= 1
                    p.wait_since_run = 0

        ready = [p for p in procs if not p.completed and p.arrival_time <= time]
        if not ready:
            running_pid = -1
            time += 1
            continue

        best = min(
            ready,
            key=lambda p: (p.current_priority, p.arrival_time, p.pid),
        )

        if best.pid != running_pid:
            if last_pid != -1 and last_pid != best.pid:
                time += ctx_cost
                ctx_switches += 1
            running_pid = best.pid
            last_pid = best.pid

        if not best.started:
            best.started = True
            best.start_time = time
            best.response_time = time - best.arrival_time

        best.remaining_time -= 1
        best.wait_since_run = 0
        time += 1

        if best.remaining_time == 0:
            best.completion_time = time
            best.turnaround_time = time - best.arrival_time
            best.waiting_time = best.turnaround_time - best.burst_time
            best.completed = True
            completed += 1
            running_pid = -1

    return build_result(f"Prio+Aging (a={aging_step})", procs, ctx_switches, time)


def print_process_details(result: SimResult) -> None:
    print(f"\n  [Per-Process Breakdown: {result.policy_name}]")
    print(f"  {'PID':<4} {'Task':<12} {'Arr':>5} {'Burst':>5} {'Prio':>5} {'Start':>6} {'End':>6} {'Wait':>6} {'TAT':>6}")
    for p in result.processes:
        print(
            f"  P{p.pid:<3} {p.name:<12} {p.arrival_time:>5} {p.burst_time:>5} "
            f"{p.base_priority:>5} {p.start_time:>6} {p.completion_time:>6} "
            f"{p.waiting_time:>6} {p.turnaround_time:>6}"
        )


def evaluate_scenario(
    title: str, procs: list[Process], ctx_cost: int = 1, show_details: bool = False
) -> None:
    print("\n" + "=" * 68)
    print(f"Scenario: {title} (Tasks={len(procs)}, Context Switch={ctx_cost})")
    print("=" * 68)
    print(f"{'Policy':<18} | {'AvgWait':>8} | {'AvgTAT':>8} | {'AvgResp':>8} | {'CtxSw':>6} | {'EndT':>5}")
    print("-" * 19 + "+" + "-" * 10 + "+" + "-" * 10 + "+" + "-" * 10 + "+" + "-" * 8 + "+" + "-" * 6)

    results = [
        run_fcfs(procs, ctx_cost),
        run_rr(procs, quantum=2, ctx_cost=ctx_cost),
        run_rr(procs, quantum=5, ctx_cost=ctx_cost),
        run_priority_aging(procs, aging_step=4, ctx_cost=ctx_cost),
    ]

    for r in results:
        print(
            f"{r.policy_name:<18} | {r.avg_waiting:>8.2f} | {r.avg_turnaround:>8.2f} | "
            f"{r.avg_response:>8.2f} | {r.context_switches:>6} | {r.total_time:>5}"
        )

    if show_details:
        print_process_details(results[0])
        print_process_details(results[3])


def main() -> None:
    s_mixed = [
        Process(1, "compiler", 0, 12, 2),
        Process(2, "ui_event", 1, 3, 1),
        Process(3, "db_query", 2, 6, 1),
        Process(4, "log_flush", 4, 4, 3),
        Process(5, "audio_pkt", 5, 2, 0),
    ]

    s_convoy = [
        Process(1, "video_enc", 0, 18, 2),
        Process(2, "shell_cmd1", 1, 2, 1),
        Process(3, "shell_cmd2", 2, 2, 1),
        Process(4, "shell_cmd3", 3, 3, 1),
    ]

    s_starve = [
        Process(1, "backup_bg", 0, 9, 4),
        Process(2, "rt_sensor1", 1, 3, 0),
        Process(3, "rt_sensor2", 4, 3, 0),
        Process(4, "rt_sensor3", 7, 3, 0),
        Process(5, "rt_sensor4", 10, 3, 0),
    ]

    evaluate_scenario("Mixed Interactive & Batch", s_mixed, ctx_cost=1, show_details=False)
    evaluate_scenario("Convoy Effect", s_convoy, ctx_cost=1, show_details=False)
    evaluate_scenario("Starvation Stress (Detailed)", s_starve, ctx_cost=1, show_details=True)


if __name__ == "__main__":
    main()
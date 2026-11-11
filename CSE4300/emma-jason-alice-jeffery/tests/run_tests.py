from __future__ import annotations
#!/usr/bin/env python3
"""smoke tests for phases 1-5: headers, build, load, dispatch stubs, metrics."""
"""tests for the scheduler simulator: build, CLI, algorithm results, edge cases."""

import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SCHEDULER = os.path.join(ROOT, "scheduler")
DATA = os.path.join(ROOT, "data", "processes1.txt")


def run(cmd, *, expect_ok=True):
    result = subprocess.run(
        cmd,
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    ok = result.returncode == 0
    if expect_ok and not ok:
        raise AssertionError(
            f"Command failed ({result.returncode}): {' '.join(cmd)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if not expect_ok and ok:
        raise AssertionError(f"Expected failure, but succeeded: {' '.join(cmd)}")
    return result


def parse_output(stdout):
    """Return ({pid: row dict}, {'turnaround':..,'waiting':..,'response':..})."""
    rows = {}
    for line in stdout.splitlines():
        parts = line.split()
        if len(parts) == 8 and all(x.lstrip("-").isdigit() for x in parts):
            pid, arr, burst, start, fin, ta, wait, resp = map(int, parts)
            rows[pid] = dict(arrival=arr, burst=burst, start=start, finish=fin,
                             turnaround=ta, waiting=wait, response=resp)
    avgs = {}
    for key in ("Turnaround", "Waiting", "Response"):
        m = re.search(rf"{key}:\s+([\d.]+)", stdout)
        if m:
            avgs[key.lower()] = float(m.group(1))
    return rows, avgs


def run_sched(algo, data_text=None, quantum=None, path=None):
    """Run ./scheduler on data_text (temp file) or an existing path."""
    cmd_tail = [algo] + ([str(quantum)] if quantum is not None else [])
    if path is not None:
        return run([SCHEDULER, path] + cmd_tail)
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(data_text)
        tmp = f.name
    try:
        return run([SCHEDULER, tmp] + cmd_tail)
    finally:
        os.remove(tmp)


def run_sched_fail(algo, data_text, quantum=None):
    cmd_tail = [algo] + ([str(quantum)] if quantum is not None else [])
    with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
        f.write(data_text)
        tmp = f.name
    try:
        return run([SCHEDULER, tmp] + cmd_tail, expect_ok=False)
    finally:
        os.remove(tmp)


def check_run(result, expected_rows, expected_avgs):
    rows, avgs = parse_output(result.stdout)
    assert set(rows) == set(expected_rows), f"pids {sorted(rows)} != {sorted(expected_rows)}"
    for pid, (start, finish) in expected_rows.items():
        r = rows[pid]
        assert (r["start"], r["finish"]) == (start, finish), \
            f"P{pid}: got start/finish {r['start']}/{r['finish']}, expected {start}/{finish}"
        assert r["turnaround"] == r["finish"] - r["arrival"], f"P{pid} turnaround inconsistent"
        assert r["waiting"] == r["turnaround"] - r["burst"], f"P{pid} waiting inconsistent"
        assert r["response"] == r["start"] - r["arrival"], f"P{pid} response inconsistent"
    for key, val in expected_avgs.items():
        assert abs(avgs[key] - val) < 0.005, f"avg {key}: got {avgs[key]}, expected {val}"


def test_required_files_exist():
    required = [
        "include/process.h",
        "include/scheduler.h",
        "include/queue.h",
        "include/metrics.h",
        "src/main.c",
        "src/queue.c",
        "src/metrics.c",
        "src/schedulers/fcfs.c",
        "src/schedulers/sjf.c",
        "src/schedulers/rr.c",
        "Makefile",
        "data/processes1.txt",
    ]
    missing = [p for p in required if not os.path.isfile(os.path.join(ROOT, p))]
    assert not missing, f"Missing files: {missing}"


def test_headers_define_expected_api():
    process_h = open(os.path.join(ROOT, "include/process.h"), encoding="utf-8").read()
    scheduler_h = open(os.path.join(ROOT, "include/scheduler.h"), encoding="utf-8").read()
    metrics_h = open(os.path.join(ROOT, "include/metrics.h"), encoding="utf-8").read()

    for field in (
        "pid",
        "arrival_time",
        "burst_time",
        "priority",
        "remaining_time",
        "start_time",
        "finish_time",
    ):
        assert field in process_h, f"process.h missing field: {field}"

    assert "typedef struct" in scheduler_h
    assert "run_fcfs" in scheduler_h
    assert "run_sjf" in scheduler_h
    assert "run_rr" in scheduler_h

    assert "metrics_turnaround" in metrics_h
    assert "metrics_waiting" in metrics_h
    assert "metrics_response" in metrics_h
    assert "metrics_print" in metrics_h


def test_make_builds():
    run(["make", "clean"])
    run(["make"])
    assert os.path.isfile(SCHEDULER), "scheduler binary was not produced"


def test_fcfs_loads_and_dispatches():
    result = run([SCHEDULER, DATA, "fcfs"])
    assert "Loaded 4 process(es)" in result.stdout
    assert "Running scheduler: fcfs" in result.stdout
    assert "Averages:" in result.stdout
    assert "Metrics unavailable" not in result.stdout


def test_sjf_loads_and_dispatches():
    result = run([SCHEDULER, DATA, "sjf"])
    assert "Loaded 4 process(es)" in result.stdout
    assert "Running scheduler: sjf" in result.stdout
    assert "Averages:" in result.stdout
    assert "Metrics unavailable" not in result.stdout


def test_rr_requires_quantum_and_dispatches():
    bad = run([SCHEDULER, DATA, "rr"], expect_ok=False)
    assert bad.returncode != 0

    result = run([SCHEDULER, DATA, "rr", "4"])
    assert "Loaded 4 process(es)" in result.stdout
    assert "Running scheduler: rr (quantum=4)" in result.stdout
    assert "Averages:" in result.stdout
    assert "Metrics unavailable" not in result.stdout


def test_unknown_scheduler_fails():
    result = run([SCHEDULER, DATA, "nope"], expect_ok=False)
    assert result.returncode != 0
    assert "Unknown scheduler" in result.stderr


def test_metrics_unit():
    run([
        "gcc",
        "-Wall",
        "-Wextra",
        "-Iinclude",
        "-o",
        "test_metrics",
        "src/metrics.c",
        "tests/test_metrics.c",
    ])
    result = run([os.path.join(ROOT, "test_metrics")])
    assert "Metrics tests passed" in result.stdout
    os.remove(os.path.join(ROOT, "test_metrics"))


# ---- Expected-value tests on data/processes1.txt ----

def test_fcfs_expected_values():
    r = run([SCHEDULER, DATA, "fcfs"])
    check_run(r, {1: (0, 8), 2: (8, 12), 3: (12, 21), 4: (21, 26)},
              {"turnaround": 15.25, "waiting": 8.75, "response": 8.75})


def test_sjf_expected_values_non_preemptive():
    # P1 must NOT be preempted by P2 (non-preemptive): P1 runs 0-8 uninterrupted.
    r = run([SCHEDULER, DATA, "sjf"])
    check_run(r, {1: (0, 8), 2: (8, 12), 4: (12, 17), 3: (17, 26)},
              {"turnaround": 14.25, "waiting": 7.75, "response": 7.75})


def test_rr_expected_values_q4():
    r = run([SCHEDULER, DATA, "rr", "4"])
    check_run(r, {1: (0, 20), 2: (4, 8), 3: (8, 26), 4: (12, 25)},
              {"turnaround": 18.25, "waiting": 11.75, "response": 4.50})


# ---- Edge cases ----

def test_idle_gap_before_first_arrival():
    data = "1 5 3 1\n"
    for algo, q in (("fcfs", None), ("sjf", None), ("rr", 2)):
        r = run_sched(algo, data, q)
        check_run(r, {1: (5, 8)}, {"turnaround": 3.0, "waiting": 0.0, "response": 0.0})


def test_idle_gap_between_jobs():
    data = "1 0 2 1\n2 10 3 1\n"
    for algo, q in (("fcfs", None), ("sjf", None), ("rr", 4)):
        r = run_sched(algo, data, q)
        check_run(r, {1: (0, 2), 2: (10, 13)},
                  {"turnaround": 2.5, "waiting": 0.0, "response": 0.0})


def test_out_of_order_input_file():
    data = "3 2 9 3\n1 0 8 2\n4 3 5 2\n2 1 4 1\n"   # same jobs as processes1.txt, shuffled
    r = run_sched("fcfs", data)
    check_run(r, {1: (0, 8), 2: (8, 12), 3: (12, 21), 4: (21, 26)},
              {"turnaround": 15.25, "waiting": 8.75, "response": 8.75})
    r = run_sched("sjf", data)
    check_run(r, {1: (0, 8), 2: (8, 12), 4: (12, 17), 3: (17, 26)},
              {"turnaround": 14.25, "waiting": 7.75, "response": 7.75})
    r = run_sched("rr", data, 4)
    check_run(r, {1: (0, 20), 2: (4, 8), 3: (8, 26), 4: (12, 25)},
              {"turnaround": 18.25, "waiting": 11.75, "response": 4.50})


def test_sjf_tie_on_burst_prefers_earlier_arrival():
    data = "1 0 5 1\n2 1 3 1\n3 2 3 1\n"
    r = run_sched("sjf", data)
    check_run(r, {1: (0, 5), 2: (5, 8), 3: (8, 11)},
              {"turnaround": 7.0, "waiting": 3.333, "response": 3.333})


def test_rr_quantum_larger_than_all_bursts_matches_fcfs():
    data = "1 0 8 2\n2 1 4 1\n3 2 9 3\n4 3 5 2\n"
    r = run_sched("rr", data, 100)
    check_run(r, {1: (0, 8), 2: (8, 12), 3: (12, 21), 4: (21, 26)},
              {"turnaround": 15.25, "waiting": 8.75, "response": 8.75})


def test_rr_quantum_one():
    data = "1 0 2 1\n2 0 2 1\n"
    r = run_sched("rr", data, 1)
    check_run(r, {1: (0, 3), 2: (1, 4)},
              {"turnaround": 3.5, "waiting": 1.5, "response": 0.5})


def test_single_process_all_algorithms():
    data = "7 0 6 1\n"
    for algo, q in (("fcfs", None), ("sjf", None), ("rr", 4)):
        r = run_sched(algo, data, q)
        check_run(r, {7: (0, 6)}, {"turnaround": 6.0, "waiting": 0.0, "response": 0.0})


def test_comments_and_blank_lines_ignored():
    data = "# header\n\n1 0 3 1\n\n# note\n2 0 2 1\n"
    r = run_sched("fcfs", data)
    rows, _ = parse_output(r.stdout)
    assert set(rows) == {1, 2}


def test_missing_file_fails():
    run([SCHEDULER, "data/does_not_exist.txt", "fcfs"], expect_ok=False)


def test_malformed_line_fails():
    r = run_sched_fail("fcfs", "1 0 abc 1\n")
    assert "Bad line" in r.stderr


def test_empty_file_fails():
    r = run_sched_fail("fcfs", "# only a comment\n")
    assert "No processes" in r.stderr


def test_bad_quantum_fails():
    data = "1 0 3 1\n"
    run_sched_fail("rr", data, 0)
    run_sched_fail("rr", data, -2)
    run_sched_fail("rr", data, "abc")


def test_too_many_args_fails():
    run([SCHEDULER, DATA, "fcfs", "4", "extra"], expect_ok=False)


def main():
    tests = [
        test_required_files_exist,
        test_headers_define_expected_api,
        test_make_builds,
        test_fcfs_loads_and_dispatches,
        test_sjf_loads_and_dispatches,
        test_rr_requires_quantum_and_dispatches,
        test_unknown_scheduler_fails,
        test_metrics_unit,
        test_fcfs_expected_values,
        test_sjf_expected_values_non_preemptive,
        test_rr_expected_values_q4,
        test_idle_gap_before_first_arrival,
        test_idle_gap_between_jobs,
        test_out_of_order_input_file,
        test_sjf_tie_on_burst_prefers_earlier_arrival,
        test_rr_quantum_larger_than_all_bursts_matches_fcfs,
        test_rr_quantum_one,
        test_single_process_all_algorithms,
        test_comments_and_blank_lines_ignored,
        test_missing_file_fails,
        test_malformed_line_fails,
        test_empty_file_fails,
        test_bad_quantum_fails,
        test_too_many_args_fails,
    ]

    failed = 0
    for test in tests:
        name = test.__name__
        try:
            test()
            print(f"PASS  {name}")
        except Exception as exc:
            failed += 1
            print(f"FAIL  {name}: {exc}")

    print()
    if failed:
        print(f"{failed}/{len(tests)} tests failed")
        return 1

    print(f"All {len(tests)} tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
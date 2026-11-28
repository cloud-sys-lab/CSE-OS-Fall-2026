import unittest
from SchedulerCPU import (
    Process,
    run_fcfs,
    run_rr,
    run_priority_aging,
)


class TestSchedulerCPU(unittest.TestCase):
    def test_fcfs_basic_metrics(self):
        procs = [
            Process(1, "p1", arrival_time=0, burst_time=4, base_priority=1),
            Process(2, "p2", arrival_time=1, burst_time=3, base_priority=1),
        ]
        res = run_fcfs(procs, ctx_cost=1)
        # p1: 0..4 (wait=0, tat=4, resp=0)
        # cs: 4..5
        # p2: 5..8 (wait=4, tat=7, resp=4)
        self.assertEqual(res.total_time, 8)
        self.assertEqual(res.context_switches, 1)
        self.assertAlmostEqual(res.avg_waiting, 2.0)
        self.assertAlmostEqual(res.avg_turnaround, 5.5)
        self.assertAlmostEqual(res.avg_response, 2.0)

    def test_idle_cpu_gap(self):
        procs = [
            Process(1, "early", arrival_time=0, burst_time=2, base_priority=1),
            Process(2, "late", arrival_time=6, burst_time=3, base_priority=1),
        ]
        for runner in (
            lambda p: run_fcfs(p, ctx_cost=1),
            lambda p: run_priority_aging(p, aging_step=3, ctx_cost=1),
        ):
            res = runner(procs)
            # p1 finishes at 2; CPU idles until 6; context switch at 6..7; p2 runs 7..10
            self.assertEqual(res.total_time, 10)
            self.assertEqual(res.context_switches, 1)

        rr_res = run_rr(procs, quantum=2, ctx_cost=1)
        self.assertEqual(rr_res.total_time, 2)
        self.assertEqual(rr_res.context_switches, 0)

    def test_rr_quantum_and_requeue(self):
        procs = [
            Process(1, "long", arrival_time=0, burst_time=5, base_priority=1),
            Process(2, "short", arrival_time=1, burst_time=2, base_priority=1),
        ]
        res = run_rr(procs, quantum=2, ctx_cost=1)
        self.assertEqual(res.total_time, 9)
        self.assertEqual(res.context_switches, 2)
        p1 = next(p for p in res.processes if p.pid == 1)
        p2 = next(p for p in res.processes if p.pid == 2)
        self.assertEqual(p2.completion_time, 7)
        self.assertEqual(p1.completion_time, 9)

    def test_priority_aging_prevents_starvation(self):
        procs = [
            Process(1, "low_bg", arrival_time=0, burst_time=4, base_priority=4),
            Process(2, "high_1", arrival_time=1, burst_time=3, base_priority=0),
            Process(3, "high_2", arrival_time=4, burst_time=3, base_priority=0),
            Process(4, "high_3", arrival_time=7, burst_time=3, base_priority=0),
        ]
        res = run_priority_aging(procs, aging_step=2, ctx_cost=1)
        low_proc = next(p for p in res.processes if p.pid == 1)
        self.assertTrue(low_proc.completed)
        self.assertLess(low_proc.current_priority, low_proc.base_priority)

    def test_input_immutability(self):
        procs = [
            Process(1, "a", arrival_time=0, burst_time=3, base_priority=2),
            Process(2, "b", arrival_time=1, burst_time=2, base_priority=1),
        ]
        run_fcfs(procs, ctx_cost=1)
        run_rr(procs, quantum=2, ctx_cost=1)
        run_priority_aging(procs, aging_step=2, ctx_cost=1)
        for p in procs:
            self.assertFalse(p.completed)
            self.assertEqual(p.remaining_time, 0)
            self.assertEqual(p.start_time, -1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
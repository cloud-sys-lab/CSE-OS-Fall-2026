#!/bin/sh
# Starts only disposable children. All remaining workers are cleaned up on exit.
set -eu
cd "$(dirname "$0")/.."
make minitask tests/demo_worker
./tests/demo_worker cpu &
cpu=$!
./tests/demo_worker memory &
memory=$!
./tests/demo_worker sleep &
sleeper=$!
cleanup() {
    kill "$cpu" "$memory" "$sleeper" 2>/dev/null || true
    wait "$cpu" "$memory" "$sleeper" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
./minitask --search mt-demo --interval 1

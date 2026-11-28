# Mini Task Manager

CSE4300 group project for Ryan Nguyen and Hunter Maxwell.

A Linux process monitor written in C. It shows real process and system data, supports sorting and search, refreshes automatically, and requests confirmation before sending SIGTERM or SIGKILL.

## Build and run

Requirements: Linux with `/proc` mounted, a C11 compiler, and GNU Make. Ubuntu under WSL works. Python 3 is needed only for the integration tests. No ncurses package or administrator privileges are required.

```sh
make
./minitask
```

Use a terminal at least 80 columns wide and 24 rows high. Type a command and press Enter. The display keeps your unfinished command while it refreshes. The first scan establishes a CPU baseline, so wait for the next refresh to see CPU usage.

On Windows, run these commands inside a Linux/WSL terminal, not Windows Command Prompt. On macOS, use a Linux VM. This program reads Linux-specific `/proc` files.

## Commands

| Command | Action |
| --- | --- |
| `sort cpu` | Highest CPU use first |
| `sort mem` | Highest resident memory first |
| `sort pid` | Lowest PID first |
| `sort name` | Case-insensitive alphabetical order |
| `search bash` | Match a name substring, ignoring case |
| `search 1234` | Match exactly PID 1234 |
| `clear` | Remove the filter |
| `next` and `prev` | Move between pages of results |
| `kill 1234` | Prepare SIGTERM for that PID |
| `force 1234` | Prepare SIGKILL for that PID |
| `yes 1234` | Confirm the specific pending signal request |
| `no` | Cancel the pending request |
| `interval 2` | Refresh every two seconds; allowed range 0.2 to 60 |
| `pause` | Pause or resume automatic collection |
| `refresh` | Collect a new sample immediately, even while paused |
| `help` | Show or hide help |
| `quit` | Exit and restore the terminal; Ctrl-C also exits |

During a pending termination request, use `yes PID`, `no`, or `quit`. Other commands do not bypass confirmation. PID 1, the manager's own PID, zero, and negative PIDs are refused. A successful signal call means the kernel accepted the signal; it does not prove the process has already exited. SIGTERM can be handled or ignored. SIGKILL cannot be caught, but a process in uninterruptible sleep may still take time to disappear.

## Plain output and repeatable demos

```sh
./minitask --batch --interval 1 --sort cpu --limit 20
./minitask --batch --iterations 3 --interval 0.5 --search bash
./minitask --help
sh tests/run_demo.sh
```

Batch mode takes an initial baseline and waits one interval before printing. Redirected input or output automatically selects batch mode. The demo script starts one CPU worker, one worker that touches 32 MiB of memory, and one sleeping worker. It filters the table to those processes and cleans them up after the manager exits. Practice termination with these disposable workers.

## Tests

```sh
make test
make soak
make sanitize
```

`make test` runs C behavior tests and Python tests against a real pseudoterminal. `make soak` runs 180 refreshes over at least three minutes. `make sanitize` rebuilds with AddressSanitizer and UndefinedBehaviorSanitizer. In environments that disallow LeakSanitizer's process inspection, use `ASAN_OPTIONS=detect_leaks=0 make sanitize`; this disables leak detection only. After a sanitizer build, run `make clean && make` for a normal build.

See `results/test_results.txt`, `results/sanitizer_results.txt`, and `results/soak_results.txt` for captured checks. The report states which checks used real processes and which remain manual. Tests signal only child processes they start.

## Source organization

| Files | Responsibility |
| --- | --- |
| `process.c/h` | Process records, CPU samples, state names, and the signal system call |
| `system_info.c/h` | Overall CPU and RAM statistics |
| `sorting.c/h` | Four deterministic sort orders |
| `search.c/h` | Case-insensitive name or exact numeric PID filtering |
| `control.c/h` | Commands, confirmation state, validation, and error messages |
| `display.c/h` | System panel, process table, help, and prompt |
| `refresh.c/h` | Single-threaded polling loop and terminal lifecycle |
| `main.c` and `Makefile` | Command-line setup and build |
| `tests/` | Automated tests and disposable demo workers |
| `docs/` | Design report, speaking notes, explanation guide, and provenance |
| `results/` | Actual captured test and demonstration evidence |

## Metric definitions and limits

- Process CPU uses change in user plus system CPU ticks divided by clock ticks per second and elapsed monotonic time. One busy core is approximately 100%; a multithreaded process may exceed 100%.
- System CPU uses busy time as a share of all logical CPU time. Idle plus I/O-wait count as non-busy in this display. Linux I/O-wait accounting is approximate.
- Process memory is approximate resident set size from `/proc/PID/stat`, multiplied by the page size. It is not a private-memory measurement. Shared pages can contribute to more than one process's RSS.
- System memory used is `MemTotal - MemAvailable`. A fallback using free memory, buffers, and cache supports older procfs layouts.
- The visible process namespace, procfs permissions, and container configuration control what can be read. Reported RAM/CPU capacity may describe the host rather than a container's cgroup limits.
- At most 8192 readable processes are sampled per refresh. The display reports when it reaches this capacity. Processes can exit between reads; unreadable rows are skipped.
- Previous CPU samples use PID plus process start time. Confirmation rechecks start time, although a narrow race remains between the check and `kill()`. This educational version does not use Linux pidfds.
- This version uses one event loop and does not implement the optional threads, mutexes, user filter, or process-detail screen.


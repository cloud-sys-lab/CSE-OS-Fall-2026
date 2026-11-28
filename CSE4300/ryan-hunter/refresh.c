#define _POSIX_C_SOURCE 200809L
#include "refresh.h"
#include "display.h"
#include "search.h"
#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static struct termios saved_terminal;
static int terminal_active;
static volatile sig_atomic_t stop_requested, suspend_requested;

static long long milliseconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static void on_stop(int signal_number) { (void)signal_number; stop_requested = 1; }
static void on_suspend(int signal_number) { (void)signal_number; suspend_requested = 1; }
static void restore_terminal(void) {
    if (terminal_active) {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_terminal);
        printf("\033[?1049l");
        fflush(stdout);
        terminal_active = 0;
    }
}
static int activate_terminal(void) {
    struct termios input = saved_terminal;
    input.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    input.c_cc[VMIN] = 0;
    input.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &input) != 0) return -1;
    terminal_active = 1;
    printf("\033[?1049h");
    fflush(stdout);
    return 0;
}

void default_run_options(RunOptions *o) {
    memset(o, 0, sizeof *o);
    o->interval_ms = 1000;
    o->limit = 20;
    o->sort = SORT_CPU;
    o->iterations = 1;
    o->batch = !(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO));
}
int run_interface(void) { RunOptions o; default_run_options(&o); return run_with_options(&o); }

static int sample(Process *list, SystemInfo *system) {
    int count = get_process_list(list, MAX_PROCESSES);
    if (count < 0 || get_system_info(system) != 0) {
        fprintf(stderr, "Could not read /proc. Run on Linux with procfs mounted.\n");
        return -1;
    }
    return count;
}

int run_with_options(const RunOptions *options) {
    Process *list = calloc(MAX_PROCESSES, sizeof *list);
    Process *view = calloc(MAX_PROCESSES, sizeof *view);
    if (!list || !view) { free(list); free(view); return 1; }
    ControlState state;
    control_init(&state);
    state.sort = options->sort;
    state.interval_ms = options->interval_ms;
    snprintf(state.query, sizeof state.query, "%s", options->query);
    struct sigaction action = {0};
    sigemptyset(&action.sa_mask);
    action.sa_handler = on_stop;
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    stop_requested = suspend_requested = 0;
    int result = 0;
    SystemInfo system;
    int count = sample(list, &system);
    if (count < 0) { result = 1; goto done; }
    if (options->batch) {
        for (int i = 0; i < options->iterations && !stop_requested; ++i) {
            int status = poll(NULL, 0, state.interval_ms);
            if (status < 0 && errno != EINTR) { result = 1; break; }
            if (stop_requested) break;
            count = sample(list, &system);
            if (count < 0) { result = 1; break; }
            int matches = search_processes(list, count, state.query, view);
            sort_processes(view, matches, state.sort);
            if (i) puts("");
            display_frame(view, matches, count, &system, &state, "", 0, options->limit);
        }
        goto done;
    }
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) ||
        tcgetattr(STDIN_FILENO, &saved_terminal) != 0 || activate_terminal() != 0) {
        fprintf(stderr, "Interactive mode needs a terminal. Try --batch.\n");
        result = 1; goto done;
    }
    atexit(restore_terminal);
    action.sa_handler = on_suspend;
    sigaction(SIGTSTP, &action, NULL);
    char input[512] = "";
    size_t input_length = 0;
    int escape = 0, dirty = 1, baseline = 1;
    long long next_refresh = milliseconds() + state.interval_ms;
    while (!state.quit && !stop_requested) {
        if (suspend_requested) {
            suspend_requested = 0;
            restore_terminal();
            raise(SIGSTOP);
            if (activate_terminal() != 0) { result = 1; break; }
            state.needs_refresh = 1;
        }
        long long now = milliseconds();
        if (state.needs_refresh || (!state.paused && now >= next_refresh)) {
            count = sample(list, &system);
            if (count < 0) { result = 1; break; }
            if (baseline) {
                if (!state.pending && !strncmp(state.message, "First sample is a baseline.", 26))
                    snprintf(state.message, sizeof state.message, "Ready. Type 'help' for commands.");
                baseline = 0;
            }
            if (count == MAX_PROCESSES && !state.pending)
                snprintf(state.message, sizeof state.message, "List reached %d-process capacity; more may exist.", MAX_PROCESSES);
            next_refresh = milliseconds() + state.interval_ms;
            state.needs_refresh = 0;
            dirty = 1;
        }
        if (dirty) {
            int matches = search_processes(list, count, state.query, view);
            sort_processes(view, matches, state.sort);
            display_frame(view, matches, count, &system, &state, input, 1, options->limit);
            dirty = 0;
        }
        long long remaining = next_refresh - milliseconds();
        int timeout = state.paused || remaining > 200 ? 200 : remaining > 0 ? (int)remaining : 0;
        struct pollfd fd = {STDIN_FILENO, POLLIN, 0};
        int status = poll(&fd, 1, timeout);
        if (status < 0) { if (errno == EINTR) continue; result = 1; break; }
        if (fd.revents & (POLLHUP | POLLERR | POLLNVAL)) break;
        if (!(fd.revents & POLLIN)) continue;
        char bytes[128];
        ssize_t n = read(STDIN_FILENO, bytes, sizeof bytes);
        if (n < 0) { if (errno == EINTR) continue; result = 1; break; }
        for (ssize_t i = 0; i < n; ++i) {
            unsigned char c = (unsigned char)bytes[i];
            if (escape) { if (c >= 0x40 && c <= 0x7e && c != '[') escape = 0; continue; }
            if (c == 27) { escape = 1; continue; }
            if (c == 4) { state.quit = 1; break; }
            if (c == '\n' || c == '\r') {
                input[input_length] = '\0';
                int old_interval = state.interval_ms;
                control_command(&state, input);
                if (old_interval != state.interval_ms) next_refresh = milliseconds() + state.interval_ms;
                input_length = 0;
            } else if (c == 127 || c == 8) {
                if (input_length) --input_length;
            } else if (c == 21) input_length = 0;
            else if (isprint(c) && input_length + 1 < sizeof input) input[input_length++] = (char)c;
            input[input_length] = '\0';
            dirty = 1;
        }
    }
done:
    restore_terminal();
    cleanup_process_cache();
    free(view);
    free(list);
    return result;
}

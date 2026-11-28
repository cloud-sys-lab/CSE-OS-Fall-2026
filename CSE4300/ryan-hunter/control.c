#define _POSIX_C_SOURCE 200809L
#include "control.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static void message(ControlState *s, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(s->message, sizeof s->message, format, args);
    va_end(args);
}

static int parse_pid(const char *text, pid_t *out) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    while (isspace((unsigned char)*end)) ++end;
    if (errno || end == text || *end || value <= 0 || value > INT_MAX) return -1;
    *out = (pid_t)value;
    return 0;
}

int parse_interval_ms(const char *text, int *out) {
    if (!text || !out) return -1;
    char *end;
    errno = 0;
    double seconds = strtod(text, &end);
    while (isspace((unsigned char)*end)) ++end;
    if (errno || end == text || *end || !isfinite(seconds) || seconds < 0.2 || seconds > 60.0)
        return -1;
    *out = (int)(seconds * 1000.0 + 0.5);
    return 0;
}

const char *signal_error_message(int result) {
    switch (result) {
        case 0: return "Signal sent. Check the next refresh for the process state.";
        case -EPERM: return "Permission denied: you cannot signal that process.";
        case -ESRCH: return "Process not found: it may already have exited.";
        case -EINVAL: return "Refused: PID 1, this program, and invalid PIDs are protected.";
        default: return "Could not send the signal.";
    }
}

void control_init(ControlState *s) {
    memset(s, 0, sizeof *s);
    s->sort = SORT_CPU;
    s->interval_ms = 1000;
    message(s, "First sample is a baseline. CPU usage updates after one interval.");
}

void kill_selected(ControlState *s, pid_t pid, int force) {
    s->pending = 0;
    if (pid <= 1 || pid == getpid() || (force != 0 && force != 1)) {
        message(s, "%s", signal_error_message(-EINVAL));
        return;
    }
    if (get_process_info(pid, &s->target) != 0) {
        int result = kill(pid, 0) == -1 ? -errno : -EIO;
        message(s, "%s", signal_error_message(result));
        return;
    }
    if (s->target.state == 'Z' || s->target.state == 'X') {
        message(s, "PID %ld has already exited; its parent must reap it.", (long)pid);
        return;
    }
    s->pending = 1;
    s->pending_force = force;
    message(s, "Send %s to %ld (%s)? Type 'yes %ld' or 'no'.",
            force ? "SIGKILL" : "SIGTERM", (long)pid, s->target.name, (long)pid);
}

static void confirm_signal(ControlState *s, const char *command, const char *arg) {
    if (!strcasecmp(command, "no") || !strcasecmp(command, "cancel")) {
        s->pending = 0;
        message(s, "Cancelled. No signal sent.");
        return;
    }
    pid_t pid;
    if (strcasecmp(command, "yes") || parse_pid(arg, &pid) != 0 || pid != s->target.pid) {
        message(s, "Confirmation required: type 'yes %ld' or 'no'.", (long)s->target.pid);
        return;
    }
    s->pending = 0;
    Process current;
    if (get_process_info(pid, &current) != 0) {
        message(s, "%s", signal_error_message(-ESRCH));
        return;
    }
    if (current.start_time_ticks != s->target.start_time_ticks) {
        message(s, "PID %ld now belongs to another process. No signal sent.", (long)pid);
        return;
    }
    if (current.state == 'Z' || current.state == 'X') {
        message(s, "PID %ld has already exited; its parent must reap it.", (long)pid);
        return;
    }
    int result = terminate_process(pid, s->pending_force);
    if (result == 0)
        message(s, "%s sent to PID %ld. Delivery does not guarantee immediate exit.",
                s->pending_force ? "SIGKILL" : "SIGTERM", (long)pid);
    else message(s, "%s", signal_error_message(result));
    s->needs_refresh = 1;
}

void control_command(ControlState *s, const char *line) {
    if (!s || !line) return;
    char buffer[512];
    if (strlen(line) >= sizeof buffer) { message(s, "Command is too long."); return; }
    snprintf(buffer, sizeof buffer, "%s", line);
    char *command = buffer;
    while (isspace((unsigned char)*command)) ++command;
    char *end = command + strlen(command);
    while (end > command && isspace((unsigned char)end[-1])) *--end = '\0';
    if (!*command) return;
    char *arg = command;
    while (*arg && !isspace((unsigned char)*arg)) ++arg;
    if (*arg) *arg++ = '\0';
    while (isspace((unsigned char)*arg)) ++arg;

    if (!strcasecmp(command, "quit") || !strcasecmp(command, "q")) { s->quit = 1; return; }
    if (s->pending) { confirm_signal(s, command, arg); return; }
    if (!strcasecmp(command, "sort")) {
        if (parse_sort_type(arg, &s->sort) != 0) message(s, "Use: sort cpu|mem|pid|name");
        else { s->page = 0; message(s, "Sorted by %s.", sort_type_name(s->sort)); }
    } else if (!strcasecmp(command, "search") || !strcmp(command, "/")) {
        if (strlen(arg) >= sizeof s->query) message(s, "Search text is too long (255 characters maximum).");
        else {
            snprintf(s->query, sizeof s->query, "%s", arg);
            s->page = 0;
            message(s, *arg ? "Filter active. Use 'clear' to show all processes." : "Filter cleared.");
        }
    } else if (!strcasecmp(command, "clear")) {
        s->query[0] = '\0'; s->page = 0; message(s, "Filter cleared.");
    } else if (!strcasecmp(command, "kill") || !strcasecmp(command, "force")) {
        pid_t pid;
        if (parse_pid(arg, &pid) != 0) message(s, "Use: %s POSITIVE_PID", command);
        else kill_selected(s, pid, !strcasecmp(command, "force"));
    } else if (!strcasecmp(command, "interval")) {
        if (parse_interval_ms(arg, &s->interval_ms) != 0)
            message(s, "Interval must be between 0.2 and 60 seconds.");
        else message(s, "Refresh interval: %.1f seconds.", s->interval_ms / 1000.0);
    } else if (!strcasecmp(command, "pause")) {
        s->paused = !s->paused;
        if (!s->paused) s->needs_refresh = 1;
        message(s, s->paused ? "Automatic refresh paused. 'refresh' samples once." : "Automatic refresh resumed.");
    } else if (!strcasecmp(command, "refresh") || !strcasecmp(command, "r")) {
        s->needs_refresh = 1; message(s, "Refreshed.");
    } else if (!strcasecmp(command, "next")) {
        if (s->page < INT_MAX) ++s->page;
    } else if (!strcasecmp(command, "prev")) {
        if (s->page > 0) --s->page;
    } else if (!strcasecmp(command, "help")) {
        s->show_help = !s->show_help;
        message(s, "Help %s. Press Enter after each command.", s->show_help ? "opened" : "closed");
    } else message(s, "Unknown command. Type 'help' to see commands.");
}

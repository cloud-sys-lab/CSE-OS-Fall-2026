#define _POSIX_C_SOURCE 200809L
#include "display.h"
#include <stdarg.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int columns = 110;
static void line(const char *format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    printf("%.*s\n", columns - 1, text);
}

void display_system_stats(const SystemInfo *info) {
    line("System CPU: %5.1f%%   Logical CPUs: %d   Process directories: %d",
         info->cpu_percent, info->num_cpus, info->num_processes);
    line("RAM: %.1f / %.1f MiB used (%4.1f%%)   Available: %.1f MiB",
         info->mem_used_kb / 1024.0, info->mem_total_kb / 1024.0,
         info->mem_percent, info->mem_available_kb / 1024.0);
}

void display_processes(const Process *list, int count) {
    line("%7s  %-9s %7s %7s %10s  %s", "PID", "STATE", "CPU%", "MEM%", "RSS MiB", "NAME");
    line("-------  --------- ------- ------- ----------  ------------------------------");
    for (int i = 0; i < count; ++i)
        line("%7ld  %-9s %7.1f %7.2f %10.2f  %s", (long)list[i].pid,
             process_state_name(list[i].state), list[i].cpu_percent, list[i].mem_percent,
             list[i].mem_kb / 1024.0, list[i].name);
    if (count == 0) line("No matching processes.");
}

void display_frame(const Process *list, int count, int sampled, const SystemInfo *info,
                   ControlState *state, const char *input, int interactive, int limit) {
    struct winsize size = {0};
    int rows = 36;
    columns = 110;
    if (interactive && ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_col > 0) columns = size.ws_col;
        if (size.ws_row > 0) rows = size.ws_row;
    }
    if (columns < 20) columns = 20;
    if (interactive) printf("\033[H\033[2J");
    time_t now = time(NULL);
    struct tm tm;
    char timestamp[32];
    localtime_r(&now, &tm);
    strftime(timestamp, sizeof timestamp, "%Y-%m-%d %H:%M:%S", &tm);
    line("MINI TASK MANAGER   %s", timestamp);
    display_system_stats(info);
    int page_size = interactive ? rows - 15 : limit;
    if (page_size < 1) page_size = 1;
    if (page_size > limit) page_size = limit;
    int pages = count > 0 ? (count - 1) / page_size + 1 : 1;
    if (state->page >= pages) state->page = pages - 1;
    int first = state->page * page_size;
    int shown = count - first;
    if (shown > page_size) shown = page_size;
    line("Sort: %s   Filter: %s   Matches: %d/%d   Page: %d/%d   %s %.1fs",
         sort_type_name(state->sort), *state->query ? state->query : "all",
         count, sampled, state->page + 1, pages, state->paused ? "PAUSED" : "Refresh",
         state->interval_ms / 1000.0);
    line("");
    if (interactive && state->show_help) {
        line("COMMANDS (type a command, then press Enter)");
        line("sort cpu|mem|pid|name     CPU/memory descending; PID/name ascending");
        line("search TEXT              Name substring (case-insensitive), or exact numeric PID");
        line("clear                    Remove the search filter");
        line("kill PID                 Request SIGTERM, then confirm with 'yes PID'");
        line("force PID                Request SIGKILL, then confirm with 'yes PID'");
        line("no                       Cancel a pending signal request");
        line("interval SECONDS         Set refresh interval from 0.2 to 60 seconds");
        line("pause / refresh          Toggle automatic refresh / sample once");
        line("next / prev              Move between pages");
        line("help / quit              Close help / exit; Ctrl-C also exits");
    } else display_processes(list + first, shown);
    line("");
    if (interactive) {
        line("sort cpu|mem|pid|name   search TEXT   clear   next   prev");
        line("kill PID   force PID   yes PID / no   interval SEC   pause   refresh   help   quit");
        line("%s", state->message);
        int available = columns > 3 ? columns - 3 : 1;
        const char *tail = input;
        size_t length = 0;
        while (input[length]) ++length;
        if (length > (size_t)available) tail += length - (size_t)available;
        printf("> %.*s", available, tail);
    } else line("CPU%%: one full core = 100%%. RSS is approximate. First baseline sample is omitted.");
    fflush(stdout);
}

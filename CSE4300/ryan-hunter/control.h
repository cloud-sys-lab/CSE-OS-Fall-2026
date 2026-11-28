#ifndef CONTROL_H
#define CONTROL_H
#include "process.h"
#include "sorting.h"

#define QUERY_MAX 256
#define MESSAGE_MAX 384
typedef struct {
    SortType sort;
    char query[QUERY_MAX];
    int interval_ms;
    int paused;
    int quit;
    int page;
    int needs_refresh;
    int show_help;
    int pending;
    int pending_force;
    Process target;
    char message[MESSAGE_MAX];
} ControlState;

void control_init(ControlState *state);
void control_command(ControlState *state, const char *line);
void kill_selected(ControlState *state, pid_t pid, int force);
const char *signal_error_message(int result);
int parse_interval_ms(const char *text, int *out);
#endif

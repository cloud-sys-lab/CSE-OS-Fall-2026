#ifndef DISPLAY_H
#define DISPLAY_H
#include "control.h"
#include "system_info.h"
void display_processes(const Process *list, int count);
void display_system_stats(const SystemInfo *info);
void display_frame(const Process *list, int count, int sampled, const SystemInfo *info,
                   ControlState *state, const char *input, int interactive, int limit);
#endif

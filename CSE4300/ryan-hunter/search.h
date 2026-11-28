#ifndef SEARCH_H
#define SEARCH_H
#include "process.h"
/* out must hold count entries; out == list is supported. Numeric queries use exact PID. */
int search_processes(const Process *list, int count, const char *query, Process *out);
#endif

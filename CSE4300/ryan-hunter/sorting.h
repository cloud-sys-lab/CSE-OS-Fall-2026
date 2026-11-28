#ifndef SORTING_H
#define SORTING_H
#include "process.h"

typedef enum { SORT_CPU, SORT_MEM, SORT_PID, SORT_NAME } SortType;
void sort_processes(Process *list, int count, SortType type);
const char *sort_type_name(SortType type);
int parse_sort_type(const char *name, SortType *out);
#endif

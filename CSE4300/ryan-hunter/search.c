#include "search.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static int contains_ignore_case(const char *text, const char *query, size_t length) {
    for (; *text; ++text) {
        size_t i = 0;
        while (i < length && text[i] &&
               tolower((unsigned char)text[i]) == tolower((unsigned char)query[i])) ++i;
        if (i == length) return 1;
    }
    return 0;
}

int search_processes(const Process *list, int count, const char *query, Process *out) {
    if (count < 0 || !query || (count > 0 && (!list || !out))) return -1;
    while (isspace((unsigned char)*query)) ++query;
    size_t length = strlen(query);
    while (length && isspace((unsigned char)query[length - 1])) --length;
    int numeric = length > 0;
    for (size_t i = 0; i < length; ++i)
        if (!isdigit((unsigned char)query[i])) numeric = 0;
    long pid = -1;
    if (numeric) {
        char *end;
        errno = 0;
        pid = strtol(query, &end, 10);
        if (errno || pid <= 0 || pid > INT_MAX) return 0;
    }
    int matches = 0;
    for (int i = 0; i < count; ++i)
        if (!length || (numeric ? list[i].pid == (pid_t)pid :
                        contains_ignore_case(list[i].name, query, length)))
            out[matches++] = list[i];
    return matches;
}

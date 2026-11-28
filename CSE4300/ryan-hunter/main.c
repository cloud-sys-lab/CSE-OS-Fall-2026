#define _POSIX_C_SOURCE 200809L
#include "refresh.h"
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(FILE *out) {
    fprintf(out,
        "Usage: ./minitask [--batch] [--iterations N] [--interval SEC]\n"
        "                  [--sort cpu|mem|pid|name] [--search QUERY] [--limit N]\n"
        "Live Linux process monitor. No root access required.\n"
        "  --batch          Print plain snapshots, then exit (default for redirected I/O).\n"
        "  --iterations N   Number of batch snapshots, 1..100000 (implies --batch).\n"
        "  --interval SEC   Refresh/sample interval, 0.2..60 seconds (default 1).\n"
        "  --limit N        Maximum rows per snapshot/page, 1..8192 (default 20).\n"
        "  --help           Show this help.\n"
        "Interactive commands are shown on screen. Enter 'help' for details.\n");
}
static int positive(const char *text, int max, int *out) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < 1 || value > max) return -1;
    *out = (int)value;
    return 0;
}
int main(int argc, char **argv) {
    RunOptions options;
    default_run_options(&options);
    static const struct option long_options[] = {
        {"batch", no_argument, NULL, 'b'}, {"iterations", required_argument, NULL, 'n'},
        {"interval", required_argument, NULL, 'i'}, {"sort", required_argument, NULL, 's'},
        {"search", required_argument, NULL, 'f'}, {"limit", required_argument, NULL, 'l'},
        {"help", no_argument, NULL, 'h'}, {NULL, 0, NULL, 0}
    };
    int c;
    while ((c = getopt_long(argc, argv, "bn:i:s:f:l:h", long_options, NULL)) != -1) {
        switch (c) {
            case 'b': options.batch = 1; break;
            case 'n': if (positive(optarg, 100000, &options.iterations)) goto invalid;
                      options.batch = 1; break;
            case 'i': if (parse_interval_ms(optarg, &options.interval_ms)) goto invalid; break;
            case 's': if (parse_sort_type(optarg, &options.sort)) goto invalid; break;
            case 'f': if (strlen(optarg) >= sizeof options.query) goto invalid;
                      snprintf(options.query, sizeof options.query, "%s", optarg); break;
            case 'l': if (positive(optarg, MAX_PROCESSES, &options.limit)) goto invalid; break;
            case 'h': usage(stdout); return 0;
            default: goto invalid;
        }
    }
    if (optind != argc) goto invalid;
    return run_with_options(&options);
invalid:
    fprintf(stderr, "Invalid arguments. Use --help for supported options.\n");
    return 2;
}

/*
 * Example usage of the log.h API.
 *
 * Compile with:
 *   mkdir -p build && cd build && cmake .. && make
 *   ./log_example
 */

#include "log.h"
#include <stdio.h>
#include <string.h>

/* A custom callback that counts log calls per level */
typedef struct {
    int counts[6];
} stats_t;

static stats_t stats;

static void stats_callback(log_event_t *ev) {
    if (ev->level >= 0 && ev->level <= LOG_FATAL) {
        stats.counts[ev->level]++;
    }
}

int main(void) {
    printf("=== log.h Example App ===\n\n");

    /* --- Basic logging --- */
    printf("-- Basic logging (default: level=TRACE, quiet=false) --\n");
    log_trace("trace message: %s", "detailed diagnostic info");
    log_debug("debug message: value=%d", 42);
    log_info("hello, world!");
    log_warn("warning: disk space low (%d%%)", 15);
    log_error("error: failed to open file '%s'", "config.ini");
    log_fatal("fatal: out of memory");
    printf("\n");

    /* --- Level filtering --- */
    printf("-- Level filtering (set to LOG_WARN) --\n");
    log_set_level(LOG_WARN);
    log_trace("you should NOT see this");
    log_debug("you should NOT see this");
    log_info("you should NOT see this");
    log_warn("you SHOULD see this warning");
    log_error("you SHOULD see this error");
    log_fatal("you SHOULD see this fatal");
    printf("\n");

    /* Reset level */
    log_set_level(LOG_TRACE);

    /* --- Quiet mode --- */
    printf("-- Quiet mode (stderr suppressed, callbacks still fire) --\n");
    log_set_quiet(true);
    log_info("this goes to callbacks only (none registered yet)");
    log_set_quiet(false);
    printf("\n");

    /* --- Verbosity (log_output / out_* macros) --- */
    printf("-- Verbosity (log_output / out_* macros) --\n");
    log_set_verbosity(OUT_NORMAL);
    out_error("error output (always printed)");
    out_info("normal output (printed at NORMAL)");
    out_verbose("verbose output (SUPPRESSED at NORMAL)");
    log_set_verbosity(OUT_VERBOSE);
    out_verbose("verbose output (printed at VERBOSE)");
    log_set_verbosity(OUT_NORMAL);
    printf("\n");

    /* --- log_data --- */
    printf("-- log_data (raw data to stdout) --\n");
    out_data("raw data line 1\n");
    out_data("raw data line 2\n");
    printf("\n");

    /* --- File logging --- */
    printf("-- File logging (see /tmp/example.log) --\n");
    FILE *fp = fopen("/tmp/example.log", "w");
    if (fp) {
        log_add_fp(fp, LOG_TRACE);
        log_info("this is written to both stderr and the file");
        log_set_quiet(true);
        log_info("this is written to the file only");
        log_set_quiet(false);
        fclose(fp);
    }
    printf("\n");

    /* --- Custom callback --- */
    printf("-- Custom callback (stats collector) --\n");
    memset(&stats, 0, sizeof(stats));
    log_add_callback(stats_callback, NULL, LOG_TRACE);
    log_trace("trace");
    log_debug("debug");
    log_info("info");
    log_warn("warn");
    log_error("error");
    log_fatal("fatal");
    printf("  trace count: %d\n", stats.counts[LOG_TRACE]);
    printf("  debug count: %d\n", stats.counts[LOG_DEBUG]);
    printf("  info count:  %d\n", stats.counts[LOG_INFO]);
    printf("  warn count:  %d\n", stats.counts[LOG_WARN]);
    printf("  error count: %d\n", stats.counts[LOG_ERROR]);
    printf("  fatal count: %d\n", stats.counts[LOG_FATAL]);
    printf("\n");

    /* --- Rotating file --- */
#ifdef _WIN32
    printf("-- Rotating file (see rotate_example.log*) --\n");
    log_add_rotate_file("rotate_example.log", LOG_INFO, 256, 3);
    printf("  done \xe2\x80\x94 check rotate_example.log*\n");
#else
    printf("-- Rotating file (see /tmp/rotate_example.log*) --\n");
    log_add_rotate_file("/tmp/rotate_example.log", LOG_INFO, 256, 3);
    printf("  done \xe2\x80\x94 check /tmp/rotate_example.log*\n");
#endif
    printf("\n");

    /* --- Level string --- */
    printf("-- log_level_string --\n");
    printf("  LOG_TRACE -> %s\n", log_level_string(LOG_TRACE));
    printf("  LOG_DEBUG -> %s\n", log_level_string(LOG_DEBUG));
    printf("  LOG_INFO  -> %s\n", log_level_string(LOG_INFO));
    printf("  LOG_WARN  -> %s\n", log_level_string(LOG_WARN));
    printf("  LOG_ERROR -> %s\n", log_level_string(LOG_ERROR));
    printf("  LOG_FATAL -> %s\n", log_level_string(LOG_FATAL));
    printf("\n");

    printf("=== Done ===\n");
    return 0;
}

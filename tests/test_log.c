/*
 * Unit tests for log.h public API using the µnit testing framework.
 *
 * Build and run:
 *   mkdir -p build && cd build && cmake .. && make
 *   ./tests/test_log
 */

#include <log.h>
#include <munit.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#include <process.h>
#endif

/*
 * Platform-safe wrappers for MSVC-deprecated POSIX/CRT names.
 * Avoids _CRT_SECURE_NO_WARNINGS / _CRT_NONSTDC_NO_DEPRECATE.
 */
#if defined(_MSC_VER)
#define p_getpid()      _getpid()
#define p_dup2(a, b)    _dup2(a, b)
#define p_close(fd)     _close(fd)
#define p_fileno(s)     _fileno(s)
#else
#define p_getpid()      getpid()
#define p_dup2(a, b)    dup2(a, b)
#define p_close(fd)     close(fd)
#define p_fileno(s)     fileno(s)
#endif

/* ------------------------------------------------------------------
 * Capture callback — stores the last event for inspection
 * ------------------------------------------------------------------ */
typedef struct {
    int         call_count;
    int         last_level;
    int         last_line;
    char        last_file[256];
    char        last_msg[4096];
} capture_t;

static capture_t g_capture;

static void capture_cb(log_event_t *ev) {
    g_capture.call_count++;
    g_capture.last_level = ev->level;
    g_capture.last_line  = ev->line;
    if (ev->file) {
#if defined(_MSC_VER)
        strncpy_s(g_capture.last_file, sizeof(g_capture.last_file), ev->file, _TRUNCATE);
#else
        strncpy(g_capture.last_file, ev->file, sizeof(g_capture.last_file) - 1);
        g_capture.last_file[sizeof(g_capture.last_file) - 1] = '\0';
#endif
    }
    vsnprintf(g_capture.last_msg, sizeof(g_capture.last_msg), ev->fmt, ev->ap);
}

static void capture_reset(void) {
    memset(&g_capture, 0, sizeof(g_capture));
}

/* Track whether the capture callback has been installed */
static int g_capture_installed = 0;

/* Extra callback for test_add_callback */
static int g_extra_count = 0;

static void extra_cb(log_event_t *ev) {
    (void)ev;
    g_extra_count++;
}

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */

/* Read the full content of a file into a heap-allocated string.
 * Caller must free().  Returns NULL on error. */
static char *slurp_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (len < 0) {
        fclose(fp);
        return NULL;
    }

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t n = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[n] = '\0';
    return buf;
}

/* Return the path of a temporary file.  Each call returns a unique
 * name based on a static counter so parallel / sequential tests don't
 * collide.  The returned string is valid until the next call. */
static const char *tmp_path(void) {
    static int counter = 0;
    static char buf[128];
#ifdef _WIN32
    snprintf(buf, sizeof(buf), "log_test_%d_%d.log", (int)p_getpid(), counter++);
#else
    snprintf(buf, sizeof(buf), "/tmp/log_test_%d_%d.log", (int)p_getpid(), counter++);
#endif
    return buf;
}

/* ------------------------------------------------------------------
 * Test: log_level_string
 * ------------------------------------------------------------------ */
static MunitResult test_level_string(const MunitParameter params[],
                                     void *user_data) {
    (void)params;
    (void)user_data;

    munit_assert_string_equal(log_level_string(LOG_TRACE), "TRACE");
    munit_assert_string_equal(log_level_string(LOG_DEBUG), "DEBUG");
    munit_assert_string_equal(log_level_string(LOG_INFO),  "INFO");
    munit_assert_string_equal(log_level_string(LOG_WARN),  "WARN");
    munit_assert_string_equal(log_level_string(LOG_ERROR), "ERROR");
    munit_assert_string_equal(log_level_string(LOG_FATAL), "FATAL");

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: log_set_level — verify global level filtering on stderr
 * ------------------------------------------------------------------ */
static MunitResult test_set_level(const MunitParameter params[],
                                  void *user_data) {
    (void)params;
    (void)user_data;

    /* To test log_set_level we capture stderr output, since the global
     * level only controls the built-in stream callback to stderr.
     * User-registered callbacks have their own independent level filter. */

    const char *err_path = tmp_path();
    FILE *err_fp = fopen(err_path, "w");
    munit_assert_not_null(err_fp);

    int orig_stderr = dup(p_fileno(stderr));
    munit_assert_int(orig_stderr, >, 0);

    /* Set level to WARN so only WARN/ERROR/FATAL go to stderr */
    log_set_level(LOG_WARN);
    log_set_quiet(false);

    fflush(stderr);
    p_dup2(p_fileno(err_fp), p_fileno(stderr));
    fclose(err_fp);

    log_trace("should NOT appear");
    log_debug("should NOT appear");
    log_info("should NOT appear");
    log_warn("warn msg");
    log_error("error msg");
    log_fatal("fatal msg");

    /* Restore stderr */
    fflush(stderr);
    p_dup2(orig_stderr, p_fileno(stderr));
    p_close(orig_stderr);

    /* Verify only WARN+ messages appear */
    char *content = slurp_file(err_path);
    munit_assert_not_null(content);

    munit_assert(strstr(content, "warn msg") != NULL);
    munit_assert(strstr(content, "error msg") != NULL);
    munit_assert(strstr(content, "fatal msg") != NULL);
    /* These should be filtered out */
    munit_assert(strstr(content, "should NOT appear") == NULL);

    free(content);
    remove(err_path);

    log_set_level(LOG_TRACE);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: log_set_quiet — quiet=true suppresses stderr but callbacks fire
 * ------------------------------------------------------------------ */
static MunitResult test_set_quiet(const MunitParameter params[],
                                  void *user_data) {
    (void)params;
    (void)user_data;

    if (!g_capture_installed) {
        log_add_callback(capture_cb, NULL, LOG_TRACE);
        g_capture_installed = 1;
    }

    int prev_level = LOG_TRACE;
    log_set_level(LOG_TRACE);

    /* With quiet=false, the stream_callback writes to stderr AND our
     * capture callback fires.  We can't easily test stderr content
     * here, but we can verify the callback still fires while quiet. */
    log_set_quiet(true);
    capture_reset();
    log_info("quiet test");
    munit_assert_int(g_capture.call_count, ==, 1);
    munit_assert_string_equal(g_capture.last_msg, "quiet test");

    log_set_quiet(false);

    /* Restore */
    log_set_level(prev_level);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: verbosity get/set
 * ------------------------------------------------------------------ */
static MunitResult test_verbosity(const MunitParameter params[],
                                  void *user_data) {
    (void)params;
    (void)user_data;

    int prev = log_get_verbosity();

    log_set_verbosity(OUT_QUIET);
    munit_assert_int(log_get_verbosity(), ==, OUT_QUIET);

    log_set_verbosity(OUT_NORMAL);
    munit_assert_int(log_get_verbosity(), ==, OUT_NORMAL);

    log_set_verbosity(OUT_VERBOSE);
    munit_assert_int(log_get_verbosity(), ==, OUT_VERBOSE);

    /* Verify clamping: negative values become OUT_QUIET */
    log_set_verbosity(-1);
    munit_assert_int(log_get_verbosity(), ==, OUT_QUIET);

    /* Restore */
    log_set_verbosity(prev);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: log_output at different verbosity levels
 *
 * out_info (OUT_NORMAL) → stdout
 * out_verbose (OUT_VERBOSE) → stdout
 * out_error (OUT_ERROR) → stderr
 * ------------------------------------------------------------------ */
static MunitResult test_log_output(const MunitParameter params[],
                                   void *user_data) {
    (void)params;
    (void)user_data;

    int prev_verb = log_get_verbosity();

    /* Redirect both stdout and stderr.
     * Use separate paths (tmp_path uses a static buffer, so capture
     * both values before the buffer is overwritten). */
    const char *out_base = tmp_path();
    const char *err_base = tmp_path();
    char out_path[256], err_path[256];
    snprintf(out_path, sizeof(out_path), "%s.out", out_base);
    snprintf(err_path, sizeof(err_path), "%s.err", err_base);

    FILE *out_fp = fopen(out_path, "w");
    munit_assert_not_null(out_fp);
    FILE *err_fp = fopen(err_path, "w");
    munit_assert_not_null(err_fp);

    int orig_stdout = dup(p_fileno(stdout));
    int orig_stderr = dup(p_fileno(stderr));
    munit_assert_int(orig_stdout, >, 0);
    munit_assert_int(orig_stderr, >, 0);

    fflush(stdout);
    fflush(stderr);
    p_dup2(p_fileno(out_fp), p_fileno(stdout));
    p_dup2(p_fileno(err_fp), p_fileno(stderr));
    fclose(out_fp);
    fclose(err_fp);

    /* Test: at OUT_NORMAL, out_info prints, out_verbose does not */
    log_set_verbosity(OUT_NORMAL);
    out_info("normal visible");
    out_verbose("verbose hidden");
    out_error("error always");

    /* Restore stdout/stderr */
    fflush(stdout);
    fflush(stderr);
    p_dup2(orig_stdout, p_fileno(stdout));
    p_dup2(orig_stderr, p_fileno(stderr));
    p_close(orig_stdout);
    p_close(orig_stderr);

    /* Read captured stdout */
    char *out_content = slurp_file(out_path);
    munit_assert_not_null(out_content);
    munit_assert_string_equal(out_content, "normal visible\n");
    free(out_content);
    remove(out_path);

    /* Read captured stderr */
    char *err_content = slurp_file(err_path);
    munit_assert_not_null(err_content);
    munit_assert_string_equal(err_content, "error always\n");
    free(err_content);
    remove(err_path);

    /* Restore */
    log_set_verbosity(prev_verb);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: log_add_callback — verify callback data
 * ------------------------------------------------------------------ */
static MunitResult test_add_callback(const MunitParameter params[],
                                     void *user_data) {
    (void)params;
    (void)user_data;

    /* Each munit test runs in a forked process, so register the
     * capture callback here (it is a fresh process). */
    if (!g_capture_installed) {
        log_add_callback(capture_cb, NULL, LOG_TRACE);
        g_capture_installed = 1;
    }

    g_extra_count = 0;

    int rc = log_add_callback(extra_cb, NULL, LOG_INFO);
    munit_assert_int(rc, ==, 0);

    log_set_level(LOG_TRACE);
    log_set_quiet(true);

    /* Log at INFO — both capture_cb and extra_cb should fire */
    capture_reset();
    log_info("cb test");
    munit_assert_int(g_capture.call_count, ==, 1);
    munit_assert_int(g_extra_count, ==, 1);

    /* Log at DEBUG — extra_cb level is INFO so it should NOT fire */
    g_extra_count = 0;
    capture_reset();
    log_debug("debug only");
    munit_assert_int(g_capture.call_count, ==, 1);
    munit_assert_int(g_extra_count, ==, 0);

    log_set_quiet(false);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: log_add_fp — logging to a file
 * ------------------------------------------------------------------ */
static MunitResult test_add_fp(const MunitParameter params[],
                               void *user_data) {
    (void)params;
    (void)user_data;

    const char *fp_path = tmp_path();
    FILE *fp = fopen(fp_path, "w");
    munit_assert_not_null(fp);

    log_clear_callbacks();
    g_capture_installed = 0;
    int rc = log_add_fp(fp, LOG_INFO);
    munit_assert_int(rc, ==, 0);

    int prev_level = LOG_TRACE;
    log_set_level(LOG_INFO);
    log_set_quiet(true);

    log_info("file test message");
    log_debug("should NOT appear in file");

    fclose(fp);

    /* Read back */
    char *content = slurp_file(fp_path);
    munit_assert_not_null(content);
    /* The file callback formats as: "YYYY-MM-DD HH:MM:SS INFO  file:line: msg"
     * We can't check the exact content (timestamp varies), but we can check
     * key parts are present. */
    munit_assert_string_not_equal(content, "");
    munit_assert(strstr(content, "INFO") != NULL);
    munit_assert(strstr(content, "file test message") != NULL);
    /* DEBUG line should NOT be in the file */
    munit_assert(strstr(content, "should NOT appear") == NULL);

    free(content);
    remove(fp_path);

    log_set_level(prev_level);
    log_set_quiet(false);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: log macros (log_trace, log_debug, log_info, etc.)
 * ------------------------------------------------------------------ */
static MunitResult test_log_macros(const MunitParameter params[],
                                   void *user_data) {
    (void)params;
    (void)user_data;

    if (!g_capture_installed) {
        log_add_callback(capture_cb, NULL, LOG_TRACE);
        g_capture_installed = 1;
    }

    int prev_level = LOG_TRACE;
    log_set_level(LOG_TRACE);
    log_set_quiet(true);

    /* Test each macro */
    capture_reset();
    log_trace("trace %s", "macro");
    munit_assert_int(g_capture.last_level, ==, LOG_TRACE);
    munit_assert_string_equal(g_capture.last_msg, "trace macro");

    capture_reset();
    log_debug("debug %d", 42);
    munit_assert_int(g_capture.last_level, ==, LOG_DEBUG);
    munit_assert_string_equal(g_capture.last_msg, "debug 42");

    capture_reset();
    log_info("info %s", "test");
    munit_assert_int(g_capture.last_level, ==, LOG_INFO);
    munit_assert_string_equal(g_capture.last_msg, "info test");

    capture_reset();
    log_warn("warn %s", "msg");
    munit_assert_int(g_capture.last_level, ==, LOG_WARN);
    munit_assert_string_equal(g_capture.last_msg, "warn msg");

    capture_reset();
    log_error("error %s", "msg");
    munit_assert_int(g_capture.last_level, ==, LOG_ERROR);
    munit_assert_string_equal(g_capture.last_msg, "error msg");

    capture_reset();
    log_fatal("fatal %s", "msg");
    munit_assert_int(g_capture.last_level, ==, LOG_FATAL);
    munit_assert_string_equal(g_capture.last_msg, "fatal msg");

    log_set_level(prev_level);
    log_set_quiet(false);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Helpers for rotate tests
 * ------------------------------------------------------------------ */

/* Clean up rotation files: base_path, base_path.1, base_path.2, ... */
static void rotate_cleanup(const char *base_path, unsigned int n) {
    for (unsigned int i = 0; i <= n; i++) {
        char path[512];
        if (i == 0)
            snprintf(path, sizeof(path), "%s", base_path);
        else
            snprintf(path, sizeof(path), "%s.%u", base_path, i);
        remove(path);
    }
}

/* ------------------------------------------------------------------
 * Test: rotate file — verify rotation actually happens and content
 *       is preserved across rotated files
 * ------------------------------------------------------------------ */
static MunitResult test_rotate_file(const MunitParameter params[],
                                    void *user_data) {
    (void)params;
    (void)user_data;

    const char *base_path = tmp_path();

    /*
     * max_size = 128 bytes, max_file_count = 2
     * Each log line is about 58-65 bytes, so ~2 lines exceed 128.
     * Writing 15 lines should trigger at least 6 rotations.
     * With max_file_count = 2, we expect at most:
     *   base_path       – newest messages
     *   base_path.1     – older messages
     *   base_path.2     – oldest messages
     */
    log_clear_callbacks();
    g_capture_installed = 0;
    int rc = log_add_rotate_file(base_path, LOG_INFO, 128, 2);
    munit_assert_int(rc, ==, 0);

    int prev_level = LOG_TRACE;
    log_set_level(LOG_INFO);
    log_set_quiet(true);

    for (int i = 0; i < 15; i++) {
        log_info("rotate test #%d", i);
    }

    log_set_level(prev_level);
    log_set_quiet(false);

    /* --- All three files should exist --- */
    char *content0 = slurp_file(base_path);
    munit_assert_not_null(content0);
    munit_assert(strstr(content0, "rotate test") != NULL);

    char b1[512]; snprintf(b1, sizeof(b1), "%s.1", base_path);
    char *content1 = slurp_file(b1);
    munit_assert_not_null(content1);
    munit_assert(strstr(content1, "rotate test") != NULL);

    char b2[512]; snprintf(b2, sizeof(b2), "%s.2", base_path);
    char *content2 = slurp_file(b2);
    munit_assert_not_null(content2);
    munit_assert(strstr(content2, "rotate test") != NULL);

    /* --- Each file's size should be ≤ max_file_size --- */
    munit_assert_size(strlen(content0), <=, 128);
    munit_assert_size(strlen(content1), <=, 128);

    /* --- Messages spread across rotated files --- */
    /* Each file should contain at least one rotation test message */
    munit_assert(strstr(content0, "rotate test") != NULL);
    munit_assert(strstr(content1, "rotate test") != NULL);
    munit_assert(strstr(content2, "rotate test") != NULL);

    free(content0);
    free(content1);
    free(content2);

    rotate_cleanup(base_path, 2);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: rotate file — max_file_count = 1  (only one backup)
 * ------------------------------------------------------------------ */
static MunitResult test_rotate_count_1(const MunitParameter params[],
                                       void *user_data) {
    (void)params;
    (void)user_data;

    const char *base_path = tmp_path();

    log_clear_callbacks();
    g_capture_installed = 0;
    int rc = log_add_rotate_file(base_path, LOG_INFO, 64, 1);
    munit_assert_int(rc, ==, 0);

    log_set_level(LOG_INFO);
    log_set_quiet(true);

    for (int i = 0; i < 10; i++) {
        log_info("cnt1 #%d", i);
    }

    log_set_quiet(false);
    log_set_level(LOG_TRACE);

    /* base and .1 should exist */
    char *c0 = slurp_file(base_path);
    munit_assert_not_null(c0);
    free(c0);

    char b1[512]; snprintf(b1, sizeof(b1), "%s.1", base_path);
    char *c1 = slurp_file(b1);
    munit_assert_not_null(c1);
    free(c1);

    /* .2 should NOT exist because max_file_count = 1 */
    char b2[512]; snprintf(b2, sizeof(b2), "%s.2", base_path);
    FILE *fp2 = fopen(b2, "rb");
    munit_assert_null(fp2);

    rotate_cleanup(base_path, 1);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: rotate file — tiny max_size, many rotations
 * ------------------------------------------------------------------ */
static MunitResult test_rotate_tiny(const MunitParameter params[],
                                    void *user_data) {
    (void)params;
    (void)user_data;

    const char *base_path = tmp_path();

    /* max_size = 1 byte → every message triggers rotation!
     * Keep 3 backups. */
    log_clear_callbacks();
    g_capture_installed = 0;
    int rc = log_add_rotate_file(base_path, LOG_INFO, 1, 3);
    munit_assert_int(rc, ==, 0);

    log_set_level(LOG_INFO);
    log_set_quiet(true);

    for (int i = 0; i < 20; i++) {
        log_info("tiny %d", i);
    }

    log_set_quiet(false);
    log_set_level(LOG_TRACE);

    /* base + .1 + .2 + .3 should all exist */
    munit_assert_not_null(slurp_file(base_path));

    char b1[512]; snprintf(b1, sizeof(b1), "%s.1", base_path);
    munit_assert_not_null(slurp_file(b1));

    char b2[512]; snprintf(b2, sizeof(b2), "%s.2", base_path);
    munit_assert_not_null(slurp_file(b2));

    char b3[512]; snprintf(b3, sizeof(b3), "%s.3", base_path);
    munit_assert_not_null(slurp_file(b3));

    /* .4 should NOT exist (max_file_count = 3 → indices 0..3) */
    char b4[512]; snprintf(b4, sizeof(b4), "%s.4", base_path);
    FILE *fp4 = fopen(b4, "rb");
    munit_assert_null(fp4);

    rotate_cleanup(base_path, 3);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: rotate file — no rotation (messages stay within max_size)
 * ------------------------------------------------------------------ */
static MunitResult test_rotate_no_rotation(const MunitParameter params[],
                                           void *user_data) {
    (void)params;
    (void)user_data;

    const char *base_path = tmp_path();

    /* max_size = 1 MB — messages won't fill this */
    int rc = log_add_rotate_file(base_path, LOG_INFO, 1024 * 1024, 2);
    munit_assert_int(rc, ==, 0);

    log_set_level(LOG_INFO);
    log_set_quiet(true);

    log_info("just one small message");

    log_set_quiet(false);
    log_set_level(LOG_TRACE);

    /* base file has content */
    char *c = slurp_file(base_path);
    munit_assert_not_null(c);
    munit_assert(strstr(c, "just one small message") != NULL);
    free(c);

    /* No backup files created */
    char b1[512]; snprintf(b1, sizeof(b1), "%s.1", base_path);
    FILE *fp1 = fopen(b1, "rb");
    munit_assert_null(fp1);

    rotate_cleanup(base_path, 2);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: lock mechanism — log_set_default_lock
 * ------------------------------------------------------------------ */
static MunitResult test_lock(const MunitParameter params[],
                             void *user_data) {
    (void)params;
    (void)user_data;

    /* log_set_default_lock should succeed without error.
     * We can't easily test thread safety here, but we can verify
     * the lock is set and logging still works. */
    log_set_default_lock();

    int prev_level = LOG_TRACE;
    log_set_level(LOG_TRACE);
    log_set_quiet(true);

    if (!g_capture_installed) {
        log_add_callback(capture_cb, NULL, LOG_TRACE);
        g_capture_installed = 1;
    }

    capture_reset();
    log_info("lock test");
    munit_assert_int(g_capture.call_count, ==, 1);
    munit_assert_string_equal(g_capture.last_msg, "lock test");

    log_set_level(prev_level);
    log_set_quiet(false);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Test: log_data output
 * ------------------------------------------------------------------ */
static MunitResult test_log_data(const MunitParameter params[],
                                 void *user_data) {
    (void)params;
    (void)user_data;

    const char *data_path = tmp_path();

    /* Redirect stdout to a file */
    FILE *out_fp = fopen(data_path, "w");
    munit_assert_not_null(out_fp);

    int orig_stdout = dup(p_fileno(stdout));
    munit_assert_int(orig_stdout, >, 0);
    fflush(stdout);
    p_dup2(p_fileno(out_fp), p_fileno(stdout));
    fclose(out_fp);

    /* Call log_data */
    out_data("hello %s", "world");
    out_data("line2\nline3");

    /* Restore stdout */
    fflush(stdout);
    p_dup2(orig_stdout, p_fileno(stdout));
    p_close(orig_stdout);

    /* Verify */
    char *content = slurp_file(data_path);
    munit_assert_not_null(content);
    munit_assert(strstr(content, "hello world") != NULL);
    munit_assert(strstr(content, "line2") != NULL);
    munit_assert(strstr(content, "line3") != NULL);

    free(content);
    remove(data_path);

    return MUNIT_OK;
}

/* ------------------------------------------------------------------
 * Suite definition
 * ------------------------------------------------------------------ */

/* Test array — order matters because of shared global state.
 * Tests that modify global state (level, quiet, callbacks) should
 * leave it in a reasonable state for subsequent tests. */
static MunitTest test_suite_tests[] = {
    { (char *)"/level_string",   test_level_string,   NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/set_level",      test_set_level,      NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/set_quiet",      test_set_quiet,      NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/verbosity",      test_verbosity,      NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/log_output",     test_log_output,     NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/add_callback",   test_add_callback,   NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/add_fp",         test_add_fp,         NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/log_macros",     test_log_macros,     NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/rotate_file",         test_rotate_file,         NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/rotate_file/count_1", test_rotate_count_1,     NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/rotate_file/tiny",    test_rotate_tiny,        NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/rotate_file/no_rotation", test_rotate_no_rotation, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/lock",           test_lock,           NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { (char *)"/log_data",       test_log_data,       NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
    { NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};

static const MunitSuite test_suite = {
    (char *)"/log",
    test_suite_tests,
    NULL,           /* no sub-suites */
    1,              /* iterations */
    MUNIT_SUITE_OPTION_NONE
};

int main(int argc, char *argv[MUNIT_ARRAY_PARAM(argc + 1)]) {
    return munit_suite_main(&test_suite, NULL, argc, argv);
}

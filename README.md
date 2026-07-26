# log

Minimalist, zero-dependency, cross-platform pure C logging library with multi-destination streaming and file rotation.

## Project Structure

```
.
├── CMakeLists.txt           # Root build — library, example, and tests
├── README.md
├── src/
│   ├── log.h                # Public API header
│   └── log.c                # Implementation
├── examples/
│   └── app.c                # Example usage
├── tests/
│   └── test_log.c           # Unit tests (14 tests via µnit)
└── deps/
    └── munit-0.2.0/         # µnit testing framework dependency
        ├── munit.h
        ├── munit.c
        └── ...
```

## Building

```sh
mkdir -p build && cd build
cmake ..
make
```

### Build targets

| Target | Description |
|--------|-------------|
| `log` | Static library (`liblog.a`) |
| `log_example` | Example application demonstrating the API |
| `test_log` | Unit test executable |

## Running

```sh
# Run the example app
./log_example

# Run the unit tests
./tests/test_log

# Or via CTest
ctest
```

## Key API Reference

### Level-based logging (macros)

| Macro | Level | Description |
|-------|-------|-------------|
| `log_trace(...)` | TRACE (0) | Detailed diagnostic information |
| `log_debug(...)` | DEBUG (1) | Debug messages |
| `log_info(...)` | INFO (2) | General information |
| `log_warn(...)` | WARN (3) | Warning conditions |
| `log_error(...)` | ERROR (4) | Error conditions |
| `log_fatal(...)` | FATAL (5) | Fatal errors |

All macros write to **stderr** by default. Format: `HH:MM:SS LEVEL  file:line: message`

### Verbosity-gated output

| Macro | Stream | Behavior |
|-------|--------|----------|
| `out_info(...)` | stdout | Printed when verbosity ≥ `OUT_NORMAL` |
| `out_verbose(...)` | stdout | Printed when verbosity ≥ `OUT_VERBOSE` |
| `out_error(...)` | stderr | Always printed |
| `out_data(...)` | stdout | Raw data, no newline added |

### Configuration functions

| Function | Description |
|----------|-------------|
| `log_set_level(int level)` | Set minimum level for stderr output (default: TRACE). **Note:** this only affects the built-in stderr stream — user-registered callbacks have their own independent level filters. |
| `log_set_quiet(bool enable)` | Suppress all stderr output. Callbacks still fire. |
| `log_set_verbosity(int verbosity)` | Set minimum verbosity for `out_info`/`out_verbose` output. Clamped to `[OUT_QUIET, OUT_VERBOSE]`. |
| `log_get_verbosity()` | Get current verbosity level. |
| `log_set_default_lock()` | Enable thread-safety via a pthreads mutex. |

### Callback and file logging

| Function | Description |
|----------|-------------|
| `log_add_callback(fn, udata, level)` | Register a custom callback invoked for events at or above `level`. Returns 0 on success, -1 if callback table is full (max 32). |
| `log_add_fp(FILE*, level)` | Log to an open file pointer at the given level. |
| `log_add_rotate_file(path, level, max_size, max_count)` | Log to a rotating file at the given level. Rotates when file exceeds `max_size` bytes, keeping up to `max_count` backup files. |

### Utility

| Function | Description |
|----------|-------------|
| `log_level_string(int level)` | Return the string name for a log level (e.g., `"INFO"`). |

## Log File Rotation

The `log_add_rotate_file()` function provides automatic log rotation:

```c
// Log messages at INFO level and above, rotate at 1 MB, keep 5 backups
log_add_rotate_file("/var/log/myapp.log", LOG_INFO, 1024 * 1024, 5);
```

When the current log file exceeds `max_size` bytes, the library:
1. Closes the current file
2. Rotates backups: `.n-1` → `.n`, `.n-2` → `.n-1`, …, `.0` → `.1`
3. Opens a new (empty) log file and continues writing

## Unit Tests (14 tests)

All tests use the [µnit](https://github.com/nemequ/munit) testing framework and run in isolated forked processes.

| Test | Coverage |
|------|----------|
| `/log/level_string` | All 6 level enums map to correct strings |
| `/log/set_level` | Global level filters stderr output correctly |
| `/log/set_quiet` | Quiet mode suppresses stderr |
| `/log/verbosity` | Get/set and clamping of verbosity |
| `/log/log_output` | `out_info` → stdout, `out_error` → stderr, `out_verbose` filtered by verbosity |
| `/log/add_callback` | Custom callbacks receive events and respect their own level filter |
| `/log/add_fp` | File logging writes correct content |
| `/log/log_macros` | All 6 macros produce correct level and formatted message |
| `/log/rotate_file` | Rotation creates backups, each file ≤ max_size, content preserved |
| `/log/rotate_file/count_1` | `max_file_count=1` → no superfluous `.2` backup |
| `/log/rotate_file/tiny` | `max_size=1` byte → every message triggers rotation, limited to 3 backups |
| `/log/rotate_file/no_rotation` | Large `max_size` → no backups created |
| `/log/lock` | `log_set_default_lock()` works without error |
| `/log/log_data` | Raw data output to stdout |

## Implementation Notes

- The library uses a static global state (file-scope `static` struct in `log.c`), so all state is shared within a process. This is by design for simplicity.
- µnit forks a child process for each test, so global state is naturally reset between tests.
- Callbacks registered via `log_add_callback` have **independent level filters** — they are not affected by `log_set_level()`. The global level only gates the built-in stderr stream callback.
- On non-Windows platforms, thread-safety is provided via `pthread_mutex_t` when `log_set_default_lock()` is called.
- Maximum 32 callbacks can be registered simultaneously.

## License

MIT License — see the copyright header in `src/log.c` for details.

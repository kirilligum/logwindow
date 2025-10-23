# PRD: Core Robustness and Performance Improvements for logwindow

Author: Core Maintainers
Status: Proposed
Target version: v1.1.0
Owners: Core
Last updated: 2025-10-23

## Summary

Improve logwindow’s correctness, robustness, and performance while keeping existing behavior that current tests rely on:
- Preserve strict line-based semantics (never split lines)
- Harden argument parsing and input handling
- Eliminate O(n²) truncation behavior
- Gracefully flush on termination signals
- Better error reporting and safer writes
- Minor performance wins (iostream tuning)

All changes must keep existing tests in `run_tests.fish` passing.

## Goals

- No partial line writes (line-based invariant)
- Predictable and safe behavior under invalid CLI input
- Fast truncation even under high volume
- Avoid data loss on SIGINT/SIGTERM
- Clear diagnostics on file I/O failures
- Maintain current debounce semantics (flush triggered by incoming lines)

## Non-goals

- No new background timer thread to flush on intervals without input
- No behavior change to existing tests’ expectations (e.g., file not created before first eligible write)
- No new external dependencies

## Detailed requirements and implementation notes

The following items are intended to be implemented in `main.cc` unless otherwise noted.

### R1. Iostream performance tuning

Add before parsing args in `main`:

```cpp
std::ios::sync_with_stdio(false);
std::cin.tie(nullptr);
```

Rationale: Reduces iostream overhead for high-throughput input.

### R2. CRLF normalization

After each `std::getline`, strip trailing `\r` (Windows line endings):

```cpp
if (!line.empty() && line.back() == '\r') line.pop_back();
```

Rationale: Avoid stray `\r` characters before the appended `\n`.

### R3. Strict long-line policy (never split lines)

If a single line (including the newline we add) cannot fit within `maxSize`, drop the entire line (strict line-based):

```cpp
// Before appending to buffer
if (line.size() + 1 > config.maxSize) {
    // Too large to fit without splitting; drop to preserve line-based semantics
    continue;
}
```

Rationale: Aligns with README claim: “Line-based - Never splits lines in the middle.”

Note: We keep the simpler “drop long line” policy instead of tailing long lines to avoid contradiction with the “never split” guarantee.

### R4. Truncation: replace O(n²) loop with single-cut

Replace the per-line front erase loop with a single cut at the first newline after the overflow point:

```cpp
auto trimToMax = [&](std::string& s, size_t max) {
    if (s.size() <= max) return;
    size_t overflow = s.size() - max;
    size_t cut = s.find('\n', overflow);
    if (cut != std::string::npos) {
        s.erase(0, cut + 1);
    } else {
        // Defensive: if somehow no newline is found, clear buffer
        // (should not occur since we always append '\n' per line and drop long lines)
        s.clear();
    }
};

// After appending `line` and then `'\n'`
trimToMax(buffer, config.maxSize);
```

Rationale: Eliminates repeated front erases (which cause O(n²) behavior).

### R5. Argument parsing robustness

- Detect missing values (next token is absent or starts with `-`).
- Parse with try/catch and validate ranges.
- Treat `--write-interval <= 0` as immediate mode for convenience (or reject with error if preferred).

Add helper:

```cpp
auto requireValue = [&](int& i, const char* flag) -> const char* {
    if (i + 1 >= argc || argv[i + 1][0] == '-') {
        std::cerr << "Error: Missing value for " << flag << "\n\n";
        printUsage(argv[0]);
        std::exit(1);
    }
    return argv[++i];
};
```

Use it:

```cpp
} else if (arg == "--max-size") {
    const char* v = requireValue(i, "--max-size");
    try {
        auto val = std::stoull(v);
        if (val == 0) {
            std::cerr << "Error: --max-size must be > 0\n";
            std::exit(1);
        }
        config.maxSize = static_cast<size_t>(val);
    } catch (...) {
        std::cerr << "Error: Invalid --max-size\n";
        std::exit(1);
    }
} else if (arg == "--write-interval") {
    const char* v = requireValue(i, "--write-interval");
    try {
        long long ms = std::stoll(v);
        if (ms <= 0) {
            config.immediate = true; // zero/negative interval means immediate writes
        } else {
            config.writeInterval = std::chrono::milliseconds(ms);
        }
    } catch (...) {
        std::cerr << "Error: Invalid --write-interval\n";
        std::exit(1);
    }
}
```

Rationale: Prevents crashes on invalid input and provides clear messages.

### R6. Signal handling for graceful final flush

- On SIGINT/SIGTERM, exit the input loop and perform the final write if needed.

Add globals and handler:

```cpp
#include <csignal>

volatile sig_atomic_t g_running = 1;
void onSignal(int) { g_running = 0; }
```

Register early in `main`:

```cpp
std::signal(SIGINT, onSignal);
std::signal(SIGTERM, onSignal);
```

Update loop condition:

```cpp
while (g_running && std::getline(std::cin, line)) {
    // ... processing ...
}
```

Final flush remains (only if not immediate and `needsWrite` is true):

```cpp
if (needsWrite && !config.immediate) {
    writeToFile(config.logFile, buffer);
}
```

Rationale: Prevents data loss when the process is terminated.

### R7. Better write errors and binary mode

Improve `writeToFile`:

```cpp
#include <cerrno>
#include <cstring>

void writeToFile(const std::string &logFile, const std::string &buffer) {
  std::ofstream outFile(logFile, std::ios::binary | std::ios::trunc);
  if (!outFile) {
    std::cerr << "Error opening " << logFile << ": " << std::strerror(errno) << '\n';
    return;
  }
  outFile.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
  if (!outFile) {
    std::cerr << "Error writing " << logFile << ": " << std::strerror(errno) << '\n';
  }
}
```

Rationale: Clear diagnostics and cross-platform-safe writes.

### R8. Optional (stage 2): Atomic file replace

Safer for external readers (avoids a window where a truncated file is empty):

```cpp
#include <filesystem>

bool writeAtomic(const std::string& path, const std::string& data) {
  auto tmp = path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec); // atomic on POSIX
  if (ec) {
    std::filesystem::copy_file(
      tmp, path, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(tmp);
    if (ec) return false;
  }
  return true;
}
```

Then swap `writeToFile` calls to `writeAtomic` (or gate via a build flag).

Note: This may change inode/permissions; preserve attributes if required.

### R9. Buffer reservation

Already present: `buffer.reserve(config.maxSize * 2);`
Keep this logic; it complements R4’s single-cut trimming.

## CLI and documentation updates

- README “Line-based” section: Clarify that lines larger than `--max-size` are dropped entirely (strict policy).
- Document that writes in debounced mode occur when new input arrives and the interval has elapsed (no background timer).
- Document that `--write-interval <= 0` implies `--immediate`.
- Optionally document an “atomic write” mode if enabled.

## Backward compatibility

- Existing tests must continue to pass unchanged:
  - Debounce test expects no file before “next input” triggers write — unchanged.
  - Immediate mode behavior — unchanged.
- For oversized lines, behavior becomes stricter (drop instead of split). This aligns with README claims.

## Test plan

Add new tests (in `run_tests.fish` or a new test file):

1. CRLF normalization
   - Input: "a\r\nb\r\n"
   - Expect: "a\nb\n"

2. Oversized line drop
   - max-size = 5, input: "123456\n7\n"
   - Expect: only "7\n" (drop "123456" because it cannot fit line-based)

3. Argument parsing
   - `--max-size foo` -> error with message (exit 1)
   - `--max-size --immediate` -> error “Missing value for --max-size”
   - `--write-interval 0` -> immediate mode, writes occur per line

4. Signal flush
   - Long interval, write one line, send SIGINT; expect file contains the line.

5. Write errors
   - Attempt to write to a non-writable path; expect stderr message (manual check or skip in CI).

## Rollout

- Implement R1–R7 first (minimal risk, maintains current test expectations).
- Optionally implement R8 (atomic writes) behind a build flag or feature toggle after verification.

## Acceptance criteria

- All existing tests pass.
- Manual checks confirm:
  - No partial lines with long inputs.
  - Graceful flush on SIGINT/SIGTERM.
  - Helpful errors on bad CLI input and write failures.
- README updated to reflect clarified behavior.

## Appendix: Minimal code insertion points

- Headers: add `<csignal>`, `<cerrno>`, `<cstring>` (and `<filesystem>` if doing atomic writes).
- Early in `main`: iostream tuning and signal handlers.
- In read loop: CRLF trim, long-line drop, single-cut truncation.
- In arg parser: use `requireValue`, try/catch, range checks.
- In `writeToFile`: binary mode + `std::strerror(errno)`.

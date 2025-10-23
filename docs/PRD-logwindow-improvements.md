# PRD: Logwindow Core Improvements (Time-driven Flushes, Robust I/O, Signal Handling)

Owner: Core
Status: Draft
Target version: v1.1.0

Summary
Logwindow should reliably produce up-to-date, line-based rolling logs with minimal overhead. This PRD addresses three core gaps:
1) Debounced writes only occur when new input arrives.
2) File I/O reopens/truncates the file for every write.
3) Process may not terminate promptly on SIGINT/SIGTERM when stdin is idle.

This document specifies functional and non-functional requirements, design, and a step-by-step implementation and test plan.

Goals
- G1: Perform time-driven debounced flushes even when input is idle.
- G2: Keep file handles open for efficiency; ensure exact-size file content on each flush.
- G3: Prompt, graceful termination on SIGINT/SIGTERM with a final flush.
- G4: Preserve strict line-based semantics and drop overlong lines safely.
- G5: Maintain OS portability (Linux/macOS primary; WSL; reasonable Windows behavior).

Non-goals
- NG1: Implementing advanced filtering (grep/sed/awk are recommended).
- NG2: Changing default CLI flags or removing existing behavior.
- NG3: Guaranteed durability (fsync) by default.

Functional Requirements
- F1 Debounced, time-driven flushes
  - F1.1: In debounced mode, if any data has been read and the write interval elapses with no further input, the buffer MUST be flushed to disk.
  - F1.2: The maximum delay between the last received line and it appearing on disk is writeInterval (+50ms tolerance).

- F2 Immediate mode semantics
  - F2.1: With --immediate, each line is flushed as it arrives (subject to minimal thread scheduling delay).
  - F2.2: Immediate mode continues to respect max-size and line-based semantics.

- F3 Graceful termination
  - F3.1: On SIGINT or SIGTERM, the process MUST exit promptly (target < 100ms) even if stdin is idle.
  - F3.2: A final flush MUST occur before exit if there is unwritten data.

- F4 Line-based rolling window
  - F4.1: Output file MUST NEVER contain partial lines.
  - F4.2: If a single input line exceeds maxSize - 1 bytes (accounting for the newline), it MUST be dropped entirely without affecting existing buffer content.
  - F4.3: CRLF normalization: trailing '\r' MUST be removed if present.

- F5 File I/O efficiency and consistency
  - F5.1: The log file SHOULD be opened once and reused for all writes.
  - F5.2: Each flush MUST produce a file whose length equals the current buffer (no stale tail data).
  - F5.3: Errors opening/writing SHOULD be reported without flooding stderr (rate-limited).

- F6 Optional atomic snapshot writes
  - F6.1: Add flag --atomic-writes to write to a temp file then atomically rename over the target (POSIX only).
  - F6.2: Document caveat: tail -f sticks to old inode; users may need tail -F.

- F7 Backward compatibility
  - F7.1: Default behavior remains the same from a user’s perspective except that debounced flushes now occur even during idle periods.
  - F7.2: Existing flags keep the same meaning.

Non-Functional Requirements
- N1: CPU usage near idle should be < 1% on modern hardware.
- N2: Memory overhead remains small (target < 2× maxSize + 64KB).
- N3: With 10k lines/sec of ~80 chars, the tool should keep up without undue CPU spikes in immediate mode.
- N4: Writes per second in debounced mode should not exceed 1 per interval.

Design Overview

D1. Concurrency model
- Introduce a dedicated writer thread.
- Shared state:
  - Container of lines (see D2), running byte count totalBytes.
  - Flags: dirty (buffer changed since last flush), shuttingDown.
  - Mutex + condition_variable cv.
- Reader path (main thread):
  - Reads lines from stdin, normalizes CRLF, enforces maxSize and overlong-line drop.
  - Appends line to container, updates totalBytes, drops from front while totalBytes > maxSize.
  - Sets dirty = true and notifies cv.
- Writer thread:
  - Loop with condition_variable wait_for(writeInterval).
  - Wake conditions:
    - Immediate notification (new line arrival),
    - Timeout elapsed (time-driven flush),
    - Shutdown requested.
  - If immediate mode, flush on each notify.
  - In debounced mode, flush if dirty on timeout or when enough time passed since last flush.
  - Always perform a final flush on shutdown if dirty.

D2. Buffer representation
- Replace “string with erase-from-front” approach with a deque<string> of full lines:
  - On append: push_back(line + '\n'), totalBytes += size.
  - While totalBytes > maxSize: pop_front() until within limit (totalBytes -= popped.size()).
- Benefits:
  - O(1) amortized insert/drop; avoids O(n) string memmove on each trim.
  - Simple, line-based invariant is explicit.
- Flush assembly:
  - On flush, assemble a contiguous string by pre-sizing and copying all deque entries.
  - Minimize lock hold time by copying lines under lock into a temporary vector, then assembling outside the lock (optional micro-optimization).

D3. File I/O
- Open-once approach (default):
  - Open std::fstream out(logFile, ios::in | ios::out | ios::binary | ios::trunc).
  - On each flush:
    - out.seekp(0);
    - out.write(buffer.data(), buffer.size());
    - out.flush();
    - std::filesystem::resize_file(logFile, buffer.size()) to ensure file shrinks correctly.
  - If open fails, retry on next flush attempt; rate-limit error messages (e.g., once per 2s).
- Atomic snapshot (optional, --atomic-writes, POSIX only):
  - Write buffer to temp path: logFile + ".tmp"
  - fsync temp fd (optional via --fsync flag if implemented later)
  - rename(temp, logFile) (atomic on POSIX)
  - Note: for Windows, this may be disabled or degrade to non-atomic behavior with documented caveat.

D4. Signals and prompt termination
- POSIX implementation (Linux/macOS):
  - Use pipe() to create a self-pipe.
  - Install sigaction handlers for SIGINT, SIGTERM that write 1 byte to the pipe (use async-signal-safe write).
  - Replace std::getline with a small chunked reader loop using poll() on:
    - fd 0 (stdin)
    - self-pipe read end
  - On self-pipe readable: set shuttingDown = true; notify writer; break loop after final flush.
  - On stdin readable: read chunks, feed a line-assembler that detects '\n' boundaries; enforce capped line logic (see D5).
- Non-POSIX fallback (Windows/WSL if POSIX APIs unavailable):
  - Keep std::getline for input.
  - Signal handling degrades to best-effort; prompt termination may depend on stdin closure.
  - Still benefit from writer thread and open-once file I/O.

D5. Capped line reader (defensive)
- During chunked read (POSIX path), track current line length.
- If current line length exceeds (maxSize - 1), mark “dropping current line” and discard bytes until the next newline.
- Ensures no unbounded memory growth due to extremely long single lines.

D6. Error handling and logging
- Rate-limit repeated open/write errors (e.g., allow at most one error message per 2 seconds).
- Include strerror(errno) or equivalent in messages.
- Continue attempting future flushes.

CLI Additions
- --atomic-writes
  - Default: off
  - When on: use write-to-temp + rename strategy on POSIX.
- (Optional, future) --fsync
  - Default: off
  - When on: fsync the file (or temp file in atomic mode) after each write.

User-visible Behavior Changes
- Debounced mode will now write even when input pauses, after the configured interval.
- Immediate mode is unchanged in intention but will be implemented via the writer thread, yielding equal or better responsiveness.

Acceptance Criteria
- A1: In debounced mode, with writeInterval=200ms, piping a single line then going idle results in the file appearing on disk within 250ms.
- A2: Immediate mode writes the first line to disk within 50ms of arrival.
- A3: Sending SIGINT while idle exits within 100ms and performs a final flush (POSIX).
- A4: Overlong input lines (> maxSize - 1) are dropped without affecting existing buffer.
- A5: The output file always contains whole lines and its size never exceeds maxSize.
- A6: CPU usage remains < 1% while idle; no repeated reopen/close per flush.
- A7: Error messages on persistent open failure are rate-limited (no flooding).
- A8: All updated tests pass (see Test Plan).

Implementation Plan

Phase 1: Core infrastructure
1) Introduce structs/classes:
   - LineBuffer {
       std::deque<std::string> lines;
       size_t totalBytes = 0;
       void appendLine(const std::string& s);
       void trimToMax(size_t max);
       void assemble(std::string& out) const; // contiguous buffer
     }
   - Writer {
       std::mutex m;
       std::condition_variable cv;
       bool dirty = false;
       bool shuttingDown = false;
       std::chrono::milliseconds interval;
       bool immediate;
       LineBuffer buf;
       // File handles + options
       std::unique_ptr<std::fstream> out;
       bool atomicWrites = false;
       // Error throttling timestamps
     }
2) main():
   - Parse new flag --atomic-writes.
   - Spawn writer thread with config.
   - Reader loop: std::getline (Phase 1), normalize CRLF, drop overlong lines, push to buf, set dirty, notify cv.
   - On EOF: set shuttingDown, notify, join writer.

3) Writer thread:
   - wait_for(interval).
   - If immediate: flush on notify; else flush on timeout if dirty.
   - Final flush on shutdown.

4) File I/O:
   - Open once on startup with ios::in|ios::out|ios::binary|ios::trunc.
   - Per flush: seekp(0), write, flush, resize_file.

Phase 2: POSIX prompt termination + capped reader
5) Replace std::getline with poll-based chunked reader (POSIX only):
   - Setup self-pipe and sigaction handlers (SA_RESTART off).
   - poll() on stdin and self-pipe.
   - Implement capped line assembly and CRLF normalization.
   - On signal event: set shuttingDown; break input loop; join writer after final flush.

6) Windows fallback:
   - Keep std::getline path behind preprocessor guards.
   - Signals remain best-effort; document behavior.

Phase 3: Optional atomic snapshots
7) Implement --atomic-writes:
   - POSIX path: write to tmp, fsync (if implemented), rename over target.
   - Non-POSIX: either disable with clear error or perform non-atomic replace with documented caveat.

API/Code Changes
- main.cc:
  - New classes for buffer and writer.
  - New threading, condition_variable, and mutex usage.
  - New CLI flag handling for --atomic-writes.
  - POSIX-only code paths for self-pipe + poll + sigaction.
  - Revised writeToFile into a writer method (open-once, resize_file).
- run_tests.fish:
  - Update debounced write test to assert time-driven flush without waiting for new input.
  - Add a signal termination test (POSIX only).
  - Add a long-line drop test and CRLF normalization test.
- README.md:
  - Document --atomic-writes flag and updated behavior note about debounced flushes during idle.

Test Plan

Unit-ish/Integration tests (via run_tests.fish):
- T1 Basic truncation (existing): unchanged expectations.
- T2 Immediate mode (existing): unchanged expectations; verify fast first-line write.
- T3 Debounced write during idle (new): write one line, sleep slightly > interval, assert file content without further input.
- T4 Signal termination (new, POSIX): start logwindow reading from a slow/paused source, send SIGINT, verify exit code 0 and final flush present.
- T5 Overlong lines (new): feed a line of size maxSize+100, verify it’s dropped and previous lines preserved.
- T6 CRLF normalization (new): feed "line\r\n"; verify file contains "line\n".
- T7 Atomic writes (new, optional): with --atomic-writes, verify readers never see partial lines across writes (best-effort).

Performance validation:
- P1 Measure CPU idle usage (<1%).
- P2 With 10k lines/sec immediate mode for 5s, ensure no errors and acceptable CPU.
- P3 Disk writes per second in debounced mode ~= 1/interval.

Risks and Mitigations
- R1 poll/self-pipe complexity:
  - Mitigation: stage in Phase 2; keep Phase 1 writer-thread improvements independent.
- R2 Atomic writes break tail -f behavior:
  - Mitigation: default off; document tail -F usage.
- R3 Cross-platform differences:
  - Mitigation: preprocessor guards; POSIX path only where available; Windows fallback remains simple.

Rollout
- Implement Phase 1 and release v1.1.0-rc1.
- Gather feedback, then proceed with Phase 2 for POSIX prompt termination.
- Add --atomic-writes in Phase 3 as opt-in.

Definition of Done
- All acceptance criteria A1–A8 pass on Linux (WSL ok).
- Tests updated and green in CI for Linux; manual spot-check on macOS where possible.
- README updated with new flags and behavior notes.
- No regression in existing features.

Appendix: Pseudocode Sketches

Writer thread (core)
- while (!shuttingDown):
    - if (immediate):
        - cv.wait(m) until dirty or shuttingDown
        - if (dirty) flush()
    - else:
        - cv.wait_for(m, interval) until dirty or timeout or shuttingDown
        - if (dirty and (timeout or timeSinceLastFlush >= interval)) flush()

Deque-based buffer
- appendLine(s):
    - lines.push_back(s)
    - totalBytes += s.size()
    - while (totalBytes > maxSize):
        - totalBytes -= lines.front().size()
        - lines.pop_front()
- assemble(out):
    - out.resize(totalBytes)
    - copy each line sequentially

POSIX input loop with self-pipe and poll
- setup pipe, install sigaction handlers writing 1 byte to pipe on SIGINT/SIGTERM
- loop:
    - poll([stdinFd, pipeReadFd], timeout = -1)
    - if pipe readable: shuttingDown = true; break
    - if stdin readable: read into buf; for each '\n', emit line; enforce cap; notify writer
    - on EOF: break; shuttingDown = true
- join writer; ensure final flush

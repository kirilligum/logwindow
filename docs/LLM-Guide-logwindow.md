# LLM Guide: Using logwindow and its log files

Purpose
- logwindow creates a small, continuously-updated snapshot of the most recent logs, sized to fit within an LLM’s context window.

What these files are
- Rolling, size-limited snapshot: keeps only the last N bytes (max-size).
- Whole-line guarantee: never contains partial lines; oldest whole lines are dropped first.
- Update cadence:
  - Debounced mode: flushes every write-interval milliseconds, even when input is idle.
  - Immediate mode: flushes each line as it arrives.
- Snapshot writes: each flush rewrites the entire file; with --atomic-writes (POSIX), updates use temp+rename for consistency.
- Normalization: CRLF is converted to LF.
- Safety: extremely long single lines (> max-size - 1) are dropped entirely.

How to read and reason
- Start from the end (most recent events) and work upward to find causes.
- Expect truncation by design; earlier history may be missing.
- If references point to earlier steps not present, request a reproduction or a larger window.

What to ask the user (when context is insufficient)
- Bigger window: rerun with --max-size 16000 (or larger).
- Faster updates: decrease --write-interval (e.g., 200) or use --immediate.
- Focused logs: filter first, then pipe to logwindow (grep/awk/sed) to keep only relevant errors and nearby context.
- Provide the exact command used (including flags) and the exact log file path(s).

Canonical usage patterns
- Default rolling log:
  - command 2>&1 | logwindow app.log --max-size 8000 --write-interval 1000 &
- Immediate (per-line) updates:
  - command 2>&1 | logwindow app.log --immediate &
- Filter first, then capture (recommended for LLM context):
  - command 2>&1 | grep -E "(ERROR|WARN|FATAL)" | logwindow issues.log --max-size 8000 &

Caveats
- Max on-disk staleness equals write-interval in debounced mode; use --immediate for step-by-step debugging.
- With --atomic-writes, readers like tail -f may stick to the old inode; tail -F follows renames. LLMs reading the file path will see consistent snapshots.
- On non-POSIX platforms, --atomic-writes may not be fully atomic.

By design, these files provide just the freshest, most relevant log context to diagnose issues quickly without overflowing the LLM’s context window.

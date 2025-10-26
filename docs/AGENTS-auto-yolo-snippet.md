# Snippet for AGENTS.md: Driving `logwindow` in Codex Auto/Yolo

Paste the following section into `AGENTS.md` so Codex knows how to keep rolling logs available when you toggle `auto` or `yolo`:

````md
### Using `logwindow` during `codex auto` / `codex yolo`

Why: Auto/yolo runs without pauses, so the agent loses interactive stdout. Pipe every long-lived process (dev server, emulator, watcher, test loop) through `logwindow` first so the agent can keep reading a capped log snapshot.

**Baseline workflow**
1. Pick the commands that need continuous monitoring (e.g., `firebase emulators:start`, `npm run dev`, `fish run_tests.fish --watch`).
2. Launch them pre-emptively with `logwindow`, keeping the log files inside the repo so Codex can `/add` them:
   ```bash
   npm run dev 2>&1 | logwindow logs/dev.log --max-size 16000 --write-interval 750 &
   firebase emulators:start 2>&1 | logwindow logs/firebase.log --max-size 20000 --write-interval 1000 --atomic-writes &
   ```
3. Tell Codex which files to keep in context: “Add `logs/dev.log` and `logs/firebase.log`, they’re rolling windows managed by logwindow.”
4. Start `codex auto` (or `codex yolo`) only after the rolling logs are live, so every subsequent build/test loop is captured automatically.

**Keeping it in Codex context**
- After creating the rolling files, run `/add logs/dev.log logs/firebase.log` (or mention them explicitly in the kick-off message) so Codex tracks them throughout the auto/yolo session.
- Because `logwindow` enforces a byte cap, each file stays small enough for Codex to re-read on every tool use; whole-line truncation means the newest stack traces remain intact while older noise quietly disappears.
- Explicitly remind the agent: “These files are rolling snapshots capped at 16–20KB, so they stay within your context even though the underlying processes run for hours.”

**Mode-specific guidance**
- `codex auto`: favor debounced writes (`--write-interval 500–1000`) to minimize disk churn while still giving ~1s freshness.
- `codex yolo`: prefer `--immediate` (or `--write-interval 200`) so fatal errors surface instantly; pair with `--max-size 8000–12000` to stay within the slim context Codex uses for risky runs.
- In both modes, keep filenames descriptive (`logs/dev-server.log`, `logs/firebase.log`) so you can reference them directly in prompts: “Auto mode: inspect `logs/dev-server.log` for the latest stack trace.”

**Recovery & hygiene**
- If a run crashes, re-seed the pipeline by rerunning the same `command | logwindow file &` line, then re-add the file if Codex dropped it from context.
- When context pressure is high, lower `--max-size` or temporarily `/drop` the least relevant log to free tokens, then re-add it when needed.
- Remind Codex that `logwindow` rewrites the entire file each flush; if it’s tailing, it should use `tail -F logs/dev.log` to follow inode swaps from `--atomic-writes`.
````

The snippet above can be copy-pasted directly into the existing AGENTS playbook without additional edits.

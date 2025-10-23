# logwindow

A fast, lightweight log truncation tool that maintains a rolling window of the most recent log data. **Designed specifically for AI coding assistants like Claude Code and Aider** - keep your Firebase emulator, dev server, and test logs within the AI's context window while getting real-time updates.

## Features

- 🚀 **Zero dependencies** - Pure C++20, compiles to a single binary
- ⚡ **High performance** - Minimal overhead, handles high-volume logs
- 🎯 **Configurable** - Adjust buffer size and write frequency
- 💾 **Smart buffering** - Debounced writes reduce disk I/O
- 📝 **Line-based** - Never splits lines in the middle

## Why?

When running development servers (React, Firebase, etc.) with AI coding tools, you want logs that:

- Stay under the AI's context window limit
- Show only recent, relevant information
- Update continuously without manual intervention
- Don't consume excessive disk space

`logwindow` solves this by maintaining only the last N bytes of your logs, automatically discarding old data.

## Installation

### Quick install (recommended)

```bash
# Clone or download
git clone https://github.com/yourusername/logwindow.git
cd logwindow

# Install to ~/.local/bin
./install.sh

# Uninstall later if needed
./uninstall.sh
```

The install script will:
- Compile the binary with your available compiler (clang++ or g++)
- Install to `~/.local/bin/logwindow` (no sudo required)
- Check if `~/.local/bin` is in your PATH and provide instructions if needed

### Manual build

```bash
# Compile
clang++ -std=c++20 -O3 -o logwindow main.cc

# Or with GCC
g++ -std=c++20 -O3 -o logwindow main.cc

# Install to ~/.local/bin (user-local, no sudo)
mkdir -p ~/.local/bin
cp logwindow ~/.local/bin/

# Or install system-wide (requires sudo)
sudo cp logwindow /usr/local/bin/
```

### Requirements

- C++20 compatible compiler (Clang 14+, GCC 11+, or MSVC 2019+)
- Linux, macOS, or Windows (WSL)

## Usage

### Basic usage

```bash
# Keep last 8000 bytes of logs (default)
yarn dev 2>&1 | logwindow yarn-dev.log &

# Custom buffer size
firebase emulators:start 2>&1 | logwindow firebase.log --max-size 16000 &
```

### Command-line options

```bash
logwindow <logfile> [options]

Options:
  --max-size <bytes>        Maximum log size in bytes (default: 8000)
  --write-interval <ms>     Write interval in milliseconds (default: 1000)
  --immediate               Write immediately on every line (ignores interval)
  --help                    Show help message
```

### Examples

```bash
# Default: 8KB buffer, write every 1 second
npm run dev 2>&1 | logwindow dev.log &

# Larger buffer for more context (16KB)
npm run dev 2>&1 | logwindow dev.log --max-size 16000 &

# Faster updates (every 500ms)
npm run dev 2>&1 | logwindow dev.log --write-interval 500 &

# Immediate writes (every line) - useful for debugging
npm run dev 2>&1 | logwindow dev.log --immediate &

# Large buffer for Claude Extended (64KB context)
npm run dev 2>&1 | logwindow dev.log --max-size 64000 --write-interval 2000 &
```

### Filtering logs with grep/sed/awk

`logwindow` follows the Unix philosophy - it does one thing well (log truncation). For filtering, pipe through standard Unix tools first:

#### Basic filtering with grep

```bash
# Only save ERROR lines
firebase emulators:start 2>&1 | grep "ERROR" | logwindow errors.log &

# Multiple patterns
firebase emulators:start 2>&1 | grep -E "(ERROR|WARN|FATAL)" | logwindow issues.log &

# Case-insensitive
firebase emulators:start 2>&1 | grep -i "error" | logwindow errors.log &

# Invert match - everything EXCEPT debug lines
firebase emulators:start 2>&1 | grep -v "DEBUG" | logwindow filtered.log &
```

#### Context lines (before/after matching lines)

```bash
# 3 lines before and after each ERROR
firebase emulators:start 2>&1 | grep -B 3 -A 3 "ERROR" | logwindow errors.log &

# 5 lines after each ERROR (for stack traces)
firebase emulators:start 2>&1 | grep -A 5 "ERROR" | logwindow errors-with-traces.log &

# 2 lines before and 10 after (for context + stack trace)
firebase emulators:start 2>&1 | grep -B 2 -A 10 "ERROR" | logwindow errors.log &

# Context on both sides (shorthand for -B and -A)
firebase emulators:start 2>&1 | grep -C 5 "ERROR" | logwindow errors.log &
```

**Note:** grep automatically handles overlapping context lines - no duplicates!

#### Real-world examples for AI assistants

```bash
# Firebase: Only authentication errors with context
firebase emulators:start 2>&1 | grep -B 2 -A 5 "auth.*error" | logwindow auth-errors.log &

# Next.js: Only compilation errors and warnings
yarn dev 2>&1 | grep -E "(Error|Warning|Failed)" | logwindow build-issues.log &

# API server: Only 4xx/5xx errors with request context
npm run server 2>&1 | grep -B 3 -A 2 -E "(40[0-9]|50[0-9])" | logwindow api-errors.log &

# Django: Only exceptions with full stack trace
python manage.py runserver 2>&1 | grep -A 20 "Traceback" | logwindow exceptions.log &
```

#### Advanced filtering with awk

```bash
# Only lines with timestamps that contain ERROR
firebase emulators:start 2>&1 | awk '/^\[.*\].*ERROR/ {print}' | logwindow errors.log &

# Print ERROR and the next 5 lines
firebase emulators:start 2>&1 | awk '/ERROR/ {for(i=0;i<=5;i++) {getline; print}}' | logwindow errors.log &

# Only log lines longer than 100 characters (detailed errors)
firebase emulators:start 2>&1 | awk 'length > 100' | logwindow verbose.log &

# Extract specific fields (e.g., timestamps and messages)
firebase emulators:start 2>&1 | awk '{print $1, $2, $NF}' | logwindow compact.log &
```

#### Advanced filtering with sed

```bash
# Remove ANSI color codes (clean logs for AI)
firebase emulators:start 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | logwindow clean.log &

# Only lines matching a pattern
firebase emulators:start 2>&1 | sed -n '/ERROR/p' | logwindow errors.log &

# Lines matching pattern plus next 3 lines
firebase emulators:start 2>&1 | sed -n '/ERROR/,+3p' | logwindow errors.log &

# Remove noise (INFO lines)
firebase emulators:start 2>&1 | sed '/INFO/d' | logwindow filtered.log &
```

#### Combining multiple filters

```bash
# Remove debug, keep only errors/warnings with context, remove color codes
firebase emulators:start 2>&1 \
  | grep -v "DEBUG" \
  | grep -B 2 -A 5 -E "(ERROR|WARN)" \
  | sed 's/\x1b\[[0-9;]*m//g' \
  | logwindow clean-errors.log &

# Filter by timestamp range and error level
npm run server 2>&1 \
  | awk '/2024-.*ERROR/ || /2024-.*FATAL/' \
  | logwindow today-errors.log &
```

#### Performance tip

Filtering happens **before** logwindow, so:
- Less data goes through logwindow = better performance
- Smaller log files = more context fits in AI's window
- More precise logs = better AI debugging

```bash
# Good: Filter first (less data)
firebase emulators:start 2>&1 | grep "ERROR" | logwindow errors.log --max-size 4000 &

# Less efficient: Save everything (more data)
firebase emulators:start 2>&1 | logwindow all.log --max-size 32000 &
```

### Fish shell functions

Add to `~/.config/fish/config.fish`:

```fish
# Basic function
function dev-logs
    # Start with truncated logs
    yarn dev 2>&1 | logwindow yarn-dev.log --max-size 8000 &
    firebase emulators:start 2>&1 | logwindow firebase.log --max-size 8000 &

    echo "Logs are being truncated to 8KB:"
    echo "  yarn-dev.log"
    echo "  firebase.log"
    echo ""
    echo "View with: tail -f yarn-dev.log"
end

# With error filtering
function dev-logs-errors
    # Only save errors and warnings with context
    yarn dev 2>&1 | grep -B 2 -A 5 -E "(Error|Warning)" | logwindow dev-errors.log --max-size 4000 &
    firebase emulators:start 2>&1 | grep -B 2 -A 5 "ERROR" | logwindow firebase-errors.log --max-size 4000 &

    echo "Capturing only errors with context:"
    echo "  dev-errors.log"
    echo "  firebase-errors.log"
end
```

### Bash/Zsh functions

Add to `~/.bashrc` or `~/.zshrc`:

```bash
# Basic function
dev-logs() {
    yarn dev 2>&1 | logwindow yarn-dev.log --max-size 8000 &
    firebase emulators:start 2>&1 | logwindow firebase.log --max-size 8000 &

    echo "Logs are being truncated to 8KB:"
    echo "  yarn-dev.log"
    echo "  firebase.log"
    echo ""
    echo "View with: tail -f yarn-dev.log"
}

# With error filtering
dev-logs-errors() {
    # Only save errors and warnings with context
    yarn dev 2>&1 | grep -B 2 -A 5 -E "(Error|Warning)" | logwindow dev-errors.log --max-size 4000 &
    firebase emulators:start 2>&1 | grep -B 2 -A 5 "ERROR" | logwindow firebase-errors.log --max-size 4000 &

    echo "Capturing only errors with context:"
    echo "  dev-errors.log"
    echo "  firebase-errors.log"
}
```

## How it works

1. **Reads from stdin** line-by-line
2. **Appends to buffer** in memory
3. **Truncates buffer** when it exceeds `--max-size` (keeps last N bytes)
4. **Writes to file** at specified interval or immediately
5. **Continues** until stdin closes

### Write modes

| Mode                    | Updates    | Best for                             |
| ----------------------- | ---------- | ------------------------------------ |
| **Debounced** (default) | Every N ms | Normal development, reduces I/O      |
| **Immediate**           | Every line | Real-time debugging, low-volume logs |

## Use cases

### With AI coding assistants

```bash
# Claude Code background tasks
yarn dev 2>&1 | logwindow dev.log --max-size 8000 &

# Now Claude can read the log without context overflow
```

### Development server monitoring

```bash
# Keep React dev server logs manageable
npm start 2>&1 | logwindow react.log &

# Monitor with
tail -f react.log
```

### CI/CD pipelines

```bash
# Keep build logs to last 50KB
./long-running-build.sh 2>&1 | logwindow build.log --max-size 51200
```

## Using with AI Coding Assistants

### Claude Code Workflow

Claude Code can read log files to debug issues, but long logs cause context overflow. `logwindow` keeps logs within Claude's context window.

#### Complete Workflow Example

```bash
# 1. Start your services with logwindow
firebase emulators:start 2>&1 | logwindow firebase.log --max-size 8000 &
yarn dev 2>&1 | logwindow dev-server.log --max-size 8000 &

# 2. In Claude Code, ask it to work on your code
# "Add a new cloud function to handle user registration and write tests"

# 3. When tests fail, tell Claude where to look
# "The test failed. Check firebase.log to see what the emulator is reporting"

# 4. Claude reads firebase.log and sees only the recent 8KB
# It provides better debugging suggestions based on actual errors

# 5. Clean up when done
pkill -f "firebase emulators"
pkill -f "yarn dev"
pkill -f logwindow
```

#### Workflow with Error Filtering

For even better results, filter logs to show only errors with context:

```bash
# 1. Start services with error-only logs
firebase emulators:start 2>&1 | grep -B 2 -A 5 "ERROR" | logwindow firebase-errors.log --max-size 4000 &
yarn dev 2>&1 | grep -B 2 -A 5 -E "(Error|Warning|Failed)" | logwindow dev-errors.log --max-size 4000 &

# 2. Now Claude only sees errors, not verbose debug output
# This leaves more room in the context window for your actual code!

# 3. Ask Claude to debug
# "Check firebase-errors.log - the test is failing with a Firestore error"

# Claude gets straight to the errors without wading through noise
```

#### Shell Helper Functions

**Fish shell** (`~/.config/fish/config.fish`):

```fish
function start-dev-with-logs
    firebase emulators:start 2>&1 | logwindow firebase.log --max-size 8000 &
    yarn dev 2>&1 | logwindow dev-server.log --max-size 8000 &

    echo "✓ Started with log windows:"
    echo "  firebase.log (8KB window)"
    echo "  dev-server.log (8KB window)"
    echo ""
    echo "Tell Claude Code: 'Check firebase.log for errors'"
end

function stop-dev-logs
    pkill -f "firebase emulators"
    pkill -f "yarn dev"
    pkill -f logwindow
    echo "✓ Stopped all dev processes"
end
```

**Bash/Zsh** (`~/.bashrc` or `~/.zshrc`):

```bash
start-dev-with-logs() {
    firebase emulators:start 2>&1 | logwindow firebase.log --max-size 8000 &
    yarn dev 2>&1 | logwindow dev-server.log --max-size 8000 &

    echo "✓ Started with log windows:"
    echo "  firebase.log (8KB window)"
    echo "  dev-server.log (8KB window)"
    echo ""
    echo "Tell Claude Code: 'Check firebase.log for errors'"
}

stop-dev-logs() {
    pkill -f "firebase emulators"
    pkill -f "yarn dev"
    pkill -f logwindow
    echo "✓ Stopped all dev processes"
}
```

#### Tips for Claude Code

1. **Keep log files in your project root** - easier for Claude to find
   ```bash
   # Good
   firebase emulators:start 2>&1 | logwindow ./firebase.log &

   # Avoid
   firebase emulators:start 2>&1 | logwindow /tmp/logs/firebase.log &
   ```

2. **Use descriptive filenames**
   ```bash
   logwindow firestore-emulator.log  # Clear
   logwindow auth-emulator.log       # Clear
   logwindow output.log              # Ambiguous
   ```

3. **Tell Claude explicitly about log locations**
   ```
   "The Firestore emulator logs are in firestore-emulator.log.
   Run the test and check that file if anything fails."
   ```

4. **Adjust size based on error verbosity**
   ```bash
   # Verbose stack traces
   logwindow app.log --max-size 16000

   # Terse errors
   logwindow app.log --max-size 4000
   ```

### Aider Workflow

Aider automatically re-reads files before each LLM call, making it perfect for tracking live logs.

#### Complete Workflow Example

```bash
# Terminal 1: Start emulator with logwindow
firebase emulators:start 2>&1 | logwindow firebase.log --max-size 8000 &

# Terminal 2: Start aider
aider

# In aider: Add your source files and the log file
/add src/functions/users.js
/add firebase.log

# Now work normally
"Add a cloud function to delete user data"

# Run your tests
/run npm test

# If test fails, ask about the logs
"Check firebase.log - what error is the emulator showing?"

# Aider automatically reads the CURRENT firebase.log (last 8KB)
# It can now debug based on actual emulator output
```

#### Workflow with Error Filtering for Aider

Save context by only tracking errors:

```bash
# Terminal 1: Only capture errors with context
firebase emulators:start 2>&1 | grep -B 2 -A 10 "ERROR" | logwindow firebase-errors.log --max-size 4000 &

# Terminal 2: Aider session
aider

# In aider: Add the filtered log
/add src/functions/users.js
/add firebase-errors.log

# Now the log file is small (only errors) leaving more room for code
# Aider can track more source files while still monitoring errors

/run npm test

# If test fails
"What does firebase-errors.log show about the Firestore write that failed?"

# Aider sees only relevant errors, not verbose debug output
```

#### Key Points for Aider

✅ **Aider re-reads files before each LLM call** - You get updated logs automatically

✅ **No need to re-add** - Once added with `/add`, aider tracks the file

✅ **logwindow keeps updating the file** - As new logs come in, old ones are discarded

✅ **Fresh context every time** - Each aider query sees the current log state

#### Multiple Log Files with Aider

```bash
# Start multiple services
firebase emulators:start 2>&1 | logwindow firebase.log --max-size 8000 &
yarn dev 2>&1 | logwindow dev-server.log --max-size 8000 &

# In aider, add all relevant files
/add src/app.js
/add firebase.log
/add dev-server.log

# Ask about any log
"What errors are in firebase.log?"
"What's the latest request in dev-server.log?"
```

#### Managing Context Window in Aider

If logs take too much context:

```bash
# Option 1: Smaller log window
logwindow firebase.log --max-size 4000

# Option 2: Remove log file when not needed
/drop firebase.log

# Option 3: Add logs only when debugging
# Don't /add by default, only when you need to debug
```

#### Aider Pro Tips

**1. Add logs only when debugging**
```bash
# Normal workflow - no logs
/add src/functions.js

# When test fails - add logs temporarily
/add firebase.log
"What went wrong in the last test run?"

# After fixing - remove logs to save context
/drop firebase.log
```

**2. Use descriptive filenames**
```bash
logwindow firestore_emulator.log
logwindow auth_emulator.log

# Then in aider
"Check firestore_emulator.log for the query error"
```

**3. Check what's in context**
```bash
/ls  # Shows all files aider is tracking, including log files
```

### Why Not Just Use `tail`?

```bash
# This doesn't work for AI assistants:
tail -f firebase.log

# Because:
# 1. tail -f doesn't limit file size - logs grow indefinitely
# 2. tail -c truncates ONCE, not continuously
# 3. No way to pipe output through tail and get a readable file
# 4. AI tools need a static file they can read, not a live stream
```

`logwindow` solves all these issues by maintaining a continuously-updated file with a fixed maximum size.

## Configuration recommendations

| Use case            | `--max-size` | `--write-interval`   |
| ------------------- | ------------ | -------------------- |
| Claude Code (free)  | 8000         | 1000                 |
| Claude Pro/Extended | 16000-32000  | 1000                 |
| Aider               | 4000-8000    | 1000                 |
| Real-time debugging | 8000         | 500 or `--immediate` |
| Low I/O systems     | 16000        | 2000                 |
| High-volume logs    | 32000+       | 500                  |

## Performance

- **Memory usage**: ~2x buffer size (typically <100KB)
- **CPU usage**: Negligible (<0.1% on modern systems)
- **Disk I/O**: Configurable (1 write/second by default)
- **Throughput**: Handles 10,000+ lines/second easily

## Comparison to alternatives

| Tool           | Real-time | Size-based | Zero config | Performance |
| -------------- | --------- | ---------- | ----------- | ----------- |
| **logwindow**  | ✅        | ✅         | ✅          | ⚡⚡⚡      |
| `logrotate`    | ❌        | ⚠️         | ❌          | ⚡⚡        |
| `tail -c`      | ❌        | ✅         | ✅          | ⚡⚡⚡      |
| Custom scripts | ⚠️        | ⚠️         | ❌          | ⚡          |

## Contributing

Contributions welcome! Please feel free to submit a Pull Request.

## License

MIT License - see LICENSE file for details

## Author

Created for developers using **Claude Code, Aider, and other AI coding assistants** who need to keep Firebase emulator logs, dev server logs, and test output within the AI's context window.

## Changelog

### v1.0.0

- Initial release
- Configurable buffer size
- Debounced and immediate write modes
- Line-based buffering

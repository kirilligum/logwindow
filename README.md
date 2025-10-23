# logwindow

A fast, lightweight log truncation tool that maintains a rolling window of the most recent log data. Perfect for keeping logs readable and manageable during development, especially when working with AI coding assistants like Claude Code or Aider that need concise context.

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

### Build from source

```bash
# Clone or download
git clone https://github.com/yourusername/logwindow.git
cd logwindow

# Compile
clang++ -std=c++20 -O3 -o logwindow logwindow.cpp

# Or with GCC
g++ -std=c++20 -O3 -o logwindow logwindow.cpp

# Install (optional)
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

### Fish shell function

Add to `~/.config/fish/config.fish`:

```fish
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
```

### Bash/Zsh function

Add to `~/.bashrc` or `~/.zshrc`:

```bash
dev-logs() {
    yarn dev 2>&1 | logwindow yarn-dev.log --max-size 8000 &
    firebase emulators:start 2>&1 | logwindow firebase.log --max-size 8000 &

    echo "Logs are being truncated to 8KB:"
    echo "  yarn-dev.log"
    echo "  firebase.log"
    echo ""
    echo "View with: tail -f yarn-dev.log"
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

## Configuration recommendations

| Use case            | `--max-size` | `--write-interval`   |
| ------------------- | ------------ | -------------------- |
| Claude Code (free)  | 8000         | 1000                 |
| Claude Pro/Extended | 16000-32000  | 1000                 |
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

Created for developers who use AI coding assistants and need clean, manageable logs.

## Changelog

### v1.0.0

- Initial release
- Configurable buffer size
- Debounced and immediate write modes
- Line-based buffering

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// POSIX-specific headers for signal handling and poll
#ifdef __unix__
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#define POSIX_AVAILABLE 1
#else
#define POSIX_AVAILABLE 0
#endif

// ============================================================================
// Configuration
// ============================================================================

struct Config {
  std::string logFile;
  size_t maxSize = 10000;
  std::chrono::milliseconds writeInterval{1000};
  bool immediate = false;
  bool atomicWrites = false;
};

void printUsage(const char *progName) {
  std::cerr << "Usage: " << progName << " <logfile> [options]\n"
            << "Options:\n"
            << "  --max-size <bytes>        Maximum log size in bytes "
               "(default: 10000)\n"
            << "  --write-interval <ms>     Write interval in milliseconds "
               "(default: 1000)\n"
            << "  --immediate               Write immediately on every line "
               "(ignores interval)\n"
            << "  --atomic-writes           Use atomic write-then-rename "
               "(POSIX only)\n"
            << "  --help                    Show this help message\n"
            << "\nExamples:\n"
            << "  " << progName << " app.log\n"
            << "  " << progName
            << " app.log --max-size 16000 --write-interval 500\n"
            << "  " << progName << " app.log --immediate\n"
            << "  " << progName << " app.log --atomic-writes\n";
}

Config parseArgs(int argc, char *argv[]) {
  Config config;

  if (argc < 2) {
    printUsage(argv[0]);
    std::exit(1);
  }

  config.logFile = argv[1];

  auto requireValue = [&](int &i, const char *flag) -> const char * {
    if (i + 1 >= argc || argv[i + 1][0] == '-') {
      std::cerr << "Error: Missing value for " << flag << "\n\n";
      printUsage(argv[0]);
      std::exit(1);
    }
    return argv[++i];
  };

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      std::exit(0);
    } else if (arg == "--max-size") {
      const char *v = requireValue(i, "--max-size");
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
      const char *v = requireValue(i, "--write-interval");
      try {
        long long ms = std::stoll(v);
        if (ms <= 0) {
          config.immediate = true;
        } else {
          config.writeInterval = std::chrono::milliseconds(ms);
        }
      } catch (...) {
        std::cerr << "Error: Invalid --write-interval\n";
        std::exit(1);
      }
    } else if (arg == "--immediate") {
      config.immediate = true;
    } else if (arg == "--atomic-writes") {
      config.atomicWrites = true;
    } else {
      std::cerr << "Unknown option: " << arg << '\n';
      printUsage(argv[0]);
      std::exit(1);
    }
  }

  return config;
}

// ============================================================================
// LineBuffer: Deque-based line storage with O(1) amortized operations
// ============================================================================

class LineBuffer {
public:
  void appendLine(const std::string &line) {
    lines_.push_back(line + '\n');
    totalBytes_ += line.size() + 1;
  }

  void trimToMax(size_t maxSize) {
    while (totalBytes_ > maxSize && !lines_.empty()) {
      totalBytes_ -= lines_.front().size();
      lines_.pop_front();
    }
  }

  void assemble(std::string &out) const {
    out.clear();
    out.reserve(totalBytes_);
    for (const auto &line : lines_) {
      out += line;
    }
  }

  size_t size() const { return totalBytes_; }

  bool empty() const { return lines_.empty(); }

private:
  std::deque<std::string> lines_;
  size_t totalBytes_ = 0;
};

// ============================================================================
// Writer: Thread-safe writer with time-driven flushes
// ============================================================================

class Writer {
public:
  Writer(const Config &config)
      : config_(config), interval_(config.writeInterval),
        immediate_(config.immediate), atomicWrites_(config.atomicWrites),
        lastFlushTime_(std::chrono::steady_clock::now()) {
    openFile();
  }

  ~Writer() { closeFile(); }

  void appendLine(const std::string &line) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.appendLine(line);
    buffer_.trimToMax(config_.maxSize);
    dirty_ = true;
    cv_.notify_one();
  }

  void writerThread() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (!shuttingDown_) {
      if (immediate_) {
        // Immediate mode: wait for notification
        cv_.wait(lock, [this] { return dirty_ || shuttingDown_; });
        if (dirty_) {
          flushLocked();
        }
      } else {
        // Debounced mode: wait with timeout
        auto status = cv_.wait_for(lock, interval_);

        // Flush if dirty and either timeout occurred or enough time passed
        if (dirty_ &&
            (status == std::cv_status::timeout ||
             std::chrono::steady_clock::now() - lastFlushTime_ >= interval_)) {
          flushLocked();
        }
      }
    }

    // Final flush on shutdown
    if (dirty_) {
      flushLocked();
    }
  }

  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      shuttingDown_ = true;
    }
    cv_.notify_one();
  }

private:
  void openFile() {
    if (atomicWrites_) {
      // Atomic writes don't keep file open
      return;
    }

    std::error_code ec;
    // Try to create parent directory if needed
    auto parent = std::filesystem::path(config_.logFile).parent_path();
    if (!parent.empty()) {
      std::filesystem::create_directories(parent, ec);
    }

    fileStream_ = std::make_unique<std::fstream>(
        config_.logFile,
        std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);

    if (!fileStream_->is_open()) {
      // Create the file first if it doesn't exist
      std::ofstream create(config_.logFile, std::ios::binary);
      create.close();

      // Try opening again
      fileStream_ = std::make_unique<std::fstream>(
          config_.logFile,
          std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);

      if (!fileStream_->is_open()) {
        reportError("Failed to open log file");
        fileStream_.reset();
      }
    }
  }

  void closeFile() {
    if (fileStream_ && fileStream_->is_open()) {
      fileStream_->close();
    }
  }

  void flushLocked() {
    std::string content;
    buffer_.assemble(content);

    if (atomicWrites_) {
      flushAtomic(content);
    } else {
      flushInPlace(content);
    }

    dirty_ = false;
    lastFlushTime_ = std::chrono::steady_clock::now();
  }

  void flushInPlace(const std::string &content) {
    if (!fileStream_ || !fileStream_->is_open()) {
      openFile();
      if (!fileStream_ || !fileStream_->is_open()) {
        return; // Error already reported
      }
    }

    fileStream_->seekp(0);
    fileStream_->write(content.data(),
                       static_cast<std::streamsize>(content.size()));
    fileStream_->flush();

    if (!*fileStream_) {
      reportError("Failed to write to log file");
      closeFile();
      return;
    }

    // Truncate file to exact size
    std::error_code ec;
    std::filesystem::resize_file(config_.logFile, content.size(), ec);
    if (ec) {
      reportError("Failed to resize log file: " + ec.message());
    }
  }

  void flushAtomic(const std::string &content) {
#if POSIX_AVAILABLE
    std::string tmpFile = config_.logFile + ".tmp";

    std::ofstream tmp(tmpFile, std::ios::binary | std::ios::trunc);
    if (!tmp) {
      reportError("Failed to open temp file for atomic write");
      return;
    }

    tmp.write(content.data(), static_cast<std::streamsize>(content.size()));
    tmp.close();

    if (!tmp) {
      reportError("Failed to write temp file for atomic write");
      return;
    }

    // Atomic rename
    if (std::rename(tmpFile.c_str(), config_.logFile.c_str()) != 0) {
      reportError("Failed to rename temp file: " + std::string(std::strerror(errno)));
      std::remove(tmpFile.c_str()); // Clean up
    }
#else
    // Fall back to non-atomic on non-POSIX systems
    std::ofstream out(config_.logFile, std::ios::binary | std::ios::trunc);
    if (!out) {
      reportError("Failed to open log file (atomic writes not supported on this platform)");
      return;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
#endif
  }

  void reportError(const std::string &msg) {
    auto now = std::chrono::steady_clock::now();
    if (now - lastErrorTime_ >= std::chrono::seconds(2)) {
      std::cerr << "Error: " << msg << " (" << std::strerror(errno) << ")\n";
      lastErrorTime_ = now;
    }
  }

  const Config &config_;
  std::chrono::milliseconds interval_;
  bool immediate_;
  bool atomicWrites_;

  std::mutex mutex_;
  std::condition_variable cv_;
  LineBuffer buffer_;
  bool dirty_ = false;
  bool shuttingDown_ = false;

  std::unique_ptr<std::fstream> fileStream_;
  std::chrono::steady_clock::time_point lastFlushTime_;
  std::chrono::steady_clock::time_point lastErrorTime_;
};

// ============================================================================
// POSIX Signal Handling with Self-Pipe
// ============================================================================

#if POSIX_AVAILABLE

static int g_signalPipe[2] = {-1, -1};

void signalHandler(int sig) {
  // Async-signal-safe: just write to pipe
  char byte = static_cast<char>(sig);
  ssize_t result = write(g_signalPipe[1], &byte, 1);
  (void)result; // Ignore return value in signal handler
}

bool setupSignalHandling() {
  if (pipe(g_signalPipe) != 0) {
    std::cerr << "Failed to create signal pipe: " << std::strerror(errno)
              << '\n';
    return false;
  }

  // Set non-blocking on read end
  int flags = fcntl(g_signalPipe[0], F_GETFL, 0);
  fcntl(g_signalPipe[0], F_SETFL, flags | O_NONBLOCK);

  // Install signal handlers
  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = signalHandler;
  sa.sa_flags = SA_RESTART; // Restart interrupted syscalls

  if (sigaction(SIGINT, &sa, nullptr) != 0 ||
      sigaction(SIGTERM, &sa, nullptr) != 0) {
    std::cerr << "Failed to install signal handlers\n";
    return false;
  }

  return true;
}

void cleanupSignalHandling() {
  if (g_signalPipe[0] >= 0) {
    close(g_signalPipe[0]);
  }
  if (g_signalPipe[1] >= 0) {
    close(g_signalPipe[1]);
  }
}

// ============================================================================
// POSIX Input Reader with Poll and Line Capping
// ============================================================================

class PosixInputReader {
public:
  PosixInputReader(Writer &writer, size_t maxSize)
      : writer_(writer), maxSize_(maxSize) {}

  bool readLoop() {
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = g_signalPipe[0];
    fds[1].events = POLLIN;

    char buffer[8192];

    while (!shuttingDown_) {
      int ret = poll(fds, 2, -1);

      if (ret < 0) {
        if (errno == EINTR) {
          continue;
        }
        std::cerr << "poll() error: " << std::strerror(errno) << '\n';
        return false;
      }

      // Check signal pipe
      if (fds[1].revents & POLLIN) {
        char dummy;
        while (read(g_signalPipe[0], &dummy, 1) > 0) {
          // Drain pipe
        }
        shuttingDown_ = true;
        break;
      }

      // Check stdin
      if (fds[0].revents & POLLIN) {
        ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));

        if (n < 0) {
          if (errno == EINTR || errno == EAGAIN) {
            continue;
          }
          std::cerr << "read() error: " << std::strerror(errno) << '\n';
          return false;
        }

        if (n == 0) {
          // EOF
          shuttingDown_ = true;
          break;
        }

        processChunk(buffer, static_cast<size_t>(n));
      }

      if (fds[0].revents & (POLLHUP | POLLERR)) {
        // stdin closed or error
        shuttingDown_ = true;
        break;
      }
    }

    // Process any remaining partial line
    if (!currentLine_.empty()) {
      emitLine();
    }

    return true;
  }

private:
  void processChunk(const char *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
      char c = data[i];

      if (c == '\n') {
        if (droppingLine_) {
          // Finished dropping overlong line
          droppingLine_ = false;
        } else {
          emitLine();
        }
        currentLine_.clear();
      } else if (droppingLine_) {
        // Continue dropping
        continue;
      } else {
        // Cap line length to prevent memory overflow
        if (currentLine_.size() >= maxSize_ - 1) {
          // Line too long, start dropping
          droppingLine_ = true;
          currentLine_.clear();
          continue;
        }

        currentLine_ += c;
      }
    }
  }

  void emitLine() {
    // CRLF normalization
    if (!currentLine_.empty() && currentLine_.back() == '\r') {
      currentLine_.pop_back();
    }

    // Check size limit (after CRLF normalization)
    if (currentLine_.size() + 1 > maxSize_) {
      // Drop overlong line
      return;
    }

    writer_.appendLine(currentLine_);
  }

  Writer &writer_;
  size_t maxSize_;
  std::string currentLine_;
  bool droppingLine_ = false;
  bool shuttingDown_ = false;
};

#endif // POSIX_AVAILABLE

// ============================================================================
// Fallback Input Reader (non-POSIX)
// ============================================================================

#if !POSIX_AVAILABLE

static volatile sig_atomic_t g_running = 1;

void fallbackSignalHandler(int) { g_running = 0; }

class FallbackInputReader {
public:
  FallbackInputReader(Writer &writer, size_t maxSize)
      : writer_(writer), maxSize_(maxSize) {}

  bool readLoop() {
    std::signal(SIGINT, fallbackSignalHandler);
    std::signal(SIGTERM, fallbackSignalHandler);

    std::string line;
    while (g_running && std::getline(std::cin, line)) {
      // CRLF normalization
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      // Drop overlong lines
      if (line.size() + 1 > maxSize_) {
        continue;
      }

      writer_.appendLine(line);
    }

    return true;
  }

private:
  Writer &writer_;
  size_t maxSize_;
};

#endif // !POSIX_AVAILABLE

// ============================================================================
// Main
// ============================================================================

int main(int argc, char *argv[]) {
  // Iostream performance tuning
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  Config config = parseArgs(argc, argv);

  // Warn if atomic writes requested on non-POSIX
#if !POSIX_AVAILABLE
  if (config.atomicWrites) {
    std::cerr << "Warning: --atomic-writes may not be fully atomic on this "
                 "platform\n";
  }
#endif

  Writer writer(config);

  // Start writer thread
  std::thread writerThread([&writer]() { writer.writerThread(); });

#if POSIX_AVAILABLE
  // POSIX: Setup signal handling and poll-based input
  if (!setupSignalHandling()) {
    std::cerr << "Failed to setup signal handling, exiting\n";
    writer.shutdown();
    writerThread.join();
    return 1;
  }

  PosixInputReader reader(writer, config.maxSize);
  reader.readLoop();

  cleanupSignalHandling();
#else
  // Non-POSIX: Use simple getline-based reader
  FallbackInputReader reader(writer, config.maxSize);
  reader.readLoop();
#endif

  // Shutdown writer thread
  writer.shutdown();
  writerThread.join();

  return 0;
}

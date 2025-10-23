#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

// Signal handling for graceful shutdown (R6)
volatile sig_atomic_t g_running = 1;
void onSignal(int) { g_running = 0; }

void printUsage(const char *progName) {
  std::cerr << "Usage: " << progName << " <logfile> [options]\n"
            << "Options:\n"
            << "  --max-size <bytes>        Maximum log size in bytes "
               "(default: 8000)\n"
            << "  --write-interval <ms>     Write interval in milliseconds "
               "(default: 1000)\n"
            << "  --immediate               Write immediately on every line "
               "(ignores interval)\n"
            << "  --help                    Show this help message\n"
            << "\nExamples:\n"
            << "  " << progName << " app.log\n"
            << "  " << progName
            << " app.log --max-size 16000 --write-interval 500\n"
            << "  " << progName << " app.log --immediate\n";
}

struct Config {
  std::string logFile;
  size_t maxSize = 8000;
  std::chrono::milliseconds writeInterval{1000};
  bool immediate = false;
};

Config parseArgs(int argc, char *argv[]) {
  Config config;

  if (argc < 2) {
    printUsage(argv[0]);
    std::exit(1);
  }

  config.logFile = argv[1];

  // Helper to require and validate option values (R5)
  auto requireValue = [&](int& i, const char* flag) -> const char* {
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
    } else if (arg == "--immediate") {
      config.immediate = true;
    } else {
      std::cerr << "Unknown option: " << arg << '\n';
      printUsage(argv[0]);
      std::exit(1);
    }
  }

  return config;
}

void writeToFile(const std::string &logFile, const std::string &buffer) {
  // R7: Binary mode and better error reporting
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

int main(int argc, char *argv[]) {
  // R1: Iostream performance tuning
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  // R6: Register signal handlers for graceful shutdown
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);

  Config config = parseArgs(argc, argv);

  std::string buffer;
  buffer.reserve(config.maxSize * 2);

  auto lastWrite = std::chrono::steady_clock::now();
  bool needsWrite = false;

  // R4: Single-cut truncation helper
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

  std::string line;
  while (g_running && std::getline(std::cin, line)) {
    // R2: CRLF normalization
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // R3: Strict long-line policy - drop lines that can't fit
    if (line.size() + 1 > config.maxSize) {
      // Too large to fit without splitting; drop to preserve line-based semantics
      continue;
    }

    buffer += line;
    buffer += '\n';

    // R4: Truncate with single-cut algorithm instead of O(n²) loop
    trimToMax(buffer, config.maxSize);

    if (config.immediate) {
      // Write immediately on every line
      writeToFile(config.logFile, buffer);
    } else {
      // Debounced writing
      needsWrite = true;
      auto now = std::chrono::steady_clock::now();

      if (now - lastWrite >= config.writeInterval) {
        writeToFile(config.logFile, buffer);
        lastWrite = now;
        needsWrite = false;
      }
    }
  }

  // Final write if needed
  if (needsWrite && !config.immediate) {
    writeToFile(config.logFile, buffer);
  }

  return 0;
}

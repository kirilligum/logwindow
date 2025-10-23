#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

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

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      std::exit(0);
    } else if (arg == "--max-size" && i + 1 < argc) {
      config.maxSize = std::stoull(argv[++i]);
    } else if (arg == "--write-interval" && i + 1 < argc) {
      config.writeInterval = std::chrono::milliseconds(std::stoll(argv[++i]));
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
  std::ofstream outFile(logFile, std::ios::trunc);
  if (outFile.is_open()) {
    outFile << buffer;
  } else {
    std::cerr << "Error: Cannot write to " << logFile << '\n';
  }
}

int main(int argc, char *argv[]) {
  Config config = parseArgs(argc, argv);

  std::string buffer;
  buffer.reserve(config.maxSize * 2);

  auto lastWrite = std::chrono::steady_clock::now();
  bool needsWrite = false;

  std::string line;
  while (std::getline(std::cin, line)) {
    buffer += line;
    buffer += '\n';

    // Truncate by removing lines from the beginning
    while (buffer.size() > config.maxSize) {
      size_t first_newline = buffer.find('\n');
      if (first_newline == std::string::npos) {
        // Only one long line left, so truncate it by bytes
        buffer.erase(0, buffer.size() - config.maxSize);
        break;
      }
      // Erase the first line including its newline character
      buffer.erase(0, first_newline + 1);
    }

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

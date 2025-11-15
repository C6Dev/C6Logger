#include "../include/Logger.h"

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#endif
#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <filesystem>

// Define ANSI color codes for terminal output
#define GREEN      "\033[32m"
#define YELLOW     "\033[33m"
#define BLUE       "\033[34m"
#define GRAY       "\033[90m"
#define RED        "\033[31m"
#define BRIGHT_RED "\033[91m"
#define RESET      "\033[0m"

namespace C6Logger {

    static std::mutex logMutex;

    // Keep log size under control
    static constexpr std::size_t MAX_LOG_LINES = 1000; // trim to last 1000 lines

    // Resolve and cache the log file path once
    static std::string GetLogPathOnce() {
        static std::string cachedPath;
        if (!cachedPath.empty()) return cachedPath;

        // Prefer a writable per-user log directory on each platform.
        // Fall back to the executable directory only if necessary.
#if defined(__APPLE__)
        const char* home = std::getenv("HOME");
        if (home && *home) {
            std::filesystem::path p = std::filesystem::path(home) / "Library" / "Logs" / "C6GE";
            std::error_code ec;
            std::filesystem::create_directories(p, ec);
            if (!ec) {
                cachedPath = (p / "log.txt").string();
                return cachedPath;
            }
        }
#elif defined(_WIN32)
        char* appdata = std::getenv("LOCALAPPDATA");
        if (appdata && *appdata) {
            std::filesystem::path p = std::filesystem::path(appdata) / "C6GE" / "Logs";
            std::error_code ec;
            std::filesystem::create_directories(p, ec);
            if (!ec) {
                cachedPath = (p / "log.txt").string();
                return cachedPath;
            }
        }
#elif defined(__linux__)
        const char* xdgState = std::getenv("XDG_STATE_HOME");
        std::filesystem::path base = xdgState && *xdgState
            ? std::filesystem::path(xdgState)
            : (std::filesystem::path(std::getenv("HOME") ? std::getenv("HOME") : ".") / ".local" / "state");
        std::filesystem::path p = base / "C6GE";
        std::error_code ec;
        std::filesystem::create_directories(p, ec);
        if (!ec) {
            cachedPath = (p / "log.txt").string();
            return cachedPath;
        }
#endif

        // Fallback: place next to executable (may fail under sandboxed environments)
        std::string exeDir;
#if defined(_WIN32)
        char buffer[MAX_PATH];
        if (GetModuleFileNameA(NULL, buffer, MAX_PATH)) {
            std::string path(buffer);
            size_t pos = path.find_last_of("\\");
            if (pos != std::string::npos) {
                exeDir = path.substr(0, pos);
            }
        }
#elif defined(__APPLE__)
        char buffer[1024];
        uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) == 0) {
            std::string path(buffer);
            size_t pos = path.find_last_of("/");
            if (pos != std::string::npos) {
                exeDir = path.substr(0, pos);
            }
        }
#elif defined(__linux__)
        char buffer[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len != -1) {
            buffer[len] = '\0';
            std::string path(buffer);
            size_t pos = path.find_last_of("/");
            if (pos != std::string::npos) {
                exeDir = path.substr(0, pos);
            }
        }
#endif
        cachedPath = exeDir + "/log.txt";
        return cachedPath;
    }

    static std::string ExtractKey(const std::string& line) {
        // Expect: "[timestamp] [messenger] [LEVEL] message" or "[timestamp] [LEVEL] message"
        // Need to find the second "] [" that precedes the level when messenger is present
        std::size_t first = line.find("] [");
        if (first == std::string::npos) return line; // fallback
        std::size_t second = line.find("] [", first + 1);
        if (second == std::string::npos) {
            // messenger absent, fall back to original behavior: use substring after first "] ["
            return line.substr(first + 2);
        }
        // Skip "] " after the second bracket to return key starting with "[LEVEL] message"
        if (second + 2 >= line.size()) return line;
        return line.substr(second + 2);
    }

    static bool TryParseRepeatSuffix(const std::string& s, std::size_t& outCount, std::size_t& suffixStartPos) {
        // Suffix format: " (repeated N times)" at the end of the line
        static const std::string prefix = " (repeated ";
        static const std::string suffix = " times)";
        if (s.size() < prefix.size() + suffix.size() + 1) return false;
        std::size_t pos = s.rfind(prefix);
        if (pos == std::string::npos) return false;
        std::size_t end = s.rfind(suffix);
        if (end == std::string::npos) return false;
        if (end + suffix.size() != s.size()) return false; // must be at end
        if (end <= pos + prefix.size()) return false;
        std::size_t numStart = pos + prefix.size();
        std::size_t numLen = end - numStart;
        std::size_t value = 0;
        for (std::size_t i = 0; i < numLen; ++i) {
            char c = s[numStart + i];
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
            value = value * 10 + static_cast<std::size_t>(c - '0');
        }
        if (value == 0) return false;
        outCount = value;
        suffixStartPos = pos;
        return true;
    }

    static std::string StripRepeatSuffix(const std::string& s) {
        std::size_t count = 0, startPos = 0;
        if (TryParseRepeatSuffix(s, count, startPos)) {
            return s.substr(0, startPos);
        }
        return s;
    }

    static bool IsTimestampStart(const std::string& s, std::size_t i) {
        if (i >= s.size()) return false;
        if (s[i] != '[') return false;
        if (i + 6 >= s.size()) return false; // need at least "[YYYY-"
        // Check YYYY-
        return std::isdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(s[i + 2])) &&
            std::isdigit(static_cast<unsigned char>(s[i + 3])) &&
            std::isdigit(static_cast<unsigned char>(s[i + 4])) &&
            s[i + 5] == '-';
    }

    static void SplitConcatenatedLines(const std::string& raw, std::vector<std::string>& out) {
        // Some previous runs may have concatenated multiple timestamped entries onto one line.
        // Split whenever we see another timestamp start in the middle of the line.
        std::size_t start = 0;
        for (std::size_t i = 1; i < raw.size(); ++i) { // start at 1 so the very first '[' at pos 0 is kept
            if (IsTimestampStart(raw, i)) {
                // flush previous segment
                if (i > start) {
                    out.emplace_back(raw.substr(start, i - start));
                }
                start = i;
            }
        }
        if (start < raw.size()) {
            out.emplace_back(raw.substr(start));
        }
    }

    static void CompressAndTrimLogFile(const std::string& logPath, std::size_t maxLines) {
        std::ifstream in(logPath.c_str());
        if (!in.is_open()) return;
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            // Split line if it contains multiple timestamped entries concatenated
            std::vector<std::string> parts;
            SplitConcatenatedLines(line, parts);
            for (auto& p : parts) {
                if (!p.empty()) lines.emplace_back(std::move(p));
            }
        }
        in.close();

        if (lines.empty()) return;

        struct Record { std::string baseLine; std::size_t count; std::size_t lastIndex; };
        std::unordered_map<std::string, std::size_t> indexByKey;
        std::vector<std::pair<std::string, Record>> records; // preserve insertion order via vector of pairs
        records.reserve(lines.size());

        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::string& l = lines[i];
            // Determine how many occurrences this line represents (1 or parsed N)
            std::size_t parsedCount = 1;
            std::size_t suffixStart = 0;
            if (TryParseRepeatSuffix(l, parsedCount, suffixStart)) {
                // ok, parsedCount set; use the line without suffix as the base line
            }
            else {
                parsedCount = 1;
                suffixStart = std::string::npos;
            }

            std::string base = (suffixStart == std::string::npos) ? l : l.substr(0, suffixStart);
            std::string key = ExtractKey(base); // key should not include suffix
            auto it = indexByKey.find(key);
            if (it == indexByKey.end()) {
                std::size_t idx = records.size();
                indexByKey.emplace(key, idx);
                records.emplace_back(key, Record{ base, parsedCount, i });
            }
            else {
                auto& rec = records[it->second].second;
                rec.count += parsedCount;
                rec.lastIndex = i;
                rec.baseLine = base; // keep most recent timestamped line (without suffix)
            }
        }

        // Sort by recency (lastIndex ascending), then keep only last maxLines
        std::stable_sort(records.begin(), records.end(), [](const std::pair<std::string, Record>& a, const std::pair<std::string, Record>& b) {
            return a.second.lastIndex < b.second.lastIndex;
            });
        if (records.size() > maxLines) {
            records.erase(records.begin(), records.end() - static_cast<std::ptrdiff_t>(maxLines));
        }

        std::ofstream out(logPath.c_str(), std::ios::trunc);
        if (!out.is_open()) return;
        for (const auto& kv : records) {
            const auto& rec = kv.second;
            if (rec.count > 1) {
                out << rec.baseLine << " (repeated " << rec.count << " times)" << '\n';
            }
            else {
                out << rec.baseLine << '\n';
            }
        }
        out.close();
    }

    std::string GetTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::tm buf;
        // Use localtime_s on MSVC, localtime_r on POSIX, fallback to localtime (unsafe) otherwise
#if defined(_MSC_VER)
        localtime_s(&buf, &in_time_t);
#elif defined(__unix__) || defined(__APPLE__)
        localtime_r(&in_time_t, &buf);
#else
        std::tm* tmp = std::localtime(&in_time_t);
        if (tmp) buf = *tmp;
#endif
        std::stringstream ss;
        ss << std::put_time(&buf, "%Y-%m-%d %X");
        return ss.str();
    }

    // Two-argument overload forwards to three-argument version with empty messenger
    void Log(LogLevel level, const std::string& message) {
        Log(level, message, std::string());
    }

    void Log(LogLevel level, const std::string& message, const std::string& messenger) {
        std::lock_guard<std::mutex> lock(logMutex);

        static const char* levelStr[] = { "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL" };
        static const char* colorStr[] = { BLUE, "", GRAY, YELLOW, RED, BRIGHT_RED };

        std::string timestamp = GetTimestamp();
        // base line (without any repeat suffix)
        bool hasMessenger = !messenger.empty();
        std::string baseLine;
        if (hasMessenger) {
            baseLine = "[" + timestamp + "] [" + messenger + "] [" + levelStr[static_cast<int>(level)] + "] " + message;
        }
        else {
            baseLine = "[" + timestamp + "] [" + levelStr[static_cast<int>(level)] + "] " + message;
        }
        // key used to detect duplicates, independent of timestamp and messenger
        std::string key = std::string("[") + levelStr[static_cast<int>(level)] + "] " + message;

        // Console output
        std::ostream& out = (level == LogLevel::error || level == LogLevel::critical) ? std::cerr : std::cout;
        out << colorStr[static_cast<int>(level)] << baseLine << RESET << std::endl;

        // Log file path (cached)
        std::string logPath = GetLogPathOnce();

        // Always append the new line first
        std::ofstream logFile(logPath.c_str(), std::ios::app);
        if (logFile.is_open()) {
            logFile << baseLine << std::endl;
            logFile.close();
        }
        else {
            std::cerr << RED << "[ERROR] Failed to open log file '" << logPath << "' for writing." << RESET << std::endl;
        }

        // Compress duplicates across the entire file and enforce line limit
        CompressAndTrimLogFile(logPath, MAX_LOG_LINES);
    }
}
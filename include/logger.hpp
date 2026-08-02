#pragma once

#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

namespace fse {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

namespace detail {

template <typename... Args>
std::string formatMessage(const char* format, Args&&... args) {
    if (format == nullptr) {
        return {};
    }

    if constexpr (sizeof...(Args) == 0) {
        return std::string(format);
    } else {
        const int size =
            std::snprintf(nullptr, 0, format, std::forward<Args>(args)...) + 1;
        if (size <= 0) {
            return std::string(format);
        }

        std::string result(static_cast<size_t>(size), '\0');
        std::snprintf(&result[0], static_cast<size_t>(size), format,
                      std::forward<Args>(args)...);
        result.pop_back();
        return result;
    }
}

}  // namespace detail

class Logger {
public:
    static Logger& instance();

    void setLogFile(const std::string& path);
    void setMinLevel(LogLevel level);

    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);

    template <typename... Args>
    void debug(const char* format, Args&&... args) {
        log(LogLevel::Debug,
            detail::formatMessage(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void info(const char* format, Args&&... args) {
        log(LogLevel::Info,
            detail::formatMessage(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void warning(const char* format, Args&&... args) {
        log(LogLevel::Warning,
            detail::formatMessage(format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    void error(const char* format, Args&&... args) {
        log(LogLevel::Error,
            detail::formatMessage(format, std::forward<Args>(args)...));
    }

    void log(LogLevel level, const std::string& message);

    // std::string levelToString(LogLevel level) const;

    template <typename... Args>
    void log(LogLevel level, const char* format, Args&&... args) {
        log(level, detail::formatMessage(format, std::forward<Args>(args)...));
    }
    // std::string levelToString(LogLevel level);

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string levelToString(LogLevel level) const;
    std::string currentTimestamp() const;
    void write(LogLevel level, const std::string& message);

    std::mutex mutex_;
    std::ofstream file_;
    LogLevel minLevel_;
    bool consoleOutput_;
};

}  // namespace fse

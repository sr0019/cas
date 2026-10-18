#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <iostream>

namespace cas {

enum class LogLevel {
    Debug,
    Info,
    Success,
    Warn,
    Error,
    Metric
};

inline const char* logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Success: return "SUCCESS";
        case LogLevel::Warn:    return "WARN";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Metric:  return "METRIC";
        default:                return "INFO";
    }
}

using LogListener = std::function<void(LogLevel level, const std::string& formattedMsg, const std::string& rawMsg)>;

class Logger {
public:
    static Logger& instance() {
        static Logger s_instance;
        return s_instance;
    }

    void addListener(LogListener listener) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listeners.push_back(listener);
    }

    void clearListeners() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_listeners.clear();
    }

    void log(LogLevel level, const std::string& message) {
        std::string timestamp = getCurrentTimestamp();
        std::string formatted = "[" + timestamp + "] [" + logLevelToString(level) + "] " + message;

        std::lock_guard<std::mutex> lock(m_mutex);
        if (level == LogLevel::Error) {
            std::cerr << formatted << std::endl;
        } else {
            std::cout << formatted << std::endl;
        }

        for (const auto& listener : m_listeners) {
            listener(level, formatted, message);
        }
    }

    static void debug(const std::string& msg)   { instance().log(LogLevel::Debug, msg); }
    static void info(const std::string& msg)    { instance().log(LogLevel::Info, msg); }
    static void success(const std::string& msg) { instance().log(LogLevel::Success, msg); }
    static void warn(const std::string& msg)    { instance().log(LogLevel::Warn, msg); }
    static void error(const std::string& msg)   { instance().log(LogLevel::Error, msg); }
    static void metric(const std::string& msg)  { instance().log(LogLevel::Metric, msg); }

private:
    Logger() = default;

    std::string getCurrentTimestamp() const {
        auto now = std::chrono::system_clock::now();
        std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &nowTime);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        return buf;
    }

    std::mutex m_mutex;
    std::vector<LogListener> m_listeners;
};

} // namespace cas

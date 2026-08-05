#pragma once

#include <fstream>
#include <mutex>
#include <string>

namespace ivp {

enum class LogLevel {
    Debug = 0,
    Info,
    Warning,
    Error
};

class Logger final {
public:
    static Logger& instance();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void setLevel(LogLevel level);
    bool setOutputFile(const std::string& file_path);
    void log(LogLevel level, const std::string& message, const char* file, int line);

    static const char* levelName(LogLevel level);

private:
    Logger() = default;

    bool shouldLog(LogLevel level) const;

    std::mutex mutex_;
    LogLevel level_{LogLevel::Info};
    std::ofstream file_;
};

}  // namespace ivp

#define IVP_LOG_DEBUG(message) ::ivp::Logger::instance().log(::ivp::LogLevel::Debug, (message), __FILE__, __LINE__)
#define IVP_LOG_INFO(message) ::ivp::Logger::instance().log(::ivp::LogLevel::Info, (message), __FILE__, __LINE__)
#define IVP_LOG_WARNING(message) ::ivp::Logger::instance().log(::ivp::LogLevel::Warning, (message), __FILE__, __LINE__)
#define IVP_LOG_ERROR(message) ::ivp::Logger::instance().log(::ivp::LogLevel::Error, (message), __FILE__, __LINE__)

#include "ivp/common/Logger.h"

#include "ivp/common/Utils.h"

#include <iostream>
#include <sstream>

namespace ivp {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

bool Logger::setOutputFile(const std::string& file_path) {
    std::lock_guard<std::mutex> lock(mutex_);

    file_.close();
    file_.open(file_path, std::ios::app);

    return file_.is_open();
}

void Logger::log(LogLevel level, const std::string& message, const char* file, int line) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!shouldLog(level)) {
        return;
    }

    std::ostringstream stream;
    stream << '[' << utils::currentDateTime() << ']'
           << " [" << levelName(level) << ']'
           << " [" << file << ':' << line << "] "
           << message;

    auto& console = level >= LogLevel::Warning ? std::cerr : std::cout;
    console << stream.str() << '\n';

    if (file_.is_open()) {
        file_ << stream.str() << '\n';
    }
}

const char* Logger::levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARNING";
        case LogLevel::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}

bool Logger::shouldLog(LogLevel level) const {
    return static_cast<int>(level) >= static_cast<int>(level_);
}

}  // namespace ivp

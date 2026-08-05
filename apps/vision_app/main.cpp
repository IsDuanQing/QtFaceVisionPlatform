#include "ivp/common/Config.h"
#include "ivp/common/Logger.h"
#include "ivp/common/Utils.h"

#include <string>

namespace {

ivp::LogLevel parseLogLevel(const std::string& value) {
    const auto normalized = ivp::utils::toLower(value);

    if (normalized == "debug") {
        return ivp::LogLevel::Debug;
    }
    if (normalized == "warning" || normalized == "warn") {
        return ivp::LogLevel::Warning;
    }
    if (normalized == "error") {
        return ivp::LogLevel::Error;
    }
    return ivp::LogLevel::Info;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string config_path = argc > 1 ? argv[1] : "configs/app.conf";

    ivp::Config config;
    if (!config.loadFromFile(config_path)) {
        IVP_LOG_WARNING("Failed to load config file: " + config_path);
    }

    ivp::Logger::instance().setLevel(parseLogLevel(config.getString("log.level", "info")));

    const auto log_file = config.getString("log.file", "");
    if (!log_file.empty() && !ivp::Logger::instance().setOutputFile(log_file)) {
        IVP_LOG_WARNING("Failed to open log file: " + log_file);
    }

    const auto app_name = config.getString("app.name", "IndustrialVisionPlatform");
    IVP_LOG_INFO("Application started: " + app_name);
    IVP_LOG_INFO("Current time: " + ivp::utils::currentDateTime());

    return 0;
}

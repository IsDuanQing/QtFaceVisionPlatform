#include "ivp/common/Config.h"

#include "ivp/common/Utils.h"

#include <fstream>

namespace ivp {

bool Config::loadFromFile(const std::string& file_path) {
    std::ifstream input(file_path);
    if (!input.is_open()) {
        return false;
    }

    values_.clear();

    std::string line;
    while (std::getline(input, line)) {
        line = utils::trim(line);

        if (line.empty() || utils::startsWith(line, "#") || utils::startsWith(line, ";")) {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        auto key = utils::trim(line.substr(0, separator));
        auto value = utils::trim(line.substr(separator + 1));

        if (!key.empty()) {
            values_[key] = value;
        }
    }

    return true;
}

bool Config::contains(const std::string& key) const {
    return values_.find(key) != values_.end();
}

std::string Config::getString(const std::string& key, const std::string& default_value) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return default_value;
    }
    return it->second;
}

int Config::getInt(const std::string& key, int default_value) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return default_value;
    }

    try {
        std::size_t parsed = 0;
        const int value = std::stoi(it->second, &parsed);
        return parsed == it->second.size() ? value : default_value;
    } catch (...) {
        return default_value;
    }
}

double Config::getDouble(const std::string& key, double default_value) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return default_value;
    }

    try {
        std::size_t parsed = 0;
        const double value = std::stod(it->second, &parsed);
        return parsed == it->second.size() ? value : default_value;
    } catch (...) {
        return default_value;
    }
}

bool Config::getBool(const std::string& key, bool default_value) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return default_value;
    }

    const auto value = utils::toLower(it->second);
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    return default_value;
}

}  // namespace ivp

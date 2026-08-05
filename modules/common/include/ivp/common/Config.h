#pragma once

#include <string>
#include <unordered_map>

namespace ivp {

class Config final {
public:
    bool loadFromFile(const std::string& file_path);

    bool contains(const std::string& key) const;

    std::string getString(const std::string& key, const std::string& default_value = "") const;
    int getInt(const std::string& key, int default_value = 0) const;
    double getDouble(const std::string& key, double default_value = 0.0) const;
    bool getBool(const std::string& key, bool default_value = false) const;

private:
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace ivp

#pragma once

#include <string>
#include <vector>

namespace ivp::utils {

std::string trim(const std::string& value);
std::vector<std::string> split(const std::string& value, char delimiter);
std::string toLower(std::string value);
bool startsWith(const std::string& value, const std::string& prefix);
std::string currentDateTime();
bool fileExists(const std::string& file_path);

}  // namespace ivp::utils

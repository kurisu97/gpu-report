#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gpu_report {

std::string trim(const std::string& value);
std::string toLowerAscii(const std::string& value);
std::string toUpperAscii(const std::string& value);
bool startsWith(const std::string& value, const std::string& prefix);
bool endsWith(const std::string& value, const std::string& suffix);
std::vector<std::string> split(const std::string& value, char delimiter);
std::vector<std::string> splitWhitespace(const std::string& value);
std::vector<std::string> splitLines(const std::string& value);
std::string join(const std::vector<std::string>& values, const std::string& separator);
std::string replaceAll(std::string value, const std::string& from, const std::string& to);

bool parseInt(const std::string& value, int& result);
bool parseUnsignedHex(const std::string& value, uint32_t& result);
std::string hexValue(uint32_t value, int width = 4);

// Terminal display helpers. They are UTF-8 aware when the current locale
// supports it and degrade to byte counting otherwise.
std::size_t displayWidth(const std::string& value);
std::string truncateDisplay(const std::string& value, std::size_t width, const std::string& ellipsis = "...");
std::string padDisplay(const std::string& value, std::size_t width);
std::string collapseWhitespace(const std::string& value);

} // namespace gpu_report

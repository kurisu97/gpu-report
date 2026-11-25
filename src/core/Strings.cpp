#include "core/Strings.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <sstream>

namespace gpu_report {

namespace {

bool isAsciiSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

int hexDigit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

} // namespace

std::string trim(const std::string& value)
{
    std::size_t begin = 0;
    while (begin < value.size() && isAsciiSpace(value[begin])) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin && isAsciiSpace(value[end - 1])) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string toLowerAscii(const std::string& value)
{
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string toUpperAscii(const std::string& value)
{
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return result;
}

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::string& value, const std::string& suffix)
{
    return value.size() >= suffix.size()
        && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& value, char delimiter)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t pos = value.find(delimiter, start);
        if (pos == std::string::npos) {
            result.push_back(value.substr(start));
            break;
        }
        result.push_back(value.substr(start, pos - start));
        start = pos + 1;
    }
    return result;
}

std::vector<std::string> splitWhitespace(const std::string& value)
{
    std::vector<std::string> result;
    std::size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && isAsciiSpace(value[i])) {
            ++i;
        }
        if (i >= value.size()) {
            break;
        }
        const std::size_t start = i;
        while (i < value.size() && !isAsciiSpace(value[i])) {
            ++i;
        }
        result.push_back(value.substr(start, i - start));
    }
    return result;
}

std::vector<std::string> splitLines(const std::string& value)
{
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t pos = value.find('\n', start);
        if (pos == std::string::npos) {
            std::string line = value.substr(start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            result.push_back(line);
            break;
        }
        std::string line = value.substr(start, pos - start);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        result.push_back(line);
        start = pos + 1;
    }
    return result;
}

std::string join(const std::vector<std::string>& values, const std::string& separator)
{
    std::string result;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            result += separator;
        }
        result += values[i];
    }
    return result;
}

std::string replaceAll(std::string value, const std::string& from, const std::string& to)
{
    if (from.empty()) {
        return value;
    }
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

bool parseInt(const std::string& value, int& result)
{
    const std::string trimmed = trim(value);
    if (trimmed.empty()) {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(trimmed.c_str(), &end, 10);
    if (errno != 0 || end == trimmed.c_str() || *end != '\0'
        || parsed < std::numeric_limits<int>::min()
        || parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    result = static_cast<int>(parsed);
    return true;
}

bool parseUnsignedHex(const std::string& value, uint32_t& result)
{
    std::string trimmed = trim(value);
    if (startsWith(trimmed, "0x") || startsWith(trimmed, "0X")) {
        trimmed = trimmed.substr(2);
    }
    if (trimmed.empty() || trimmed.size() > 8) {
        return false;
    }
    uint32_t parsed = 0;
    for (char c : trimmed) {
        const int digit = hexDigit(c);
        if (digit < 0) {
            return false;
        }
        parsed = (parsed << 4) | static_cast<uint32_t>(digit);
    }
    result = parsed;
    return true;
}

std::string hexValue(uint32_t value, int width)
{
    std::ostringstream out;
    out << "0x" << std::hex;
    if (width > 0) {
        out.width(width);
        out.fill('0');
    }
    out << value;
    return out.str();
}

std::size_t displayWidth(const std::string& value)
{
    std::size_t width = 0;
    std::mbstate_t state {};
    const char* pointer = value.c_str();
    std::size_t remaining = value.size();
    while (remaining > 0) {
        wchar_t wide = 0;
        const std::size_t consumed = std::mbrtowc(&wide, pointer, remaining, &state);
        if (consumed == static_cast<std::size_t>(-1) || consumed == static_cast<std::size_t>(-2)) {
            // Invalid or incomplete sequence: count one byte and resynchronise.
            state = std::mbstate_t {};
            ++width;
            ++pointer;
            --remaining;
            continue;
        }
        if (consumed == 0) {
            break;
        }
        const int cellWidth = wcwidth(wide);
        // Combining characters legitimately occupy zero terminal cells;
        // non-printable characters (wcwidth < 0) still consume a fallback cell.
        width += (cellWidth >= 0) ? static_cast<std::size_t>(cellWidth) : 1;
        pointer += consumed;
        remaining -= consumed;
    }
    return width;
}

std::string truncateDisplay(const std::string& value, std::size_t width, const std::string& ellipsis)
{
    if (displayWidth(value) <= width) {
        return value;
    }
    const std::size_t ellipsisWidth = displayWidth(ellipsis);
    if (width <= ellipsisWidth) {
        return truncateDisplay(value, width, std::string());
    }

    const std::size_t target = width - ellipsisWidth;
    std::size_t used = 0;
    std::size_t index = 0;
    std::mbstate_t state {};
    while (index < value.size()) {
        wchar_t wide = 0;
        const std::size_t consumed = std::mbrtowc(&wide, value.c_str() + index,
                                                  value.size() - index, &state);
        std::size_t charWidth = 1;
        std::size_t advance = 1;
        if (consumed != static_cast<std::size_t>(-1)
            && consumed != static_cast<std::size_t>(-2)
            && consumed != 0) {
            const int cellWidth = wcwidth(wide);
            charWidth = (cellWidth >= 0) ? static_cast<std::size_t>(cellWidth) : 1;
            advance = consumed;
        } else {
            state = std::mbstate_t {};
        }
        if (used + charWidth > target) {
            break;
        }
        used += charWidth;
        index += advance;
    }
    return value.substr(0, index) + ellipsis;
}

std::string padDisplay(const std::string& value, std::size_t width)
{
    const std::size_t current = displayWidth(value);
    if (current >= width) {
        return value;
    }
    return value + std::string(width - current, ' ');
}

std::string collapseWhitespace(const std::string& value)
{
    std::string result;
    bool previousSpace = false;
    for (char c : value) {
        if (isAsciiSpace(c)) {
            if (!previousSpace && !result.empty()) {
                result.push_back(' ');
            }
            previousSpace = true;
        } else {
            result.push_back(c);
            previousSpace = false;
        }
    }
    return trim(result);
}

} // namespace gpu_report

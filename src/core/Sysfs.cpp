#include "core/Sysfs.hpp"

#include "core/Strings.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

namespace gpu_report {

bool readFile(const std::filesystem::path& path, std::string& content)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    content.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

std::string readFileOr(const std::filesystem::path& path, const std::string& fallback)
{
    std::string content;
    if (!readFile(path, content)) {
        return fallback;
    }
    return content;
}

bool pathExists(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::exists(path, error);
}

bool isDirectory(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

std::vector<std::string> listDirectoryNames(const std::filesystem::path& path)
{
    std::vector<std::string> names;
    std::error_code error;
    std::filesystem::directory_iterator iterator(path, error);
    if (error) {
        return names;
    }
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        names.push_back(iterator->path().filename().string());
        iterator.increment(error);
        if (error) {
            break;
        }
    }
    return names;
}

std::string canonicalPath(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::canonical(path, error);
    if (error) {
        return {};
    }
    return canonical.string();
}

std::string readSymlinkName(const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path target = std::filesystem::read_symlink(path, error);
    if (error) {
        return {};
    }
    return target.filename().string();
}

bool readHexFile(const std::filesystem::path& path, uint32_t& value)
{
    std::string content;
    if (!readFile(path, content)) {
        return false;
    }
    return parseUnsignedHex(trim(content), value);
}

} // namespace gpu_report

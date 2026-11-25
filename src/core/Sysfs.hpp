#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gpu_report {

std::string readFileOr(const std::filesystem::path& path, const std::string& fallback = {});
bool readFile(const std::filesystem::path& path, std::string& content);
bool pathExists(const std::filesystem::path& path);
bool isDirectory(const std::filesystem::path& path);
std::vector<std::string> listDirectoryNames(const std::filesystem::path& path);
std::string canonicalPath(const std::filesystem::path& path);
std::string readSymlinkName(const std::filesystem::path& path);
bool readHexFile(const std::filesystem::path& path, uint32_t& value);

} // namespace gpu_report

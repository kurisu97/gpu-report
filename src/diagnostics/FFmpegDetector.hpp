#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace gpu_report {

struct FfmpegEntry {
    std::string name;
    std::string codec;
    std::string api;
    std::string description;
};

struct FfmpegCapabilities {
    bool available = false;
    bool timedOut = false;
    int exitCode = -1;
    std::string version;
    std::string path;
    std::map<std::string, FfmpegEntry> encoders;
    std::map<std::string, FfmpegEntry> decoders;
    std::set<std::string> hwaccels;
    std::map<std::string, std::vector<std::string>> pixelFormats;

    bool hasEncoder(const std::string& name) const;
    bool hasDecoder(const std::string& name) const;
    bool hasHwaccel(const std::string& name) const;
    std::vector<const FfmpegEntry*> encodersForApi(const std::string& api) const;
    std::vector<const FfmpegEntry*> decodersForApi(const std::string& api) const;
};

std::string ffmpegApiFromName(const std::string& name);

class FFmpegDetector {
public:
    static FfmpegCapabilities detect(int timeoutMs = 12000);
};

} // namespace gpu_report

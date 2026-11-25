#include "diagnostics/FFmpegDetector.hpp"

#include "core/Process.hpp"
#include "core/Strings.hpp"
#include "gpu/Codecs.hpp"

#include <algorithm>

namespace gpu_report {

namespace {

bool looksLikeFlags(const std::string& token)
{
    if (token.size() < 5) {
        return false;
    }
    for (char c : token) {
        if (c != 'V' && c != 'A' && c != 'S' && c != 'D' && c != '.') {
            return false;
        }
    }
    return true;
}

std::string codecTokenFromName(const std::string& name)
{
    static const std::vector<std::string> suffixes = {
        "_v4l2m2m", "_vaapi", "_nvenc", "_cuvid", "_qsv", "_amf", "_vulkan",
    };
    for (const std::string& suffix : suffixes) {
        if (endsWith(name, suffix)) {
            return name.substr(0, name.size() - suffix.size());
        }
    }
    return {};
}

std::string codecFromDescription(const std::string& description)
{
    const std::size_t marker = description.find("(codec ");
    if (marker == std::string::npos) {
        return {};
    }
    const std::size_t begin = marker + std::string("(codec ").size();
    const std::size_t end = description.find(')', begin);
    if (end == std::string::npos) {
        return {};
    }
    return description.substr(begin, end - begin);
}

std::map<std::string, FfmpegEntry> parseEntries(const std::string& output, bool encoders)
{
    std::map<std::string, FfmpegEntry> entries;
    for (const std::string& line : splitLines(output)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.back() == ':') {
            continue;
        }
        const std::vector<std::string> tokens = splitWhitespace(trimmed);
        if (tokens.size() < 2 || !looksLikeFlags(tokens[0])) {
            continue;
        }

        FfmpegEntry entry;
        entry.name = tokens[1];
        const std::size_t namePosition = trimmed.find(entry.name);
        if (namePosition != std::string::npos) {
            entry.description = trim(trimmed.substr(namePosition + entry.name.size()));
        }

        entry.codec = codecFromDescription(entry.description);
        if (entry.codec.empty()) {
            entry.codec = codecTokenFromName(entry.name);
        }
        if (entry.codec.empty()) {
            continue;
        }
        entry.codec = codecs::codecFromFfmpegName(entry.codec);
        if (entry.codec.empty()) {
            continue;
        }
        entry.api = ffmpegApiFromName(entry.name);
        if (entry.api.empty() && !encoders) {
            // Software decoders are not hardware evidence.
            continue;
        }
        entries[entry.name] = entry;
    }
    return entries;
}

std::string parseVersion(const std::string& output)
{
    const std::string firstLine = splitLines(output).empty() ? std::string() : splitLines(output).front();
    const std::size_t marker = firstLine.find("version ");
    if (marker == std::string::npos) {
        return trim(firstLine);
    }
    const std::size_t begin = marker + std::string("version ").size();
    const std::size_t end = firstLine.find_first_of(" \t", begin);
    return firstLine.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
}

std::vector<std::string> parsePixelFormats(const std::string& output)
{
    std::vector<std::string> formats;
    for (const std::string& line : splitLines(output)) {
        const std::string trimmed = trim(line);
        if (!startsWith(trimmed, "Supported pixel formats:")) {
            continue;
        }
        const std::string value = trim(trimmed.substr(std::string("Supported pixel formats:").size()));
        for (const std::string& format : splitWhitespace(value)) {
            if (std::find(formats.begin(), formats.end(), format) == formats.end()) {
                formats.push_back(format);
            }
        }
        break;
    }
    return formats;
}

} // namespace

std::string ffmpegApiFromName(const std::string& name)
{
    if (endsWith(name, "_vaapi")) {
        return "VA-API";
    }
    if (endsWith(name, "_nvenc")) {
        return "NVENC";
    }
    if (endsWith(name, "_cuvid")) {
        return "NVDEC";
    }
    if (endsWith(name, "_qsv")) {
        return "QSV";
    }
    if (endsWith(name, "_amf")) {
        return "AMF";
    }
    if (endsWith(name, "_v4l2m2m")) {
        return "V4L2";
    }
    if (endsWith(name, "_vulkan")) {
        return "Vulkan";
    }
    return {};
}

bool FfmpegCapabilities::hasEncoder(const std::string& name) const
{
    return encoders.find(name) != encoders.end();
}

bool FfmpegCapabilities::hasDecoder(const std::string& name) const
{
    return decoders.find(name) != decoders.end();
}

bool FfmpegCapabilities::hasHwaccel(const std::string& name) const
{
    return hwaccels.find(name) != hwaccels.end();
}

std::vector<const FfmpegEntry*> FfmpegCapabilities::encodersForApi(const std::string& api) const
{
    std::vector<const FfmpegEntry*> result;
    for (const auto& item : encoders) {
        if (item.second.api == api) {
            result.push_back(&item.second);
        }
    }
    return result;
}

std::vector<const FfmpegEntry*> FfmpegCapabilities::decodersForApi(const std::string& api) const
{
    std::vector<const FfmpegEntry*> result;
    for (const auto& item : decoders) {
        if (item.second.api == api) {
            result.push_back(&item.second);
        }
    }
    return result;
}

FfmpegCapabilities FFmpegDetector::detect(int timeoutMs)
{
    FfmpegCapabilities capabilities;
    std::string ffmpegPath;
    if (!findExecutable("ffmpeg", &ffmpegPath)) {
        return capabilities;
    }
    capabilities.path = ffmpegPath;

    const ProcessResult versionResult = runProcess({ffmpegPath, "-hide_banner", "-version"},
                                                   timeoutMs, 512 * 1024);
    if (!versionResult.started || versionResult.timedOut || versionResult.exitCode != 0) {
        capabilities.timedOut = versionResult.timedOut;
        capabilities.exitCode = versionResult.exitCode;
        return capabilities;
    }
    capabilities.version = parseVersion(versionResult.output);

    const ProcessResult encodersResult = runProcess({ffmpegPath, "-hide_banner", "-encoders"},
                                                    timeoutMs, 2 * 1024 * 1024);
    const ProcessResult decodersResult = runProcess({ffmpegPath, "-hide_banner", "-decoders"},
                                                    timeoutMs, 2 * 1024 * 1024);
    const ProcessResult hwaccelsResult = runProcess({ffmpegPath, "-hide_banner", "-hwaccels"},
                                                     timeoutMs, 256 * 1024);

    if (encodersResult.timedOut || decodersResult.timedOut) {
        capabilities.timedOut = true;
        capabilities.exitCode = -1;
        return capabilities;
    }
    if (!encodersResult.started || encodersResult.exitCode != 0
        || !decodersResult.started || decodersResult.exitCode != 0) {
        capabilities.exitCode = encodersResult.exitCode != 0
            ? encodersResult.exitCode
            : decodersResult.exitCode;
        return capabilities;
    }

    capabilities.encoders = parseEntries(encodersResult.combined(), true);
    capabilities.decoders = parseEntries(decodersResult.combined(), false);

    for (const std::string& line : splitLines(hwaccelsResult.output)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.back() == ':' || trimmed.find(' ') != std::string::npos) {
            continue;
        }
        capabilities.hwaccels.insert(trimmed);
    }

    // Query pixel formats only for hardware encoders that we might report.
    for (const auto& item : capabilities.encoders) {
        if (item.second.api.empty()) {
            continue;
        }
        const ProcessResult help = runProcess({ffmpegPath, "-hide_banner", "-h",
                                               "encoder=" + item.first},
                                              5000, 512 * 1024);
        const std::vector<std::string> formats = parsePixelFormats(help.combined());
        if (!formats.empty()) {
            capabilities.pixelFormats[item.first] = formats;
        }
    }

    capabilities.available = true;
    capabilities.exitCode = 0;
    return capabilities;
}

} // namespace gpu_report

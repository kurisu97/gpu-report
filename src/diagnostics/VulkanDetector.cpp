#include "diagnostics/VulkanDetector.hpp"

#include "core/Process.hpp"
#include "core/Strings.hpp"
#include "gpu/Codecs.hpp"

#include <cctype>
#include <map>

namespace gpu_report {

namespace {

bool isGpuHeader(const std::string& line)
{
    const std::string trimmed = trim(line);
    if (trimmed.size() < 5 || !startsWith(trimmed, "GPU") || trimmed.back() != ':') {
        return false;
    }
    for (std::size_t i = 3; i + 1 < trimmed.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
            return false;
        }
    }
    return true;
}

std::string valueAfterEquals(const std::string& line)
{
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
        return {};
    }
    return trim(line.substr(equals + 1));
}

std::string keyBeforeEquals(const std::string& line)
{
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) {
        return {};
    }
    return trim(line.substr(0, equals));
}

std::vector<VulkanDevice> parseSummary(const std::string& output)
{
    std::vector<VulkanDevice> devices;
    VulkanDevice current;
    bool inGpu = false;

    auto flush = [&]() {
        if (inGpu && (!current.name.empty() || current.vendorId != 0)) {
            devices.push_back(current);
        }
        current = VulkanDevice();
    };

    for (const std::string& line : splitLines(output)) {
        if (isGpuHeader(line)) {
            flush();
            inGpu = true;
            continue;
        }
        if (!inGpu) {
            continue;
        }
        const std::string key = keyBeforeEquals(line);
        if (key.empty()) {
            continue;
        }
        const std::string value = valueAfterEquals(line);
        if (key == "vendorID") {
            uint32_t parsed = 0;
            if (parseUnsignedHex(value, parsed)) {
                current.vendorId = static_cast<uint16_t>(parsed);
            }
        } else if (key == "deviceID") {
            uint32_t parsed = 0;
            if (parseUnsignedHex(value, parsed)) {
                current.deviceId = parsed;
            }
        } else if (key == "deviceName") {
            current.name = value;
        } else if (key == "deviceType") {
            current.deviceType = value;
            current.integrated = value.find("INTEGRATED_GPU") != std::string::npos;
            current.discrete = value.find("DISCRETE_GPU") != std::string::npos;
        } else if (key == "driverVersion") {
            current.driverVersion = value;
        } else if (key == "driverName") {
            current.driverName = value;
        } else if (key == "driverInfo") {
            current.driverInfo = value;
        }
    }
    flush();
    return devices;
}

void parseExtensions(const std::string& output, std::vector<VulkanDevice>& devices,
                     std::string& detail)
{
    const std::vector<std::string> lines = splitLines(output);
    std::size_t index = 0;
    bool inSection = false;
    while (index < lines.size() && trim(lines[index]) != "Device Properties and Extensions:") {
        ++index;
    }
    if (index >= lines.size()) {
        detail = "device extension section not found";
        return;
    }
    inSection = true;
    ++index;

    std::size_t gpuIndex = 0;
    bool inGpu = false;
    bool inExtensions = false;
    std::size_t extensionCount = 0;

    for (; index < lines.size(); ++index) {
        const std::string& line = lines[index];
        if (!inSection) {
            break;
        }
        if (isGpuHeader(line)) {
            inGpu = true;
            inExtensions = false;
            ++gpuIndex;
            if (gpuIndex > 1 && gpuIndex - 1 > devices.size()) {
                // More GPUs than the summary reported; stop at the first unknown block.
                break;
            }
            continue;
        }
        if (!inGpu) {
            continue;
        }

        const std::string trimmed = trim(line);
        if (startsWith(trimmed, "Device Extensions:")) {
            inExtensions = true;
            continue;
        }
        if (inExtensions) {
            if (line.empty()) {
                inExtensions = false;
                continue;
            }
            if (line[0] != '\t') {
                inExtensions = false;
                continue;
            }
            const std::vector<std::string> tokens = splitWhitespace(trimmed);
            if (!tokens.empty() && startsWith(tokens[0], "VK_") && gpuIndex >= 1
                && gpuIndex <= devices.size()) {
                devices[gpuIndex - 1].extensions.insert(tokens[0]);
                ++extensionCount;
            }
        }
    }

    detail = "extensions parsed: " + std::to_string(extensionCount);
}

} // namespace

VulkanReport VulkanDetector::detect(int summaryTimeoutMs, int fullTimeoutMs)
{
    VulkanReport report;
    std::string path;
    if (!findExecutable("vulkaninfo", &path)) {
        report.detail = "vulkaninfo not found";
        return report;
    }
    report.toolFound = true;

    const ProcessResult summary = runProcess({path, "--summary"}, summaryTimeoutMs,
                                              4 * 1024 * 1024);
    if (summary.timedOut) {
        report.timedOut = true;
        report.detail = "vulkaninfo --summary timed out";
        return report;
    }
    report.exitCode = summary.exitCode;
    report.devices = parseSummary(summary.combined());
    if (report.devices.empty()) {
        report.detail = "vulkaninfo produced no device information (exit " +
            std::to_string(summary.exitCode) + ")";
        return report;
    }
    report.available = true;

    std::vector<std::string> descriptions;
    for (const VulkanDevice& device : report.devices) {
        descriptions.push_back(device.name + " (" + hexValue(device.vendorId) + ":"
                               + hexValue(device.deviceId) + ")");
    }
    report.detail = join(descriptions, "; ");

    const ProcessResult full = runProcess({path}, fullTimeoutMs, 16 * 1024 * 1024);
    if (full.timedOut) {
        report.extensionDetail = "vulkaninfo timed out during extension scan";
    } else if (!full.started || full.exitCode != 0) {
        report.extensionDetail = "vulkaninfo extension scan failed with exit code "
            + std::to_string(full.exitCode);
    } else {
        parseExtensions(full.output, report.devices, report.extensionDetail);
        report.extensionsScanned = true;
    }

    return report;
}

std::vector<VulkanVideoEvidence> vulkanVideoEvidence(const VulkanDevice& device)
{
    std::vector<VulkanVideoEvidence> evidence;
    struct Mapping {
        const char* needle;
        const char* codec;
        bool decode;
    };
    static const Mapping mappings[] = {
        {"video_decode_h264", codecs::kH264, true},
        {"video_decode_h265", codecs::kHEVC, true},
        {"video_decode_hevc", codecs::kHEVC, true},
        {"video_decode_vp8", codecs::kVP8, true},
        {"video_decode_vp9", codecs::kVP9, true},
        {"video_decode_av1", codecs::kAV1, true},
        {"video_encode_h264", codecs::kH264, false},
        {"video_encode_h265", codecs::kHEVC, false},
        {"video_encode_hevc", codecs::kHEVC, false},
        {"video_encode_vp8", codecs::kVP8, false},
        {"video_encode_vp9", codecs::kVP9, false},
        {"video_encode_av1", codecs::kAV1, false},
    };

    for (const std::string& extension : device.extensions) {
        for (const Mapping& mapping : mappings) {
            if (extension.find(mapping.needle) != std::string::npos) {
                VulkanVideoEvidence item;
                item.codec = mapping.codec;
                item.decode = mapping.decode;
                item.encode = !mapping.decode;
                item.extension = extension;
                evidence.push_back(item);
            }
        }
    }
    return evidence;
}

} // namespace gpu_report

#pragma once

#include "core/SystemInfo.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gpu_report {

enum class TriState {
    Unknown = 0,
    No = 1,
    Yes = 2
};

enum class GPUVendor {
    Unknown,
    Intel,
    AMD,
    NVIDIA,
    Other
};

std::string triStateToString(TriState value); // YES / NO / UNKNOWN
std::string triStateToXml(TriState value);    // true / false / unknown

struct CapabilityStatus {
    TriState value = TriState::Unknown;
    std::vector<std::string> sources;
};

struct CodecCapability {
    std::string codec;
    CapabilityStatus decode;
    CapabilityStatus encode;
    std::vector<std::string> profiles;
    std::vector<int> bitDepths;
    std::vector<std::string> pixelFormats;
};

struct DiagnosticCheck {
    std::string interfaceName; // stable machine-readable name, e.g. "VA-API"
    std::string interfaceKey;  // localization key, e.g. "diag.interface.vaapi"
    std::string status;        // available, unavailable, error, timeout, skipped, not-applicable
    std::string detail;        // raw technical detail, used verbatim in XML

    // Optional localized rendering of our own prose detail. Raw tool output
    // stays in `detail` and is shown unchanged when detailKey is empty.
    std::string detailKey;
    std::vector<std::string> detailArguments;
};

struct EncoderInfo {
    std::string name;
    std::string api;
    std::string codec;
    bool availableInFfmpeg = true;
    // Formats advertised by `ffmpeg -h encoder=<name>`. These describe the
    // installed FFmpeg build, not formats verified on this specific GPU.
    std::vector<std::string> ffmpegPixelFormats;
};

struct Recommendation {
    std::string resolution;
    int fps = 0;
    std::string codec;
    std::string bitrate;
    std::string encoder;
    std::string command;
};

struct GPUInfo {
    std::string name;
    std::string nameSource;
    std::string vendorName;
    std::string pciDeviceName;
    uint16_t vendorId = 0;
    uint16_t deviceId = 0;
    GPUVendor vendor = GPUVendor::Unknown;
    std::string pciPath;
    std::string driver;
    std::string driverVersion;
    std::string driverVersionSource;
    std::vector<std::string> drmCards;
    std::vector<std::string> renderNodes;
    TriState integrated = TriState::Unknown;
    std::string status = "detected";
    std::string vulkanDeviceName;

    std::vector<DiagnosticCheck> checks;
    std::vector<CodecCapability> codecs;
    std::vector<EncoderInfo> encoders;
    std::vector<EncoderInfo> decoders;
    std::vector<Recommendation> recommendations;

    CodecCapability* findCodec(const std::string& codecName);
    const CodecCapability* findCodec(const std::string& codecName) const;
    std::string existingRenderNode() const;
};

struct Inventory {
    std::vector<GPUInfo> gpus;
    std::vector<DiagnosticCheck> globalChecks;
    HostInfo host;
    std::string localeCode;
    std::string generatedAt;
};

GPUVendor vendorFromId(uint16_t vendorId);
std::string vendorDisplayName(GPUVendor vendor);

} // namespace gpu_report

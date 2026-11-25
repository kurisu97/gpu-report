#include "report/Recommendations.hpp"

#include "core/Sysfs.hpp"
#include "gpu/Codecs.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace gpu_report {

namespace {

struct BasePreset {
    const char* resolution;
    int fps;
    int minimumMbps;
    int maximumMbps;
};

const std::vector<BasePreset>& basePresets()
{
    static const std::vector<BasePreset> presets = {
        {"1280x720", 30, 3, 5},
        {"1920x1080", 30, 5, 8},
        {"1920x1080", 60, 8, 12},
        {"2560x1440", 60, 16, 24},
        {"3840x2160", 30, 25, 45},
        {"3840x2160", 60, 45, 70},
    };
    return presets;
}

double bitrateMultiplier(const std::string& codec)
{
    if (codec == codecs::kHEVC) {
        return 0.65;
    }
    if (codec == codecs::kAV1) {
        return 0.55;
    }
    if (codec == codecs::kVP9) {
        return 0.75;
    }
    if (codec == codecs::kVP8) {
        return 1.20;
    }
    if (codec == codecs::kMPEG2) {
        return 1.60;
    }
    if (codec == codecs::kVC1) {
        return 1.40;
    }
    return 1.0;
}

std::vector<std::string> preferredApis(GPUVendor vendor)
{
    switch (vendor) {
    case GPUVendor::NVIDIA:
        return {"NVENC"};
    case GPUVendor::Intel:
        return {"VA-API", "QSV"};
    case GPUVendor::AMD:
        return {"VA-API", "AMF"};
    case GPUVendor::Other:
        return {"VA-API", "V4L2", "AMF"};
    case GPUVendor::Unknown:
    default:
        return {"VA-API"};
    }
}

const EncoderInfo* chooseEncoder(const GPUInfo& gpu, const std::string& codec)
{
    const std::vector<std::string> apis = preferredApis(gpu.vendor);
    for (const std::string& api : apis) {
        if (api == "VA-API" && gpu.existingRenderNode().empty()) {
            continue;
        }
        for (const EncoderInfo& encoder : gpu.encoders) {
            if (encoder.codec == codec && encoder.api == api && encoder.availableInFfmpeg) {
                return &encoder;
            }
        }
    }
    return nullptr;
}

std::string buildCommand(const GPUInfo& gpu, const EncoderInfo& encoder,
                         const std::string& codec, int bitrateMbps)
{
    const std::string bitrate = std::to_string(bitrateMbps) + "M";
    if (encoder.api == "VA-API") {
        const std::string node = gpu.existingRenderNode();
        if (node.empty()) {
            return {};
        }
        return "ffmpeg -vaapi_device " + node
            + " -i input.mp4 -vf 'format=nv12,hwupload' -c:v " + encoder.name
            + " -b:v " + bitrate + " output.mp4";
    }
    if (encoder.api == "NVENC") {
        return "ffmpeg -hwaccel cuda -hwaccel_output_format cuda -i input.mp4 -c:v "
            + encoder.name + " -preset p4 -b:v " + bitrate + " output.mp4";
    }
    if (encoder.api == "QSV") {
        return "ffmpeg -init_hw_device qsv=hw -filter_hw_device hw -i input.mp4 -c:v "
            + encoder.name + " -b:v " + bitrate + " output.mp4";
    }
    if (encoder.api == "AMF") {
        return "ffmpeg -i input.mp4 -c:v " + encoder.name + " -b:v " + bitrate
            + " output.mp4";
    }
    if (encoder.api == "V4L2") {
        return "ffmpeg -i input.mp4 -c:v " + encoder.name + " -b:v " + bitrate
            + " output.mp4";
    }
    (void)codec;
    return {};
}

} // namespace

void RecommendationGenerator::generate(GPUInfo& gpu)
{
    gpu.recommendations.clear();

    for (const CodecCapability& capability : gpu.codecs) {
        if (capability.encode.value != TriState::Yes) {
            continue;
        }
        const EncoderInfo* encoder = chooseEncoder(gpu, capability.codec);
        if (encoder == nullptr) {
            continue;
        }

        const double multiplier = bitrateMultiplier(capability.codec);
        for (const BasePreset& base : basePresets()) {
            int minimum = std::max(1, static_cast<int>(std::lround(base.minimumMbps * multiplier)));
            int maximum = std::max(minimum + 1,
                                   static_cast<int>(std::lround(base.maximumMbps * multiplier)));
            const int middle = std::max(1, (minimum + maximum) / 2);

            const std::string command = buildCommand(gpu, *encoder, capability.codec, middle);
            if (command.empty()) {
                continue;
            }

            Recommendation recommendation;
            recommendation.resolution = base.resolution;
            recommendation.fps = base.fps;
            recommendation.codec = capability.codec;
            recommendation.bitrate = std::to_string(minimum) + "-"
                + std::to_string(maximum) + "M";
            recommendation.encoder = encoder->name;
            recommendation.command = command;
            gpu.recommendations.push_back(recommendation);
        }
    }
}

} // namespace gpu_report

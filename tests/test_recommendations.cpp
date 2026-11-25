#include "TestHarness.hpp"

#include "gpu/Codecs.hpp"
#include "gpu/GPUInfo.hpp"
#include "report/Recommendations.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace gpu_report;

namespace {

std::filesystem::path temporaryRoot()
{
    std::error_code error;
    if (std::filesystem::is_directory("/tmp/opencode", error)) {
        return "/tmp/opencode";
    }
    return std::filesystem::temp_directory_path();
}

EncoderInfo makeEncoder(const std::string& name, const std::string& api,
                        const std::string& codec, bool available = true)
{
    EncoderInfo encoder;
    encoder.name = name;
    encoder.api = api;
    encoder.codec = codec;
    encoder.availableInFfmpeg = available;
    return encoder;
}

CodecCapability makeCodec(const std::string& codec, TriState encode)
{
    CodecCapability capability;
    capability.codec = codec;
    capability.decode.value = TriState::Yes;
    capability.encode.value = encode;
    return capability;
}

const Recommendation* findPreset(const GPUInfo& gpu, const std::string& codec,
                                 const std::string& resolution, int fps)
{
    for (const Recommendation& recommendation : gpu.recommendations) {
        if (recommendation.codec == codec && recommendation.resolution == resolution
            && recommendation.fps == fps) {
            return &recommendation;
        }
    }
    return nullptr;
}

} // namespace

int main()
{
    const std::filesystem::path root = temporaryRoot();
    const std::string suffix = std::to_string(::getpid());
    const std::filesystem::path renderNode = root / ("gpu_report_render_" + suffix);
    {
        std::ofstream output(renderNode);
        output << "node";
    }

    {
        GPUInfo gpu;
        gpu.vendor = GPUVendor::Intel;
        gpu.renderNodes = {renderNode.string()};
        gpu.encoders = {makeEncoder("h264_vaapi", "VA-API", codecs::kH264)};
        gpu.codecs = {makeCodec(codecs::kH264, TriState::Yes)};
        RecommendationGenerator::generate(gpu);

        CHECK(gpu.recommendations.size() == 6);

        const Recommendation* fullHd30 = findPreset(gpu, codecs::kH264, "1920x1080", 30);
        CHECK(fullHd30 != nullptr);
        if (fullHd30 != nullptr) {
            CHECK(fullHd30->encoder == "h264_vaapi");
            CHECK(fullHd30->bitrate.find('M') != std::string::npos);
            CHECK(fullHd30->command.find("h264_vaapi") != std::string::npos);
            CHECK(fullHd30->command.find(renderNode.string()) != std::string::npos);
            CHECK(fullHd30->command.find("input.mp4") != std::string::npos);
        }

        const Recommendation* uhd60 = findPreset(gpu, codecs::kH264, "3840x2160", 60);
        CHECK(uhd60 != nullptr);

        bool monotonic = true;
        const std::vector<std::string> order = {
            "1280x720:30", "1920x1080:30", "1920x1080:60",
            "2560x1440:60", "3840x2160:30", "3840x2160:60"};
        std::vector<std::string> actual;
        for (const Recommendation& recommendation : gpu.recommendations) {
            actual.push_back(recommendation.resolution + ":"
                             + std::to_string(recommendation.fps));
        }
        monotonic = actual == order;
        CHECK(monotonic);
    }

    {
        GPUInfo gpu;
        gpu.vendor = GPUVendor::NVIDIA;
        gpu.encoders = {makeEncoder("hevc_nvenc", "NVENC", codecs::kHEVC)};
        gpu.codecs = {makeCodec(codecs::kHEVC, TriState::Yes)};
        RecommendationGenerator::generate(gpu);
        CHECK(gpu.recommendations.size() == 6);
        const Recommendation* fullHd30 = findPreset(gpu, codecs::kHEVC, "1920x1080", 30);
        CHECK(fullHd30 != nullptr);
        if (fullHd30 != nullptr) {
            CHECK(fullHd30->encoder == "hevc_nvenc");
            CHECK(fullHd30->command.find("hevc_nvenc") != std::string::npos);
            CHECK(fullHd30->command.find("-preset p4") != std::string::npos);
            CHECK(fullHd30->command.find("nvenc") != std::string::npos);
        }
    }

    {
        GPUInfo gpu;
        gpu.vendor = GPUVendor::AMD;
        gpu.renderNodes = {renderNode.string()};
        gpu.encoders = {makeEncoder("av1_vaapi", "VA-API", codecs::kAV1)};
        gpu.codecs = {makeCodec(codecs::kAV1, TriState::Yes)};
        RecommendationGenerator::generate(gpu);
        CHECK(gpu.recommendations.size() == 6);
        for (const Recommendation& recommendation : gpu.recommendations) {
            CHECK(recommendation.codec == codecs::kAV1);
            CHECK(recommendation.encoder == "av1_vaapi");
        }
    }

    {
        GPUInfo gpu;
        gpu.vendor = GPUVendor::Intel;
        gpu.renderNodes = {renderNode.string()};
        gpu.encoders = {makeEncoder("h264_vaapi", "VA-API", codecs::kH264)};
        gpu.codecs = {
            makeCodec(codecs::kH264, TriState::Yes),
            makeCodec(codecs::kHEVC, TriState::No),
            makeCodec(codecs::kAV1, TriState::Unknown),
            makeCodec(codecs::kVP9, TriState::Unknown),
        };
        RecommendationGenerator::generate(gpu);
        CHECK(gpu.recommendations.size() == 6);
        for (const Recommendation& recommendation : gpu.recommendations) {
            CHECK(recommendation.codec == codecs::kH264);
        }
    }

    {
        GPUInfo gpu;
        gpu.vendor = GPUVendor::Intel;
        gpu.renderNodes = {renderNode.string()};
        gpu.encoders = {
            makeEncoder("h264_vaapi", "VA-API", codecs::kH264, false),
            makeEncoder("h264_qsv", "QSV", codecs::kH264, true),
        };
        gpu.codecs = {makeCodec(codecs::kH264, TriState::Yes)};
        RecommendationGenerator::generate(gpu);
        CHECK(gpu.recommendations.size() == 6);
        for (const Recommendation& recommendation : gpu.recommendations) {
            CHECK(recommendation.encoder == "h264_qsv");
            CHECK(recommendation.command.find("h264_qsv") != std::string::npos);
        }
    }

    {
        GPUInfo gpu;
        gpu.vendor = GPUVendor::Intel;
        gpu.renderNodes = {"/dev/does/not/exist"};
        gpu.encoders = {makeEncoder("h264_vaapi", "VA-API", codecs::kH264)};
        gpu.codecs = {makeCodec(codecs::kH264, TriState::Yes)};
        RecommendationGenerator::generate(gpu);
        CHECK(gpu.recommendations.empty());
    }

    {
        GPUInfo gpu;
        gpu.vendor = GPUVendor::Intel;
        gpu.renderNodes = {renderNode.string()};
        gpu.encoders = {};
        gpu.codecs = {makeCodec(codecs::kH264, TriState::Yes)};
        RecommendationGenerator::generate(gpu);
        CHECK(gpu.recommendations.empty());
    }

    {
        GPUInfo gpu;
        gpu.vendor = GPUVendor::Unknown;
        gpu.encoders = {makeEncoder("v4l2m2m", "V4L2", codecs::kH264)};
        gpu.codecs = {makeCodec(codecs::kH264, TriState::Yes)};
        gpu.recommendations.push_back(Recommendation{});
        RecommendationGenerator::generate(gpu);
        CHECK(gpu.recommendations.empty());
    }

    {
        GPUInfo gpu;
        gpu.vendor = GPUVendor::Other;
        gpu.encoders = {makeEncoder("h264_v4l2m2m", "V4L2", codecs::kH264)};
        gpu.codecs = {makeCodec(codecs::kH264, TriState::Yes)};
        RecommendationGenerator::generate(gpu);
        CHECK(gpu.recommendations.size() == 6);
        CHECK(gpu.recommendations.front().encoder == "h264_v4l2m2m");
    }

    std::error_code error;
    std::filesystem::remove(renderNode, error);

    return testharness::summary("recommendations");
}

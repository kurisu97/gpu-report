#include "TestHarness.hpp"

#include "gpu/Codecs.hpp"
#include "gpu/GPUInfo.hpp"
#include "localization/Localization.hpp"
#include "report/ReportGenerator.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace gpu_report;

namespace {

CodecCapability makeCodec(const std::string& codec, TriState decode, TriState encode)
{
    CodecCapability capability;
    capability.codec = codec;
    capability.decode.value = decode;
    capability.decode.sources = {"VA-API test evidence"};
    capability.encode.value = encode;
    capability.encode.sources = {"VA-API test evidence"};
    return capability;
}

GPUInfo makeGpu()
{
    GPUInfo gpu;
    gpu.name = "Test GPU 9000";
    gpu.nameSource = "pci.ids";
    gpu.vendorName = "Intel Corporation";
    gpu.pciDeviceName = "Test Display Controller";
    gpu.vendorId = 0x8086;
    gpu.deviceId = 0x1234;
    gpu.vendor = GPUVendor::Intel;
    gpu.pciPath = "0000:00:01.0";
    gpu.driver = "i915";
    gpu.driverVersion = "1.2.3";
    gpu.driverVersionSource = "kernel";
    gpu.drmCards = {"/dev/dri/card0"};
    gpu.renderNodes = {"/dev/dri/renderD128"};
    gpu.integrated = TriState::Yes;
    gpu.status = "detected";

    gpu.codecs.push_back(makeCodec(codecs::kH264, TriState::Yes, TriState::Yes));
    gpu.codecs.push_back(makeCodec(codecs::kMPEG2, TriState::Yes, TriState::No));
    for (const std::string& codec : codecs::mandatoryCodecs()) {
        if (codec == codecs::kH264 || codec == codecs::kMPEG2) {
            continue;
        }
        gpu.codecs.push_back(makeCodec(codec, TriState::Unknown, TriState::Unknown));
    }

    DiagnosticCheck check;
    check.interfaceName = "VA-API";
    check.interfaceKey = "diag.interface.vaapi";
    check.status = "available";
    check.detail = "test render node";
    gpu.checks.push_back(check);

    EncoderInfo encoder;
    encoder.name = "h264_vaapi";
    encoder.api = "VA-API";
    encoder.codec = codecs::kH264;
    encoder.availableInFfmpeg = true;
    encoder.ffmpegPixelFormats = {"nv12", "yuv420p"};
    gpu.encoders.push_back(encoder);

    Recommendation recommendation;
    recommendation.resolution = "1920x1080";
    recommendation.fps = 60;
    recommendation.codec = codecs::kH264;
    recommendation.bitrate = "8-12M";
    recommendation.encoder = "h264_vaapi";
    recommendation.command = "ffmpeg -vaapi_device /dev/dri/renderD128 -i input.mp4 "
                             "-vf 'format=nv12,hwupload' -c:v h264_vaapi -b:v 10M output.mp4";
    gpu.recommendations.push_back(recommendation);

    return gpu;
}

bool containsLine(const std::string& text, const std::string& needle)
{
    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t end = text.find('\n', position);
        const std::string line = text.substr(
            position, end == std::string::npos ? std::string::npos : end - position);
        if (line.find(needle) != std::string::npos) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        position = end + 1;
    }
    return false;
}

} // namespace

int main()
{
    Localization english;
    CHECK(english.loadFromDirectory(Language::English, GPU_REPORT_BUILD_LOCALES_DIR));
    CHECK(english.loaded());

    DiagnosticCheck localizedDetail;
    localizedDetail.detail = "PCI display device at 0000:00:01.0";
    localizedDetail.detailKey = "diag.detail.sysfs_device";
    localizedDetail.detailArguments = {"0000:00:01.0"};
    CHECK(ReportGenerator::localizedCheckDetail(localizedDetail, english)
          == "PCI display device at 0000:00:01.0");

    DiagnosticCheck missingDetailKey = localizedDetail;
    missingDetailKey.detailKey = "diag.detail.does_not_exist";
    CHECK(ReportGenerator::localizedCheckDetail(missingDetailKey, english)
          == missingDetailKey.detail);

    const GPUInfo gpu = makeGpu();
    const ReportOptions options{false, 80};

    const std::string list = ReportGenerator::generateGpuList({gpu}, english, options);
    CHECK_MSG(list.find("Test GPU 9000") != std::string::npos, list);
    CHECK_MSG(list.find("0x8086") != std::string::npos, list);
    CHECK_MSG(list.find("0x1234") != std::string::npos, list);
    CHECK_MSG(list.find("0000:00:01.0") != std::string::npos, list);
    CHECK_MSG(list.find("/dev/dri/renderD128") != std::string::npos, list);
    CHECK_MSG(list.find("i915") != std::string::npos, list);

    const std::string report =
        ReportGenerator::generateGpuReport(gpu, 0, 1, english, options);
    CHECK_MSG(containsLine(report, "Test GPU 9000 : i915 : 0000:00:01.0"), report);
    CHECK_MSG(report.find("H.264/AVC") != std::string::npos, report);
    CHECK_MSG(report.find("MPEG-2") != std::string::npos, report);
    CHECK_MSG(report.find("h264_vaapi") != std::string::npos, report);
    CHECK_MSG(report.find("not verified per GPU") != std::string::npos, report);
    CHECK_MSG(report.find("1920x1080") != std::string::npos, report);
    CHECK_MSG(report.find("FFmpeg") != std::string::npos, report);

    // Unknown codecs must stay UNKNOWN in the rendered report.
    for (const std::string& codec :
         std::vector<std::string>{codecs::kHEVC, codecs::kVP9, codecs::kAV1}) {
        bool found = false;
        std::size_t position = 0;
        while (position < report.size()) {
            const std::size_t end = report.find('\n', position);
            const std::string line = report.substr(
                position,
                end == std::string::npos ? std::string::npos : end - position);
            if (line.find(codec) != std::string::npos) {
                found = true;
                CHECK_MSG(line.find("UNKNOWN") != std::string::npos, line);
                CHECK_MSG(line.find(" NO ") == std::string::npos, line);
                break;
            }
            if (end == std::string::npos) {
                break;
            }
            position = end + 1;
        }
        CHECK_MSG(found, codec);
    }

    // Adaptive table width: a narrow report must not produce wide table rows.
    ReportOptions narrow{false, 40};
    const std::string narrowReport =
        ReportGenerator::generateGpuReport(gpu, 0, 1, english, narrow);
    CHECK_MSG(!narrowReport.empty(), "narrow report");

    bool codecRowFits = false;
    std::size_t position = 0;
    while (position < narrowReport.size()) {
        const std::size_t end = narrowReport.find('\n', position);
        const std::string line = narrowReport.substr(
            position, end == std::string::npos ? std::string::npos : end - position);
        if (line.rfind("H.264/AVC", 0) == 0 && line.find("YES") != std::string::npos) {
            codecRowFits = line.size() <= 40;
            break;
        }
        if (end == std::string::npos) {
            break;
        }
        position = end + 1;
    }
    CHECK_MSG(codecRowFits, "narrow codec row should fit the requested width");

    Inventory inventory;
    inventory.gpus.push_back(gpu);
    inventory.globalChecks.push_back(gpu.checks.front());
    inventory.host.hostname = "test-host";
    inventory.host.kernel = "6.0.0-test";
    inventory.host.architecture = "x86_64";
    inventory.host.distribution = "Test Linux";
    inventory.localeCode = "en";
    inventory.generatedAt = "2026-01-02T03:04:05Z";

    const std::string inventoryReport =
        ReportGenerator::generateInventoryReport(inventory, english, options);
    CHECK_MSG(inventoryReport.find("test-host") != std::string::npos, inventoryReport);
    CHECK_MSG(inventoryReport.find("Test GPU 9000") != std::string::npos, inventoryReport);

    const std::vector<std::pair<Language, std::string>> languages = {
        {Language::English, "en"},
        {Language::Russian, "ru"},
        {Language::Japanese, "ja"},
        {Language::Korean, "ko"},
        {Language::Chinese, "zh"},
        {Language::Spanish, "es"},
    };

    for (const auto& entry : languages) {
        Localization localization;
        std::string error;
        const bool loaded = localization.loadFromDirectory(
            entry.first, GPU_REPORT_BUILD_LOCALES_DIR, &error);
        CHECK_MSG(loaded, entry.second + " " + error);
        const std::string localized =
            ReportGenerator::generateGpuReport(gpu, 0, 1, localization, options);
        CHECK_MSG(!localized.empty(), entry.second);
        CHECK_MSG(containsLine(localized, "Test GPU 9000 : i915 : 0000:00:01.0"),
                  entry.second);
        CHECK_MSG(localized.find("H.264/AVC") != std::string::npos, entry.second);
        CHECK_MSG(ReportGenerator::localizedCheckDetail(localizedDetail, localization)
                      .find("0000:00:01.0") != std::string::npos,
                  entry.second);
        if (entry.first != Language::English) {
            CHECK_MSG(ReportGenerator::localizedCheckDetail(localizedDetail, localization)
                          != localizedDetail.detail,
                      entry.second);
        }
    }

    return testharness::summary("report");
}

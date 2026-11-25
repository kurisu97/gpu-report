#include "diagnostics/DiagnosticEngine.hpp"

#include "core/Process.hpp"
#include "core/Strings.hpp"
#include "core/Sysfs.hpp"
#include "diagnostics/Diagnostics.hpp"
#include "diagnostics/NVDECDetector.hpp"
#include "diagnostics/NVENCDetector.hpp"
#include "diagnostics/NvidiaCodecTable.hpp"
#include "diagnostics/VAAPIDetector.hpp"
#include "gpu/Codecs.hpp"
#include "gpu/PCIDatabase.hpp"

#include <algorithm>
#include <cctype>
#include <dlfcn.h>
#include <map>
#include <set>

namespace gpu_report {

namespace {

std::string normalizeBusId(const std::string& value)
{
    std::string normalized = toLowerAscii(trim(value));
    if (normalized.size() > 12) {
        normalized = normalized.substr(normalized.size() - 12);
    }
    return normalized;
}

bool isCanonicalBusId(const std::string& value)
{
    if (value.size() != 12 || value[4] != ':' || value[7] != ':' || value[10] != '.') {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7 || index == 10) {
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(value[index]))) {
            return false;
        }
    }
    return true;
}

std::string cleanVulkanName(std::string name)
{
    name = trim(name);
    while (!name.empty() && name.back() == ')') {
        const std::size_t open = name.rfind(" (");
        if (open == std::string::npos) {
            break;
        }
        name = trim(name.substr(0, open));
    }
    name = replaceAll(name, "(R)", "");
    name = replaceAll(name, "(TM)", "");
    return collapseWhitespace(name);
}

bool containsBracketName(const std::string& value)
{
    return value.find('[') != std::string::npos && value.find(']') != std::string::npos;
}

std::string encoderSuffix(const std::string& api)
{
    if (api == "VA-API") {
        return "_vaapi";
    }
    if (api == "NVENC") {
        return "_nvenc";
    }
    if (api == "QSV") {
        return "_qsv";
    }
    if (api == "AMF") {
        return "_amf";
    }
    if (api == "V4L2") {
        return "_v4l2m2m";
    }
    return {};
}

std::vector<std::string> encoderApisForVendor(GPUVendor vendor)
{
    switch (vendor) {
    case GPUVendor::Intel:
        return {"VA-API", "QSV"};
    case GPUVendor::AMD:
        return {"VA-API", "AMF"};
    case GPUVendor::NVIDIA:
        return {"NVENC"};
    case GPUVendor::Other:
        return {"VA-API", "V4L2", "AMF"};
    case GPUVendor::Unknown:
    default:
        return {"VA-API"};
    }
}

std::vector<std::string> decoderApisForVendor(GPUVendor vendor)
{
    switch (vendor) {
    case GPUVendor::Intel:
        return {"QSV", "VA-API"};
    case GPUVendor::AMD:
        return {"VA-API", "AMF", "V4L2"};
    case GPUVendor::NVIDIA:
        return {"NVDEC"};
    case GPUVendor::Other:
        return {"VA-API", "V4L2", "AMF"};
    case GPUVendor::Unknown:
    default:
        return {"VA-API"};
    }
}

template <typename T>
void appendUnique(std::vector<T>& target, const T& value)
{
    if (std::find(target.begin(), target.end(), value) == target.end()) {
        target.push_back(value);
    }
}

bool isHardwareFrameContext(const std::string& format)
{
    // These are hardware frame/context identifiers rather than pixel formats.
    return format == "vaapi" || format == "qsv" || format == "cuda"
        || format == "cuarray" || format == "d3d11" || format == "d3d11va"
        || format == "dxva2" || format == "vulkan" || format == "opencl"
        || format == "mediacodec" || format == "drm_prime" || format == "xvmc"
        || format == "vdpau";
}

void addEncoder(GPUInfo& gpu, const FfmpegCapabilities& ffmpeg, const std::string& name,
                 const std::string& api, const std::string& codec)
{
    const bool availableInFfmpeg = ffmpeg.hasEncoder(name);
    // VA-API and NVENC are the interfaces that directly confirmed the
    // capability. QSV/AMF/V4L2 entries are only inferred when this FFmpeg
    // build actually advertises the corresponding implementation.
    if (!availableInFfmpeg && api != "VA-API" && api != "NVENC") {
        return;
    }
    for (const EncoderInfo& existing : gpu.encoders) {
        if (existing.name == name && existing.codec == codec) {
            return;
        }
    }
    EncoderInfo info;
    info.name = name;
    info.api = api;
    info.codec = codec;
    info.availableInFfmpeg = availableInFfmpeg;
    const auto formats = ffmpeg.pixelFormats.find(name);
    if (formats != ffmpeg.pixelFormats.end()) {
        for (const std::string& format : formats->second) {
            if (!isHardwareFrameContext(format)) {
                appendUnique(info.ffmpegPixelFormats, format);
            }
        }
    }
    gpu.encoders.push_back(info);
}

void addDecoder(GPUInfo& gpu, const std::string& name, const std::string& api,
                const std::string& codec, bool availableInFfmpeg)
{
    for (EncoderInfo& existing : gpu.decoders) {
        if (existing.name == name && existing.codec == codec) {
            existing.availableInFfmpeg = existing.availableInFfmpeg || availableInFfmpeg;
            return;
        }
    }
    EncoderInfo info;
    info.name = name;
    info.api = api;
    info.codec = codec;
    info.availableInFfmpeg = availableInFfmpeg;
    gpu.decoders.push_back(info);
}

void populateHardwareLists(GPUInfo& gpu, const FfmpegCapabilities& ffmpeg)
{
    const std::vector<std::string> encoderApis = encoderApisForVendor(gpu.vendor);
    const std::vector<std::string> decoderApis = decoderApisForVendor(gpu.vendor);

    for (const CodecCapability& capability : gpu.codecs) {
        const std::string ffmpegCodec = codecs::ffmpegCodecName(capability.codec);
        if (ffmpegCodec.empty()) {
            continue;
        }

        if (capability.encode.value == TriState::Yes) {
            for (const std::string& api : encoderApis) {
                const std::string suffix = encoderSuffix(api);
                if (suffix.empty()) {
                    continue;
                }
                addEncoder(gpu, ffmpeg, ffmpegCodec + suffix, api, capability.codec);
            }
        }

        if (capability.decode.value == TriState::Yes) {
            for (const std::string& api : decoderApis) {
                if (api == "VA-API") {
                    addDecoder(gpu, "vaapi", "VA-API", capability.codec,
                               ffmpeg.hasHwaccel("vaapi"));
                    continue;
                }
                if (api == "NVDEC") {
                    const std::string decoderName = ffmpegCodec + "_cuvid";
                    addDecoder(gpu, decoderName, "NVDEC", capability.codec,
                               ffmpeg.hasDecoder(decoderName));
                    continue;
                }
                const std::string suffix = encoderSuffix(api);
                if (suffix.empty()) {
                    continue;
                }
                const std::string decoderName = ffmpegCodec + suffix;
                if (ffmpeg.hasDecoder(decoderName)) {
                    addDecoder(gpu, decoderName, api, capability.codec, true);
                }
            }
        }
    }

}

std::vector<std::string> relevantFfmpegEncoders(const GPUInfo& gpu,
                                                const FfmpegCapabilities& ffmpeg)
{
    std::vector<std::string> names;
    const std::vector<std::string> apis = encoderApisForVendor(gpu.vendor);
    for (const auto& entry : ffmpeg.encoders) {
        if (std::find(apis.begin(), apis.end(), entry.second.api) != apis.end()) {
            names.push_back(entry.first);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

DiagnosticCheck makeCheck(const std::string& name, const std::string& key,
                          const std::string& status, const std::string& detail,
                          const std::string& detailKey = {},
                          const std::vector<std::string>& detailArguments = {})
{
    DiagnosticCheck check;
    check.interfaceName = name;
    check.interfaceKey = key;
    check.status = status;
    check.detail = detail;
    check.detailKey = detailKey;
    check.detailArguments = detailArguments;
    return check;
}

std::string vendorApiEncoders(const FfmpegCapabilities& ffmpeg, const std::string& api)
{
    std::vector<std::string> names;
    for (const auto& entry : ffmpeg.encoders) {
        if (entry.second.api == api) {
            names.push_back(entry.first);
        }
    }
    std::sort(names.begin(), names.end());
    return join(names, ", ");
}

struct NvidiaSmiInfo {
    std::string name;
    std::string driverVersion;
    std::string busId;
};

struct NvidiaSmiData {
    bool available = false;
    bool timedOut = false;
    int exitCode = -1;
    std::vector<NvidiaSmiInfo> gpus;
    std::map<std::string, std::string> architectures;
    std::map<std::string, TriState> encoderEngines;
    std::map<std::string, TriState> decoderEngines;
    std::string detail;
};

NvidiaSmiData queryNvidiaSmi(int timeoutMs)
{
    NvidiaSmiData data;
    std::string path;
    if (!findExecutable("nvidia-smi", &path)) {
        return data;
    }

    const ProcessResult query = runProcess({path, "--query-gpu=name,driver_version,pci.bus_id",
                                            "--format=csv,noheader"},
                                           timeoutMs, 512 * 1024);
    if (query.timedOut) {
        data.timedOut = true;
        return data;
    }
    if (!query.started || query.exitCode != 0) {
        data.exitCode = query.exitCode;
        return data;
    }

    for (const std::string& line : splitLines(query.output)) {
        const std::vector<std::string> fields = split(line, ',');
        if (fields.size() < 3) {
            continue;
        }
        NvidiaSmiInfo info;
        info.name = trim(fields[0]);
        info.driverVersion = trim(fields[1]);
        info.busId = normalizeBusId(fields[2]);
        if (!info.name.empty()) {
            data.gpus.push_back(info);
        }
    }

    const ProcessResult detailed = runProcess({path, "-q"}, timeoutMs, 4 * 1024 * 1024);
    if (!detailed.timedOut && detailed.started && detailed.exitCode == 0) {
        std::string currentBus;
        for (const std::string& line : splitLines(detailed.output)) {
            const std::string trimmed = trim(line);
            if (startsWith(trimmed, "GPU ") && trimmed.find(':') != std::string::npos) {
                const std::string candidate = normalizeBusId(trim(trimmed.substr(4)));
                if (isCanonicalBusId(candidate)) {
                    currentBus = candidate;
                    continue;
                }
            }
            if (currentBus.empty()) {
                continue;
            }
            if (startsWith(trimmed, "Product Architecture")) {
                const std::size_t colon = trimmed.find(':');
                if (colon != std::string::npos) {
                    data.architectures[currentBus] = trim(trimmed.substr(colon + 1));
                }
                continue;
            }
            const std::size_t colon = trimmed.find(':');
            if (colon != std::string::npos) {
                const std::string key = trim(trimmed.substr(0, colon));
                const std::string value = toLowerAscii(trim(trimmed.substr(colon + 1)));
                const TriState present = value == "n/a" ? TriState::No
                    : (value.empty() ? TriState::Unknown : TriState::Yes);
                if (key == "Encoder") {
                    data.encoderEngines[currentBus] = present;
                } else if (key == "Decoder") {
                    data.decoderEngines[currentBus] = present;
                }
            }
        }
    }

    data.available = !data.gpus.empty();
    data.exitCode = 0;
    if (data.available) {
        data.detail = data.gpus.front().name + ", driver " + data.gpus.front().driverVersion;
    }
    return data;
}

bool nvmlLibraryPresent()
{
    void* handle = dlopen("libnvidia-ml.so.1", RTLD_LAZY | RTLD_LOCAL);
    if (handle == nullptr) {
        handle = dlopen("libnvidia-ml.so", RTLD_LAZY | RTLD_LOCAL);
    }
    if (handle == nullptr) {
        return false;
    }
    dlclose(handle);
    return true;
}

} // namespace

DiagnosticEngine::DiagnosticEngine(const PCIDatabase& database, EngineOptions options)
    : database_(database)
    , options_(options)
{
}

void DiagnosticEngine::diagnose(std::vector<GPUInfo>& gpus)
{
    globalChecks_.clear();

    bool anyNvidia = false;
    bool anyVaapiCandidate = false;
    for (const GPUInfo& gpu : gpus) {
        if (gpu.vendor == GPUVendor::NVIDIA) {
            anyNvidia = true;
        } else {
            anyVaapiCandidate = true;
        }
    }

    const std::string gpuCount = std::to_string(gpus.size());
    globalChecks_.push_back(makeCheck("sysfs", "diag.interface.sysfs", "available",
                                      gpuCount + " display device(s) found",
                                      "diag.detail.sysfs_devices", {gpuCount}));

    if (database_.loaded()) {
        const std::string deviceCount = std::to_string(database_.deviceCount());
        globalChecks_.push_back(makeCheck("PCI ID database", "diag.interface.pci_ids",
                                          "available",
                                          database_.sourcePath() + " (" + deviceCount
                                              + " device entries)",
                                          "diag.detail.pci_db",
                                          {database_.sourcePath(), deviceCount}));
    } else {
        globalChecks_.push_back(makeCheck("PCI ID database", "diag.interface.pci_ids",
                                          "unavailable",
                                          "pci.ids not found; generic device names are used",
                                          "diag.detail.pci_db_missing"));
    }

    ffmpeg_ = FFmpegDetector::detect(options_.ffmpegTimeoutMs);
    if (ffmpeg_.available) {
        const std::string encoderCount = std::to_string(ffmpeg_.encoders.size());
        const std::string decoderCount = std::to_string(ffmpeg_.decoders.size());
        globalChecks_.push_back(makeCheck(
            "FFmpeg", "diag.interface.ffmpeg", "available",
            "FFmpeg " + ffmpeg_.version + ": " + encoderCount + " encoders, "
                + decoderCount + " decoders",
            "diag.detail.ffmpeg_available",
            {ffmpeg_.version, encoderCount, decoderCount}));
    } else if (ffmpeg_.timedOut) {
        globalChecks_.push_back(makeCheck("FFmpeg", "diag.interface.ffmpeg", "timeout",
                                          "ffmpeg did not respond in time",
                                          "diag.detail.ffmpeg_timeout"));
    } else {
        const std::string exitCode = std::to_string(ffmpeg_.exitCode);
        globalChecks_.push_back(makeCheck("FFmpeg", "diag.interface.ffmpeg", "unavailable",
                                          "ffmpeg not found or failed (exit " + exitCode + ")",
                                          "diag.detail.ffmpeg_failed", {exitCode}));
    }

    const VulkanReport vulkan = VulkanDetector::detect(options_.vulkanSummaryTimeoutMs,
                                                       options_.vulkanFullTimeoutMs);
    if (!vulkan.toolFound) {
        globalChecks_.push_back(makeCheck("Vulkan", "diag.interface.vulkan", "unavailable",
                                          "vulkaninfo not found",
                                          "diag.detail.vulkan_unavailable"));
    } else if (vulkan.timedOut) {
        globalChecks_.push_back(makeCheck("Vulkan", "diag.interface.vulkan", "timeout",
                                          "vulkaninfo timed out",
                                          "diag.detail.vulkan_timeout"));
    } else if (!vulkan.available) {
        globalChecks_.push_back(makeCheck("Vulkan", "diag.interface.vulkan", "error",
                                          vulkan.detail));
    } else {
        std::string detail = vulkan.detail;
        if (!vulkan.extensionDetail.empty()) {
            detail += "; " + vulkan.extensionDetail;
        }
        std::string detailKey;
        std::vector<std::string> detailArguments;
        if (vulkan.extensionsScanned) {
            std::size_t extensionCount = 0;
            for (const VulkanDevice& device : vulkan.devices) {
                extensionCount += device.extensions.size();
            }
            detailKey = "diag.detail.vulkan_scan_complete";
            detailArguments = {vulkan.detail, std::to_string(extensionCount)};
        } else if (vulkan.extensionDetail.find("timed out") != std::string::npos) {
            detailKey = "diag.detail.vulkan_scan_timeout";
            detailArguments = {vulkan.detail};
        } else if (!vulkan.extensionDetail.empty()) {
            detailKey = "diag.detail.vulkan_scan_failed";
            detailArguments = {vulkan.detail};
        }
        globalChecks_.push_back(makeCheck("Vulkan", "diag.interface.vulkan", "available",
                                           detail, detailKey, detailArguments));
    }

    std::string vainfoPath;
    if (!anyVaapiCandidate) {
        globalChecks_.push_back(makeCheck("VA-API", "diag.interface.vaapi", "not-applicable",
                                          "no Intel/AMD/other VA-API candidate adapters",
                                          "diag.detail.not_applicable"));
    } else if (!findExecutable("vainfo", &vainfoPath)) {
        globalChecks_.push_back(makeCheck("VA-API", "diag.interface.vaapi", "unavailable",
                                          "vainfo not found",
                                          "diag.detail.tool_missing", {"vainfo"}));
    } else {
        globalChecks_.push_back(makeCheck("VA-API", "diag.interface.vaapi", "available",
                                          vainfoPath));
    }

    const NvidiaSmiData smi = queryNvidiaSmi(options_.nvidiaSmiTimeoutMs);
    if (!anyNvidia) {
        globalChecks_.push_back(makeCheck("nvidia-smi", "diag.interface.nvidia_smi",
                                          "not-applicable", "no NVIDIA adapters",
                                          "diag.detail.not_applicable"));
    } else if (smi.available) {
        globalChecks_.push_back(makeCheck("nvidia-smi", "diag.interface.nvidia_smi",
                                          "available", smi.detail,
                                          "diag.detail.nvidia_smi_device",
                                          {smi.gpus.front().name,
                                           smi.gpus.front().driverVersion}));
    } else if (smi.timedOut) {
        globalChecks_.push_back(makeCheck("nvidia-smi", "diag.interface.nvidia_smi",
                                          "timeout", "nvidia-smi timed out",
                                          "diag.detail.nvidia_smi_timeout"));
    } else {
        const std::string exitCode = std::to_string(smi.exitCode);
        globalChecks_.push_back(makeCheck("nvidia-smi", "diag.interface.nvidia_smi",
                                          "unavailable",
                                          "nvidia-smi not found or failed (exit " + exitCode + ")",
                                          "diag.detail.nvidia_smi_failed", {exitCode}));
    }

    if (!anyNvidia) {
        globalChecks_.push_back(makeCheck("NVML", "diag.interface.nvml", "not-applicable",
                                          "no NVIDIA adapters",
                                          "diag.detail.not_applicable"));
    } else if (nvmlLibraryPresent()) {
        globalChecks_.push_back(makeCheck("NVML", "diag.interface.nvml", "available",
                                          "libnvidia-ml is present",
                                          "diag.detail.nvml_present"));
    } else {
        globalChecks_.push_back(makeCheck("NVML", "diag.interface.nvml", "unavailable",
                                          "libnvidia-ml not found",
                                          "diag.detail.nvml_absent"));
    }

    std::vector<bool> vulkanUsed(vulkan.devices.size(), false);

    for (GPUInfo& gpu : gpus) {
        ensureMandatoryCodecs(gpu.codecs);
        DiagnosticOutput output;

        const VulkanDevice* matchedVulkan = nullptr;
        for (std::size_t i = 0; i < vulkan.devices.size(); ++i) {
            if (vulkanUsed[i]) {
                continue;
            }
            if (vulkan.devices[i].vendorId == gpu.vendorId
                && vulkan.devices[i].deviceId == gpu.deviceId) {
                matchedVulkan = &vulkan.devices[i];
                vulkanUsed[i] = true;
                break;
            }
        }

        if (matchedVulkan != nullptr) {
            gpu.vulkanDeviceName = matchedVulkan->name;
            if (matchedVulkan->integrated) {
                gpu.integrated = TriState::Yes;
            } else if (matchedVulkan->discrete) {
                gpu.integrated = TriState::No;
            }

            const std::string cleanedName = cleanVulkanName(matchedVulkan->name);
            if (!cleanedName.empty()
                && (gpu.nameSource == "generic" || !containsBracketName(gpu.pciDeviceName))) {
                gpu.name = cleanedName;
                gpu.nameSource = "Vulkan";
            }

            std::string detail = matchedVulkan->name;
            if (!matchedVulkan->driverName.empty()) {
                detail += " / " + matchedVulkan->driverName;
            }
            if (!matchedVulkan->driverInfo.empty()) {
                detail += " " + matchedVulkan->driverInfo;
            }
            std::string localizedVulkanDetailKey;
            std::vector<std::string> localizedVulkanDetailArguments;
            if (vulkan.extensionsScanned) {
                bool anyVideo = false;
                for (const VulkanVideoEvidence& item : vulkanVideoEvidence(*matchedVulkan)) {
                    anyVideo = true;
                    CodecEvidence evidence;
                    evidence.codec = item.codec;
                    evidence.decodeSource = "Vulkan " + item.extension;
                    evidence.encodeSource = "Vulkan " + item.extension;
                    if (item.decode) {
                        evidence.decode = TriState::Yes;
                    } else {
                        evidence.encode = TriState::Yes;
                    }
                    output.evidence.push_back(evidence);
                }
                if (!anyVideo) {
                    localizedVulkanDetailKey = "diag.detail.vulkan_device_no_video";
                    localizedVulkanDetailArguments = {detail};
                    detail += "; no Vulkan video extensions advertised";
                }
            }
            output.checks.push_back(makeCheck("Vulkan", "diag.interface.vulkan", "available",
                                               detail, localizedVulkanDetailKey,
                                               localizedVulkanDetailArguments));
        } else if (!vulkan.available) {
            output.checks.push_back(makeCheck(
                "Vulkan", "diag.interface.vulkan", "unavailable",
                vulkan.detail.empty() ? "no Vulkan device information" : vulkan.detail,
                vulkan.detail.empty() ? "diag.detail.vulkan_unavailable" : std::string()));
        } else {
            output.checks.push_back(makeCheck("Vulkan", "diag.interface.vulkan", "unavailable",
                                              "no matching Vulkan device",
                                              "diag.detail.vulkan_no_match"));
        }

        VAAPIDetector::detect(gpu, options_.vainfoTimeoutMs, output);

        NvidiaIdentity nvidiaIdentity;
        if (gpu.vendor == GPUVendor::NVIDIA) {
            nvidiaIdentity.driverPresent = pathExists("/proc/driver/nvidia/version")
                || pathExists("/sys/module/nvidia") || gpu.driver == "nvidia";
            nvidiaIdentity.driverVersion = gpu.driverVersion;
            nvidiaIdentity.busId = normalizeBusId(gpu.pciPath);
            nvidiaIdentity.chip = nvidiaChipFromName(gpu.pciDeviceName);
            nvidiaIdentity.smiAvailable = smi.available;

            for (const NvidiaSmiInfo& info : smi.gpus) {
                if (info.busId == nvidiaIdentity.busId) {
                    nvidiaIdentity.smiName = info.name;
                    nvidiaIdentity.driverVersion = info.driverVersion;
                    break;
                }
            }
            const auto architecture = smi.architectures.find(nvidiaIdentity.busId);
            if (architecture != smi.architectures.end()) {
                nvidiaIdentity.architecture = architecture->second;
            }
            const auto encoderEngine = smi.encoderEngines.find(nvidiaIdentity.busId);
            if (encoderEngine != smi.encoderEngines.end()) {
                nvidiaIdentity.nvencEngine = encoderEngine->second;
            }
            const auto decoderEngine = smi.decoderEngines.find(nvidiaIdentity.busId);
            if (decoderEngine != smi.decoderEngines.end()) {
                nvidiaIdentity.nvdecEngine = decoderEngine->second;
            }

            if (!nvidiaIdentity.smiName.empty()) {
                output.checks.push_back(makeCheck(
                    "nvidia-smi", "diag.interface.nvidia_smi", "available",
                    nvidiaIdentity.smiName + ", driver " + nvidiaIdentity.driverVersion,
                    "diag.detail.nvidia_smi_device",
                    {nvidiaIdentity.smiName, nvidiaIdentity.driverVersion}));
            } else {
                output.checks.push_back(makeCheck(
                    "nvidia-smi", "diag.interface.nvidia_smi", "unavailable",
                    "nvidia-smi not available or no matching GPU",
                    "diag.detail.nvidia_smi_no_match"));
            }

            NVENCDetector::detect(gpu, nvidiaIdentity, output);
            NVDECDetector::detect(gpu, nvidiaIdentity, output);
        }

        if (gpu.vendor == GPUVendor::Intel) {
            const std::string qsv = vendorApiEncoders(ffmpeg_, "QSV");
            output.checks.push_back(makeCheck(
                "Intel Quick Sync (QSV)", "diag.interface.qsv",
                qsv.empty() ? "unavailable" : "available",
                qsv.empty() ? "no FFmpeg QSV encoders" : qsv,
                qsv.empty() ? "diag.detail.qsv_unavailable" : std::string()));
        } else if (gpu.vendor == GPUVendor::AMD) {
            const std::string amf = vendorApiEncoders(ffmpeg_, "AMF");
            output.checks.push_back(makeCheck(
                "AMD AMF", "diag.interface.amf",
                amf.empty() ? "unavailable" : "available",
                amf.empty() ? "no FFmpeg AMF encoders" : amf,
                amf.empty() ? "diag.detail.amf_unavailable" : std::string()));
        } else if (gpu.vendor == GPUVendor::Other) {
            const std::string v4l2 = vendorApiEncoders(ffmpeg_, "V4L2");
            output.checks.push_back(makeCheck(
                "V4L2 mem2mem", "diag.interface.v4l2",
                v4l2.empty() ? "unavailable" : "available",
                v4l2.empty() ? "no FFmpeg V4L2 encoders" : v4l2,
                v4l2.empty() ? "diag.detail.v4l2_unavailable" : std::string()));
        }

        const std::vector<std::string> ffmpegNames = relevantFfmpegEncoders(gpu, ffmpeg_);
        if (ffmpeg_.available) {
            output.checks.push_back(makeCheck(
                "FFmpeg", "diag.interface.ffmpeg", "available",
                ffmpegNames.empty()
                    ? "no matching FFmpeg hardware encoders in this build"
                    : join(ffmpegNames, ", "),
                ffmpegNames.empty() ? "diag.detail.encoder_ffmpeg_none"
                                    : std::string()));
        } else {
            output.checks.push_back(makeCheck("FFmpeg", "diag.interface.ffmpeg",
                                              "unavailable", "ffmpeg not available",
                                              "diag.detail.ffmpeg_unavailable"));
        }

        mergeEvidence(gpu.codecs, output.evidence);
        for (const DiagnosticCheck& check : output.checks) {
            gpu.checks.push_back(check);
        }

        populateHardwareLists(gpu, ffmpeg_);
    }
}

} // namespace gpu_report

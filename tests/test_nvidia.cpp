#include "TestHarness.hpp"

#include "diagnostics/Diagnostics.hpp"
#include "diagnostics/NVDECDetector.hpp"
#include "diagnostics/NVENCDetector.hpp"
#include "diagnostics/NvidiaCodecTable.hpp"
#include "gpu/Codecs.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace gpu_report;

namespace {

const DiagnosticCheck* findCheck(const DiagnosticOutput& output, const std::string& name)
{
    for (const DiagnosticCheck& check : output.checks) {
        if (check.interfaceName == name) {
            return &check;
        }
    }
    return nullptr;
}

std::vector<CodecCapability> mergedCapabilities(const DiagnosticOutput& output)
{
    std::vector<CodecCapability> capabilities;
    ensureMandatoryCodecs(capabilities);
    mergeEvidence(capabilities, output.evidence);
    return capabilities;
}

const CodecCapability* findCodec(const std::vector<CodecCapability>& capabilities,
                                 const std::string& codec)
{
    for (const CodecCapability& capability : capabilities) {
        if (capability.codec == codec) {
            return &capability;
        }
    }
    return nullptr;
}

} // namespace

int main()
{
    CHECK(nvidiaArchitectureFromString("Maxwell", "GM108") == NvidiaArch::Maxwell1);
    CHECK(nvidiaArchitectureFromString("", "GM206") == NvidiaArch::Maxwell2);
    CHECK(nvidiaArchitectureFromString("Ada Lovelace", "") == NvidiaArch::Ada);
    CHECK(nvidiaArchitectureFromString("unknown", "") == NvidiaArch::Unknown);
    CHECK(nvidiaNvencEngineForPciDevice(0x1341) == TriState::No);
    CHECK(nvidiaNvdecEngineForPciDevice(0x1341) == TriState::No);
    CHECK(nvidiaNvencEngineForPciDevice(0xffff) == TriState::Unknown);

    const NvidiaCapabilities maxwell2 = nvidiaCapabilities("GM206", "Maxwell");
    const NvidiaCodecSupport* maxwellHevc = maxwell2.find(codecs::kHEVC);
    CHECK(maxwellHevc != nullptr);
    if (maxwellHevc != nullptr) {
        CHECK(maxwellHevc->decode == TriState::Yes);
        CHECK(maxwellHevc->encode == TriState::Yes);
        CHECK((maxwellHevc->profiles == std::vector<std::string>{"Main"}));
        CHECK((maxwellHevc->bitDepths == std::vector<int>{8}));
    }

    const NvidiaCapabilities pascal = nvidiaCapabilities("GP106", "Pascal");
    CHECK(pascal.find(codecs::kVP8)->decode == TriState::Unknown);
    CHECK(pascal.find(codecs::kVP9)->decode == TriState::Unknown);

    const NvidiaCapabilities ada = nvidiaCapabilities("AD104", "Ada Lovelace");
    const NvidiaCodecSupport* adaAv1 = ada.find(codecs::kAV1);
    CHECK(adaAv1 != nullptr);
    if (adaAv1 != nullptr) {
        CHECK(adaAv1->decode == TriState::Yes);
        CHECK(adaAv1->encode == TriState::Yes);
        CHECK((adaAv1->profiles == std::vector<std::string>{"Main"}));
    }

    GPUInfo gpu;
    gpu.vendor = GPUVendor::NVIDIA;

    NvidiaIdentity noEngines;
    noEngines.driverPresent = true;
    noEngines.smiAvailable = true;
    noEngines.smiName = "NVIDIA GeForce 840M";
    noEngines.architecture = "Maxwell";
    noEngines.chip = "GM108";
    noEngines.nvencEngine = TriState::No;
    noEngines.nvdecEngine = TriState::No;

    DiagnosticOutput unavailable;
    NVENCDetector::detect(gpu, noEngines, unavailable);
    NVDECDetector::detect(gpu, noEngines, unavailable);

    const DiagnosticCheck* nvencCheck = findCheck(unavailable, "NVENC");
    const DiagnosticCheck* nvdecCheck = findCheck(unavailable, "NVDEC");
    CHECK(nvencCheck != nullptr);
    CHECK(nvdecCheck != nullptr);
    if (nvencCheck != nullptr) {
        CHECK(nvencCheck->status == "unavailable");
        CHECK(nvencCheck->detailKey == "diag.detail.nvenc_engine_unavailable");
    }
    if (nvdecCheck != nullptr) {
        CHECK(nvdecCheck->status == "unavailable");
        CHECK(nvdecCheck->detailKey == "diag.detail.nvdec_engine_unavailable");
    }

    const std::vector<CodecCapability> unavailableCodecs = mergedCapabilities(unavailable);
    for (const std::string& codec : codecs::mandatoryCodecs()) {
        const CodecCapability* capability = findCodec(unavailableCodecs, codec);
        CHECK_MSG(capability != nullptr, codec);
        if (capability == nullptr) {
            continue;
        }
        if (codec == codecs::kMJPEG) {
            CHECK(capability->decode.value == TriState::Unknown);
            CHECK(capability->encode.value == TriState::Unknown);
        } else {
            CHECK_MSG(capability->decode.value == TriState::No, codec);
            CHECK_MSG(capability->encode.value == TriState::No, codec);
        }
    }

    const CodecCapability* unavailableH264 = findCodec(unavailableCodecs, codecs::kH264);
    CHECK(unavailableH264 != nullptr);
    if (unavailableH264 != nullptr) {
        CHECK(!unavailableH264->decode.sources.empty());
        CHECK(!unavailableH264->encode.sources.empty());
        CHECK(unavailableH264->decode.sources.front().find("Decoder: N/A")
              != std::string::npos);
        CHECK(unavailableH264->encode.sources.front().find("Encoder: N/A")
              != std::string::npos);
        CHECK(unavailableH264->profiles.empty());
        CHECK(unavailableH264->bitDepths.empty());
    }

    NvidiaIdentity tableOnly = noEngines;
    tableOnly.nvencEngine = TriState::Unknown;
    tableOnly.nvdecEngine = TriState::Unknown;
    DiagnosticOutput inferred;
    NVENCDetector::detect(gpu, tableOnly, inferred);
    NVDECDetector::detect(gpu, tableOnly, inferred);
    const std::vector<CodecCapability> inferredCodecs = mergedCapabilities(inferred);
    const CodecCapability* inferredH264 = findCodec(inferredCodecs, codecs::kH264);
    CHECK(inferredH264 != nullptr);
    if (inferredH264 != nullptr) {
        CHECK(inferredH264->decode.value == TriState::Yes);
        CHECK(inferredH264->encode.value == TriState::Yes);
    }

    GPUInfo exact840m = gpu;
    exact840m.vendorId = 0x10de;
    exact840m.deviceId = 0x1341;
    DiagnosticOutput matrixOverride;
    NVENCDetector::detect(exact840m, tableOnly, matrixOverride);
    NVDECDetector::detect(exact840m, tableOnly, matrixOverride);
    const std::vector<CodecCapability> matrixCodecs = mergedCapabilities(matrixOverride);
    const CodecCapability* matrixH264 = findCodec(matrixCodecs, codecs::kH264);
    CHECK(matrixH264 != nullptr);
    if (matrixH264 != nullptr) {
        CHECK(matrixH264->decode.value == TriState::No);
        CHECK(matrixH264->encode.value == TriState::No);
        CHECK(!matrixH264->decode.sources.empty());
        CHECK(!matrixH264->encode.sources.empty());
        if (!matrixH264->decode.sources.empty()) {
            CHECK(matrixH264->decode.sources.front().find("support matrix")
                  != std::string::npos);
        }
        if (!matrixH264->encode.sources.empty()) {
            CHECK(matrixH264->encode.sources.front().find("support matrix")
                  != std::string::npos);
        }
    }
    const DiagnosticCheck* matrixNvenc = findCheck(matrixOverride, "NVENC");
    const DiagnosticCheck* matrixNvdec = findCheck(matrixOverride, "NVDEC");
    CHECK(matrixNvenc != nullptr);
    CHECK(matrixNvdec != nullptr);
    if (matrixNvenc != nullptr) {
        CHECK(matrixNvenc->detailKey == "diag.detail.nvenc_matrix_unavailable");
    }
    if (matrixNvdec != nullptr) {
        CHECK(matrixNvdec->detailKey == "diag.detail.nvdec_matrix_unavailable");
    }

    return testharness::summary("nvidia");
}

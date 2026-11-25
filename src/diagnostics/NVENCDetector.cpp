#include "diagnostics/NVENCDetector.hpp"

#include "core/Strings.hpp"
#include "diagnostics/NvidiaCodecTable.hpp"
#include "gpu/Codecs.hpp"

namespace gpu_report {

namespace {

struct DirectionStats {
    bool anyYes = false;
    bool anyNo = false;
    bool anyUnknown = false;
};

std::string tableSource(const NvidiaCapabilities& capabilities, const char* api)
{
    std::string source = std::string("NVIDIA ") + api + " capability table ("
        + capabilities.architectureName;
    if (!capabilities.chip.empty()) {
        source += ", " + capabilities.chip;
    }
    source += ")";
    return source;
}

} // namespace

void NVENCDetector::detect(const GPUInfo& gpu, const NvidiaIdentity& identity,
                           DiagnosticOutput& output)
{
    DiagnosticCheck check;
    check.interfaceName = "NVENC";
    check.interfaceKey = "diag.interface.nvenc";

    if (!identity.driverPresent) {
        check.status = "unavailable";
        check.detail = "NVIDIA kernel driver not loaded";
        check.detailKey = "diag.detail.nvidia_driver_missing";
        output.checks.push_back(check);
        return;
    }

    const NvidiaCapabilities capabilities = nvidiaCapabilities(identity.chip,
                                                               identity.architecture);
    const bool unavailableFromSmi = identity.nvencEngine == TriState::No;
    const bool unavailableFromPci = identity.nvencEngine == TriState::Unknown
        && nvidiaNvencEngineForPciDevice(gpu.deviceId) == TriState::No;
    const bool engineUnavailable = unavailableFromSmi || unavailableFromPci;
    if (!capabilities.known() && !engineUnavailable) {
        check.status = "skipped";
        check.detail = "GPU generation not recognised; NVENC support cannot be inferred";
        check.detailKey = "diag.detail.nvenc_unknown_generation";
        output.checks.push_back(check);
        return;
    }

    DirectionStats stats;
    const std::string source = engineUnavailable
        ? (unavailableFromSmi
               ? "nvidia-smi reports Encoder: N/A (NVENC engine unavailable); "
               : "NVIDIA support matrix reports no NVENC engine for PCI device 0x10de:"
                   + hexValue(gpu.deviceId).substr(2) + "; ")
            + tableSource(capabilities, "NVENC")
        : tableSource(capabilities, "NVENC");
    for (const std::string& codec : codecs::mandatoryCodecs()) {
        const NvidiaCodecSupport* support = capabilities.find(codec);
        if (support == nullptr) {
            stats.anyUnknown = true;
            continue;
        }
        TriState encode = support->encode;
        if (engineUnavailable
            && (codec == codecs::kH264 || codec == codecs::kHEVC
                || codec == codecs::kAV1)) {
            encode = TriState::No;
        }
        if (encode == TriState::Unknown) {
            stats.anyUnknown = true;
            continue;
        }
        CodecEvidence evidence;
        evidence.codec = codec;
        evidence.encode = encode;
        evidence.encodeSource = source;
        if (encode == TriState::Yes) {
            evidence.profiles = support->profiles;
            evidence.bitDepths = support->bitDepths;
        }
        output.evidence.push_back(evidence);

        if (encode == TriState::Yes) {
            stats.anyYes = true;
        } else {
            stats.anyNo = true;
        }
    }

    if (engineUnavailable) {
        check.status = "unavailable";
        if (unavailableFromSmi) {
            check.detail = "nvidia-smi reports Encoder: N/A; NVENC engine unavailable";
            check.detailKey = "diag.detail.nvenc_engine_unavailable";
        } else {
            check.detail = "NVIDIA support matrix reports no NVENC engine for this PCI device";
            check.detailKey = "diag.detail.nvenc_matrix_unavailable";
        }
        output.checks.push_back(check);
        return;
    }
    if (stats.anyYes) {
        check.status = "available";
    } else if (stats.anyUnknown) {
        check.status = "skipped";
    } else {
        check.status = "unavailable";
    }
    check.detail = "NVENC capability table: " + capabilities.architectureName;
    if (!capabilities.chip.empty()) {
        check.detail += " (" + capabilities.chip + ")";
        check.detailKey = "diag.detail.nvenc_table_chip";
        check.detailArguments = {capabilities.architectureName, capabilities.chip};
    } else {
        check.detailKey = "diag.detail.nvenc_table";
        check.detailArguments = {capabilities.architectureName};
    }
    check.detail += "; no active encode test was performed";
    output.checks.push_back(check);
}

} // namespace gpu_report

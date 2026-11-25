#include "diagnostics/NVDECDetector.hpp"

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

std::string tableSource(const NvidiaCapabilities& capabilities)
{
    std::string source = "NVIDIA NVDEC capability table (" + capabilities.architectureName;
    if (!capabilities.chip.empty()) {
        source += ", " + capabilities.chip;
    }
    source += ")";
    return source;
}

} // namespace

void NVDECDetector::detect(const GPUInfo& gpu, const NvidiaIdentity& identity,
                           DiagnosticOutput& output)
{
    DiagnosticCheck check;
    check.interfaceName = "NVDEC";
    check.interfaceKey = "diag.interface.nvdec";

    if (!identity.driverPresent) {
        check.status = "unavailable";
        check.detail = "NVIDIA kernel driver not loaded";
        check.detailKey = "diag.detail.nvidia_driver_missing";
        output.checks.push_back(check);
        return;
    }

    const NvidiaCapabilities capabilities = nvidiaCapabilities(identity.chip,
                                                               identity.architecture);
    const bool unavailableFromSmi = identity.nvdecEngine == TriState::No;
    const bool unavailableFromPci = identity.nvdecEngine == TriState::Unknown
        && nvidiaNvdecEngineForPciDevice(gpu.deviceId) == TriState::No;
    const bool engineUnavailable = unavailableFromSmi || unavailableFromPci;
    if (!capabilities.known() && !engineUnavailable) {
        check.status = "skipped";
        check.detail = "GPU generation not recognised; NVDEC support cannot be inferred";
        check.detailKey = "diag.detail.nvdec_unknown_generation";
        output.checks.push_back(check);
        return;
    }

    DirectionStats stats;
    const std::string source = engineUnavailable
        ? (unavailableFromSmi
               ? "nvidia-smi reports Decoder: N/A (NVDEC engine unavailable); "
               : "NVIDIA support matrix reports no NVDEC engine for PCI device 0x10de:"
                   + hexValue(gpu.deviceId).substr(2) + "; ")
            + tableSource(capabilities)
        : tableSource(capabilities);
    for (const std::string& codec : codecs::mandatoryCodecs()) {
        const NvidiaCodecSupport* support = capabilities.find(codec);
        if (support == nullptr) {
            stats.anyUnknown = true;
            continue;
        }
        TriState decode = support->decode;
        if (engineUnavailable && codec != codecs::kMJPEG) {
            decode = TriState::No;
        }
        if (decode == TriState::Unknown) {
            stats.anyUnknown = true;
            continue;
        }
        CodecEvidence evidence;
        evidence.codec = codec;
        evidence.decode = decode;
        evidence.decodeSource = source;
        if (decode == TriState::Yes) {
            evidence.profiles = support->profiles;
            evidence.bitDepths = support->bitDepths;
        }
        output.evidence.push_back(evidence);

        if (decode == TriState::Yes) {
            stats.anyYes = true;
        } else {
            stats.anyNo = true;
        }
    }

    if (engineUnavailable) {
        check.status = "unavailable";
        if (unavailableFromSmi) {
            check.detail = "nvidia-smi reports Decoder: N/A; NVDEC engine unavailable";
            check.detailKey = "diag.detail.nvdec_engine_unavailable";
        } else {
            check.detail = "NVIDIA support matrix reports no NVDEC engine for this PCI device";
            check.detailKey = "diag.detail.nvdec_matrix_unavailable";
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
    check.detail = "NVDEC capability table: " + capabilities.architectureName;
    if (!capabilities.chip.empty()) {
        check.detail += " (" + capabilities.chip + ")";
        check.detailKey = "diag.detail.nvdec_table_chip";
        check.detailArguments = {capabilities.architectureName, capabilities.chip};
    } else {
        check.detailKey = "diag.detail.nvdec_table";
        check.detailArguments = {capabilities.architectureName};
    }
    check.detail += "; no active decode test was performed";
    output.checks.push_back(check);
}

} // namespace gpu_report

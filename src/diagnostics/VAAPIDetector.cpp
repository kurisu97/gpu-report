#include "diagnostics/VAAPIDetector.hpp"

#include "core/Process.hpp"
#include "core/Strings.hpp"
#include "core/Sysfs.hpp"
#include "gpu/Codecs.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

namespace gpu_report {

namespace {

struct VaProfileInfo {
    std::string codec;
    std::string profile;
    std::vector<int> bitDepths;
};

struct CodecAccumulator {
    bool decode = false;
    bool encode = false;
    std::set<std::string> profiles;
    std::set<int> bitDepths;
};

std::string humanizeProfile(const std::string& value)
{
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (i > 0 && std::isupper(static_cast<unsigned char>(c))
            && (std::islower(static_cast<unsigned char>(value[i - 1]))
                || std::isdigit(static_cast<unsigned char>(value[i - 1])))) {
            result.push_back(' ');
        }
        result.push_back(c);
    }
    return result;
}

std::optional<VaProfileInfo> mapVaProfile(const std::string& profile)
{
    static const std::map<std::string, VaProfileInfo> exact = {
        {"VAProfileMPEG2Simple", {codecs::kMPEG2, "Simple", {8}}},
        {"VAProfileMPEG2Main", {codecs::kMPEG2, "Main", {8}}},
        {"VAProfileH264ConstrainedBaseline", {codecs::kH264, "Constrained Baseline", {8}}},
        {"VAProfileH264Main", {codecs::kH264, "Main", {8}}},
        {"VAProfileH264High", {codecs::kH264, "High", {8}}},
        {"VAProfileH264MultiviewHigh", {codecs::kH264, "Multiview High", {8}}},
        {"VAProfileH264StereoHigh", {codecs::kH264, "Stereo High", {8}}},
        {"VAProfileVC1Simple", {codecs::kVC1, "Simple", {8}}},
        {"VAProfileVC1Main", {codecs::kVC1, "Main", {8}}},
        {"VAProfileVC1Advanced", {codecs::kVC1, "Advanced", {8}}},
        {"VAProfileVP8Version0_3", {codecs::kVP8, "Version 0.3", {8}}},
        {"VAProfileVP9Profile0", {codecs::kVP9, "Profile 0", {8}}},
        {"VAProfileVP9Profile1", {codecs::kVP9, "Profile 1", {8}}},
        {"VAProfileVP9Profile2", {codecs::kVP9, "Profile 2", {10}}},
        {"VAProfileVP9Profile3", {codecs::kVP9, "Profile 3", {10}}},
        {"VAProfileHEVCMain", {codecs::kHEVC, "Main", {8}}},
        {"VAProfileHEVCMain10", {codecs::kHEVC, "Main10", {10}}},
        {"VAProfileHEVCMainStillPicture", {codecs::kHEVC, "Main Still Picture", {8}}},
        {"VAProfileHEVCMain12", {codecs::kHEVC, "Main12", {12}}},
        {"VAProfileHEVCMain422_10", {codecs::kHEVC, "Main 4:2:2 10", {10}}},
        {"VAProfileHEVCMain444", {codecs::kHEVC, "Main 4:4:4", {8}}},
        {"VAProfileHEVCMain444_10", {codecs::kHEVC, "Main 4:4:4 10", {10}}},
        {"VAProfileAV1Profile0", {codecs::kAV1, "Main", {8, 10}}},
        {"VAProfileAV1Profile1", {codecs::kAV1, "High", {8, 10}}},
        {"VAProfileJPEGBaseline", {codecs::kMJPEG, "Baseline", {8}}},
    };

    const auto iterator = exact.find(profile);
    if (iterator != exact.end()) {
        return iterator->second;
    }

    // Future VA-API profiles that share a codec prefix are still useful.
    const std::pair<const char*, const char*> prefixes[] = {
        {"VAProfileH264", codecs::kH264},
        {"VAProfileHEVC", codecs::kHEVC},
        {"VAProfileVP9", codecs::kVP9},
        {"VAProfileVP8", codecs::kVP8},
        {"VAProfileAV1", codecs::kAV1},
        {"VAProfileMPEG2", codecs::kMPEG2},
        {"VAProfileVC1", codecs::kVC1},
        {"VAProfileJPEG", codecs::kMJPEG},
    };
    for (const auto& prefix : prefixes) {
        if (startsWith(profile, prefix.first)) {
            VaProfileInfo info;
            info.codec = prefix.second;
            info.profile = humanizeProfile(profile.substr(std::string(prefix.first).size()));
            return info;
        }
    }
    return std::nullopt;
}

std::string driverVersionFromOutput(const std::string& output)
{
    for (const std::string& line : splitLines(output)) {
        if (line.find("Driver version:") != std::string::npos) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                return trim(line.substr(colon + 1));
            }
        }
    }
    return {};
}

void parseVainfoOutput(const std::string& output,
                       std::map<std::string, CodecAccumulator>& accumulators,
                       bool& sawProfileSection)
{
    sawProfileSection = false;
    for (const std::string& line : splitLines(output)) {
        if (line.find("Supported profile and entrypoints") != std::string::npos) {
            sawProfileSection = true;
            continue;
        }
        if (!sawProfileSection) {
            continue;
        }

        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string profile = trim(line.substr(0, colon));
        if (!startsWith(profile, "VAProfile")) {
            continue;
        }
        const std::optional<VaProfileInfo> info = mapVaProfile(profile);
        if (!info.has_value()) {
            continue;
        }

        CodecAccumulator& accumulator = accumulators[info->codec];
        accumulator.profiles.insert(info->profile);
        for (int depth : info->bitDepths) {
            accumulator.bitDepths.insert(depth);
        }

        const std::string entrypoints = replaceAll(line.substr(colon + 1), ",", " ");
        for (const std::string& entrypoint : splitWhitespace(entrypoints)) {
            if (entrypoint.find("VAEntrypointVLD") != std::string::npos) {
                accumulator.decode = true;
            } else if (entrypoint.find("VAEntrypointEnc") != std::string::npos) {
                accumulator.encode = true;
            }
        }
    }
}

} // namespace

void VAAPIDetector::detect(const GPUInfo& gpu, int timeoutMs, DiagnosticOutput& output)
{
    DiagnosticCheck check;
    check.interfaceName = "VA-API";
    check.interfaceKey = "diag.interface.vaapi";

    if (gpu.vendor == GPUVendor::NVIDIA) {
        check.status = "not-applicable";
        check.detail = "VA-API is not used for NVIDIA adapters";
        check.detailKey = "diag.detail.vaapi_not_applicable";
        output.checks.push_back(check);
        return;
    }

    if (gpu.renderNodes.empty()) {
        check.status = "unavailable";
        check.detail = "no DRM render node available";
        check.detailKey = "diag.detail.vaapi_no_node";
        output.checks.push_back(check);
        return;
    }

    std::string vainfoPath;
    if (!findExecutable("vainfo", &vainfoPath)) {
        check.status = "unavailable";
        check.detail = "vainfo not found";
        check.detailKey = "diag.detail.tool_missing";
        check.detailArguments = {"vainfo"};
        output.checks.push_back(check);
        return;
    }

    std::map<std::string, CodecAccumulator> accumulators;
    std::vector<std::string> successfulNodes;
    std::vector<std::string> details;
    std::string driverVersion;
    bool anySuccess = false;
    bool anyTimeout = false;

    for (const std::string& node : gpu.renderNodes) {
        if (!pathExists(node)) {
            details.push_back(node + ": device node missing");
            continue;
        }
        const ProcessResult result = runProcess({vainfoPath, "--display", "drm",
                                                 "--device", node},
                                                timeoutMs, 2 * 1024 * 1024);
        if (result.timedOut) {
            anyTimeout = true;
            details.push_back(node + ": timeout");
            continue;
        }
        bool sawProfiles = false;
        parseVainfoOutput(result.combined(), accumulators, sawProfiles);
        if (!sawProfiles) {
            details.push_back(node + ": no VA-API profile section (exit "
                              + std::to_string(result.exitCode) + ")");
            continue;
        }

        anySuccess = true;
        successfulNodes.push_back(node);
        const std::string version = driverVersionFromOutput(result.combined());
        if (driverVersion.empty()) {
            driverVersion = version;
        }
        details.push_back(node + (version.empty() ? ": initialized" : ": " + version));
    }

    if (!anySuccess) {
        check.status = anyTimeout ? "timeout" : "error";
        check.detail = join(details, "; ");
        if (check.detail.empty()) {
            check.detail = "vainfo did not initialise any render node";
            check.detailKey = "diag.detail.vaapi_failed";
        }
        output.checks.push_back(check);
        return;
    }

    std::string source = "VA-API (vainfo";
    if (!successfulNodes.empty()) {
        source += ", " + join(successfulNodes, ", ");
    }
    if (!driverVersion.empty()) {
        source += ", " + driverVersion;
    }
    source += ")";

    for (const std::string& codec : codecs::mandatoryCodecs()) {
        CodecEvidence evidence;
        evidence.codec = codec;

        const auto iterator = accumulators.find(codec);
        if (iterator == accumulators.end()) {
            evidence.decode = TriState::No;
            evidence.encode = TriState::No;
        } else {
            evidence.decode = iterator->second.decode ? TriState::Yes : TriState::No;
            evidence.encode = iterator->second.encode ? TriState::Yes : TriState::No;
            evidence.profiles.assign(iterator->second.profiles.begin(),
                                     iterator->second.profiles.end());
            evidence.bitDepths.assign(iterator->second.bitDepths.begin(),
                                      iterator->second.bitDepths.end());
        }
        evidence.decodeSource = source;
        evidence.encodeSource = source;
        output.evidence.push_back(evidence);
    }

    check.status = "available";
    check.detail = join(details, "; ");
    output.checks.push_back(check);
}

} // namespace gpu_report

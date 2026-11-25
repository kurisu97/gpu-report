#include "diagnostics/NvidiaCodecTable.hpp"

#include "core/Strings.hpp"
#include "gpu/Codecs.hpp"

#include <algorithm>
#include <cctype>

namespace gpu_report {

namespace {

void setCodec(NvidiaCapabilities& capabilities, const std::string& codec,
              TriState decode, TriState encode,
              const std::vector<std::string>& profiles = {},
              const std::vector<int>& bitDepths = {})
{
    NvidiaCodecSupport support;
    support.decode = decode;
    support.encode = encode;
    support.profiles = profiles;
    support.bitDepths = bitDepths;
    capabilities.codecs[codec] = support;
}

void fillUnknown(NvidiaCapabilities& capabilities)
{
    for (const std::string& codec : codecs::mandatoryCodecs()) {
        capabilities.codecs[codec] = NvidiaCodecSupport();
    }
}

bool startsWithAny(const std::string& value, const std::vector<std::string>& prefixes)
{
    for (const std::string& prefix : prefixes) {
        if (startsWith(value, prefix)) {
            return true;
        }
    }
    return false;
}

std::string normalizedChip(const std::string& chip)
{
    return toUpperAscii(trim(chip));
}

} // namespace

std::string nvidiaChipFromName(const std::string& pciDeviceName)
{
    std::string token;
    for (char c : pciDeviceName) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
            token.push_back(c);
        } else {
            break;
        }
    }
    if (token.empty()) {
        return {};
    }
    if (token.size() > 1 && token.back() == 'M'
        && std::isdigit(static_cast<unsigned char>(token[token.size() - 2]))) {
        token.pop_back();
    }
    return token;
}

NvidiaArch nvidiaArchitectureFromString(const std::string& architecture,
                                        const std::string& chip)
{
    const std::string normalized = toLowerAscii(trim(architecture));
    const std::string upperChip = normalizedChip(chip);

    if (normalized.find("blackwell") != std::string::npos
        || startsWithAny(upperChip, {"GB", "B100", "B200"})) {
        return NvidiaArch::Blackwell;
    }
    if (normalized.find("ada") != std::string::npos || startsWith(upperChip, "AD")) {
        return NvidiaArch::Ada;
    }
    if (normalized.find("hopper") != std::string::npos || startsWith(upperChip, "GH")) {
        return NvidiaArch::Hopper;
    }
    if (normalized.find("ampere") != std::string::npos || startsWith(upperChip, "GA")) {
        return NvidiaArch::Ampere;
    }
    if (normalized.find("turing") != std::string::npos || startsWith(upperChip, "TU")) {
        return NvidiaArch::Turing;
    }
    if (normalized.find("volta") != std::string::npos || startsWith(upperChip, "GV")) {
        return NvidiaArch::Volta;
    }
    if (normalized.find("pascal") != std::string::npos || startsWith(upperChip, "GP")) {
        return NvidiaArch::Pascal;
    }
    if (normalized.find("maxwell") != std::string::npos) {
        return startsWith(upperChip, "GM206") ? NvidiaArch::Maxwell2 : NvidiaArch::Maxwell1;
    }
    if (startsWith(upperChip, "GM")) {
        return startsWith(upperChip, "GM206") ? NvidiaArch::Maxwell2 : NvidiaArch::Maxwell1;
    }
    if (normalized.find("kepler") != std::string::npos || startsWith(upperChip, "GK")) {
        return NvidiaArch::Kepler;
    }
    if (normalized.find("fermi") != std::string::npos || startsWith(upperChip, "GF")) {
        return NvidiaArch::Fermi;
    }
    if (normalized.find("tesla") != std::string::npos
        || startsWithAny(upperChip, {"G80", "G84", "G86", "G92", "G94", "G96", "G98",
                                     "GT200", "GT21", "MCP"})) {
        return NvidiaArch::Tesla;
    }
    return NvidiaArch::Unknown;
}

std::string nvidiaArchitectureName(NvidiaArch architecture)
{
    switch (architecture) {
    case NvidiaArch::Tesla:
        return "Tesla";
    case NvidiaArch::Fermi:
        return "Fermi";
    case NvidiaArch::Kepler:
        return "Kepler";
    case NvidiaArch::Maxwell1:
        return "Maxwell 1st generation";
    case NvidiaArch::Maxwell2:
        return "Maxwell 2nd generation";
    case NvidiaArch::Pascal:
        return "Pascal";
    case NvidiaArch::Volta:
        return "Volta";
    case NvidiaArch::Turing:
        return "Turing";
    case NvidiaArch::Ampere:
        return "Ampere";
    case NvidiaArch::Ada:
        return "Ada Lovelace";
    case NvidiaArch::Hopper:
        return "Hopper";
    case NvidiaArch::Blackwell:
        return "Blackwell";
    case NvidiaArch::Unknown:
    default:
        return "unknown";
    }
}

TriState nvidiaNvencEngineForPciDevice(uint16_t deviceId)
{
    // Exact per-board exceptions from NVIDIA's Video Encode/Decode Support
    // Matrix take precedence over broad architecture inference. Keep unknown
    // for devices not explicitly listed here rather than guessing by model name.
    // https://developer.nvidia.com/video-encode-decode-gpu-support-matrix
    switch (deviceId) {
    case 0x1341: // GM108M [GeForce 840M]
        return TriState::No;
    default:
        return TriState::Unknown;
    }
}

TriState nvidiaNvdecEngineForPciDevice(uint16_t deviceId)
{
    switch (deviceId) {
    case 0x1341: // GM108M [GeForce 840M]
        return TriState::No;
    default:
        return TriState::Unknown;
    }
}

const NvidiaCodecSupport* NvidiaCapabilities::find(const std::string& codec) const
{
    const auto iterator = codecs.find(codec);
    return iterator == codecs.end() ? nullptr : &iterator->second;
}

NvidiaCapabilities nvidiaCapabilities(const std::string& chip,
                                      const std::string& architecture)
{
    NvidiaCapabilities capabilities;
    capabilities.chip = chip;
    capabilities.architecture = nvidiaArchitectureFromString(architecture, chip);
    capabilities.architectureName = nvidiaArchitectureName(capabilities.architecture);
    fillUnknown(capabilities);

    const std::vector<std::string> h264Profiles = {"Baseline", "Main", "High"};
    const std::vector<std::string> hevcProfiles = {"Main", "Main10"};
    const std::vector<std::string> vp9Profiles = {"Profile 0", "Profile 2"};
    const std::vector<std::string> av1Profiles = {"Main"};

    switch (capabilities.architecture) {
    case NvidiaArch::Tesla:
    case NvidiaArch::Fermi:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::No, h264Profiles, {8});
        setCodec(capabilities, codecs::kMPEG2, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVC1, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVP8, TriState::Unknown, TriState::Unknown);
        setCodec(capabilities, codecs::kVP9, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kHEVC, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kAV1, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        break;

    case NvidiaArch::Kepler:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::Yes, h264Profiles, {8});
        setCodec(capabilities, codecs::kMPEG2, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVC1, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVP8, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kVP9, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kHEVC, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kAV1, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        break;

    case NvidiaArch::Maxwell1:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::Yes, h264Profiles, {8});
        setCodec(capabilities, codecs::kMPEG2, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVC1, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVP8, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kVP9, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kHEVC, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kAV1, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        break;

    case NvidiaArch::Maxwell2:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::Yes, h264Profiles, {8});
        // GM206 decodes Main10, but Maxwell NVENC only confirms 8-bit HEVC.
        // Keep shared profile/bit-depth metadata at the intersection of both directions.
        setCodec(capabilities, codecs::kHEVC, TriState::Yes, TriState::Yes, {"Main"}, {8});
        setCodec(capabilities, codecs::kVP9, TriState::Yes, TriState::No, {"Profile 0"}, {8});
        setCodec(capabilities, codecs::kMPEG2, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVC1, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVP8, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kAV1, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        break;

    case NvidiaArch::Pascal:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::Yes, h264Profiles, {8});
        setCodec(capabilities, codecs::kHEVC, TriState::Yes, TriState::Yes, hevcProfiles, {8, 10});
        // Pascal SKUs vary in VP8/VP9 decode blocks; architecture alone is insufficient.
        setCodec(capabilities, codecs::kVP9, TriState::Unknown, TriState::No);
        setCodec(capabilities, codecs::kVP8, TriState::Unknown, TriState::No);
        setCodec(capabilities, codecs::kAV1, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kMPEG2, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVC1, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        break;

    case NvidiaArch::Volta:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::Yes, h264Profiles, {8});
        setCodec(capabilities, codecs::kHEVC, TriState::Yes, TriState::Yes, hevcProfiles, {8, 10});
        setCodec(capabilities, codecs::kVP9, TriState::Yes, TriState::No, vp9Profiles, {8, 10});
        setCodec(capabilities, codecs::kVP8, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kAV1, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kMPEG2, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVC1, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        break;

    case NvidiaArch::Turing:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::Yes, h264Profiles, {8});
        setCodec(capabilities, codecs::kHEVC, TriState::Yes, TriState::Yes, hevcProfiles, {8, 10});
        setCodec(capabilities, codecs::kVP9, TriState::Yes, TriState::No, vp9Profiles, {8, 10});
        setCodec(capabilities, codecs::kVP8, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kAV1, TriState::No, TriState::No);
        setCodec(capabilities, codecs::kMPEG2, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVC1, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        break;

    case NvidiaArch::Ampere:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::Yes, h264Profiles, {8});
        setCodec(capabilities, codecs::kHEVC, TriState::Yes, TriState::Yes, hevcProfiles, {8, 10});
        setCodec(capabilities, codecs::kVP9, TriState::Yes, TriState::No, vp9Profiles, {8, 10});
        setCodec(capabilities, codecs::kAV1, TriState::Yes, TriState::No, {"Main"}, {8, 10});
        setCodec(capabilities, codecs::kVP8, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kMPEG2, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVC1, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        if (startsWith(normalizedChip(chip), "GA100")) {
            // Data-centre GA100 parts vary in their media engine configuration.
            fillUnknown(capabilities);
        }
        break;

    case NvidiaArch::Ada:
    case NvidiaArch::Blackwell:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::Yes, h264Profiles, {8});
        setCodec(capabilities, codecs::kHEVC, TriState::Yes, TriState::Yes, hevcProfiles, {8, 10});
        setCodec(capabilities, codecs::kVP9, TriState::Yes, TriState::No, vp9Profiles, {8, 10});
        setCodec(capabilities, codecs::kAV1, TriState::Yes, TriState::Yes, av1Profiles, {8, 10});
        setCodec(capabilities, codecs::kVP8, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kMPEG2, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kVC1, TriState::Yes, TriState::No, {}, {8});
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        break;

    case NvidiaArch::Hopper:
        setCodec(capabilities, codecs::kH264, TriState::Yes, TriState::Unknown, h264Profiles, {8});
        setCodec(capabilities, codecs::kHEVC, TriState::Yes, TriState::Unknown, hevcProfiles, {8, 10});
        setCodec(capabilities, codecs::kVP9, TriState::Yes, TriState::Unknown, vp9Profiles, {8, 10});
        setCodec(capabilities, codecs::kAV1, TriState::Unknown, TriState::Unknown);
        setCodec(capabilities, codecs::kVP8, TriState::Unknown, TriState::Unknown);
        setCodec(capabilities, codecs::kMPEG2, TriState::Unknown, TriState::Unknown);
        setCodec(capabilities, codecs::kVC1, TriState::Unknown, TriState::Unknown);
        setCodec(capabilities, codecs::kMJPEG, TriState::Unknown, TriState::Unknown);
        break;

    case NvidiaArch::Unknown:
    default:
        break;
    }

    return capabilities;
}

} // namespace gpu_report

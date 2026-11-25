#pragma once

#include "gpu/GPUInfo.hpp"

#include <map>
#include <string>
#include <vector>

namespace gpu_report {

enum class NvidiaArch {
    Unknown,
    Tesla,
    Fermi,
    Kepler,
    Maxwell1,
    Maxwell2,
    Pascal,
    Volta,
    Turing,
    Ampere,
    Ada,
    Hopper,
    Blackwell
};

struct NvidiaCodecSupport {
    TriState decode = TriState::Unknown;
    TriState encode = TriState::Unknown;
    std::vector<std::string> profiles;
    std::vector<int> bitDepths;
};

struct NvidiaCapabilities {
    NvidiaArch architecture = NvidiaArch::Unknown;
    std::string architectureName;
    std::string chip;
    std::map<std::string, NvidiaCodecSupport> codecs;

    bool known() const { return architecture != NvidiaArch::Unknown; }
    const NvidiaCodecSupport* find(const std::string& codec) const;
};

NvidiaArch nvidiaArchitectureFromString(const std::string& architecture,
                                        const std::string& chip);
std::string nvidiaArchitectureName(NvidiaArch architecture);
TriState nvidiaNvencEngineForPciDevice(uint16_t deviceId);
TriState nvidiaNvdecEngineForPciDevice(uint16_t deviceId);
std::string nvidiaChipFromName(const std::string& pciDeviceName);
NvidiaCapabilities nvidiaCapabilities(const std::string& chip,
                                      const std::string& architecture);

} // namespace gpu_report

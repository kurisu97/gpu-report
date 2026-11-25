#pragma once

#include "gpu/GPUInfo.hpp"

#include <string>
#include <vector>

namespace gpu_report {

struct CodecEvidence {
    std::string codec;
    TriState decode = TriState::Unknown;
    TriState encode = TriState::Unknown;
    std::string decodeSource;
    std::string encodeSource;
    std::vector<std::string> profiles;
    std::vector<int> bitDepths;
    std::vector<std::string> pixelFormats;
};

struct DiagnosticOutput {
    std::vector<DiagnosticCheck> checks;
    std::vector<CodecEvidence> evidence;
};

void ensureMandatoryCodecs(std::vector<CodecCapability>& capabilities);
void mergeEvidence(std::vector<CodecCapability>& capabilities,
                   const std::vector<CodecEvidence>& evidence);

} // namespace gpu_report

#pragma once

#include "diagnostics/Diagnostics.hpp"

#include <string>

namespace gpu_report {

struct NvidiaIdentity {
    bool driverPresent = false;
    bool smiAvailable = false;
    std::string driverVersion;
    std::string smiName;
    std::string architecture;
    std::string chip;
    std::string busId;
    TriState nvencEngine = TriState::Unknown;
    TriState nvdecEngine = TriState::Unknown;
};

class NVENCDetector {
public:
    static void detect(const GPUInfo& gpu, const NvidiaIdentity& identity,
                       DiagnosticOutput& output);
};

} // namespace gpu_report

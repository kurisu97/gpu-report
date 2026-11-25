#pragma once

#include "diagnostics/Diagnostics.hpp"

#include "diagnostics/NVENCDetector.hpp"

namespace gpu_report {

class NVDECDetector {
public:
    static void detect(const GPUInfo& gpu, const NvidiaIdentity& identity,
                       DiagnosticOutput& output);
};

} // namespace gpu_report

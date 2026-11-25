#pragma once

#include "diagnostics/Diagnostics.hpp"

namespace gpu_report {

class VAAPIDetector {
public:
    static void detect(const GPUInfo& gpu, int timeoutMs, DiagnosticOutput& output);
};

} // namespace gpu_report

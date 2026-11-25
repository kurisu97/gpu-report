#pragma once

#include "gpu/GPUInfo.hpp"

namespace gpu_report {

class RecommendationGenerator {
public:
    static void generate(GPUInfo& gpu);
};

} // namespace gpu_report

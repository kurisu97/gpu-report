#pragma once

#include "gpu/GPUInfo.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace gpu_report {

class PCIDatabase;

class GPUDetector {
public:
    explicit GPUDetector(const PCIDatabase& database);

    std::vector<GPUInfo> detectAll() const;

    static bool isDisplayClass(uint32_t classCode);

private:
    const PCIDatabase& database_;
};

} // namespace gpu_report

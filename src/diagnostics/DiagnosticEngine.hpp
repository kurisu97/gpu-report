#pragma once

#include "diagnostics/FFmpegDetector.hpp"
#include "diagnostics/VulkanDetector.hpp"
#include "gpu/GPUInfo.hpp"

#include <string>
#include <vector>

namespace gpu_report {

class PCIDatabase;

struct EngineOptions {
    int vainfoTimeoutMs = 10000;
    int vulkanSummaryTimeoutMs = 20000;
    int vulkanFullTimeoutMs = 30000;
    int ffmpegTimeoutMs = 12000;
    int nvidiaSmiTimeoutMs = 10000;
};

class DiagnosticEngine {
public:
    DiagnosticEngine(const PCIDatabase& database, EngineOptions options = {});

    void diagnose(std::vector<GPUInfo>& gpus);

    const std::vector<DiagnosticCheck>& globalChecks() const { return globalChecks_; }
    const FfmpegCapabilities& ffmpeg() const { return ffmpeg_; }

private:
    const PCIDatabase& database_;
    EngineOptions options_;
    std::vector<DiagnosticCheck> globalChecks_;
    FfmpegCapabilities ffmpeg_;
};

} // namespace gpu_report

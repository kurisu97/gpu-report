#pragma once

#include "gpu/GPUInfo.hpp"
#include "localization/Localization.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace gpu_report {

struct ReportOptions {
    bool color = false;
    // Zero means "detect from the terminal, then fall back to 100 columns".
    std::size_t width = 0;
};

class ReportGenerator {
public:
    static std::string generateInventoryReport(const Inventory& inventory,
                                               const Localization& localization,
                                               const ReportOptions& options);
    static std::string generateGpuReport(const GPUInfo& gpu, std::size_t index,
                                         std::size_t total,
                                         const Localization& localization,
                                         const ReportOptions& options);
    static std::string generateGpuList(const std::vector<GPUInfo>& gpus,
                                       const Localization& localization,
                                       const ReportOptions& options);

    static std::size_t detectTerminalWidth();

    static std::string localizedStatus(TriState value, const Localization& localization);
    static std::string localizedDetectionStatus(const std::string& status,
                                                const Localization& localization);
    static std::string localizedDiagnosticStatus(const std::string& status,
                                                 const Localization& localization);
    static std::string localizedCheckDetail(const DiagnosticCheck& check,
                                            const Localization& localization);
    static std::string localizedAdapterType(TriState integrated,
                                            const Localization& localization);
};

} // namespace gpu_report

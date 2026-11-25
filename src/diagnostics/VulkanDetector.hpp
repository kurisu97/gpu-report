#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace gpu_report {

struct VulkanDevice {
    uint16_t vendorId = 0;
    uint32_t deviceId = 0;
    std::string name;
    std::string deviceType;
    std::string driverVersion;
    std::string driverName;
    std::string driverInfo;
    bool integrated = false;
    bool discrete = false;
    std::set<std::string> extensions;
};

struct VulkanReport {
    bool toolFound = false;
    bool available = false;
    bool timedOut = false;
    int exitCode = -1;
    std::string detail;
    std::vector<VulkanDevice> devices;
    bool extensionsScanned = false;
    std::string extensionDetail;
};

struct VulkanVideoEvidence {
    std::string codec;
    bool decode = false;
    bool encode = false;
    std::string extension;
};

class VulkanDetector {
public:
    static VulkanReport detect(int summaryTimeoutMs = 20000, int fullTimeoutMs = 30000);
};

std::vector<VulkanVideoEvidence> vulkanVideoEvidence(const VulkanDevice& device);

} // namespace gpu_report

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace gpu_report {

class PCIDatabase {
public:
    bool loadDefault();
    bool loadFile(const std::string& path);

    bool loaded() const { return loaded_; }
    std::size_t deviceCount() const { return devices_.size(); }
    const std::string& sourcePath() const { return sourcePath_; }

    std::string vendorName(uint16_t vendorId) const;
    std::string deviceName(uint16_t vendorId, uint16_t deviceId) const;

private:
    std::map<uint16_t, std::string> vendors_;
    std::map<uint32_t, std::string> devices_;
    std::string sourcePath_;
    bool loaded_ = false;
};

} // namespace gpu_report

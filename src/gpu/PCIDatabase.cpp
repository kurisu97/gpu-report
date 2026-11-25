#include "gpu/PCIDatabase.hpp"

#include "core/Strings.hpp"
#include "core/Sysfs.hpp"

#include <cstdlib>
#include <fstream>
#include <vector>

namespace gpu_report {

namespace {

bool parseFourDigitHex(const std::string& token, uint16_t& value)
{
    if (token.size() != 4) {
        return false;
    }
    uint32_t parsed = 0;
    if (!parseUnsignedHex(token, parsed) || parsed > 0xffff) {
        return false;
    }
    value = static_cast<uint16_t>(parsed);
    return true;
}

std::string nameAfterCode(const std::string& line, std::size_t codeOffset)
{
    if (line.size() <= codeOffset + 4) {
        return {};
    }
    return trim(line.substr(codeOffset + 4));
}

} // namespace

bool PCIDatabase::loadFile(const std::string& path)
{
    std::ifstream input(path);
    if (!input) {
        return false;
    }

    std::map<uint16_t, std::string> vendors;
    std::map<uint32_t, std::string> devices;
    uint16_t currentVendor = 0;
    bool inClassSection = false;

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (startsWith(line, "C ")) {
            // Class definitions follow the vendor/device database.
            inClassSection = true;
            continue;
        }
        if (inClassSection) {
            continue;
        }

        if (line[0] == '\t') {
            if (line.size() > 1 && line[1] == '\t') {
                // Subsystem entry; not needed for adapter names.
                continue;
            }
            if (currentVendor == 0) {
                continue;
            }
            uint16_t deviceId = 0;
            if (line.size() >= 5 && parseFourDigitHex(line.substr(1, 4), deviceId)) {
                const std::string name = nameAfterCode(line, 1);
                if (!name.empty()) {
                    devices[(static_cast<uint32_t>(currentVendor) << 16) | deviceId] = name;
                }
            }
            continue;
        }

        uint16_t vendorId = 0;
        if (line.size() >= 4 && parseFourDigitHex(line.substr(0, 4), vendorId)) {
            currentVendor = vendorId;
            const std::string name = nameAfterCode(line, 0);
            if (!name.empty()) {
                vendors[vendorId] = name;
            }
        }
    }

    if (vendors.empty()) {
        return false;
    }

    vendors_ = std::move(vendors);
    devices_ = std::move(devices);
    sourcePath_ = path;
    loaded_ = true;
    return true;
}

bool PCIDatabase::loadDefault()
{
    const char* environment = std::getenv("GPU_REPORT_PCI_IDS");
    std::vector<std::string> candidates;
    if (environment != nullptr && *environment != '\0') {
        candidates.emplace_back(environment);
    }
    candidates.emplace_back("/usr/share/hwdata/pci.ids");
    candidates.emplace_back("/usr/share/misc/pci.ids");
    candidates.emplace_back("/usr/share/pci.ids");
    candidates.emplace_back("/var/lib/pciutils/pci.ids");

    for (const std::string& candidate : candidates) {
        if (pathExists(candidate) && loadFile(candidate)) {
            return true;
        }
    }
    return false;
}

std::string PCIDatabase::vendorName(uint16_t vendorId) const
{
    const auto iterator = vendors_.find(vendorId);
    if (iterator != vendors_.end()) {
        return iterator->second;
    }
    switch (vendorId) {
    case 0x8086:
        return "Intel Corporation";
    case 0x10de:
        return "NVIDIA Corporation";
    case 0x1002:
        return "Advanced Micro Devices, Inc. [AMD/ATI]";
    case 0x1022:
        return "Advanced Micro Devices, Inc. [AMD]";
    case 0x15ad:
        return "VMware, Inc.";
    case 0x1af4:
        return "Red Hat, Inc.";
    default:
        return {};
    }
}

std::string PCIDatabase::deviceName(uint16_t vendorId, uint16_t deviceId) const
{
    const auto iterator = devices_.find((static_cast<uint32_t>(vendorId) << 16) | deviceId);
    if (iterator != devices_.end()) {
        return iterator->second;
    }
    return {};
}

} // namespace gpu_report

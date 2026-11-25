#include "gpu/GPUDetector.hpp"

#include "core/Strings.hpp"
#include "core/Sysfs.hpp"
#include "core/SystemInfo.hpp"
#include "gpu/PCIDatabase.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <regex>
#include <system_error>

namespace gpu_report {

namespace {

struct DrmNodes {
    std::vector<std::string> cards;
    std::vector<std::string> renders;
};

std::string marketingName(const std::string& rawName)
{
    const std::size_t open = rawName.find('[');
    const std::size_t close = rawName.rfind(']');
    if (open != std::string::npos && close != std::string::npos && close > open) {
        return trim(rawName.substr(open + 1, close - open - 1));
    }
    return trim(rawName);
}

std::string cleanName(std::string value)
{
    value = replaceAll(value, "(R)", "");
    value = replaceAll(value, "(TM)", "");
    value = replaceAll(value, "  ", " ");
    return collapseWhitespace(value);
}

std::string readDriverVersion(const std::string& driver)
{
    if (driver.empty()) {
        return {};
    }
    std::string moduleVersion = trim(readFileOr("/sys/module/" + driver + "/version"));
    if (!moduleVersion.empty()) {
        return moduleVersion;
    }
    if (driver == "nvidia") {
        const std::string nvidiaVersion = readFileOr("/proc/driver/nvidia/version");
        const std::size_t marker = nvidiaVersion.find("Kernel Module");
        if (marker != std::string::npos) {
            const std::vector<std::string> tokens = splitWhitespace(nvidiaVersion.substr(marker));
            for (std::size_t i = 0; i + 1 < tokens.size(); ++i) {
                if (tokens[i] == "Module") {
                    return tokens[i + 1];
                }
            }
        }
    }
    return {};
}

std::map<std::string, DrmNodes> collectDrmNodes()
{
    std::map<std::string, DrmNodes> result;
    std::error_code error;
    const std::filesystem::path drmClass("/sys/class/drm");
    std::filesystem::directory_iterator iterator(drmClass, error);
    if (error) {
        return result;
    }

    const std::regex cardPattern("^card[0-9]+$");
    const std::regex renderPattern("^renderD[0-9]+$");
    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const std::string entryName = iterator->path().filename().string();
        const bool isCard = std::regex_match(entryName, cardPattern);
        const bool isRender = std::regex_match(entryName, renderPattern);
        if (!isCard && !isRender) {
            iterator.increment(error);
            if (error) {
                break;
            }
            continue;
        }

        const std::string canonical = canonicalPath(iterator->path() / "device");
        if (!canonical.empty()) {
            const std::string node = "/dev/dri/" + entryName;
            if (isCard) {
                result[canonical].cards.push_back(node);
            } else {
                result[canonical].renders.push_back(node);
            }
        }

        iterator.increment(error);
        if (error) {
            break;
        }
    }

    for (auto& entry : result) {
        std::sort(entry.second.cards.begin(), entry.second.cards.end());
        std::sort(entry.second.renders.begin(), entry.second.renders.end());
    }
    return result;
}

std::string nodeListDetail(const std::vector<std::string>& nodes)
{
    if (nodes.empty()) {
        return {};
    }
    std::vector<std::string> missing;
    for (const std::string& node : nodes) {
        if (!pathExists(node)) {
            missing.push_back(node);
        }
    }
    if (missing.empty()) {
        return {};
    }
    return join(missing, ", ");
}

} // namespace

GPUDetector::GPUDetector(const PCIDatabase& database)
    : database_(database)
{
}

bool GPUDetector::isDisplayClass(uint32_t classCode)
{
    // PCI base class 0x03 covers VGA, 3D controllers and other display devices.
    return ((classCode >> 16) & 0xffu) == 0x03u;
}

std::vector<GPUInfo> GPUDetector::detectAll() const
{
    std::vector<GPUInfo> gpus;
    const std::map<std::string, DrmNodes> drmNodes = collectDrmNodes();

    std::error_code error;
    const std::filesystem::path deviceRoot("/sys/bus/pci/devices");
    std::filesystem::directory_iterator iterator(deviceRoot, error);
    if (error) {
        return gpus;
    }

    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const std::filesystem::path devicePath = iterator->path();
        uint32_t classCode = 0;
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;
        if (!readHexFile(devicePath / "class", classCode)
            || !readHexFile(devicePath / "vendor", vendorId)
            || !readHexFile(devicePath / "device", deviceId)
            || !isDisplayClass(classCode)) {
            iterator.increment(error);
            if (error) {
                break;
            }
            continue;
        }

        GPUInfo gpu;
        gpu.vendorId = static_cast<uint16_t>(vendorId);
        gpu.deviceId = static_cast<uint16_t>(deviceId);
        gpu.vendor = vendorFromId(gpu.vendorId);
        gpu.pciPath = devicePath.filename().string();

        gpu.vendorName = database_.vendorName(gpu.vendorId);
        if (gpu.vendorName.empty()) {
            gpu.vendorName = vendorDisplayName(gpu.vendor);
        }

        gpu.pciDeviceName = database_.deviceName(gpu.vendorId, gpu.deviceId);
        const std::string marketing = cleanName(marketingName(gpu.pciDeviceName));
        if (!marketing.empty()) {
            gpu.name = marketing;
            gpu.nameSource = "pci.ids";
        } else {
            gpu.name = gpu.vendorName + " Device " + hexValue(gpu.deviceId);
            gpu.nameSource = "generic";
        }

        gpu.driver = readSymlinkName(devicePath / "driver");
        const std::string moduleVersion = readDriverVersion(gpu.driver);
        if (!moduleVersion.empty()) {
            gpu.driverVersion = moduleVersion;
            gpu.driverVersionSource = "module";
        } else if (!gpu.driver.empty()) {
            gpu.driverVersion = kernelRelease();
            gpu.driverVersionSource = "kernel";
        } else {
            gpu.driverVersion.clear();
            gpu.driverVersionSource = "unknown";
        }

        const std::string canonicalDevice = canonicalPath(devicePath);
        const auto drmIterator = drmNodes.find(canonicalDevice);
        if (drmIterator != drmNodes.end()) {
            gpu.drmCards = drmIterator->second.cards;
            gpu.renderNodes = drmIterator->second.renders;
        }

        if (gpu.driver.empty()) {
            gpu.status = "no_driver";
        } else if (gpu.renderNodes.empty() && gpu.drmCards.empty()) {
            gpu.status = "no_drm_node";
        } else {
            gpu.status = "detected";
        }

        DiagnosticCheck sysfsCheck;
        sysfsCheck.interfaceName = "sysfs";
        sysfsCheck.interfaceKey = "diag.interface.sysfs";
        sysfsCheck.status = "available";
        sysfsCheck.detail = "PCI display device at " + gpu.pciPath;
        sysfsCheck.detailKey = "diag.detail.sysfs_device";
        sysfsCheck.detailArguments = {gpu.pciPath};
        gpu.checks.push_back(sysfsCheck);

        DiagnosticCheck drmCheck;
        drmCheck.interfaceName = "DRM";
        drmCheck.interfaceKey = "diag.interface.drm";
        if (!gpu.renderNodes.empty() || !gpu.drmCards.empty()) {
            drmCheck.status = "available";
            std::vector<std::string> nodes = gpu.renderNodes;
            nodes.insert(nodes.end(), gpu.drmCards.begin(), gpu.drmCards.end());
            const std::string nodeList = join(nodes, ", ");
            drmCheck.detail = nodeList;
            const std::string missing = nodeListDetail(nodes);
            if (!missing.empty()) {
                drmCheck.detail += "; missing device nodes: " + missing;
                drmCheck.detailKey = "diag.detail.drm_missing_nodes";
                drmCheck.detailArguments = {nodeList, missing};
            }
        } else {
            drmCheck.status = "unavailable";
            drmCheck.detail = "no DRM nodes found";
            drmCheck.detailKey = "diag.detail.drm_no_nodes";
        }
        gpu.checks.push_back(drmCheck);

        gpus.push_back(std::move(gpu));

        iterator.increment(error);
        if (error) {
            break;
        }
    }

    std::sort(gpus.begin(), gpus.end(), [](const GPUInfo& left, const GPUInfo& right) {
        return left.pciPath < right.pciPath;
    });

    return gpus;
}

} // namespace gpu_report

#include "gpu/GPUInfo.hpp"

#include "core/Sysfs.hpp"

namespace gpu_report {

std::string triStateToString(TriState value)
{
    switch (value) {
    case TriState::Yes:
        return "YES";
    case TriState::No:
        return "NO";
    case TriState::Unknown:
    default:
        return "UNKNOWN";
    }
}

std::string triStateToXml(TriState value)
{
    switch (value) {
    case TriState::Yes:
        return "true";
    case TriState::No:
        return "false";
    case TriState::Unknown:
    default:
        return "unknown";
    }
}

CodecCapability* GPUInfo::findCodec(const std::string& codecName)
{
    for (CodecCapability& capability : codecs) {
        if (capability.codec == codecName) {
            return &capability;
        }
    }
    return nullptr;
}

const CodecCapability* GPUInfo::findCodec(const std::string& codecName) const
{
    for (const CodecCapability& capability : codecs) {
        if (capability.codec == codecName) {
            return &capability;
        }
    }
    return nullptr;
}

std::string GPUInfo::existingRenderNode() const
{
    for (const std::string& node : renderNodes) {
        if (pathExists(node)) {
            return node;
        }
    }
    return {};
}

GPUVendor vendorFromId(uint16_t vendorId)
{
    switch (vendorId) {
    case 0x8086:
        return GPUVendor::Intel;
    case 0x1002:
    case 0x1022:
        return GPUVendor::AMD;
    case 0x10de:
        return GPUVendor::NVIDIA;
    default:
        return vendorId == 0 ? GPUVendor::Unknown : GPUVendor::Other;
    }
}

std::string vendorDisplayName(GPUVendor vendor)
{
    switch (vendor) {
    case GPUVendor::Intel:
        return "Intel";
    case GPUVendor::AMD:
        return "AMD";
    case GPUVendor::NVIDIA:
        return "NVIDIA";
    case GPUVendor::Other:
        return "Other";
    case GPUVendor::Unknown:
    default:
        return "Unknown";
    }
}

} // namespace gpu_report

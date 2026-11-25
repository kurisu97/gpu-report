#pragma once

#include <string>

namespace gpu_report {

struct HostInfo {
    std::string hostname;
    std::string kernel;
    std::string architecture;
    std::string distribution;
};

HostInfo collectHostInfo();
std::string iso8601Now();
std::string kernelRelease();

} // namespace gpu_report

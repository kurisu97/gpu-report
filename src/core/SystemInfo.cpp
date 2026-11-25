#include "core/SystemInfo.hpp"

#include "core/Strings.hpp"
#include "core/Sysfs.hpp"

#include <chrono>
#include <ctime>
#include <sys/utsname.h>
#include <unistd.h>

namespace gpu_report {

namespace {

std::string unquote(const std::string& value)
{
    std::string result = trim(value);
    if (result.size() >= 2 && result.front() == '"' && result.back() == '"') {
        result = result.substr(1, result.size() - 2);
    }
    return result;
}

std::string readDistribution()
{
    for (const char* candidate : {"/etc/os-release", "/usr/lib/os-release"}) {
        std::string content;
        if (!readFile(candidate, content)) {
            continue;
        }
        for (const std::string& line : splitLines(content)) {
            if (startsWith(line, "PRETTY_NAME=")) {
                return unquote(line.substr(std::string("PRETTY_NAME=").size()));
            }
        }
        for (const std::string& line : splitLines(content)) {
            if (startsWith(line, "NAME=")) {
                return unquote(line.substr(std::string("NAME=").size()));
            }
        }
    }
    return "unknown";
}

} // namespace

std::string kernelRelease()
{
    struct utsname info;
    if (uname(&info) == 0) {
        return info.release;
    }
    return "unknown";
}

HostInfo collectHostInfo()
{
    HostInfo info;
    info.kernel = kernelRelease();

    struct utsname uts;
    if (uname(&uts) == 0) {
        info.architecture = uts.machine;
    } else {
        info.architecture = "unknown";
    }

    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
        info.hostname = hostname;
    } else {
        info.hostname = "unknown";
    }

    info.distribution = readDistribution();
    return info;
}

std::string iso8601Now()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    struct tm utc;
    if (gmtime_r(&now, &utc) == nullptr) {
        return "unknown";
    }
    char buffer[64] = {0};
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0) {
        return "unknown";
    }
    return buffer;
}

} // namespace gpu_report

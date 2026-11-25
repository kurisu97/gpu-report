#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace gpu_report {

struct ProcessResult {
    bool started = false;
    bool timedOut = false;
    int exitCode = -1;
    std::string output;
    std::string errorOutput;

    std::string combined() const
    {
        if (errorOutput.empty()) {
            return output;
        }
        if (output.empty()) {
            return errorOutput;
        }
        return output + "\n" + errorOutput;
    }

    std::string firstErrorLine() const;
};

// Finds an executable in PATH (or accepts an absolute/relative path).
bool findExecutable(const std::string& name, std::string* path);

// Runs a program without a shell. Arguments are passed verbatim, which avoids
// shell-injection problems for values that originate from sysfs or the CLI.
ProcessResult runProcess(const std::vector<std::string>& arguments,
                          int timeoutMs = 10000,
                          std::size_t maxOutputBytes = 8U * 1024U * 1024U);

} // namespace gpu_report

#pragma once

#include "gpu/GPUInfo.hpp"

#include <string>

namespace gpu_report {

struct XMLExportResult {
    bool ok = false;
    std::string path;
    std::string errorKey;
    std::string errorDetail;
    std::string errorDetail2;
};

class XMLExporter {
public:
    // Writes a temporary file, re-parses it with libxml2 and only then renames
    // it into place. This prevents a partially written report from replacing a
    // previously valid one.
    static XMLExportResult save(const Inventory& inventory, const std::string& path);

    static bool validateFile(const std::string& path, std::string* error);
};

} // namespace gpu_report

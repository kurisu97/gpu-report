#include "TestHarness.hpp"

#include "gpu/GPUDetector.hpp"
#include "gpu/GPUInfo.hpp"
#include "gpu/PCIDatabase.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

using namespace gpu_report;

namespace {

std::filesystem::path temporaryRoot()
{
    std::error_code error;
    if (std::filesystem::is_directory("/tmp/opencode", error)) {
        return "/tmp/opencode";
    }
    return std::filesystem::temp_directory_path();
}

} // namespace

int main()
{
    CHECK(GPUDetector::isDisplayClass(0x030000));
    CHECK(GPUDetector::isDisplayClass(0x038000));
    CHECK(GPUDetector::isDisplayClass(0x030200));
    CHECK(!GPUDetector::isDisplayClass(0x020000));
    CHECK(!GPUDetector::isDisplayClass(0x040000));

    CHECK(vendorFromId(0x8086) == GPUVendor::Intel);
    CHECK(vendorFromId(0x10de) == GPUVendor::NVIDIA);
    CHECK(vendorFromId(0x1002) == GPUVendor::AMD);
    CHECK(vendorFromId(0x1022) == GPUVendor::AMD);
    CHECK(vendorFromId(0x1234) == GPUVendor::Other);
    CHECK(vendorFromId(0x0000) == GPUVendor::Unknown);

    CHECK(vendorDisplayName(GPUVendor::Intel) == "Intel");
    CHECK(vendorDisplayName(GPUVendor::NVIDIA) == "NVIDIA");
    CHECK(vendorDisplayName(GPUVendor::AMD) == "AMD");

    CHECK(triStateToString(TriState::Yes) == "YES");
    CHECK(triStateToString(TriState::No) == "NO");
    CHECK(triStateToString(TriState::Unknown) == "UNKNOWN");
    CHECK(triStateToXml(TriState::Yes) == "true");
    CHECK(triStateToXml(TriState::No) == "false");
    CHECK(triStateToXml(TriState::Unknown) == "unknown");

    const std::filesystem::path root = temporaryRoot();
    const std::string suffix = std::to_string(::getpid());
    const std::filesystem::path pciFile = root / ("gpu_report_pci_" + suffix + ".ids");
    {
        std::ofstream output(pciFile);
        output << "# test database\n";
        output << "8086  Intel Corporation\n";
        output << "\t0416  Test Graphics [Twin Device]\n";
        output << "\t0a16  Other Test Device\n";
        output << "10de  NVIDIA Corporation\n";
        output << "\t1341  GeForce Test [GF108]\n";
    }

    PCIDatabase database;
    CHECK(database.loadFile(pciFile.string()));
    CHECK(database.loaded());
    CHECK(database.deviceCount() == 3);
    CHECK(database.vendorName(0x8086) == "Intel Corporation");
    CHECK(database.vendorName(0x10de) == "NVIDIA Corporation");
    CHECK(database.deviceName(0x8086, 0x0416) == "Test Graphics [Twin Device]");
    CHECK(database.deviceName(0x10de, 0x1341) == "GeForce Test [GF108]");
    CHECK(database.deviceName(0x8086, 0xffff).empty());

    GPUDetector detector(database);
    const std::vector<GPUInfo> gpus = detector.detectAll();
    std::set<std::string> paths;
    for (const GPUInfo& gpu : gpus) {
        CHECK(!gpu.pciPath.empty());
        CHECK(paths.insert(gpu.pciPath).second);
        CHECK(gpu.vendor != GPUVendor::Unknown);
        CHECK(gpu.vendorId != 0);
        CHECK(!gpu.status.empty());
        CHECK(gpu.status == "detected" || gpu.status == "no_driver"
              || gpu.status == "no_drm_node");
        CHECK(!gpu.checks.empty());
        bool hasSysfsCheck = false;
        for (const DiagnosticCheck& check : gpu.checks) {
            if (check.interfaceName == "sysfs") {
                hasSysfsCheck = true;
            }
        }
        CHECK(hasSysfsCheck);
        for (const std::string& node : gpu.renderNodes) {
            CHECK(node.find("/dev/dri/renderD") == 0);
        }
        for (const std::string& card : gpu.drmCards) {
            CHECK(card.find("/dev/dri/card") == 0);
        }
        CHECK(!gpu.name.empty());
        if (gpu.driver.empty()) {
            CHECK(gpu.driverVersion.empty());
        } else {
            CHECK(!gpu.driverVersion.empty());
        }
    }

    const std::filesystem::path existingNode = root / ("gpu_report_node_" + suffix);
    {
        std::ofstream output(existingNode);
        output << "node";
    }
    GPUInfo fake;
    fake.renderNodes = {"/dev/does/not/exist", existingNode.string()};
    CHECK(fake.existingRenderNode() == existingNode.string());
    fake.renderNodes = {"/dev/does/not/exist"};
    CHECK(fake.existingRenderNode().empty());

    std::error_code error;
    std::filesystem::remove(pciFile, error);
    std::filesystem::remove(existingNode, error);

    return testharness::summary("gpu_detection");
}

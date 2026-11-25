#include "TestHarness.hpp"

#include "gpu/Codecs.hpp"
#include "xml/XMLExporter.hpp"

#include <filesystem>
#include <fstream>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace gpu_report;

namespace {

xmlNodePtr findChild(xmlNodePtr parent, const std::string& name)
{
    if (parent == nullptr) {
        return nullptr;
    }
    for (xmlNodePtr node = parent->children; node != nullptr; node = node->next) {
        if (node->type == XML_ELEMENT_NODE
            && std::string(reinterpret_cast<const char*>(node->name)) == name) {
            return node;
        }
    }
    return nullptr;
}

xmlNodePtr findPath(xmlNodePtr root, const std::vector<std::string>& path)
{
    xmlNodePtr node = root;
    for (const std::string& part : path) {
        node = findChild(node, part);
        if (node == nullptr) {
            return nullptr;
        }
    }
    return node;
}

std::string textOf(xmlNodePtr root, const std::vector<std::string>& path)
{
    xmlNodePtr node = findPath(root, path);
    if (node == nullptr) {
        return {};
    }
    xmlChar* content = xmlNodeGetContent(node);
    std::string result = content != nullptr
        ? reinterpret_cast<const char*>(content)
        : std::string();
    if (content != nullptr) {
        xmlFree(content);
    }
    return result;
}

std::string attributeOf(xmlNodePtr node, const std::string& name)
{
    if (node == nullptr) {
        return {};
    }
    xmlChar* value = xmlGetProp(node, BAD_CAST name.c_str());
    std::string result = value != nullptr
        ? reinterpret_cast<const char*>(value)
        : std::string();
    if (value != nullptr) {
        xmlFree(value);
    }
    return result;
}

std::filesystem::path temporaryRoot()
{
    std::error_code error;
    if (std::filesystem::is_directory("/tmp/opencode", error)) {
        return "/tmp/opencode";
    }
    return std::filesystem::temp_directory_path();
}

Inventory makeInventory()
{
    Inventory inventory;
    inventory.generatedAt = "2026-01-02T03:04:05Z";
    inventory.localeCode = "en";
    inventory.host.hostname = "test-host";
    inventory.host.kernel = "6.0.0-test";
    inventory.host.architecture = "x86_64";
    inventory.host.distribution = "Test Linux";

    DiagnosticCheck global;
    global.interfaceName = "sysfs";
    global.interfaceKey = "diag.interface.sysfs";
    global.status = "available";
    global.detail = "2 display device(s) found";
    inventory.globalChecks.push_back(global);

    GPUInfo gpu;
    gpu.name = "Twin & <GPU> \"quoted\"";
    gpu.nameSource = "pci.ids";
    gpu.vendorName = "Intel Corporation";
    gpu.pciDeviceName = "Test Device [Twin & <GPU>]";
    gpu.vendorId = 0x8086;
    gpu.deviceId = 0x0416;
    gpu.vendor = GPUVendor::Intel;
    gpu.pciPath = "0000:00:02.0";
    gpu.driver = "i915";
    gpu.driverVersion = "6.0.0";
    gpu.driverVersionSource = "module";
    gpu.renderNodes = {"/dev/dri/renderD128"};
    gpu.drmCards = {"/dev/dri/card1"};
    gpu.integrated = TriState::Yes;
    gpu.status = "detected";
    gpu.vulkanDeviceName = "Intel Test GPU";

    DiagnosticCheck check;
    check.interfaceName = "VA-API";
    check.interfaceKey = "diag.interface.vaapi";
    check.status = "available";
    check.detail = "a & b < c > d";
    gpu.checks.push_back(check);

    CodecCapability h264;
    h264.codec = codecs::kH264;
    h264.decode.value = TriState::Yes;
    h264.decode.sources = {"VA-API (test & source)"};
    h264.encode.value = TriState::No;
    h264.encode.sources = {"VA-API (negative <evidence>)"};
    h264.profiles = {"Main", "High"};
    h264.bitDepths = {8};
    h264.pixelFormats = {"nv12"};
    gpu.codecs.push_back(h264);

    CodecCapability hevc;
    hevc.codec = codecs::kHEVC;
    hevc.decode.value = TriState::Unknown;
    hevc.encode.value = TriState::Unknown;
    gpu.codecs.push_back(hevc);

    EncoderInfo encoder;
    encoder.name = "h264_vaapi";
    encoder.api = "VA-API";
    encoder.codec = codecs::kH264;
    encoder.availableInFfmpeg = true;
    encoder.ffmpegPixelFormats = {"nv12", "yuv420p"};
    gpu.encoders.push_back(encoder);

    EncoderInfo decoder;
    decoder.name = "vaapi";
    decoder.api = "VA-API";
    decoder.codec = codecs::kH264;
    decoder.availableInFfmpeg = true;
    gpu.decoders.push_back(decoder);

    Recommendation recommendation;
    recommendation.resolution = "1920x1080";
    recommendation.fps = 60;
    recommendation.codec = codecs::kH264;
    recommendation.bitrate = "8-12M";
    recommendation.encoder = "h264_vaapi";
    recommendation.command = "ffmpeg -i in.mp4 -c:v h264_vaapi -b:v 10M out&.mp4";
    gpu.recommendations.push_back(recommendation);

    inventory.gpus.push_back(gpu);
    return inventory;
}

} // namespace

int main()
{
    const std::filesystem::path root = temporaryRoot();
    const std::string suffix = std::to_string(::getpid());
    const std::filesystem::path output = root / ("gpu_report_test_" + suffix + ".xml");
    const std::filesystem::path broken = root / ("gpu_report_broken_" + suffix + ".xml");
    const std::filesystem::path missingParent = root
        / ("gpu_report_missing_" + suffix) / "nested" / "out.xml";
    const std::filesystem::path invalidUtf8 = root
        / ("gpu_report_utf8_" + suffix + ".xml");
    const std::filesystem::path renameTarget = root
        / ("gpu_report_rename_target_" + suffix);

    const Inventory inventory = makeInventory();
    const XMLExportResult emptyPath = XMLExporter::save(inventory, "");
    CHECK(!emptyPath.ok);
    CHECK(emptyPath.errorKey == "xml.empty_path");

    const XMLExportResult result = XMLExporter::save(inventory, output.string());
    CHECK(result.ok);
    CHECK(result.errorKey.empty());
    CHECK(std::filesystem::exists(output));

    std::string validationError;
    CHECK(XMLExporter::validateFile(output.string(), &validationError));
    CHECK(validationError.empty());

    xmlDocPtr document = xmlReadFile(output.string().c_str(), nullptr, XML_PARSE_NONET);
    CHECK(document != nullptr);
    if (document != nullptr) {
        xmlNodePtr documentRoot = xmlDocGetRootElement(document);
        CHECK(documentRoot != nullptr);
        CHECK(std::string(reinterpret_cast<const char*>(documentRoot->name))
              == "gpu_inventory");
        CHECK(attributeOf(documentRoot, "schema_version") == "1.0");
        CHECK(attributeOf(documentRoot, "active_probe") == "none");

        CHECK(textOf(documentRoot, {"generated_at"}) == "2026-01-02T03:04:05Z");
        CHECK(textOf(documentRoot, {"locale"}) == "en");
        CHECK(textOf(documentRoot, {"host", "hostname"}) == "test-host");
        CHECK(attributeOf(findPath(documentRoot, {"program"}), "name") == "gpu-report");
        CHECK(attributeOf(findPath(documentRoot, {"program"}), "version") == "1.0.0");
        CHECK(textOf(documentRoot, {"gpu", "name"}) == inventory.gpus.front().name);
        CHECK(textOf(documentRoot, {"gpu", "vendor_id"}) == "0x8086");
        CHECK(textOf(documentRoot, {"gpu", "device_id"}) == "0x0416");
        CHECK(textOf(documentRoot, {"gpu", "adapter_type"}) == "integrated");
        CHECK(textOf(documentRoot, {"gpu", "pci_path"}) == "0000:00:02.0");
        CHECK(textOf(documentRoot, {"gpu", "render_nodes", "node"})
              == "/dev/dri/renderD128");

        xmlNodePtr h264 = findPath(documentRoot, {"gpu", "capabilities", "codec"});
        CHECK(h264 != nullptr);
        CHECK(attributeOf(h264, "name") == codecs::kH264);
        CHECK(attributeOf(findChild(h264, "decode"), "supported") == "true");
        CHECK(attributeOf(findChild(h264, "encode"), "supported") == "false");
        CHECK(textOf(h264, {"profiles", "profile"}) == "Main");
        CHECK(textOf(h264, {"pixel_formats", "pixel_format"}) == "nv12");
        CHECK(textOf(h264, {"sources", "decode_sources", "source"})
              == "VA-API (test & source)");

        const xmlNodePtr codecList = findPath(documentRoot, {"gpu", "capabilities"});
        xmlNodePtr hevc = nullptr;
        if (codecList != nullptr) {
            for (xmlNodePtr node = codecList->children; node != nullptr; node = node->next) {
                if (node->type == XML_ELEMENT_NODE
                    && attributeOf(node, "name") == codecs::kHEVC) {
                    hevc = node;
                }
            }
        }
        CHECK(hevc != nullptr);
        if (hevc != nullptr) {
            CHECK(attributeOf(findChild(hevc, "decode"), "supported") == "unknown");
            CHECK(attributeOf(findChild(hevc, "encode"), "supported") == "unknown");
        }

        xmlNodePtr encoder = findPath(documentRoot, {"gpu", "hw_encoders", "encoder"});
        CHECK(encoder != nullptr);
        CHECK(attributeOf(encoder, "name") == "h264_vaapi");
        CHECK(attributeOf(encoder, "api") == "VA-API");
        CHECK(attributeOf(encoder, "available_in_ffmpeg") == "true");
        xmlNodePtr encoderFormats = findChild(encoder, "pixel_formats");
        CHECK(encoderFormats != nullptr);
        CHECK(attributeOf(encoderFormats, "source") == "ffmpeg_build");
        CHECK(attributeOf(encoderFormats, "per_gpu_verified") == "false");
        CHECK(textOf(encoderFormats, {"pixel_format"}) == "nv12");

        xmlNodePtr decoder = findPath(documentRoot, {"gpu", "hw_decoders", "decoder"});
        CHECK(decoder != nullptr);
        CHECK(attributeOf(decoder, "name") == "vaapi");

        xmlNodePtr preset = findPath(documentRoot,
                                     {"gpu", "encoding_recommendations", "preset"});
        CHECK(preset != nullptr);
        CHECK(attributeOf(preset, "resolution") == "1920x1080");
        CHECK(attributeOf(preset, "fps") == "60");
        CHECK(attributeOf(preset, "bitrate") == "8-12M");
        CHECK(textOf(preset, {"command"}) == inventory.gpus.front().recommendations.front().command);

        xmlNodePtr interface = findPath(documentRoot, {"interfaces", "interface"});
        CHECK(interface != nullptr);
        CHECK(attributeOf(interface, "status") == "available");
        CHECK(textOf(interface, {"detail"}) == "2 display device(s) found");

        xmlFreeDoc(document);
    }

    const XMLExportResult missing = XMLExporter::save(inventory, missingParent.string());
    CHECK(!missing.ok);
    CHECK(missing.errorKey == "xml.parent_missing");
    CHECK(!std::filesystem::exists(missingParent));

    std::error_code setupError;
    CHECK(std::filesystem::create_directory(renameTarget, setupError));
    const XMLExportResult renameFailure = XMLExporter::save(inventory, renameTarget.string());
    CHECK(!renameFailure.ok);
    CHECK(renameFailure.errorKey == "xml.rename_failed");
    CHECK(std::filesystem::is_directory(renameTarget));

    {
        std::ofstream outputFile(broken);
        outputFile << "<gpu_inventory><broken>";
    }
    std::string brokenError;
    CHECK(!XMLExporter::validateFile(broken.string(), &brokenError));
    CHECK(!brokenError.empty());

    Inventory invalid = makeInventory();
    invalid.gpus.front().name =
        std::string("bad \x01\xc0\x80\xed\xa0\x80\xef\xbf\xbe\xef\xbf\xbf\xff name");
    const XMLExportResult sanitized = XMLExporter::save(invalid, invalidUtf8.string());
    CHECK(sanitized.ok);
    CHECK(XMLExporter::validateFile(invalidUtf8.string(), &brokenError));
    xmlDocPtr sanitizedDocument = xmlReadFile(invalidUtf8.string().c_str(), nullptr,
                                              XML_PARSE_NONET);
    CHECK(sanitizedDocument != nullptr);
    if (sanitizedDocument != nullptr) {
        const std::string name = textOf(xmlDocGetRootElement(sanitizedDocument),
                                        {"gpu", "name"});
        CHECK(name.find("bad ") == 0);
        CHECK(name.find(" name") != std::string::npos);
        xmlFreeDoc(sanitizedDocument);
    }

    std::error_code error;
    std::filesystem::remove(output, error);
    std::filesystem::remove(broken, error);
    std::filesystem::remove(invalidUtf8, error);
    std::filesystem::remove(renameTarget, error);

    xmlCleanupParser();
    return testharness::summary("xml_export");
}

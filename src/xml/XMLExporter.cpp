#include "xml/XMLExporter.hpp"

#include "core/Strings.hpp"
#include "core/Version.hpp"
#include "gpu/Codecs.hpp"

#include <cstdint>
#include <filesystem>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlerror.h>
#include <libxml/xmlsave.h>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace gpu_report {

namespace {

std::string sanitizeUtf8(const std::string& value)
{
    std::string output;
    output.reserve(value.size());

    std::size_t index = 0;
    while (index < value.size()) {
        const unsigned char first = static_cast<unsigned char>(value[index]);
        if (first < 0x80) {
            // XML 1.0 does not permit most C0 control characters. Replace them
            // so arbitrary tool output cannot make the document invalid.
            if (first < 0x20 && first != '\t' && first != '\n' && first != '\r') {
                output += "\xef\xbf\xbd";
            } else {
                output.push_back(static_cast<char>(first));
            }
            ++index;
            continue;
        }

        std::size_t length = 0;
        if ((first & 0xe0) == 0xc0) {
            length = 2;
        } else if ((first & 0xf0) == 0xe0) {
            length = 3;
        } else if ((first & 0xf8) == 0xf0) {
            length = 4;
        }

        bool valid = length != 0 && index + length <= value.size();
        uint32_t codepoint = 0;
        if (valid) {
            codepoint = first & (length == 2 ? 0x1f : (length == 3 ? 0x0f : 0x07));
            for (std::size_t offset = 1; offset < length; ++offset) {
                const unsigned char continuation =
                    static_cast<unsigned char>(value[index + offset]);
                if ((continuation & 0xc0) != 0x80) {
                    valid = false;
                    break;
                }
                codepoint = (codepoint << 6) | (continuation & 0x3f);
            }
        }
        if (valid) {
            if ((length == 2 && codepoint < 0x80)
                || (length == 3 && codepoint < 0x800)
                || (length == 4 && codepoint < 0x10000)
                || codepoint > 0x10ffff
                || (codepoint >= 0xd800 && codepoint <= 0xdfff)
                || codepoint == 0xfffe || codepoint == 0xffff) {
                valid = false;
            }
        }

        if (valid) {
            output.append(value, index, length);
            index += length;
        } else {
            // U+FFFD replacement character keeps the XML well-formed.
            output += "\xef\xbf\xbd";
            ++index;
        }
    }
    return output;
}

void setAttribute(xmlNodePtr node, const char* name, const std::string& value)
{
    const std::string clean = sanitizeUtf8(value);
    xmlSetProp(node, BAD_CAST name, BAD_CAST clean.c_str());
}

void addText(xmlNodePtr parent, const char* name, const std::string& value)
{
    const std::string clean = sanitizeUtf8(value);
    xmlNewTextChild(parent, nullptr, BAD_CAST name, BAD_CAST clean.c_str());
}

std::string boolText(bool value)
{
    return value ? "true" : "false";
}

std::string adapterTypeText(TriState integrated)
{
    switch (integrated) {
    case TriState::Yes:
        return "integrated";
    case TriState::No:
        return "discrete";
    case TriState::Unknown:
    default:
        return "unknown";
    }
}

void addInterfaces(xmlNodePtr parent, const std::vector<DiagnosticCheck>& checks)
{
    xmlNodePtr interfaces = xmlNewChild(parent, nullptr, BAD_CAST "interfaces", nullptr);
    for (const DiagnosticCheck& check : checks) {
        xmlNodePtr node = xmlNewChild(interfaces, nullptr, BAD_CAST "interface", nullptr);
        setAttribute(node, "name", check.interfaceName);
        setAttribute(node, "key", check.interfaceKey);
        setAttribute(node, "status", check.status);
        addText(node, "detail", check.detail);
    }
}

void addEncoderList(xmlNodePtr parent, const char* name, const char* itemName,
                    const std::vector<EncoderInfo>& entries)
{
    xmlNodePtr list = xmlNewChild(parent, nullptr, BAD_CAST name, nullptr);
    for (const EncoderInfo& entry : entries) {
        xmlNodePtr node = xmlNewChild(list, nullptr, BAD_CAST itemName, nullptr);
        setAttribute(node, "name", entry.name);
        setAttribute(node, "api", entry.api);
        setAttribute(node, "codec", entry.codec);
        setAttribute(node, "available_in_ffmpeg", boolText(entry.availableInFfmpeg));
        if (!entry.ffmpegPixelFormats.empty()) {
            xmlNodePtr formats = xmlNewChild(node, nullptr, BAD_CAST "pixel_formats", nullptr);
            setAttribute(formats, "source", "ffmpeg_build");
            setAttribute(formats, "per_gpu_verified", "false");
            for (const std::string& format : entry.ffmpegPixelFormats) {
                addText(formats, "pixel_format", format);
            }
        }
    }
}

void addCapabilities(xmlNodePtr parent, const GPUInfo& gpu)
{
    xmlNodePtr capabilities = xmlNewChild(parent, nullptr, BAD_CAST "capabilities", nullptr);
    for (const CodecCapability& capability : gpu.codecs) {
        xmlNodePtr node = xmlNewChild(capabilities, nullptr, BAD_CAST "codec", nullptr);
        setAttribute(node, "name", capability.codec);

        xmlNodePtr decode = xmlNewChild(node, nullptr, BAD_CAST "decode", nullptr);
        setAttribute(decode, "supported", triStateToXml(capability.decode.value));

        xmlNodePtr encode = xmlNewChild(node, nullptr, BAD_CAST "encode", nullptr);
        setAttribute(encode, "supported", triStateToXml(capability.encode.value));

        if (!capability.profiles.empty()) {
            xmlNodePtr profiles = xmlNewChild(node, nullptr, BAD_CAST "profiles", nullptr);
            for (const std::string& profile : capability.profiles) {
                addText(profiles, "profile", profile);
            }
        }
        if (!capability.bitDepths.empty()) {
            xmlNodePtr depths = xmlNewChild(node, nullptr, BAD_CAST "bit_depths", nullptr);
            for (int depth : capability.bitDepths) {
                addText(depths, "bit_depth", std::to_string(depth));
            }
        }
        if (!capability.pixelFormats.empty()) {
            xmlNodePtr formats = xmlNewChild(node, nullptr, BAD_CAST "pixel_formats", nullptr);
            for (const std::string& format : capability.pixelFormats) {
                addText(formats, "pixel_format", format);
            }
        }
        if (!capability.decode.sources.empty() || !capability.encode.sources.empty()) {
            xmlNodePtr sources = xmlNewChild(node, nullptr, BAD_CAST "sources", nullptr);
            if (!capability.decode.sources.empty()) {
                xmlNodePtr decodeSources = xmlNewChild(sources, nullptr,
                                                       BAD_CAST "decode_sources", nullptr);
                for (const std::string& source : capability.decode.sources) {
                    addText(decodeSources, "source", source);
                }
            }
            if (!capability.encode.sources.empty()) {
                xmlNodePtr encodeSources = xmlNewChild(sources, nullptr,
                                                       BAD_CAST "encode_sources", nullptr);
                for (const std::string& source : capability.encode.sources) {
                    addText(encodeSources, "source", source);
                }
            }
        }
    }
}

void addRecommendations(xmlNodePtr parent, const GPUInfo& gpu)
{
    xmlNodePtr recommendations = xmlNewChild(parent, nullptr,
                                             BAD_CAST "encoding_recommendations", nullptr);
    for (const Recommendation& recommendation : gpu.recommendations) {
        xmlNodePtr preset = xmlNewChild(recommendations, nullptr,
                                        BAD_CAST "preset", nullptr);
        setAttribute(preset, "resolution", recommendation.resolution);
        setAttribute(preset, "fps", std::to_string(recommendation.fps));
        setAttribute(preset, "codec", recommendation.codec);
        setAttribute(preset, "bitrate", recommendation.bitrate);
        setAttribute(preset, "encoder", recommendation.encoder);
        addText(preset, "command", recommendation.command);
    }
}

void addStringList(xmlNodePtr parent, const char* containerName, const char* itemName,
                   const std::vector<std::string>& values)
{
    xmlNodePtr container = xmlNewChild(parent, nullptr, BAD_CAST containerName, nullptr);
    for (const std::string& value : values) {
        addText(container, itemName, value);
    }
}

void addGpu(xmlNodePtr root, const GPUInfo& gpu)
{
    xmlNodePtr node = xmlNewChild(root, nullptr, BAD_CAST "gpu", nullptr);
    addText(node, "name", gpu.name);
    addText(node, "name_source", gpu.nameSource);
    addText(node, "vendor", vendorDisplayName(gpu.vendor));
    addText(node, "vendor_name", gpu.vendorName);
    addText(node, "pci_device_name", gpu.pciDeviceName);
    addText(node, "vendor_id", hexValue(gpu.vendorId));
    addText(node, "device_id", hexValue(gpu.deviceId));
    addText(node, "driver", gpu.driver);
    addText(node, "driver_version", gpu.driverVersion);
    addText(node, "driver_version_source", gpu.driverVersionSource);
    addText(node, "pci_path", gpu.pciPath);

    addText(node, "adapter_type", adapterTypeText(gpu.integrated));
    xmlNodePtr integrated = xmlNewChild(node, nullptr, BAD_CAST "integrated", nullptr);
    setAttribute(integrated, "supported", triStateToXml(gpu.integrated));
    addText(node, "status", gpu.status);
    addText(node, "vulkan_device", gpu.vulkanDeviceName);

    addStringList(node, "render_nodes", "node", gpu.renderNodes);
    addStringList(node, "drm_cards", "card", gpu.drmCards);

    addInterfaces(node, gpu.checks);
    addCapabilities(node, gpu);
    addEncoderList(node, "hw_encoders", "encoder", gpu.encoders);
    addEncoderList(node, "hw_decoders", "decoder", gpu.decoders);
    addRecommendations(node, gpu);
}

} // namespace

bool XMLExporter::validateFile(const std::string& path, std::string* error)
{
    xmlResetLastError();
    xmlDocPtr document = xmlReadFile(path.c_str(), nullptr,
                                     XML_PARSE_NONET | XML_PARSE_NOERROR
                                         | XML_PARSE_NOWARNING);
    if (document == nullptr) {
        if (error != nullptr) {
            const xmlError* xmlError = xmlGetLastError();
            *error = xmlError != nullptr && xmlError->message != nullptr
                ? trim(xmlError->message)
                : "XML parser rejected the generated document";
        }
        return false;
    }
    xmlNodePtr root = xmlDocGetRootElement(document);
    const bool valid = root != nullptr
        && xmlStrcmp(root->name, BAD_CAST "gpu_inventory") == 0;
    if (!valid && error != nullptr) {
        *error = "root element is not <gpu_inventory>";
    }
    xmlFreeDoc(document);
    return valid;
}

XMLExportResult XMLExporter::save(const Inventory& inventory, const std::string& path)
{
    XMLExportResult result;
    result.path = path;

    if (trim(path).empty()) {
        result.errorKey = "xml.empty_path";
        return result;
    }

    std::error_code filesystemError;
    const std::filesystem::path target(path);
    std::filesystem::path parent = target.parent_path();
    if (parent.empty()) {
        parent = ".";
    }
    if (!std::filesystem::exists(parent, filesystemError)
        || !std::filesystem::is_directory(parent, filesystemError)) {
        result.errorKey = "xml.parent_missing";
        result.errorDetail = parent.string();
        return result;
    }

    xmlDocPtr document = xmlNewDoc(BAD_CAST "1.0");
    if (document == nullptr) {
        result.errorKey = "xml.allocation_failed";
        return result;
    }

    xmlNodePtr root = xmlNewNode(nullptr, BAD_CAST "gpu_inventory");
    if (root == nullptr) {
        xmlFreeDoc(document);
        result.errorKey = "xml.root_failed";
        return result;
    }
    xmlDocSetRootElement(document, root);
    setAttribute(root, "schema_version", kSchemaVersion);
    setAttribute(root, "active_probe", "none");

    addText(root, "generated_at", inventory.generatedAt);
    addText(root, "locale", inventory.localeCode.empty() ? "en" : inventory.localeCode);

    xmlNodePtr program = xmlNewChild(root, nullptr, BAD_CAST "program", nullptr);
    setAttribute(program, "name", kProgramName);
    setAttribute(program, "version", kProgramVersion);
    setAttribute(program, "schema_version", kSchemaVersion);

    xmlNodePtr host = xmlNewChild(root, nullptr, BAD_CAST "host", nullptr);
    addText(host, "hostname", inventory.host.hostname);
    addText(host, "kernel", inventory.host.kernel);
    addText(host, "architecture", inventory.host.architecture);
    addText(host, "distribution", inventory.host.distribution);

    addInterfaces(root, inventory.globalChecks);
    for (const GPUInfo& gpu : inventory.gpus) {
        addGpu(root, gpu);
    }

    // mkstemp avoids predictable-name races and symlink attacks when the
    // destination is in a shared directory such as /tmp.
    const std::string temporaryPattern = (parent
        / (target.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> temporaryBuffer(temporaryPattern.begin(), temporaryPattern.end());
    temporaryBuffer.push_back('\0');
    const int temporaryFd = ::mkstemp(temporaryBuffer.data());
    if (temporaryFd < 0) {
        xmlFreeDoc(document);
        result.errorKey = "xml.temp_create_failed";
        result.errorDetail = parent.string();
        return result;
    }
    const std::string temporaryPath(temporaryBuffer.data());

    xmlSaveCtxtPtr saveContext = xmlSaveToFd(temporaryFd, "UTF-8", XML_SAVE_FORMAT);
    if (saveContext == nullptr) {
        ::close(temporaryFd);
        xmlFreeDoc(document);
        std::filesystem::remove(temporaryPath, filesystemError);
        result.errorKey = "xml.save_context_failed";
        return result;
    }
    const long written = xmlSaveDoc(saveContext, document);
    const int closeResult = xmlSaveClose(saveContext);
    xmlFreeDoc(document);

    if (written < 0 || closeResult < 0) {
        std::filesystem::remove(temporaryPath, filesystemError);
        result.errorKey = "xml.write_failed";
        result.errorDetail = temporaryPath;
        return result;
    }

    std::string verifyError;
    if (!validateFile(temporaryPath, &verifyError)) {
        std::filesystem::remove(temporaryPath, filesystemError);
        result.errorKey = "xml.verify_error";
        result.errorDetail = verifyError;
        return result;
    }

    std::filesystem::rename(temporaryPath, target, filesystemError);
    if (filesystemError) {
        const std::string renameError = filesystemError.message();
        std::filesystem::remove(temporaryPath, filesystemError);
        result.errorKey = "xml.rename_failed";
        result.errorDetail = target.string();
        result.errorDetail2 = renameError;
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace gpu_report

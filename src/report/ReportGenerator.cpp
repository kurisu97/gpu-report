#include "report/ReportGenerator.hpp"

#include "core/Strings.hpp"
#include "core/Version.hpp"
#include "gpu/Codecs.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <sys/ioctl.h>
#include <unistd.h>

namespace gpu_report {

namespace {

constexpr const char* kReset = "\033[0m";
constexpr const char* kGreen = "\033[32m";
constexpr const char* kRed = "\033[31m";
constexpr const char* kYellow = "\033[33m";
constexpr const char* kGray = "\033[90m";
constexpr const char* kCyan = "\033[1;36m";
constexpr const char* kBold = "\033[1m";

enum class CellColor {
    None,
    Green,
    Red,
    Yellow,
    Gray
};

struct TableCell {
    std::string text;
    CellColor color = CellColor::None;
};

using TableRow = std::vector<TableCell>;

std::size_t effectiveWidth(const ReportOptions& options)
{
    std::size_t width = options.width;
    if (width == 0) {
        width = ReportGenerator::detectTerminalWidth();
    }
    return std::max<std::size_t>(40, std::min<std::size_t>(width, 240));
}

std::string colorText(const std::string& text, const char* color, bool enabled)
{
    if (!enabled) {
        return text;
    }
    return std::string(color) + text + kReset;
}

std::string cellColorCode(CellColor color)
{
    switch (color) {
    case CellColor::Green:
        return kGreen;
    case CellColor::Red:
        return kRed;
    case CellColor::Yellow:
        return kYellow;
    case CellColor::Gray:
        return kGray;
    case CellColor::None:
    default:
        return {};
    }
}

std::vector<std::size_t> tableWidths(const std::vector<std::string>& headers,
                                     const std::vector<TableRow>& rows,
                                     std::size_t width)
{
    const std::size_t columns = headers.size();
    std::vector<std::size_t> natural(columns, 0);
    for (std::size_t i = 0; i < columns; ++i) {
        natural[i] = displayWidth(headers[i]);
    }
    for (const TableRow& row : rows) {
        for (std::size_t i = 0; i < columns && i < row.size(); ++i) {
            natural[i] = std::max(natural[i], displayWidth(row[i].text));
        }
    }

    const std::size_t gaps = columns > 1 ? (columns - 1) * 2 : 0;
    const std::size_t available = width > gaps ? width - gaps : width;

    std::size_t total = 0;
    for (std::size_t value : natural) {
        total += value;
    }
    if (total <= available) {
        natural.back() += available - total;
        return natural;
    }

    std::vector<std::size_t> widths = natural;
    std::size_t excess = total - available;
    const std::size_t minimumLast = 8;

    if (widths.back() > minimumLast) {
        const std::size_t take = std::min(excess, widths.back() - minimumLast);
        widths.back() -= take;
        excess -= take;
    }
    for (std::size_t i = columns - 1; i-- > 0 && excess > 0;) {
        const std::size_t minimum = 6;
        if (widths[i] > minimum) {
            const std::size_t take = std::min(excess, widths[i] - minimum);
            widths[i] -= take;
            excess -= take;
        }
    }
    if (excess > 0 && !widths.empty()) {
        widths.back() = width > gaps + 1 ? std::max<std::size_t>(1, available) : widths.back();
    }
    return widths;
}

std::vector<std::string> renderTable(const std::vector<std::string>& headers,
                                     const std::vector<TableRow>& rows,
                                     std::size_t width, bool color)
{
    std::vector<std::string> lines;
    if (headers.empty()) {
        return lines;
    }
    const std::vector<std::size_t> widths = tableWidths(headers, rows, width);

    auto renderRow = [&](const std::vector<TableCell>& cells) {
        std::string line;
        for (std::size_t i = 0; i < widths.size(); ++i) {
            const std::string text = i < cells.size()
                ? truncateDisplay(cells[i].text, widths[i])
                : std::string();
            if (i != 0) {
                line += "  ";
            }
            line += padDisplay(text, widths[i]);
            const CellColor cellColor = i < cells.size() ? cells[i].color : CellColor::None;
            if (color && cellColor != CellColor::None) {
                const std::string code = cellColorCode(cellColor);
                if (!code.empty()) {
                    const std::string padded = padDisplay(text, widths[i]);
                    line.erase(line.size() - padded.size());
                    line += code + text + kReset
                        + std::string(widths[i] - displayWidth(text), ' ');
                }
            }
        }
        lines.push_back(line);
    };

    std::vector<TableCell> headerCells;
    headerCells.reserve(headers.size());
    for (const std::string& header : headers) {
        headerCells.push_back({header, CellColor::None});
    }
    renderRow(headerCells);

    std::string underline;
    for (std::size_t i = 0; i < widths.size(); ++i) {
        if (i != 0) {
            underline += "  ";
        }
        underline += std::string(widths[i], '-');
    }
    lines.push_back(underline);

    for (const TableRow& row : rows) {
        renderRow(row);
    }
    return lines;
}

void appendTable(std::vector<std::string>& lines, const std::vector<std::string>& table)
{
    lines.insert(lines.end(), table.begin(), table.end());
}

std::string joinOr(const std::vector<std::string>& values, const std::string& separator,
                   const std::string& fallback)
{
    if (values.empty()) {
        return fallback;
    }
    return join(values, separator);
}

std::string nonEmptyOr(const std::string& value, const std::string& fallback)
{
    return trim(value).empty() ? fallback : value;
}

std::string intListOr(const std::vector<int>& values, const std::string& fallback)
{
    if (values.empty()) {
        return fallback;
    }
    std::vector<std::string> strings;
    strings.reserve(values.size());
    for (int value : values) {
        strings.push_back(std::to_string(value));
    }
    return join(strings, ", ");
}

CellColor triStateColor(TriState value)
{
    switch (value) {
    case TriState::Yes:
        return CellColor::Green;
    case TriState::No:
        return CellColor::Red;
    case TriState::Unknown:
    default:
        return CellColor::Yellow;
    }
}

CellColor diagnosticColor(const std::string& status)
{
    if (status == "available") {
        return CellColor::Green;
    }
    if (status == "unavailable" || status == "error" || status == "timeout") {
        return CellColor::Red;
    }
    if (status == "skipped") {
        return CellColor::Yellow;
    }
    return CellColor::Gray;
}

std::string heading(const std::string& text, bool color)
{
    return colorText(text, kCyan, color);
}

std::string subheading(const std::string& text, bool color)
{
    return colorText(text, kBold, color);
}

void appendHeading(std::vector<std::string>& lines, const std::string& text, bool color)
{
    lines.push_back(heading(text, color));
    lines.push_back(std::string(displayWidth(text), '-'));
}

std::vector<std::string> diagnosticsTable(const std::vector<DiagnosticCheck>& checks,
                                          const Localization& localization,
                                          const ReportOptions& options)
{
    std::vector<std::string> lines;
    if (checks.empty()) {
        lines.push_back(localization.tr("report.no_checks"));
        return lines;
    }
    std::vector<TableRow> rows;
    rows.reserve(checks.size());
    for (const DiagnosticCheck& check : checks) {
        TableCell name;
        name.text = check.interfaceKey.empty()
            ? check.interfaceName
            : localization.tr(check.interfaceKey);
        if (name.text == check.interfaceKey && !check.interfaceName.empty()) {
            name.text = check.interfaceName;
        }
        TableCell status;
        status.text = ReportGenerator::localizedDiagnosticStatus(check.status, localization);
        status.color = diagnosticColor(check.status);
        TableCell detail;
        detail.text = ReportGenerator::localizedCheckDetail(check, localization);
        rows.push_back({name, status, detail});
    }
    appendTable(lines, renderTable({localization.tr("common.interface"),
                                    localization.tr("report.check_status"),
                                    localization.tr("report.check_detail")},
                                   rows, effectiveWidth(options), options.color));
    return lines;
}

std::vector<std::string> encoderTable(const std::vector<EncoderInfo>& entries,
                                      const char* firstColumnKey,
                                      const Localization& localization,
                                      const ReportOptions& options)
{
    std::vector<std::string> lines;
    if (entries.empty()) {
        lines.push_back(localization.tr("common.none"));
        return lines;
    }
    std::vector<TableRow> rows;
    rows.reserve(entries.size());
    for (const EncoderInfo& entry : entries) {
        TableCell available;
        available.text = ReportGenerator::localizedStatus(
            entry.availableInFfmpeg ? TriState::Yes : TriState::No, localization);
        available.color = entry.availableInFfmpeg ? CellColor::Green : CellColor::Red;
        rows.push_back({{entry.name, CellColor::None},
                        {entry.api, CellColor::None},
                        {entry.codec, CellColor::None},
                        available});
    }
    appendTable(lines, renderTable({localization.tr(firstColumnKey),
                                    localization.tr("common.api"),
                                    localization.tr("common.codec"),
                                    localization.tr("report.available_in_ffmpeg")},
                                   rows, effectiveWidth(options), options.color));
    for (const EncoderInfo& entry : entries) {
        if (!entry.ffmpegPixelFormats.empty()) {
            lines.push_back("  " + entry.name + " - "
                            + localization.tr("report.ffmpeg_pixel_formats") + ": "
                            + join(entry.ffmpegPixelFormats, ", "));
        }
    }
    return lines;
}

std::vector<std::string> codecTable(const GPUInfo& gpu, const Localization& localization,
                                    const ReportOptions& options)
{
    std::vector<TableRow> rows;
    rows.reserve(gpu.codecs.size());
    for (const CodecCapability& capability : gpu.codecs) {
        TableCell codec;
        codec.text = capability.codec;
        TableCell decode;
        decode.text = ReportGenerator::localizedStatus(capability.decode.value, localization);
        decode.color = triStateColor(capability.decode.value);
        TableCell encode;
        encode.text = ReportGenerator::localizedStatus(capability.encode.value, localization);
        encode.color = triStateColor(capability.encode.value);
        TableCell comment;
        comment.text = localization.codecComment(capability.codec);
        rows.push_back({codec, decode, encode, comment});
    }

    std::vector<std::string> lines = renderTable(
        {localization.tr("common.codec"), localization.tr("common.decode"),
         localization.tr("common.encode"), localization.tr("common.comment")},
        rows, effectiveWidth(options), options.color);

    for (const CodecCapability& capability : gpu.codecs) {
        const bool hasDetails = !capability.profiles.empty() || !capability.bitDepths.empty()
            || !capability.pixelFormats.empty();
        if (!hasDetails
            || (capability.decode.value != TriState::Yes
                && capability.encode.value != TriState::Yes)) {
            continue;
        }
        std::vector<std::string> parts;
        if (!capability.profiles.empty()) {
            parts.push_back(localization.tr("common.profiles") + ": "
                            + join(capability.profiles, ", "));
        }
        if (!capability.bitDepths.empty()) {
            parts.push_back(localization.tr("common.bit_depths") + ": "
                            + intListOr(capability.bitDepths, localization.tr("common.unknown")));
        }
        if (!capability.pixelFormats.empty()) {
            parts.push_back(localization.tr("common.pixel_formats") + ": "
                            + join(capability.pixelFormats, ", "));
        }
        lines.push_back("  " + capability.codec + " - " + join(parts, "; "));
    }
    return lines;
}

std::vector<std::string> sourceLines(const GPUInfo& gpu, const Localization& localization)
{
    std::vector<std::string> lines;
    for (const CodecCapability& capability : gpu.codecs) {
        if (!capability.decode.sources.empty()) {
            lines.push_back("  " + capability.codec + " " + localization.tr("common.decode")
                            + " [" + ReportGenerator::localizedStatus(capability.decode.value,
                                                                       localization)
                            + "]: " + join(capability.decode.sources, "; "));
        }
        if (!capability.encode.sources.empty()) {
            lines.push_back("  " + capability.codec + " " + localization.tr("common.encode")
                            + " [" + ReportGenerator::localizedStatus(capability.encode.value,
                                                                       localization)
                            + "]: " + join(capability.encode.sources, "; "));
        }
    }
    return lines;
}

std::vector<std::pair<std::string, std::string>> recommendationGroups(const GPUInfo& gpu)
{
    std::vector<std::pair<std::string, std::string>> groups;
    for (const Recommendation& recommendation : gpu.recommendations) {
        const std::pair<std::string, std::string> key(recommendation.codec,
                                                      recommendation.encoder);
        if (std::find(groups.begin(), groups.end(), key) == groups.end()) {
            groups.push_back(key);
        }
    }
    return groups;
}

std::vector<std::string> recommendationLines(const GPUInfo& gpu,
                                             const Localization& localization,
                                             const ReportOptions& options)
{
    std::vector<std::string> lines;
    if (gpu.recommendations.empty()) {
        lines.push_back(localization.tr("report.no_confirmed_encoder"));
        lines.push_back(localization.tr("report.encoder_note"));
        return lines;
    }

    const std::size_t width = effectiveWidth(options);
    for (const auto& group : recommendationGroups(gpu)) {
        lines.push_back(subheading(localization.tr("rec.codec_heading",
                                                   {group.first, group.second}),
                                   options.color));
        std::vector<TableRow> rows;
        const std::string* example = nullptr;
        for (const Recommendation& recommendation : gpu.recommendations) {
            if (recommendation.codec != group.first || recommendation.encoder != group.second) {
                continue;
            }
            rows.push_back({{recommendation.resolution, CellColor::None},
                            {std::to_string(recommendation.fps), CellColor::None},
                            {recommendation.bitrate, CellColor::None}});
            if (example == nullptr
                || (recommendation.resolution == "1920x1080" && recommendation.fps == 30)) {
                example = &recommendation.command;
            }
        }
        appendTable(lines, renderTable({localization.tr("common.resolution"),
                                        localization.tr("common.fps"),
                                        localization.tr("common.bitrate")},
                                       rows, width, options.color));
        if (example != nullptr && !example->empty()) {
            lines.push_back(localization.tr("rec.example") + ":");
            lines.push_back("  " + *example);
        }
        lines.push_back("");
    }

    lines.push_back(localization.tr("rec.note"));
    lines.push_back(localization.tr("report.bitrate_note"));
    lines.push_back(localization.tr("report.bitrate_basis"));
    lines.push_back(localization.tr("rec.quality_note"));

    bool legacy = false;
    for (const Recommendation& recommendation : gpu.recommendations) {
        if (recommendation.codec == codecs::kVP8 || recommendation.codec == codecs::kMPEG2
            || recommendation.codec == codecs::kVC1) {
            legacy = true;
            break;
        }
    }
    if (legacy) {
        lines.push_back(localization.tr("rec.legacy_note"));
    }
    return lines;
}

void appendGpuBody(std::vector<std::string>& lines, const GPUInfo& gpu,
                   const Localization& localization, const ReportOptions& options)
{
    const std::string unknown = localization.tr("common.unknown");
    const std::string none = localization.tr("common.none");
    const std::string notAvailable = localization.tr("common.not_available");

    const std::string vendor = !trim(gpu.vendorName).empty()
        ? gpu.vendorName
        : (gpu.vendor == GPUVendor::Unknown ? localization.tr("common.unknown")
                                            : vendorDisplayName(gpu.vendor));
    lines.push_back(localization.tr("common.vendor") + ": " + vendor);
    if (!gpu.pciDeviceName.empty()) {
        lines.push_back(localization.tr("common.pci_device_name") + ": "
                        + gpu.pciDeviceName);
    }
    lines.push_back(localization.tr("common.vendor_id") + ": " + hexValue(gpu.vendorId));
    lines.push_back(localization.tr("common.device_id") + ": " + hexValue(gpu.deviceId));
    lines.push_back(localization.tr("common.pci_path") + ": "
                    + nonEmptyOr(gpu.pciPath, notAvailable));
    lines.push_back(localization.tr("common.driver") + ": "
                    + nonEmptyOr(gpu.driver, notAvailable));
    lines.push_back(localization.tr("common.driver_version") + ": "
                    + nonEmptyOr(gpu.driverVersion, unknown));
    lines.push_back(localization.tr("common.render_nodes") + ": "
                    + joinOr(gpu.renderNodes, ", ", none));
    lines.push_back(localization.tr("common.drm_cards") + ": "
                    + joinOr(gpu.drmCards, ", ", none));
    lines.push_back(localization.tr("report.type") + ": "
                    + ReportGenerator::localizedAdapterType(gpu.integrated, localization));
    lines.push_back(localization.tr("report.gpu_status") + ": "
                    + ReportGenerator::localizedDetectionStatus(gpu.status, localization));
    if (!gpu.nameSource.empty()) {
        lines.push_back(localization.tr("report.name_source") + ": " + gpu.nameSource);
    }
    if (!gpu.vulkanDeviceName.empty()) {
        lines.push_back(localization.tr("report.vulkan_device") + ": "
                        + gpu.vulkanDeviceName);
    }

    lines.push_back("");
    appendHeading(lines, localization.tr("report.diagnostics"), options.color);
    appendTable(lines, diagnosticsTable(gpu.checks, localization, options));

    lines.push_back("");
    appendHeading(lines, localization.tr("report.capabilities"), options.color);
    appendTable(lines, codecTable(gpu, localization, options));

    const std::vector<std::string> sources = sourceLines(gpu, localization);
    if (!sources.empty()) {
        lines.push_back("");
        appendHeading(lines, localization.tr("report.sources"), options.color);
        lines.insert(lines.end(), sources.begin(), sources.end());
    }

    lines.push_back("");
    appendHeading(lines, localization.tr("report.hw_encoders"), options.color);
    appendTable(lines, encoderTable(gpu.encoders, "common.encoder", localization, options));

    lines.push_back("");
    appendHeading(lines, localization.tr("report.hw_decoders"), options.color);
    appendTable(lines, encoderTable(gpu.decoders, "common.decoder", localization, options));

    lines.push_back("");
    appendHeading(lines, localization.tr("report.recommendations"), options.color);
    appendTable(lines, recommendationLines(gpu, localization, options));

    lines.push_back("");
    appendHeading(lines, localization.tr("report.notes"), options.color);
    lines.push_back("- " + localization.tr("report.reliability_note"));
    lines.push_back("- " + localization.tr("report.encoder_note"));
    lines.push_back("- " + localization.tr("report.bitrate_note"));
    lines.push_back("- " + localization.tr("report.export_hint"));
}

} // namespace

std::size_t ReportGenerator::detectTerminalWidth()
{
    struct winsize size {};
    if (isatty(STDOUT_FILENO) && ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0
        && size.ws_col > 0) {
        return size.ws_col;
    }
    const char* columns = std::getenv("COLUMNS");
    if (columns != nullptr) {
        int value = 0;
        if (parseInt(columns, value) && value > 0) {
            return static_cast<std::size_t>(value);
        }
    }
    return 100;
}

std::string ReportGenerator::localizedStatus(TriState value,
                                             const Localization& localization)
{
    switch (value) {
    case TriState::Yes:
        return localization.tr("common.yes");
    case TriState::No:
        return localization.tr("common.no");
    case TriState::Unknown:
    default:
        return localization.tr("common.unknown_status");
    }
}

std::string ReportGenerator::localizedDetectionStatus(const std::string& status,
                                                      const Localization& localization)
{
    if (status == "detected") {
        return localization.tr("status.detected");
    }
    if (status == "no_driver") {
        return localization.tr("status.no_driver");
    }
    if (status == "no_drm_node") {
        return localization.tr("status.no_drm_node");
    }
    if (status == "error") {
        return localization.tr("status.error");
    }
    return status.empty() ? localization.tr("common.unknown") : status;
}

std::string ReportGenerator::localizedCheckDetail(const DiagnosticCheck& check,
                                                  const Localization& localization)
{
    if (check.detailKey.empty()) {
        return check.detail;
    }
    std::string translated = localization.tr(check.detailKey, check.detailArguments);
    // Missing catalogue entries returned unchanged; fall back to the raw
    // technical detail rather than showing a key.
    if (translated == check.detailKey) {
        return check.detail;
    }
    return translated;
}

std::string ReportGenerator::localizedDiagnosticStatus(const std::string& status,
                                                       const Localization& localization)
{
    if (status == "available") {
        return localization.tr("diag.status.available");
    }
    if (status == "unavailable") {
        return localization.tr("diag.status.unavailable");
    }
    if (status == "error") {
        return localization.tr("diag.status.error");
    }
    if (status == "timeout") {
        return localization.tr("diag.status.timeout");
    }
    if (status == "skipped") {
        return localization.tr("diag.status.skipped");
    }
    if (status == "not-applicable" || status == "not_applicable") {
        return localization.tr("diag.status.not_applicable");
    }
    return status.empty() ? localization.tr("common.unknown") : status;
}

std::string ReportGenerator::localizedAdapterType(TriState integrated,
                                                  const Localization& localization)
{
    switch (integrated) {
    case TriState::Yes:
        return localization.tr("report.type.integrated");
    case TriState::No:
        return localization.tr("report.type.discrete");
    case TriState::Unknown:
    default:
        return localization.tr("report.type.unknown");
    }
}

std::string ReportGenerator::generateGpuReport(const GPUInfo& gpu, std::size_t index,
                                               std::size_t total,
                                               const Localization& localization,
                                               const ReportOptions& options)
{
    std::vector<std::string> lines;
    lines.push_back(localization.tr("common.gpu_counter",
                                    {std::to_string(index), std::to_string(total)}));
    const std::string header = nonEmptyOr(gpu.name, localization.tr("common.unknown"))
        + " : " + nonEmptyOr(gpu.driver, localization.tr("common.unknown"))
        + " : " + nonEmptyOr(gpu.pciPath, localization.tr("common.unknown"));
    lines.push_back(subheading(header, options.color));
    lines.push_back(std::string(std::min<std::size_t>(displayWidth(header),
                                                      effectiveWidth(options)), '='));
    lines.push_back("");
    appendGpuBody(lines, gpu, localization, options);

    std::string result;
    for (const std::string& line : lines) {
        result += line;
        result += "\n";
    }
    return result;
}

std::string ReportGenerator::generateInventoryReport(const Inventory& inventory,
                                                     const Localization& localization,
                                                     const ReportOptions& options)
{
    std::vector<std::string> lines;
    lines.push_back(heading(localization.tr("report.title"), options.color));
    lines.push_back(std::string(displayWidth(localization.tr("report.title")), '='));
    lines.push_back("");

    lines.push_back(localization.tr("common.generated") + ": " + inventory.generatedAt);
    lines.push_back(localization.tr("common.program_version") + ": " + kProgramVersion);
    lines.push_back(localization.tr("common.language") + ": "
                    + localization.tr(languageNameKey(localization.language())));
    lines.push_back(localization.tr("common.hostname") + ": "
                    + nonEmptyOr(inventory.host.hostname, localization.tr("common.unknown")));
    lines.push_back(localization.tr("common.kernel") + ": "
                    + nonEmptyOr(inventory.host.kernel, localization.tr("common.unknown")));
    lines.push_back(localization.tr("common.architecture") + ": "
                    + nonEmptyOr(inventory.host.architecture,
                                 localization.tr("common.unknown")));
    lines.push_back(localization.tr("common.distribution") + ": "
                    + nonEmptyOr(inventory.host.distribution,
                                 localization.tr("common.unknown")));
    lines.push_back(localization.tr("report.gpu_count",
                                    {std::to_string(inventory.gpus.size())}));
    lines.push_back("");

    appendHeading(lines, localization.tr("report.diagnostics"), options.color);
    appendTable(lines, diagnosticsTable(inventory.globalChecks, localization, options));
    lines.push_back("");

    if (inventory.gpus.empty()) {
        lines.push_back(localization.tr("report.no_gpus"));
    } else {
        for (std::size_t i = 0; i < inventory.gpus.size(); ++i) {
            const std::string gpuReport = generateGpuReport(inventory.gpus[i], i + 1,
                                                            inventory.gpus.size(),
                                                            localization, options);
            std::vector<std::string> gpuLines = splitLines(gpuReport);
            while (!gpuLines.empty() && gpuLines.back().empty()) {
                gpuLines.pop_back();
            }
            lines.insert(lines.end(), gpuLines.begin(), gpuLines.end());
            if (i + 1 < inventory.gpus.size()) {
                lines.push_back("");
            }
        }
    }

    std::string result;
    for (const std::string& line : lines) {
        result += line;
        result += "\n";
    }
    return result;
}

std::string ReportGenerator::generateGpuList(const std::vector<GPUInfo>& gpus,
                                             const Localization& localization,
                                             const ReportOptions& options)
{
    std::vector<std::string> lines;
    lines.push_back(heading(localization.tr("list.title"), options.color));
    lines.push_back(std::string(displayWidth(localization.tr("list.title")), '='));
    lines.push_back(localization.tr("list.count", {std::to_string(gpus.size())}));
    lines.push_back("");

    if (gpus.empty()) {
        lines.push_back(localization.tr("list.empty"));
    } else {
        const std::string notAvailable = localization.tr("common.not_available");
        const std::string none = localization.tr("common.none");
        for (std::size_t i = 0; i < gpus.size(); ++i) {
            const GPUInfo& gpu = gpus[i];
            lines.push_back("[" + std::to_string(i) + "] "
                            + nonEmptyOr(gpu.name, localization.tr("common.unknown")));
            const std::string vendor = !trim(gpu.vendorName).empty()
                ? gpu.vendorName
                : (gpu.vendor == GPUVendor::Unknown ? localization.tr("common.unknown")
                                                    : vendorDisplayName(gpu.vendor));
            lines.push_back("  " + localization.tr("common.vendor") + ": " + vendor);
            lines.push_back("  " + localization.tr("common.vendor_id") + ": "
                            + hexValue(gpu.vendorId));
            lines.push_back("  " + localization.tr("common.device_id") + ": "
                            + hexValue(gpu.deviceId));
            lines.push_back("  " + localization.tr("common.pci_path") + ": "
                            + nonEmptyOr(gpu.pciPath, notAvailable));
            lines.push_back("  " + localization.tr("common.driver") + ": "
                            + nonEmptyOr(gpu.driver, notAvailable));
            lines.push_back("  " + localization.tr("common.driver_version") + ": "
                            + nonEmptyOr(gpu.driverVersion,
                                         localization.tr("common.unknown")));
            lines.push_back("  " + localization.tr("common.render_nodes") + ": "
                            + joinOr(gpu.renderNodes, ", ", none));
            lines.push_back("  " + localization.tr("common.status") + ": "
                            + localizedDetectionStatus(gpu.status, localization));
            if (i + 1 < gpus.size()) {
                lines.push_back("");
            }
        }
    }

    std::string result;
    for (const std::string& line : lines) {
        result += line;
        result += "\n";
    }
    return result;
}

} // namespace gpu_report

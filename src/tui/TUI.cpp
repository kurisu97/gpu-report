#include "tui/TUI.hpp"

#include "core/Strings.hpp"
#include "gpu/Codecs.hpp"
#include "report/ReportGenerator.hpp"
#include "xml/XMLExporter.hpp"

#include <algorithm>
#include <cstdio>
#include <ncurses.h>
#include <vector>

namespace gpu_report {

namespace {

constexpr int kPairHeader = 1;
constexpr int kPairYes = 2;
constexpr int kPairNo = 3;
constexpr int kPairUnknown = 4;
constexpr int kPairHeading = 5;
constexpr int kPairError = 6;

constexpr int kMinimumColumns = 40;
constexpr int kMinimumLines = 10;

struct Segment {
    std::string text;
    int pair = 0;
    bool bold = false;
};

using Line = std::vector<Segment>;

void addText(Line& line, const std::string& text, int pair = 0, bool bold = false)
{
    line.push_back({text, pair, bold});
}

void addLine(std::vector<Line>& lines, const std::string& text, int pair = 0,
             bool bold = false)
{
    Line line;
    addText(line, text, pair, bold);
    lines.push_back(std::move(line));
}

void addHeading(std::vector<Line>& lines, const std::string& text)
{
    addLine(lines, text, kPairHeading, true);
    lines.push_back({{std::string(displayWidth(text), '-'), 0, false}});
}

int statusPair(TriState value)
{
    switch (value) {
    case TriState::Yes:
        return kPairYes;
    case TriState::No:
        return kPairNo;
    case TriState::Unknown:
    default:
        return kPairUnknown;
    }
}

int diagnosticPair(const std::string& status)
{
    if (status == "available") {
        return kPairYes;
    }
    if (status == "unavailable" || status == "error" || status == "timeout") {
        return kPairNo;
    }
    if (status == "skipped") {
        return kPairUnknown;
    }
    return 0;
}

void addLabelValue(std::vector<Line>& lines, const Localization& localization,
                   const std::string& key, const std::string& value)
{
    Line line;
    addText(line, localization.tr(key) + ": ", 0, true);
    addText(line, value);
    lines.push_back(std::move(line));
}

std::string stringListOr(const std::vector<std::string>& values, const std::string& fallback)
{
    return values.empty() ? fallback : join(values, ", ");
}

std::vector<Line> buildOverview(const Inventory& inventory, const Localization& localization,
                                std::size_t selected, int columns)
{
    std::vector<Line> lines;
    if (inventory.gpus.empty()) {
        addLine(lines, localization.tr("tui.no_gpus"), kPairUnknown);
        return lines;
    }
    const GPUInfo& gpu = inventory.gpus[selected];
    const std::string unknown = localization.tr("common.unknown");
    const std::string none = localization.tr("common.none");

    const std::string headerLine = (gpu.name.empty() ? unknown : gpu.name)
        + " : " + (gpu.driver.empty() ? unknown : gpu.driver)
        + " : " + (gpu.pciPath.empty() ? unknown : gpu.pciPath);
    addLine(lines, headerLine, kPairHeader, true);
    lines.push_back({});

    const std::string vendor = !trim(gpu.vendorName).empty()
        ? gpu.vendorName
        : (gpu.vendor == GPUVendor::Unknown ? localization.tr("common.unknown")
                                            : vendorDisplayName(gpu.vendor));
    addLabelValue(lines, localization, "common.vendor", vendor);
    addLabelValue(lines, localization, "common.vendor_id", hexValue(gpu.vendorId));
    addLabelValue(lines, localization, "common.device_id", hexValue(gpu.deviceId));
    addLabelValue(lines, localization, "report.type",
                  ReportGenerator::localizedAdapterType(gpu.integrated, localization));
    addLabelValue(lines, localization, "common.driver_version",
                  gpu.driverVersion.empty() ? unknown : gpu.driverVersion);
    addLabelValue(lines, localization, "common.render_nodes",
                  stringListOr(gpu.renderNodes, none));
    addLabelValue(lines, localization, "report.gpu_status",
                  ReportGenerator::localizedDetectionStatus(gpu.status, localization));
    if (!gpu.vulkanDeviceName.empty()) {
        addLabelValue(lines, localization, "report.vulkan_device", gpu.vulkanDeviceName);
    }
    lines.push_back({});

    addHeading(lines, localization.tr("report.capabilities"));

    const std::string codecHeader = localization.tr("common.codec");
    const std::string decodeHeader = localization.tr("common.decode");
    const std::string encodeHeader = localization.tr("common.encode");

    int codecWidth = std::max<int>(10, static_cast<int>(displayWidth(codecHeader)));
    for (const CodecCapability& capability : gpu.codecs) {
        codecWidth = std::max<int>(codecWidth,
                                   static_cast<int>(displayWidth(capability.codec)));
    }
    codecWidth = std::min(codecWidth, 22);

    int statusWidth = std::max<int>(7, static_cast<int>(displayWidth(decodeHeader)));
    statusWidth = std::max<int>(statusWidth, static_cast<int>(displayWidth(encodeHeader)));
    statusWidth = std::max<int>(statusWidth,
                                static_cast<int>(displayWidth(localization.tr("common.unknown_status"))));
    statusWidth = std::min(statusWidth, 12);

    int commentWidth = columns - codecWidth - 2 * statusWidth - 6;
    if (commentWidth < 6) {
        const int deficit = 6 - commentWidth;
        codecWidth = std::max(8, codecWidth - deficit);
        commentWidth = columns - codecWidth - 2 * statusWidth - 6;
    }

    Line header;
    addText(header, padDisplay(truncateDisplay(codecHeader, codecWidth), codecWidth), 0, true);
    addText(header, "  ");
    addText(header, padDisplay(truncateDisplay(decodeHeader, statusWidth), statusWidth), 0, true);
    addText(header, "  ");
    addText(header, padDisplay(truncateDisplay(encodeHeader, statusWidth), statusWidth), 0, true);
    addText(header, "  ");
    addText(header, truncateDisplay(localization.tr("common.comment"),
                                    std::max(1, commentWidth)), 0, true);
    lines.push_back(std::move(header));

    std::string underline(codecWidth + 2 * statusWidth + 6 + std::max(1, commentWidth), '-');
    addLine(lines, truncateDisplay(underline, std::max(1, columns)));

    for (const CodecCapability& capability : gpu.codecs) {
        Line row;
        addText(row, padDisplay(truncateDisplay(capability.codec, codecWidth), codecWidth));
        addText(row, "  ");
        addText(row, padDisplay(ReportGenerator::localizedStatus(capability.decode.value,
                                                                 localization),
                                statusWidth),
                statusPair(capability.decode.value), true);
        addText(row, "  ");
        addText(row, padDisplay(ReportGenerator::localizedStatus(capability.encode.value,
                                                                 localization),
                                statusWidth),
                statusPair(capability.encode.value), true);
        addText(row, "  ");
        addText(row, truncateDisplay(localization.codecComment(capability.codec),
                                     std::max(1, commentWidth)));
        lines.push_back(std::move(row));
    }

    lines.push_back({});
    addLine(lines, localization.tr("tui.interface_note"), kPairHeading);
    return lines;
}

std::vector<Line> buildDetails(const Inventory& inventory, const Localization& localization,
                               std::size_t selected, int columns)
{
    std::vector<Line> lines;
    if (inventory.gpus.empty()) {
        addLine(lines, localization.tr("tui.no_gpus"), kPairUnknown);
        return lines;
    }
    const GPUInfo& gpu = inventory.gpus[selected];
    addLine(lines, gpu.name + " : " + gpu.driver + " : " + gpu.pciPath, kPairHeader, true);
    lines.push_back({});

    addHeading(lines, localization.tr("report.diagnostics"));
    if (gpu.checks.empty()) {
        addLine(lines, localization.tr("report.no_checks"));
    } else {
        int nameWidth = std::max<int>(12, static_cast<int>(displayWidth(
            localization.tr("common.interface"))));
        for (const DiagnosticCheck& check : gpu.checks) {
            const std::string name = check.interfaceKey.empty()
                ? check.interfaceName
                : localization.tr(check.interfaceKey);
            nameWidth = std::max<int>(nameWidth, static_cast<int>(displayWidth(name)));
        }
        nameWidth = std::min(nameWidth, std::max(12, columns / 3));
        const int statusWidth = std::max<int>(8, static_cast<int>(displayWidth(
            localization.tr("diag.status.not_applicable"))));
        const int detailWidth = std::max(8, columns - nameWidth - statusWidth - 4);

        Line header;
        addText(header, padDisplay(localization.tr("common.interface"), nameWidth), 0, true);
        addText(header, "  ");
        addText(header, padDisplay(localization.tr("report.check_status"), statusWidth), 0, true);
        addText(header, "  ");
        addText(header, truncateDisplay(localization.tr("report.check_detail"), detailWidth),
                0, true);
        lines.push_back(std::move(header));
        addLine(lines, std::string(std::min(columns, nameWidth + statusWidth + detailWidth + 4),
                                   '-'));

        for (const DiagnosticCheck& check : gpu.checks) {
            const std::string name = check.interfaceKey.empty()
                ? check.interfaceName
                : localization.tr(check.interfaceKey);
            Line row;
            addText(row, padDisplay(truncateDisplay(name, nameWidth), nameWidth));
            addText(row, "  ");
            addText(row, padDisplay(ReportGenerator::localizedDiagnosticStatus(check.status,
                                                                               localization),
                                    statusWidth),
                    diagnosticPair(check.status), true);
            addText(row, "  ");
            addText(row, truncateDisplay(
                              ReportGenerator::localizedCheckDetail(check, localization),
                              detailWidth));
            lines.push_back(std::move(row));
        }
    }

    bool hasSources = false;
    for (const CodecCapability& capability : gpu.codecs) {
        if (!capability.decode.sources.empty() || !capability.encode.sources.empty()) {
            hasSources = true;
            break;
        }
    }
    if (hasSources) {
        lines.push_back({});
        addHeading(lines, localization.tr("report.sources"));
        for (const CodecCapability& capability : gpu.codecs) {
            if (!capability.decode.sources.empty()) {
                addLine(lines, "  " + capability.codec + " "
                                + localization.tr("common.decode") + " ["
                                + ReportGenerator::localizedStatus(capability.decode.value,
                                                                   localization)
                                + "]: " + join(capability.decode.sources, "; "));
            }
            if (!capability.encode.sources.empty()) {
                addLine(lines, "  " + capability.codec + " "
                                + localization.tr("common.encode") + " ["
                                + ReportGenerator::localizedStatus(capability.encode.value,
                                                                   localization)
                                + "]: " + join(capability.encode.sources, "; "));
            }
        }
    }

    lines.push_back({});
    addHeading(lines, localization.tr("report.notes"));
    addLine(lines, "- " + localization.tr("report.reliability_note"));
    addLine(lines, "- " + localization.tr("report.encoder_note"));
    addLine(lines, "- " + localization.tr("diag.detail.nvidia_table"));
    return lines;
}

std::vector<Line> buildReport(const Inventory& inventory, const Localization& localization,
                              std::size_t selected, int columns)
{
    std::vector<Line> lines;
    if (inventory.gpus.empty()) {
        addLine(lines, localization.tr("tui.no_gpus"), kPairUnknown);
        return lines;
    }
    ReportOptions options;
    options.color = false;
    options.width = static_cast<std::size_t>(std::max(20, columns));
    const std::string report = ReportGenerator::generateGpuReport(
        inventory.gpus[selected], selected + 1, inventory.gpus.size(), localization, options);
    for (const std::string& text : splitLines(report)) {
        addLine(lines, text);
    }
    while (!lines.empty() && lines.back().empty()) {
        lines.pop_back();
    }
    return lines;
}

std::vector<Line> buildHelp(const Localization& localization)
{
    std::vector<Line> lines;
    addHeading(lines, localization.tr("tui.help_title"));
    for (int item = 1; item <= 8; ++item) {
        addLine(lines, "  " + localization.tr("tui.help.body." + std::to_string(item)));
    }
    lines.push_back({});
    addLine(lines, localization.tr("tui.interface_note"), kPairHeading);
    return lines;
}

int segmentAttribute(const Segment& segment, bool colors)
{
    int attribute = segment.bold ? A_BOLD : 0;
    if (colors && segment.pair > 0) {
        attribute |= COLOR_PAIR(segment.pair);
    }
    return attribute;
}

void drawLine(int row, int column, const Line& line, bool colors, int width)
{
    int x = column;
    for (const Segment& segment : line) {
        if (x >= width) {
            break;
        }
        const std::size_t remaining = static_cast<std::size_t>(width - x);
        const std::string text = truncateDisplay(segment.text, remaining);
        if (text.empty()) {
            continue;
        }
        const int attribute = segmentAttribute(segment, colors);
        if (attribute != 0) {
            attron(attribute);
        }
        mvaddstr(row, x, text.c_str());
        if (attribute != 0) {
            attroff(attribute);
        }
        x += static_cast<int>(displayWidth(text));
    }
}

std::string localizedExportError(const XMLExportResult& result,
                                 const Localization& localization)
{
    if (result.errorKey == "xml.rename_failed") {
        return localization.tr(result.errorKey, {result.errorDetail, result.errorDetail2});
    }
    if (!result.errorDetail.empty()) {
        return localization.tr(result.errorKey, {result.errorDetail});
    }
    return localization.tr(result.errorKey);
}

std::size_t maxScrollFor(const std::vector<Line>& lines, std::size_t bodyRows)
{
    return lines.size() > bodyRows ? lines.size() - bodyRows : 0;
}

} // namespace

TUI::TUI(const Inventory& inventory, const Localization& localization,
         std::string exportPath)
    : inventory_(inventory)
    , localization_(localization)
    , exportPath_(std::move(exportPath))
{
}

int TUI::run()
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(kPairHeader, COLOR_CYAN, -1);
        init_pair(kPairYes, COLOR_GREEN, -1);
        init_pair(kPairNo, COLOR_RED, -1);
        init_pair(kPairUnknown, COLOR_YELLOW, -1);
        init_pair(kPairHeading, COLOR_CYAN, -1);
        init_pair(kPairError, COLOR_RED, -1);
        colors_ = true;
    }

    while (true) {
        draw();
        const int key = getch();
        if (key == ERR || key == KEY_RESIZE) {
            continue;
        }
        if (key == 'q' || key == 'Q') {
            break;
        }
        handleKey(key);
    }

    endwin();
    return 0;
}

std::size_t TUI::bodyHeight() const
{
    const int rows = LINES - 4;
    return rows > 0 ? static_cast<std::size_t>(rows) : 0;
}

void TUI::draw()
{
    erase();

    if (COLS < kMinimumColumns || LINES < kMinimumLines) {
        const std::string message = localization_.tr(
            "tui.small_terminal",
            {std::to_string(COLS), std::to_string(LINES),
             std::to_string(kMinimumColumns), std::to_string(kMinimumLines)});
        const int row = LINES > 1 ? LINES / 2 : 0;
        mvaddstr(row, 0, truncateDisplay(message, std::max(1, COLS)).c_str());
        if (LINES > 0) {
            mvaddstr(LINES - 1, 0, truncateDisplay(localization_.tr("tui.footer_short"),
                                                   std::max(1, COLS)).c_str());
        }
        lastLineCount_ = 0;
        scroll_ = 0;
        refresh();
        return;
    }

    const std::string title = localization_.tr("tui.title");
    const std::string language = localization_.tr(languageNameKey(localization_.language()));
    std::string header = title + "  |  " + localization_.tr("tui.language") + ": " + language;
    attron(A_REVERSE | A_BOLD);
    mvhline(0, 0, ' ', COLS);
    attroff(A_REVERSE | A_BOLD);
    mvaddstr(0, 0, truncateDisplay(header, std::max(1, COLS - 2)).c_str());

    if (!inventory_.gpus.empty()) {
        const std::string counter = localization_.tr(
            "common.gpu_counter",
            {std::to_string(selected_ + 1), std::to_string(inventory_.gpus.size())});
        const int x = COLS - static_cast<int>(displayWidth(counter)) - 1;
        if (x > static_cast<int>(displayWidth(header))) {
            mvaddstr(0, x, counter.c_str());
        }
    }

    const std::vector<std::string> tabs = {
        "1 " + localization_.tr("tui.overview"),
        "2 " + localization_.tr("tui.details"),
        "3 " + localization_.tr("tui.report"),
        "4 " + localization_.tr("tui.help"),
    };
    const View views[] = {View::Overview, View::Details, View::Report, View::Help};
    int x = 1;
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        const std::string label = " " + tabs[i] + " ";
        const int attribute = (view_ == views[i]) ? (A_REVERSE | A_BOLD) : A_BOLD;
        if (x + static_cast<int>(displayWidth(label)) > COLS) {
            break;
        }
        attron(attribute);
        mvaddstr(1, x, label.c_str());
        attroff(attribute);
        x += static_cast<int>(displayWidth(label)) + 1;
    }
    mvhline(2, 0, ACS_HLINE, COLS);

    std::vector<Line> lines;
    switch (view_) {
    case View::Overview:
        lines = buildOverview(inventory_, localization_, selected_, COLS);
        break;
    case View::Details:
        lines = buildDetails(inventory_, localization_, selected_, COLS);
        break;
    case View::Report:
        lines = buildReport(inventory_, localization_, selected_, COLS);
        break;
    case View::Help:
        lines = buildHelp(localization_);
        break;
    }

    const std::size_t rows = bodyHeight();
    const std::size_t maximumScroll = maxScrollFor(lines, rows);
    scroll_ = std::min(scroll_, maximumScroll);
    lastLineCount_ = lines.size();

    for (std::size_t i = 0; i < rows && scroll_ + i < lines.size(); ++i) {
        drawLine(static_cast<int>(3 + i), 0, lines[scroll_ + i], colors_, COLS);
    }

    std::string footer = message_;
    int footerPair = 0;
    if (footer.empty()) {
        footer = localization_.tr(COLS < 92 ? "tui.footer_short" : "tui.footer");
    } else if (messageError_) {
        footerPair = kPairError;
    }
    mvhline(LINES - 1, 0, ' ', COLS);
    const int attribute = colors_ && footerPair > 0 ? COLOR_PAIR(footerPair) : 0;
    if (attribute != 0) {
        attron(attribute);
    }
    mvaddstr(LINES - 1, 0, truncateDisplay(footer, std::max(1, COLS)).c_str());
    if (attribute != 0) {
        attroff(attribute);
    }

    refresh();
}

void TUI::handleKey(int key)
{
    switch (key) {
    case KEY_LEFT:
        changeGpu(-1);
        break;
    case KEY_RIGHT:
    case '\t':
        changeGpu(1);
        break;
    case KEY_UP:
        scrollBy(-1);
        break;
    case KEY_DOWN:
        scrollBy(1);
        break;
    case KEY_PPAGE:
        scrollBy(-static_cast<int>(bodyHeight() > 1 ? bodyHeight() - 1 : 1));
        break;
    case KEY_NPAGE:
        scrollBy(static_cast<int>(bodyHeight() > 1 ? bodyHeight() - 1 : 1));
        break;
    case KEY_HOME:
        scrollTo(0);
        break;
    case KEY_END:
        scrollTo(static_cast<int>(lastLineCount_));
        break;
    case 'd':
    case 'D':
    case '2':
        changeView(View::Details, true);
        break;
    case 'r':
    case 'R':
    case '3':
        changeView(View::Report, true);
        break;
    case '1':
    case 'o':
    case 'O':
    case 'b':
    case 'B':
    case '\b':
    case 127:
    case KEY_BACKSPACE:
    case 27:
        changeView(View::Overview, false);
        break;
    case '4':
    case '?':
    case KEY_F(1):
        changeView(View::Help, true);
        break;
    case 'e':
    case 'E':
        exportXml();
        break;
    default:
        break;
    }
}

void TUI::changeView(View requested, bool toggle)
{
    view_ = toggle && view_ == requested ? View::Overview : requested;
    scroll_ = 0;
    message_.clear();
    messageError_ = false;
}

void TUI::changeGpu(int direction)
{
    if (inventory_.gpus.empty()) {
        return;
    }
    const int count = static_cast<int>(inventory_.gpus.size());
    selected_ = static_cast<std::size_t>(
        (static_cast<int>(selected_) + direction + count) % count);
    scroll_ = 0;
    message_.clear();
    messageError_ = false;
}

void TUI::scrollBy(int amount)
{
    const std::size_t rows = bodyHeight();
    const long maximum = lastLineCount_ > rows
        ? static_cast<long>(lastLineCount_ - rows)
        : 0;
    const long target = static_cast<long>(scroll_) + amount;
    scroll_ = static_cast<std::size_t>(
        std::max<long>(0, std::min<long>(target, maximum)));
}

void TUI::scrollTo(int position)
{
    const std::size_t rows = bodyHeight();
    const std::size_t maximum = lastLineCount_ > rows ? lastLineCount_ - rows : 0;
    const int target = std::max(0, position);
    scroll_ = std::min(static_cast<std::size_t>(target), maximum);
}

void TUI::exportXml()
{
    const XMLExportResult result = XMLExporter::save(inventory_, exportPath_);
    if (result.ok) {
        message_ = localization_.tr("tui.export_success", {result.path});
        messageError_ = false;
    } else {
        message_ = localization_.tr("tui.export_error",
                                    {localizedExportError(result, localization_)});
        messageError_ = true;
    }
}

} // namespace gpu_report

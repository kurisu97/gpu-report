#include "cli/Arguments.hpp"
#include "core/Strings.hpp"
#include "core/SystemInfo.hpp"
#include "core/Version.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "gpu/GPUDetector.hpp"
#include "gpu/PCIDatabase.hpp"
#include "localization/Localization.hpp"
#include "report/Recommendations.hpp"
#include "report/ReportGenerator.hpp"
#include "tui/TUI.hpp"
#include "xml/XMLExporter.hpp"

#include <algorithm>
#include <clocale>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using namespace gpu_report;

void initializeCharacterLocale()
{
    std::setlocale(LC_ALL, "");
    if (MB_CUR_MAX > 1) {
        return;
    }
    if (std::setlocale(LC_ALL, "C.UTF-8") != nullptr) {
        return;
    }
    std::setlocale(LC_ALL, "en_US.UTF-8");
}

std::string describeError(const Localization& localization, const std::string& key,
                          const std::string& detail,
                          const std::string& detail2 = {})
{
    if (key.empty()) {
        return localization.tr("common.unknown");
    }
    if (detail2.empty()) {
        if (!detail.empty()) {
            return localization.tr(key, {detail});
        }
        return localization.tr(key);
    }
    return localization.tr(key, {detail, detail2});
}

void printArgumentError(const Localization& localization, const Arguments& arguments,
                        const std::string& programName)
{
    std::cerr << localization.tr("error.argument",
                                 {describeError(localization, arguments.errorKey,
                                                arguments.errorDetail)}) << "\n";
    std::cerr << localization.tr("error.usage_hint", {programName}) << "\n";
}

bool useColor(const Arguments& arguments, bool stdoutIsTty)
{
    switch (arguments.color) {
    case ColorMode::Always:
        return true;
    case ColorMode::Never:
        return false;
    case ColorMode::Auto:
    default:
        if (!stdoutIsTty) {
            return false;
        }
        {
            const char* term = std::getenv("TERM");
            return term == nullptr || std::strcmp(term, "dumb") != 0;
        }
    }
}

bool gpuMatchesSelector(const GPUInfo& gpu, const std::string& selector, bool& syntaxValid)
{
    const std::string normalized = toLowerAscii(trim(selector));
    int index = 0;
    if (parseInt(normalized, index)) {
        syntaxValid = true;
        return false; // Index selection is handled before this helper.
    }

    const std::vector<std::string> parts = split(normalized, ':');
    if (parts.size() == 2) {
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;
        if (parseUnsignedHex(parts[0], vendorId) && parseUnsignedHex(parts[1], deviceId)) {
            syntaxValid = true;
            return gpu.vendorId == vendorId && gpu.deviceId == deviceId;
        }
        syntaxValid = false;
        return false;
    }

    if (normalized.find(':') != std::string::npos) {
        syntaxValid = true;
        const std::string path = toLowerAscii(gpu.pciPath);
        if (path == normalized) {
            return true;
        }
        return path.size() > normalized.size() && endsWith(path, normalized);
    }

    syntaxValid = false;
    return false;
}

std::vector<GPUInfo> selectGpus(const std::vector<GPUInfo>& gpus,
                                const Arguments& arguments, std::string& errorKey,
                                std::string& errorDetail, bool& syntaxValid)
{
    syntaxValid = true;
    if (!arguments.gpuSpecified) {
        return gpus;
    }
    if (gpus.empty()) {
        errorKey = "error.no_gpus";
        return {};
    }

    int index = 0;
    if (parseInt(trim(arguments.gpuSelector), index)) {
        if (index < 0 || static_cast<std::size_t>(index) >= gpus.size()) {
            errorKey = "error.gpu_not_found";
            errorDetail = arguments.gpuSelector;
            return {};
        }
        return {gpus[static_cast<std::size_t>(index)]};
    }

    std::vector<GPUInfo> matches;
    for (const GPUInfo& gpu : gpus) {
        if (gpuMatchesSelector(gpu, arguments.gpuSelector, syntaxValid)) {
            matches.push_back(gpu);
        }
    }
    if (!syntaxValid) {
        errorKey = "error.invalid_gpu_selector";
        errorDetail = arguments.gpuSelector;
        return {};
    }
    if (matches.empty()) {
        errorKey = "error.gpu_not_found";
        errorDetail = arguments.gpuSelector;
        return {};
    }
    return matches;
}

bool isInteractiveTerminal()
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        return false;
    }
    const char* term = std::getenv("TERM");
    return term != nullptr && std::strcmp(term, "dumb") != 0;
}

} // namespace

int main(int argc, char** argv)
{
    initializeCharacterLocale();

    const std::string programName = argc > 0 ? argv[0] : kProgramName;
    const Arguments arguments = parseArguments(argc, argv);
    const LanguageResolution resolution = resolveLanguageFromEnvironment(
        arguments.languageSpecified ? arguments.language : std::string());

    Localization localization;
    const bool localeLoaded = localization.load(resolution.language);
    if (!localeLoaded) {
        std::cerr << localization.tr("error.locale_missing") << "\n";
    }
    if (resolution.invalidExplicit) {
        std::cerr << localization.tr(
            "error.argument",
            {localization.tr("error.invalid_language", {resolution.requested})}) << "\n";
    }

    if (!arguments.valid) {
        printArgumentError(localization, arguments, programName);
        return 2;
    }

    if (arguments.action == Action::Help || !arguments.hadArguments) {
        std::cout << buildHelpText(localization, programName);
        return 0;
    }
    if (arguments.action == Action::Version) {
        std::cout << buildVersionText(localization);
        return 0;
    }

    try {
        PCIDatabase database;
        database.loadDefault();

        GPUDetector detector(database);
        std::vector<GPUInfo> gpus = detector.detectAll();

        std::string selectorError;
        std::string selectorDetail;
        bool selectorSyntaxValid = true;
        gpus = selectGpus(gpus, arguments, selectorError, selectorDetail,
                          selectorSyntaxValid);
        if (!selectorError.empty()) {
            std::cerr << localization.tr("error.argument",
                                         {describeError(localization, selectorError,
                                                        selectorDetail)}) << "\n";
            if (selectorError == "error.gpu_not_found"
                || selectorError == "error.invalid_gpu_selector") {
                ReportOptions listOptions;
                listOptions.color = useColor(arguments, isatty(STDOUT_FILENO));
                std::cerr << ReportGenerator::generateGpuList(detector.detectAll(),
                                                              localization, listOptions);
            }
            return 1;
        }

        ReportOptions reportOptions;
        reportOptions.color = useColor(arguments, isatty(STDOUT_FILENO));
        reportOptions.width = ReportGenerator::detectTerminalWidth();

        if (arguments.action == Action::ListGpu) {
            std::cout << ReportGenerator::generateGpuList(gpus, localization,
                                                          reportOptions);
            return 0;
        }

        DiagnosticEngine engine(database);
        engine.diagnose(gpus);
        for (GPUInfo& gpu : gpus) {
            RecommendationGenerator::generate(gpu);
        }

        Inventory inventory;
        inventory.gpus = std::move(gpus);
        inventory.globalChecks = engine.globalChecks();
        inventory.host = collectHostInfo();
        inventory.localeCode = languageCode(localization.language());
        inventory.generatedAt = iso8601Now();

        const bool interactive = isInteractiveTerminal();
        const bool tui = interactive && (arguments.forceTui || !arguments.noTui);

        XMLExportResult exportResult;
        bool exportAttempted = false;
        if (arguments.exportSpecified) {
            exportAttempted = true;
            exportResult = XMLExporter::save(inventory, arguments.exportPath);
        }

        if (tui) {
            TUI tuiInterface(inventory, localization,
                             arguments.exportSpecified ? arguments.exportPath
                                                       : "gpu_info.xml");
            const int tuiResult = tuiInterface.run();
            if (exportAttempted && !exportResult.ok) {
                std::cerr << localization.tr("error.argument",
                    {describeError(localization, exportResult.errorKey,
                                   exportResult.errorDetail,
                                   exportResult.errorDetail2)}) << "\n";
                return 1;
            }
            return tuiResult;
        }

        std::cout << ReportGenerator::generateInventoryReport(inventory, localization,
                                                              reportOptions);
        if (exportAttempted) {
            if (exportResult.ok) {
                std::cout << localization.tr("report.xml_exported",
                                             {exportResult.path}) << "\n";
            } else {
                std::cerr << localization.tr("error.argument",
                    {describeError(localization, exportResult.errorKey,
                                   exportResult.errorDetail,
                                   exportResult.errorDetail2)}) << "\n";
                return 1;
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << localization.tr("error.argument",
                                     {localization.tr("error.internal", {error.what()})})
                  << "\n";
        return 1;
    }
}

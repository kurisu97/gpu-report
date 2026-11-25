#include "TestHarness.hpp"

#include "cli/Arguments.hpp"
#include "core/Version.hpp"
#include "localization/Localization.hpp"

#include <string>
#include <vector>

using namespace gpu_report;

namespace {

Arguments parse(std::vector<std::string> arguments)
{
    return parseArguments(arguments);
}

} // namespace

int main()
{
    Arguments none = parse({});
    CHECK(none.valid);
    CHECK(!none.hadArguments);
    CHECK(none.action == Action::None);

    Arguments help = parse({"--help"});
    CHECK(help.valid);
    CHECK(help.action == Action::Help);

    Arguments shortHelp = parse({"-h"});
    CHECK(shortHelp.action == Action::Help);

    Arguments version = parse({"--version"});
    CHECK(version.valid);
    CHECK(version.action == Action::Version);

    Arguments list = parse({"--list-gpu"});
    CHECK(list.valid);
    CHECK(list.action == Action::ListGpu);

    Arguments report = parse({"--get-report"});
    CHECK(report.valid);
    CHECK(report.action == Action::GetReport);

    Arguments combined = parse({"--get-report", "--set-gpu", "0", "--export", "out.xml",
                                "--set-local=russian", "--color=always", "--no-tui"});
    CHECK(combined.valid);
    CHECK(combined.action == Action::GetReport);
    CHECK(combined.gpuSpecified);
    CHECK(combined.gpuSelector == "0");
    CHECK(combined.exportSpecified);
    CHECK(combined.exportPath == "out.xml");
    CHECK(combined.languageSpecified);
    CHECK(combined.language == "russian");
    CHECK(combined.colorSpecified);
    CHECK(combined.color == ColorMode::Always);
    CHECK(combined.noTui);
    CHECK(!combined.forceTui);

    Arguments equals = parse({"--get-report", "--set-gpu=0000:01:00.0",
                              "--set-local=ja", "--color", "never"});
    CHECK(equals.valid);
    CHECK(equals.gpuSelector == "0000:01:00.0");
    CHECK(equals.language == "ja");
    CHECK(equals.color == ColorMode::Never);

    Arguments tuiLast = parse({"--get-report", "--no-tui", "--tui"});
    CHECK(tuiLast.valid);
    CHECK(tuiLast.forceTui);
    CHECK(!tuiLast.noTui);

    Arguments noTuiLast = parse({"--get-report", "--tui", "--no-tui"});
    CHECK(noTuiLast.valid);
    CHECK(noTuiLast.noTui);
    CHECK(!noTuiLast.forceTui);

    Arguments helpWins = parse({"--list-gpu", "--help"});
    CHECK(helpWins.valid);
    CHECK(helpWins.action == Action::Help);

    Arguments unknown = parse({"--bogus"});
    CHECK(!unknown.valid);
    CHECK(unknown.errorKey == "error.unknown_option");
    CHECK(unknown.errorDetail == "--bogus");

    Arguments missing = parse({"--set-gpu"});
    CHECK(!missing.valid);
    CHECK(missing.errorKey == "error.missing_value");
    CHECK(missing.errorDetail == "--set-gpu");

    Arguments emptyValue = parse({"--set-local="});
    CHECK(!emptyValue.valid);
    CHECK(emptyValue.errorKey == "error.missing_value");

    Arguments unexpected = parse({"file.xml"});
    CHECK(!unexpected.valid);
    CHECK(unexpected.errorKey == "error.unexpected_argument");
    CHECK(unexpected.errorDetail == "file.xml");

    Arguments doubleDash = parse({"--", "file.xml"});
    CHECK(!doubleDash.valid);
    CHECK(doubleDash.errorKey == "error.unexpected_argument");

    Arguments conflict = parse({"--list-gpu", "--get-report"});
    CHECK(!conflict.valid);
    CHECK(conflict.errorKey == "error.invalid_action");

    Arguments badExport = parse({"--list-gpu", "--export", "out.xml"});
    CHECK(!badExport.valid);
    CHECK(badExport.errorKey == "error.export_requires_report");

    Arguments badColor = parse({"--get-report", "--color=sometimes"});
    CHECK(!badColor.valid);
    CHECK(badColor.errorKey == "error.color_invalid");
    CHECK(badColor.errorDetail == "sometimes");

    Localization localization;
    std::string error;
    CHECK(localization.loadFromDirectory(Language::English, GPU_REPORT_BUILD_LOCALES_DIR,
                                         &error));
    const std::string helpText = buildHelpText(localization, "gpu-report");
    CHECK(helpText.find("GPU Report") != std::string::npos);
    CHECK(helpText.find("--list-gpu") != std::string::npos);
    CHECK(helpText.find("--set-gpu") != std::string::npos);
    CHECK(helpText.find("--set-local") != std::string::npos);
    CHECK(helpText.find("--export") != std::string::npos);
    CHECK(helpText.find("--no-tui") != std::string::npos);
    CHECK(helpText.find("gpu-report --get-report") != std::string::npos);
    CHECK(helpText.find("{0}") == std::string::npos);
    CHECK(helpText.find("help.example") == std::string::npos);

    const std::string versionText = buildVersionText(localization);
    CHECK(versionText.find(kProgramVersion) != std::string::npos);
    CHECK(versionText.find(kProgramName) != std::string::npos);

    return testharness::summary("arguments");
}

#include "cli/Arguments.hpp"

#include "core/Strings.hpp"
#include "core/Version.hpp"

#include <algorithm>
#include <sstream>

namespace gpu_report {

namespace {

bool isValueOption(const std::string& argument, const std::string& option,
                   std::string& inlineValue, bool& hasInlineValue)
{
    const std::string prefix = option + "=";
    if (startsWith(argument, prefix)) {
        inlineValue = argument.substr(prefix.size());
        hasInlineValue = true;
        return true;
    }
    if (argument == option) {
        inlineValue.clear();
        hasInlineValue = false;
        return true;
    }
    return false;
}

void setError(Arguments& result, const std::string& key, const std::string& detail)
{
    if (result.valid) {
        result.valid = false;
        result.errorKey = key;
        result.errorDetail = detail;
    }
}

bool nextValue(const std::vector<std::string>& arguments, std::size_t& index,
               bool hasInlineValue, const std::string& inlineValue,
               const std::string& option, std::string& value, Arguments& result)
{
    if (hasInlineValue) {
        value = inlineValue;
    } else if (index + 1 < arguments.size()) {
        ++index;
        value = arguments[index];
    } else {
        setError(result, "error.missing_value", option);
        return false;
    }
    if (value.empty()) {
        setError(result, "error.missing_value", option);
        return false;
    }
    return true;
}

std::string optionLine(const std::string& label, const std::string& description)
{
    std::string line = "  " + label;
    const std::size_t width = displayWidth(line);
    const std::size_t target = 34;
    if (width < target) {
        line.append(target - width, ' ');
    } else {
        line += "\n";
        line.append(target, ' ');
    }
    line += description;
    return line;
}

} // namespace

Arguments parseArguments(const std::vector<std::string>& arguments)
{
    Arguments result;
    result.hadArguments = !arguments.empty();

    bool helpRequested = false;
    bool versionRequested = false;
    int listRequests = 0;
    int reportRequests = 0;

    const std::string helpOption = "--help";
    const std::string versionOption = "--version";
    const std::string listOption = "--list-gpu";
    const std::string reportOption = "--get-report";
    const std::string gpuOption = "--set-gpu";
    const std::string localOption = "--set-local";
    const std::string exportOption = "--export";
    const std::string colorOption = "--color";

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        std::string inlineValue;
        bool hasInlineValue = false;
        std::string value;

        if (argument == helpOption || argument == "-h") {
            helpRequested = true;
            continue;
        }
        if (argument == versionOption || argument == "-V") {
            versionRequested = true;
            continue;
        }
        if (argument == listOption) {
            ++listRequests;
            continue;
        }
        if (argument == reportOption) {
            ++reportRequests;
            continue;
        }
        if (argument == "--tui") {
            result.forceTui = true;
            result.noTui = false;
            continue;
        }
        if (argument == "--no-tui") {
            result.noTui = true;
            result.forceTui = false;
            continue;
        }
        if (isValueOption(argument, gpuOption, inlineValue, hasInlineValue)) {
            if (nextValue(arguments, index, hasInlineValue, inlineValue, gpuOption,
                          value, result)) {
                result.gpuSpecified = true;
                result.gpuSelector = value;
            }
            continue;
        }
        if (isValueOption(argument, localOption, inlineValue, hasInlineValue)) {
            if (nextValue(arguments, index, hasInlineValue, inlineValue, localOption,
                          value, result)) {
                result.languageSpecified = true;
                result.language = value;
            }
            continue;
        }
        if (isValueOption(argument, exportOption, inlineValue, hasInlineValue)) {
            if (nextValue(arguments, index, hasInlineValue, inlineValue, exportOption,
                          value, result)) {
                result.exportSpecified = true;
                result.exportPath = value;
            }
            continue;
        }
        if (isValueOption(argument, colorOption, inlineValue, hasInlineValue)) {
            if (nextValue(arguments, index, hasInlineValue, inlineValue, colorOption,
                          value, result)) {
                const std::string normalized = toLowerAscii(trim(value));
                if (normalized == "auto") {
                    result.color = ColorMode::Auto;
                } else if (normalized == "always") {
                    result.color = ColorMode::Always;
                } else if (normalized == "never") {
                    result.color = ColorMode::Never;
                } else {
                    setError(result, "error.color_invalid", value);
                }
                result.colorSpecified = true;
            }
            continue;
        }

        if (argument == "--") {
            if (index + 1 < arguments.size()) {
                setError(result, "error.unexpected_argument", arguments[index + 1]);
            }
            break;
        }
        if (!argument.empty() && argument[0] == '-') {
            setError(result, "error.unknown_option", argument);
            break;
        }
        setError(result, "error.unexpected_argument", argument);
        break;
    }

    if (!result.valid) {
        return result;
    }

    if (helpRequested) {
        result.action = Action::Help;
    } else if (versionRequested) {
        result.action = Action::Version;
    } else if (listRequests + reportRequests > 1) {
        setError(result, "error.invalid_action", {});
        return result;
    } else if (reportRequests == 1) {
        result.action = Action::GetReport;
    } else if (listRequests == 1) {
        result.action = Action::ListGpu;
    } else {
        result.action = Action::None;
    }

    if (result.exportSpecified && result.action != Action::Help
        && result.action != Action::Version && result.action != Action::GetReport) {
        setError(result, "error.export_requires_report", {});
    }

    return result;
}

Arguments parseArguments(int argc, char** argv)
{
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        arguments.emplace_back(argv[i]);
    }
    return parseArguments(arguments);
}

std::string buildHelpText(const Localization& localization,
                          const std::string& programName)
{
    std::ostringstream output;
    output << localization.tr("app.title") << " - " << localization.tr("app.tagline")
           << "\n\n";
    output << localization.tr("help.usage", {programName}) << "\n";
    output << localization.tr("help.description") << "\n\n";

    output << localization.tr("help.action_title") << "\n";
    output << optionLine("--help", localization.tr("help.option.help")) << "\n";
    output << optionLine("--version",
                         localization.tr("help.option.version")) << "\n";
    output << optionLine("--list-gpu",
                         localization.tr("help.option.list_gpu")) << "\n";
    output << optionLine("--get-report",
                         localization.tr("help.option.get_report")) << "\n\n";

    output << localization.tr("help.option_title") << "\n";
    output << optionLine("--set-gpu <index|pci|vendor:device>",
                         localization.tr("help.option.set_gpu")) << "\n";
    output << optionLine("--set-local=<language>",
                         localization.tr("help.option.set_local")) << "\n";
    output << optionLine("--export <path>",
                         localization.tr("help.option.export")) << "\n";
    output << optionLine("--tui",
                         localization.tr("help.option.tui")) << "\n";
    output << optionLine("--no-tui",
                         localization.tr("help.option.no_tui")) << "\n";
    output << optionLine("--color=<auto|always|never>",
                         localization.tr("help.option.color")) << "\n\n";

    output << localization.tr("help.examples") << "\n";
    for (int example = 1; example <= 6; ++example) {
        output << "  " << localization.tr("help.example." + std::to_string(example),
                                          {programName}) << "\n";
    }
    output << "\n";

    output << localization.tr("help.notes") << "\n";
    output << "  - " << localization.tr("help.note.reliability") << "\n";
    output << "  - " << localization.tr("help.note.optional") << "\n";
    output << "  - " << localization.tr("help.note.no_root") << "\n";
    output << "  - " << localization.tr("help.note.active_probe") << "\n";
    output << "  - " << localization.tr("help.note.tui") << "\n";
    output << "  - " << localization.tr("help.note.export") << "\n";
    return output.str();
}

std::string buildVersionText(const Localization& localization)
{
    std::string result = std::string(kProgramName) + " " + kProgramVersion;
    result += "\n" + localization.tr("common.program_version") + ": "
        + kProgramVersion;
    result += "\n";
    return result;
}

} // namespace gpu_report

#pragma once

#include "localization/Localization.hpp"

#include <string>
#include <vector>

namespace gpu_report {

enum class Action {
    None,
    Help,
    Version,
    ListGpu,
    GetReport
};

enum class ColorMode {
    Auto,
    Always,
    Never
};

struct Arguments {
    bool hadArguments = false;
    Action action = Action::None;

    bool gpuSpecified = false;
    std::string gpuSelector;

    bool languageSpecified = false;
    std::string language;

    bool exportSpecified = false;
    std::string exportPath;

    bool forceTui = false;
    bool noTui = false;

    ColorMode color = ColorMode::Auto;
    bool colorSpecified = false;

    bool valid = true;
    std::string errorKey;
    std::string errorDetail;
};

Arguments parseArguments(const std::vector<std::string>& arguments);
Arguments parseArguments(int argc, char** argv);

// Builds localized help/version text. All human-readable strings come from the
// localization catalogue, so no user-facing text lives in the CLI logic.
std::string buildHelpText(const Localization& localization,
                          const std::string& programName);
std::string buildVersionText(const Localization& localization);

} // namespace gpu_report

#pragma once

#include "gpu/GPUInfo.hpp"
#include "localization/Localization.hpp"

#include <string>

namespace gpu_report {

class TUI {
public:
    TUI(const Inventory& inventory, const Localization& localization,
        std::string exportPath);

    int run();

private:
    enum class View {
        Overview,
        Details,
        Report,
        Help
    };

    void draw();
    void handleKey(int key);
    void changeView(View requested, bool toggle);
    void changeGpu(int direction);
    void scrollBy(int amount);
    void scrollTo(int position);
    void exportXml();
    std::size_t bodyHeight() const;

    const Inventory& inventory_;
    const Localization& localization_;
    std::string exportPath_;
    std::size_t selected_ = 0;
    std::size_t scroll_ = 0;
    View view_ = View::Overview;
    std::string message_;
    bool messageError_ = false;
    bool colors_ = false;
    std::size_t lastLineCount_ = 0;
};

} // namespace gpu_report

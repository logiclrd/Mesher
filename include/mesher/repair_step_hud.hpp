#pragma once

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

namespace mesher {

// Pure layout contract shared by the CPU regression and Vulkan HUD: the
// original classifier lines always occupy rows 0/1; operation/arguments
// start at row 2. No part of this helper touches GLFW, GPU or worker state.
struct RepairStepHudPage {
    static constexpr int firstOperationRow=2;
    std::vector<std::string> lines;
    std::size_t maxScroll{};
    std::size_t actualScroll{};
};
inline RepairStepHudPage layoutRepairStepHud(
    const std::vector<std::string>& details,
    std::size_t columns,std::size_t availableRows,std::size_t scroll) {
    if(columns<1||availableRows<1)
        throw std::invalid_argument("R06-M16D2 requires finite HUD width/height");
    std::vector<std::string> wrapped;
    for(const auto& line:details) {
        if(line.empty()){wrapped.emplace_back();continue;}
        for(std::size_t offset=0;offset<line.size();offset+=columns)
            wrapped.push_back(line.substr(offset,columns));
    }
    RepairStepHudPage page;
    page.maxScroll=wrapped.size()>availableRows?
        wrapped.size()-availableRows:0;
    page.actualScroll=std::min(scroll,page.maxScroll);
    const auto count=std::min(availableRows,wrapped.size()-page.actualScroll);
    page.lines.insert(page.lines.end(),
        wrapped.begin()+static_cast<std::ptrdiff_t>(page.actualScroll),
        wrapped.begin()+static_cast<std::ptrdiff_t>(page.actualScroll+count));
    return page;
}
} // namespace mesher

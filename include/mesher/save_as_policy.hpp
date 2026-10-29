#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace mesher {

// Only user-selected checkpoint destinations reach the worker. Keep this
// format guard separate from the native chooser for headless CI tests.
// No filename is invented on cancellation; that case is std::nullopt at
// the caller and never reaches this function.
inline std::filesystem::path checkpointSaveDestination(
    std::filesystem::path chosen) {
    if(chosen.empty()||chosen.filename().empty()||
       chosen.filename()=="."||chosen.filename()=="..")
        throw std::invalid_argument("Save As requires a checkpoint filename");
    auto extension=chosen.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),
                   [](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension.empty())chosen+=".mshr";
    else if(extension!=".mshr")
        throw std::invalid_argument("Checkpoint Save As must use .mshr");
    return chosen;
}
} // namespace mesher

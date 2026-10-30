#pragma once

#include <filesystem>
#include <stdexcept>

namespace mesher {
// UI-thread policy shared by every desktop picker. CWD is intentionally the
// process's directory, not the Shell/Finder's remembered location or the
// parent of the previous filename suggestion.
inline std::filesystem::path nativeDialogInitialDirectory() {
    return std::filesystem::current_path();
}

// Called ONLY after the file dialog has accepted and returned a valid file
// path. Resolve before chdir: both the current import and asynchronous
// checkpoint writer must receive an absolute path that remains stable after
// subsequent dialogs change the process CWD.
inline std::filesystem::path acceptNativeDialogFilePath(
    const std::filesystem::path& selection) {
    if(selection.empty()||selection.filename().empty())
        throw std::invalid_argument("Native dialog returned an empty filename");
    const auto absolute=std::filesystem::absolute(selection).lexically_normal();
    std::filesystem::current_path(absolute.parent_path());
    return absolute;
}
} // namespace mesher

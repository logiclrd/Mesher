#pragma once

#include <filesystem>
#include <optional>

// Implemented by each platform's native file picker. This is a user-facing
// modal operation, called exclusively from the GLFW/UI thread. Cancellation
// returns nullopt; an unavailable/failed dialog throws (never silently saves
// under the default filename). The UI owns its window and filename choice.
struct GLFWwindow;
namespace mesher {
std::optional<std::filesystem::path> nativeSaveCheckpointDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion);
}

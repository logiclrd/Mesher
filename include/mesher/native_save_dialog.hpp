#pragma once

#include "mesher/mesh_open_candidate.hpp"
#include <filesystem>
#include <optional>
#include <string>

// Implemented by each platform's native file picker. This is a user-facing
// modal operation, called exclusively from the GLFW/UI thread. Cancellation
// returns nullopt; an unavailable/failed dialog throws (never silently saves
// under the default filename). The UI owns its window and filename choice.
struct GLFWwindow;
namespace mesher {
std::optional<std::filesystem::path> nativeSaveCheckpointDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion);
// Native Open is modal on the UI thread; nullopt means cancelled.
// Format support and validation are deliberately separate and private.
struct NativeOpenSelection {
    std::filesystem::path path;
    // "All supported files" (default) and "All Files" both map to
    // MeshOpenFilter::AllFiles for content-based interpretation.
    MeshOpenFilter filter{MeshOpenFilter::AllFiles};
};
std::optional<NativeOpenSelection> nativeOpenMeshDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion);
// Never show an error for cancellation; only real failures reach here.
void nativeOpenErrorMessage(GLFWwindow* owner,const std::string& message);
// Yes: use PLY source alpha as editable mesh alpha. No: keep mesh opaque,
// with source alpha retained. Cancel: leave the current editor unchanged.
std::optional<bool> nativePlyAlphaOpacityDialog(
    GLFWwindow* owner,float minimum,float maximum);
struct MeshOpenCancelled {};

#ifdef __linux__
// Called once by VulkanViewport, before GLFW destroys its X11 window.
// Does nothing if Qt dialogs were never used. Cannot be reinitialized
// later in the same process (Qt disallows repeated QApplication lifetimes).
// Optional early lazy initialization for a modal-dialog test or UI user;
// the process still owns only one QApplication for its entire lifetime.
void initializeNativeDialogs();
void shutdownNativeDialogs() noexcept;
#endif
}

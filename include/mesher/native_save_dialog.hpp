#pragma once

#include "mesher/mesh_open_candidate.hpp"
#include "mesher/mesh_export_formats.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

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
// The selected name-filter index is the sole codec authority. Cancellation
// leaves editor and export queue unchanged; the accepted path is made
// absolute and updates CWD through the same UX06F/G path policy.
struct NativeMeshExportSelection {
    std::filesystem::path path;
    MeshExportPreset preset{MeshExportPreset::PlyLittleMeshAlpha};
};
std::optional<NativeMeshExportSelection> nativeSaveMeshExportDialog(
    GLFWwindow* owner,const std::filesystem::path& suggestion,
    MeshExportPreset initialPreset=MeshExportPreset::PlyLittleMeshAlpha);
// False means the user declined the disclosed lossy/nonstandard conversion.
bool nativeConfirmMeshExportDisclosure(GLFWwindow* owner,
                                       std::string_view disclosure);
 // If the picker omits an extension and we append one after it returns,
 // reconfirm any existing FINAL filename rather than trusting an earlier
 // overwrite prompt for a different path.
bool nativeConfirmMeshExportOverwrite(
    GLFWwindow* owner,const std::filesystem::path& destination);
void nativeMeshExportErrorMessage(GLFWwindow* owner,const std::string& message);

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

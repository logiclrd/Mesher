#pragma once
#include "mesher/mesh.hpp"

#include <functional>
#include <string>

namespace mesher {

// Rendering is independent of how an animation edits the mesh. An advance()
// call returns true when it changed the mesh and false once playback has ended.
// The caller owns the mesh and the callback state; both outlive runViewport.
struct ViewportAnimation {
    std::function<bool()> advance;
    std::function<void()> reset;
    std::function<std::string()> caption;
    double stampsPerSecond = 10.0;
};

// Primary-monitor fullscreen; Escape exits. With animation: Space pauses or
// resumes, Right applies one stamp while paused, R restores/replays. A current
// mesh is uploaded after each stamp before the next Vulkan frame.
void runViewport(const Mesh& mesh, ViewportAnimation animation = {});
} // namespace mesher

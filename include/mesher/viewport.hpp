#pragma once
#include "mesher/mesh.hpp"
#include "mesher/brush_radius.hpp"

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

// User-facing mouse interaction callbacks, independent of GLFW and Vulkan.
// Times are monotonic seconds elapsed since mouse down. The host picks the
// freshly modified mesh at every update, stores the resulting input segments,
// and drains at most one 4D Step sample between rendered frames.
struct ViewportInteraction {
    std::function<void(Vec3)> begin;
    std::function<void(Vec3,double)> update;
    std::function<void(Vec3,double)> end;
    // Consume zero or one queued brush sample between rendered frames.
    std::function<bool()> pump;
    std::function<bool()> canBegin;
    std::function<std::size_t()> pendingSamples;
    std::function<std::size_t()> pendingSegments;
    std::function<std::string()> caption;
    std::function<std::size_t()> completedStamps;
    // Formatter invoked only when a 250ms diagnostic interval is due.
    std::function<std::string()> lastStampDetails;
    std::function<std::size_t()> skippedStamps;
    std::function<void(double)> setRadius;
    BrushRadiusRange radiusRange{};
    double radius = 0.4;
    double stationaryStampsPerSecond = 0;
};

// On X11, show a normally managed window, then ask the WM to make it
// fullscreen on its chosen display. Other platforms use managed maximization.
// Escape exits. The HUD shows measured rendered FPS and actual stamp throughput.
// With animation: Space pauses/resumes, Right steps, R replays. The mesh is
// uploaded after each stamp before the next Vulkan frame.
void runViewport(const Mesh& mesh, ViewportAnimation animation = {},
                 ViewportInteraction interaction = {});
} // namespace mesher

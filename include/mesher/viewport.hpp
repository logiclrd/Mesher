#pragma once
#include "mesher/mesh.hpp"
#include "mesher/brush_radius.hpp"
#include "mesher/partition_render.hpp"

#include <functional>
#include <optional>
#include <memory>
#include <vector>
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
struct ViewportMeshUpdate {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::vector<PartitionId> dirtyPartitions;
    std::size_t completedStamps = 0;
};

struct ViewportInteraction {
    std::function<void(Vec3)> begin;
    std::function<void(Vec3,double)> update;
    std::function<void(Vec3,double)> end;
    // Consume zero or one queued brush sample between rendered frames.
    std::function<bool()> pump;
    // Async mode: consume the latest fully committed mesh snapshot. When
    // present, the Vulkan/picking thread never reads the worker's live mesh.
    std::function<std::optional<ViewportMeshUpdate>()> takeSnapshot;
    // The initial immutable root is an explicit one-time full partition build;
    // each later frame only refreshes its coalesced dirty partition IDs.
    std::shared_ptr<const RegionTopologyIndex> initialRevision;
    std::function<bool()> canBegin;
    std::function<void()> undo;
    std::function<void()> redo;
    std::function<void()> checkpoint;
    // M48: strictly opt-in desktop contract. The user selects actual stable
    // FaceIds from this exact immutable displayed root; the host queues the
    // M47 bounded discovery, never a render-thread collision traversal.
    std::function<void(std::shared_ptr<const RegionTopologyIndex>,
                       std::vector<FaceId>)> mergeSelected;
    std::function<std::string()> mergeStatus;
    std::function<std::string()> collisionStatus; // opt-in OFF-mode worker diagnostics
    // Only deliberately named M50/M51 fixtures supply a finite first-
    // original FaceId list, with its exact initial immutable root. A in
    // ON mode is an explicit human nomination, never an automatic weld.
    std::shared_ptr<const RegionTopologyIndex> mergeFixtureOriginal;
    std::vector<FaceId> mergeFixtureOriginals;
    std::function<std::size_t()> pendingSamples;
    std::function<std::size_t()> pendingSegments;
    std::function<std::size_t()> inputCommands;
    std::function<std::size_t()> totalQueuedSteps;
    // Opt-in event trace for precise mouse-input and queueing diagnostics.
    bool verboseInput = false;
    std::function<std::string()> caption;
    std::function<std::size_t()> completedStamps;
    // Formatter invoked only when a 250ms diagnostic interval is due.
    std::function<std::string()> lastStampDetails;
    std::function<std::size_t()> skippedStamps;
    std::function<void(double)> setRadius;
    BrushRadiusRange radiusRange{};
    double radius = 0.4;
    // M51: explicit developer fixtures are larger than the default unit
    // sphere. Shared render/picker scale only; normal sessions stay 1.0.
    double cameraScale = 1.0;
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

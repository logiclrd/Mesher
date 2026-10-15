#pragma once

#include "mesher/mesh.hpp"
#include "mesher/remesh.hpp"
#include "mesher/refine.hpp"
#include "mesher/spatial_grid.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace mesher {

// A spherical influence in mesh/world units. Hardness is the fraction of the
// radius with 100% influence. The remainder follows a smooth S-curve.
struct BrushArea {
    Vec3 center;
    double radius = 0.1;
    double hardness = 0.3;
};

double brushFalloff(double distance, double radius, double hardness);
double brushInfluence(Vec3 point, std::span<const BrushArea> areas);

// One application represents a simultaneous stamp for all influence areas;
// overlapping areas use maximum influence, never an additive sum.
class Brush {
public:
    virtual ~Brush() = default;
    virtual void begin(Mesh& mesh) { (void)mesh; }
    virtual void stamp(Mesh& mesh, std::span<const BrushArea> areas) = 0;
    virtual void end(Mesh& mesh) { (void)mesh; }
};

// Experimental, opt-in mean-curvature correction. Positive strength couples
// outward Inflate velocity with a curvature-flow term: convex regions inflate
// less rapidly, while concave troughs inflate more rapidly. It is not a
// guarantee against folds or a replacement for topology validity constraints.
struct CurvatureSettings {
    bool enabled = false;
    double strength = 1.0;
};

// Strength is displacement per full-influence stamp in model units.
// The normalized average of incident unit face normals is re-evaluated from
// the deformed mesh BEFORE each stamp. This can fold triangles; the animation
// demo deliberately exposes that effect rather than concealing it.
class InflateBrush final : public Brush {
public:
    explicit InflateBrush(double strength, CurvatureSettings curvature = {});
    void begin(Mesh& mesh) override;
    void stamp(Mesh& mesh, std::span<const BrushArea> areas) override;
    void end(Mesh& mesh) override;
    const std::vector<std::uint32_t>& movedVertices() const noexcept { return movedVertices_; }
    std::size_t candidateCount() const noexcept { return candidateCount_; }
    const std::vector<std::uint32_t>& affectedFaces() const noexcept { return affectedFaces_; }
private:
    double strength_;
    CurvatureSettings curvature_;
    std::vector<std::vector<std::uint32_t>> incidentFaces_;
    VertexSpatialGrid spatial_{0.15};
    std::vector<std::uint32_t> movedVertices_;
    std::vector<std::uint32_t> affectedFaces_;
    std::size_t candidateCount_ = 0;
};

// A stroke begins with a stamp at the starting positions. All subsequent
// control-point updates must move every point by the same distance. The
// entire set is sampled at a shared fixed arc-length interval (Step), with
// the unused fraction carried across calls and segment corners. Ending the
// stroke processes its final segment and stamps the terminal positions if
// they have not already been stamped.
class BrushStroke final {
public:
    BrushStroke(Mesh& mesh, Brush& brush, double step);
    ~BrushStroke();
    BrushStroke(const BrushStroke&) = delete;
    BrushStroke& operator=(const BrushStroke&) = delete;

    void start(std::vector<BrushArea> areas);
    void update(std::span<const Vec3> positions);
    void finish(std::span<const Vec3> positions);
    // Fourth coordinate is elapsed seconds multiplied by timeSpeed (model
    // units/second). Shared timestamps and mirrored spatial movement preserve
    // the equal-4D-length invariant. Even a stationary pointer creates stamps.
    void startTimed(std::vector<BrushArea> areas, double timeSpeed);
    void updateTimed(std::span<const Vec3> positions, double elapsedSeconds);
    void finishTimed(std::span<const Vec3> positions, double elapsedSeconds);

    // Interactive input capture is inexpensive: record an exact polygonal
    // (x,y,z,time) path without applying stamps or discarding old segments.
    void updateTimedRealtime(std::span<const Vec3> positions, double elapsedSeconds);
    // Queues the terminal point and ends input; the renderer must continue
    // draining samples until active() becomes false.
    void finishTimedRealtime(std::span<const Vec3> positions, double elapsedSeconds);
    // Consumes at most one expensive stamp, leaving remaining path for the
    // next rendered frame. Returns true exactly when a stamp was applied.
    bool pumpTimedRealtime();
    std::size_t pendingSegments() const noexcept { return pending_.size(); }
    std::size_t pendingSamples() const noexcept;
    // Monotonic number of Step boundaries accepted from mouse input.
    // Unlike pendingSamples(), this does not decrease while stamps render.
    std::size_t totalQueuedSteps() const noexcept { return totalQueuedSteps_; }
    // Changes the influence radius for subsequent stamps in an active stroke.
    // Does not reset 4D travel phase, so slider drags never create a stamp.
    void setRadius(double radius);

    bool active() const noexcept { return active_; }
    std::size_t stamps() const noexcept { return stamps_; }
    std::size_t skippedStamps() const noexcept { return 0; } // No dropped samples
private:
    void advance(std::span<const Vec3> positions, double elapsedSeconds);
    void queueTimedInput(std::span<const Vec3> positions, double elapsedSeconds);
    struct PathPoint {
        std::vector<Vec3> centers;
        double time = 0;
    };
    void stamp();

    Mesh& mesh_;
    Brush& brush_;
    double step_;
    double untilNext_;
    double timeSpeed_ = 0;
    double currentTime_ = 0, lastStampTime_ = 0;
    bool timed_ = false;
    bool active_ = false;
    std::size_t stamps_ = 0;
    std::deque<PathPoint> pending_;
    std::vector<Vec3> lastInputCenters_;
    double lastInputTime_ = 0;
    double pendingDistance_ = 0;
    double inputArcLength_ = 0;
    std::size_t totalQueuedSteps_ = 0;
    bool inputFinished_ = false;
    bool realtime_ = false;
    std::vector<BrushArea> areas_;
    std::vector<Vec3> lastStamped_;
};

struct StampDetails {
    std::vector<BrushArea> areas;
    std::size_t influenced = 0;
    std::size_t moved = 0;
    std::size_t splitPairs = 0;
    std::size_t flipPairs = 0;
    std::size_t collapsedPairs = 0;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
};

struct StampTimings {
    double inflateSeconds = 0;
    double refineSeconds = 0;
    double collapseSeconds = 0;
    double rebuildSeconds = 0;
};

// Interactive sculpting uses the same Inflate and local topology algorithms
// as the recorded demo, wrapped as a Brush for the 4D stroke sampler.
class AdaptiveInflateBrush final : public Brush {
public:
    AdaptiveInflateBrush(const Mesh& mesh, double strength,
                         CurvatureSettings curvature, CollapseSettings collapse,
                         RefineSettings refine, bool symmetric);
    void begin(Mesh& mesh) override;
    void stamp(Mesh& mesh, std::span<const BrushArea> areas) override;
    void end(Mesh& mesh) override;
    std::size_t completedStamps() const noexcept { return completed_; }
    StampTimings lastTimings() const noexcept { return lastTimings_; }
    const StampDetails& lastDetails() const noexcept { return lastDetails_; }
private:
    InflateBrush inflate_;
    DirectionalCollapser collapser_;
    LocalRefiner refiner_;
    std::size_t completed_ = 0;
    bool begun_ = false;
    StampTimings lastTimings_{};
    StampDetails lastDetails_{};
};

// CPU-only, incremental playback of the demonstration. BrushStroke generates
// the exact same fixed-distance sample positions as interactive updates, but
// recording them first lets the viewport advance just one stamp per timer tick.
// The original vertices are retained to support rewind/replay.
class InflateDemoAnimation {
public:
    InflateDemoAnimation(Mesh& mesh, double step = 0.02,
                         double strength = 0.012, std::uint32_t segments = 128,
                         double radius = 0.4,
                         CollapseSettings collapse = {}, bool symmetric = true,
                         CurvatureSettings curvature = {},
                         RefineSettings refine = {});
    ~InflateDemoAnimation();
    InflateDemoAnimation(const InflateDemoAnimation&) = delete;
    InflateDemoAnimation& operator=(const InflateDemoAnimation&) = delete;

    // Applies exactly one simultaneous stamp, or returns false when complete.
    bool advance();
    void reset();
    std::size_t completed() const noexcept { return next_; }
    std::size_t total() const noexcept { return stamps_.size(); }
    const MeshDiagnostics& preCollapseDiagnostics() const noexcept { return preCollapse_; }
    const MeshDiagnostics& diagnostics() const noexcept { return afterCollapse_; }
    const CollapseStatistics& lastCollapse() const noexcept { return lastCollapse_; }
    const RefineStatistics& lastRefine() const noexcept { return lastRefine_; }
    const std::vector<std::uint32_t>& mirrors() const noexcept { return refiner_.mirrors(); }
    std::size_t totalAdded() const noexcept { return totalAdded_; }
    std::size_t totalFlips() const noexcept { return totalFlips_; }
    std::size_t totalRemoved() const noexcept { return totalRemoved_; }
    double minimumEdgeTarget() const noexcept { return collapser_.minimumEdge(); }
    double maximumEdgeTarget() const noexcept { return refiner_.maximumEdge(); }
private:
    Mesh& mesh_;
    Mesh originalMesh_;
    std::vector<std::vector<BrushArea>> stamps_;
    InflateBrush brush_;
    DirectionalCollapser collapser_;
    LocalRefiner refiner_;
    std::vector<std::uint32_t> originalMirrors_;
    MeshDiagnostics preCollapse_{}, afterCollapse_{};
    CollapseStatistics lastCollapse_{};
    RefineStatistics lastRefine_{};
    std::size_t totalAdded_ = 0, totalFlips_ = 0;
    std::size_t totalRemoved_ = 0;
    std::size_t next_ = 0;
    bool brushActive_ = false;
};

// Visible symmetric demonstration: two mirror-image hemisphere-side arcs
// on the front of the sphere, each approximating a half-circle. Returns
// the number of simultaneous stamps. No topology changes unless explicitly
// configured with a directional collapser in InflateDemoAnimation.
std::size_t inflateHalfCircleDemo(Mesh& mesh, double step = 0.02,
                                  double strength = 0.012,
                                  std::uint32_t segments = 128,
                                  double radius = 0.4);

} // namespace mesher

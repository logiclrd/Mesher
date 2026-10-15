#pragma once

#include "mesher/mesh.hpp"

#include <cstddef>
#include <cstdint>
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

// Strength is displacement per full-influence stamp in model units.
// The normalized average of incident unit face normals is re-evaluated from
// the deformed mesh BEFORE each stamp. This can fold triangles; the animation
// demo deliberately exposes that effect rather than concealing it.
class InflateBrush final : public Brush {
public:
    explicit InflateBrush(double strength);
    void begin(Mesh& mesh) override;
    void stamp(Mesh& mesh, std::span<const BrushArea> areas) override;
    void end(Mesh& mesh) override;
private:
    double strength_;
    std::vector<std::vector<std::uint32_t>> incidentFaces_;
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

    bool active() const noexcept { return active_; }
    std::size_t stamps() const noexcept { return stamps_; }
private:
    void advance(std::span<const Vec3> positions);
    void stamp();

    Mesh& mesh_;
    Brush& brush_;
    double step_;
    double untilNext_;
    bool active_ = false;
    std::size_t stamps_ = 0;
    std::vector<BrushArea> areas_;
    std::vector<Vec3> lastStamped_;
};

// CPU-only, incremental playback of the demonstration. BrushStroke generates
// the exact same fixed-distance sample positions as interactive updates, but
// recording them first lets the viewport advance just one stamp per timer tick.
// The original vertices are retained to support rewind/replay.
class InflateDemoAnimation {
public:
    InflateDemoAnimation(Mesh& mesh, double step = 0.02,
                         double strength = 0.012, std::uint32_t segments = 128,
                         double radius = 0.4);
    ~InflateDemoAnimation();
    InflateDemoAnimation(const InflateDemoAnimation&) = delete;
    InflateDemoAnimation& operator=(const InflateDemoAnimation&) = delete;

    // Applies exactly one simultaneous stamp, or returns false when complete.
    bool advance();
    void reset();
    std::size_t completed() const noexcept { return next_; }
    std::size_t total() const noexcept { return stamps_.size(); }
private:
    Mesh& mesh_;
    std::vector<Vec3> originalVertices_;
    std::vector<std::vector<BrushArea>> stamps_;
    InflateBrush brush_;
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

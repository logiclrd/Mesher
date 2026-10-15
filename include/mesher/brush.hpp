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

// Units of strength: displacement in mesh units per full-influence stamp.
// The displacement follows the normalized equal-weight sum of unit face
// normals incident to each vertex. Freezing directions at stroke start is the
// default: repeated stamps cannot feed back on their own deformed normals.
// PerStamp retains the experimental live-normal behaviour when desired.
enum class NormalSampling { StrokeStart, PerStamp };
class InflateBrush final : public Brush {
public:
    explicit InflateBrush(double strength, NormalSampling sampling = NormalSampling::StrokeStart);
    void begin(Mesh& mesh) override;
    void stamp(Mesh& mesh, std::span<const BrushArea> areas) override;
    void end(Mesh& mesh) override;
private:
    double strength_;
    NormalSampling sampling_;
    std::vector<std::vector<std::uint32_t>> incidentFaces_;
    std::vector<Vec3> strokeNormals_;
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

// Visible symmetric demonstration: two mirror-image hemisphere-side arcs
// on the front of the sphere, each approximating a half-circle. Returns
// the number of simultaneous stamps. Topology never changes here.
std::size_t inflateHalfCircleDemo(Mesh& mesh, double step = 0.02,
                                  double strength = 0.012,
                                  std::uint32_t segments = 128,
                                  double radius = 0.4);

} // namespace mesher

#include "mesher/brush.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mesher {
namespace {

bool finite(Vec3 p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}

void validateArea(const BrushArea& area) {
    if (!finite(area.center) || !std::isfinite(area.radius) || area.radius <= 0 ||
        !std::isfinite(area.hardness) || area.hardness < 0 || area.hardness > 1)
        throw std::invalid_argument("Brush area needs finite center, positive radius, and hardness in [0,1]");
}

} // namespace

double brushFalloff(double distance, double radius, double hardness) {
    if (!std::isfinite(distance) || distance < 0 || !std::isfinite(radius) || radius <= 0 ||
        !std::isfinite(hardness) || hardness < 0 || hardness > 1)
        throw std::invalid_argument("Invalid brush falloff arguments");
    if (distance >= radius) return 0.0;
    const double fullRadius = hardness * radius;
    if (distance <= fullRadius) return 1.0;
    const double t = (distance - fullRadius) / (radius - fullRadius);
    // Complement of smootherstep: zero slope and curvature at both ends.
    return 1.0 - t*t*t*(t*(t*6.0 - 15.0) + 10.0);
}

double brushInfluence(Vec3 point, std::span<const BrushArea> areas) {
    if (!finite(point)) throw std::invalid_argument("Invalid influence sample position");
    double result = 0;
    for (const auto& area : areas) {
        validateArea(area);
        const double d = length(point - area.center);
        if (d < area.radius) result = std::max(result, brushFalloff(d, area.radius, area.hardness));
        if (result == 1.0) break;
    }
    return result;
}

InflateBrush::InflateBrush(double strength) : strength_(strength) {
    if (!std::isfinite(strength))
        throw std::invalid_argument("Inflate strength must be finite");
}

void InflateBrush::begin(Mesh& mesh) {
    incidentFaces_.assign(mesh.vertices.size(), {});
    for (std::uint32_t i = 0; i < mesh.triangles.size(); ++i) {
        auto tri = mesh.triangles[i];
        if (tri.a >= incidentFaces_.size() || tri.b >= incidentFaces_.size() ||
            tri.c >= incidentFaces_.size())
            throw std::invalid_argument("Mesh has out-of-range triangle indices");
        incidentFaces_[tri.a].push_back(i);
        incidentFaces_[tri.b].push_back(i);
        incidentFaces_[tri.c].push_back(i);
    }
}

void InflateBrush::stamp(Mesh& mesh, std::span<const BrushArea> areas) {
    if (incidentFaces_.size() != mesh.vertices.size())
        throw std::logic_error("Inflate brush requires begin and unchanged topology");
    // All normals and displacements are calculated before modifying any vertex.
    // Repeating this process changes the normals: it can fold a surface under
    // intense deformation. This is intentional in the diagnostic demo.
    struct Movement { std::size_t index; Vec3 offset; };
    std::vector<Movement> movements;
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        const double influence = brushInfluence(mesh.vertices[i], areas);
        if (influence <= 0 || incidentFaces_[i].empty()) continue;
        Vec3 sum{};
        for (auto faceIndex : incidentFaces_[i]) {
            const auto t = mesh.triangles[faceIndex];
            const Vec3 n = cross(mesh.vertices[t.b] - mesh.vertices[t.a],
                                 mesh.vertices[t.c] - mesh.vertices[t.a]);
            if (length(n) > 1e-20) sum = sum + normalized(n);
        }
        if (length(sum) > 1e-20) {
            const Vec3 delta = normalized(sum) * (strength_ * influence);
            if (!finite(mesh.vertices[i] + delta))
                throw std::overflow_error("Inflate displacement is not finite");
            movements.push_back({i, delta});
        }
    }
    for (const auto& m : movements)
        mesh.vertices[m.index] = mesh.vertices[m.index] + m.offset;
}

void InflateBrush::end(Mesh&) { incidentFaces_.clear(); }

BrushStroke::BrushStroke(Mesh& mesh, Brush& brush, double step)
    : mesh_(mesh), brush_(brush), step_(step), untilNext_(step) {
    if (!std::isfinite(step) || step <= 0)
        throw std::invalid_argument("Step must be finite and positive");
}

BrushStroke::~BrushStroke() {
    if (active_) {
        try { brush_.end(mesh_); } catch (...) { /* destructors must not throw */ }
    }
}

void BrushStroke::start(std::vector<BrushArea> areas) {
    if (active_) throw std::logic_error("Stroke already started");
    if (areas.empty()) throw std::invalid_argument("Stroke needs at least one area");
    for (const auto& area : areas) validateArea(area);
    brush_.begin(mesh_);
    active_ = true;
    areas_ = std::move(areas);
    untilNext_ = step_;
    stamps_ = 0;
    stamp();
}

void BrushStroke::stamp() {
    brush_.stamp(mesh_, areas_);
    lastStamped_.clear();
    for (const auto& a : areas_) lastStamped_.push_back(a.center);
    ++stamps_;
}

void BrushStroke::advance(std::span<const Vec3> positions) {
    if (!active_) throw std::logic_error("Stroke has not started");
    if (positions.size() != areas_.size()) throw std::invalid_argument("Stroke point count changed");
    if (positions.empty()) throw std::logic_error("Stroke has no areas");
    const double travel = length(positions[0] - areas_[0].center);
    if (!std::isfinite(travel)) throw std::invalid_argument("Invalid stroke position");
    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (!finite(positions[i])) throw std::invalid_argument("Invalid stroke position");
        const double d = length(positions[i] - areas_[i].center);
        if (std::abs(d - travel) > 1e-9 * std::max({1.0, travel, step_}))
            throw std::invalid_argument("All stroke points must travel equal distances");
    }
    if (travel == 0) return;

    std::vector<Vec3> from;
    from.reserve(areas_.size());
    for (const auto& a : areas_) from.push_back(a.center);
    // Maintain the sampling phase across segment boundaries, independent
    // of the number and sizes of the incoming position updates.
    double distance = untilNext_;
    while (distance <= travel + 1e-12 * std::max(1.0, travel)) {
        const double t = std::min(distance / travel, 1.0);
        for (std::size_t i = 0; i < areas_.size(); ++i)
            areas_[i].center = from[i] + (positions[i] - from[i]) * t;
        stamp();
        distance += step_;
    }
    untilNext_ = distance - travel; // the overshoot into the next segment
    for (std::size_t i = 0; i < areas_.size(); ++i) areas_[i].center = positions[i];
}

void BrushStroke::update(std::span<const Vec3> positions) { advance(positions); }

void BrushStroke::finish(std::span<const Vec3> positions) {
    advance(positions);
    bool atLastStamp = true;
    for (std::size_t i = 0; i < areas_.size(); ++i)
        if (length(areas_[i].center - lastStamped_[i]) > 1e-12) atLastStamp = false;
    if (!atLastStamp) stamp();
    brush_.end(mesh_);
    active_ = false;
}

namespace {
struct DemoRecorder : Brush {
    std::vector<std::vector<BrushArea>> frames;
    void stamp(Mesh&, std::span<const BrushArea> areas) override {
        frames.emplace_back(areas.begin(), areas.end());
    }
};

// Produce both mirror-image paths through the ordinary brush-stroke spacing
// logic. Geometry is NOT modified during recording.
std::vector<std::vector<BrushArea>> makeDemoStamps(double step,
                                                    std::uint32_t segments,
                                                    double radius) {
    if (segments == 0) throw std::invalid_argument("Demo requires at least one segment");
    if (!std::isfinite(radius) || radius <= 0)
        throw std::invalid_argument("Demo brush radius must be positive and finite");
    constexpr double pi = 3.14159265358979323846264338327950288;
    constexpr double y = -0.75;
    const double arcRadius = std::sqrt(1 - y*y);
    auto point = [&](std::uint32_t i) -> Vec3 {
        const double angle = -pi / 2 + pi * (double(i) / segments);
        const double x = i == 0 || i == segments ? 0.0 : arcRadius * std::cos(angle);
        return {x, y, arcRadius * std::sin(angle)};
    };
    Mesh dummy;
    DemoRecorder recorder;
    BrushStroke stroke(dummy, recorder, step);
    const auto first = point(0);
    stroke.start({{first, radius, 0.3},
                  {{-first.x, first.y, first.z}, radius, 0.3}});
    for (std::uint32_t i = 1; i < segments; ++i) {
        const auto p = point(i);
        const Vec3 pair[] = {p, {-p.x, p.y, p.z}};
        stroke.update(pair);
    }
    const auto p = point(segments);
    const Vec3 pair[] = {p, {-p.x, p.y, p.z}};
    stroke.finish(pair);
    return std::move(recorder.frames);
}
} // namespace

InflateDemoAnimation::InflateDemoAnimation(Mesh& mesh, double step,
                                           double strength,
                                           std::uint32_t segments, double radius,
                                           CollapseSettings collapse, bool symmetric)
    : mesh_(mesh), originalMesh_(mesh),
      stamps_(makeDemoStamps(step, segments, radius)), brush_(strength),
      collapser_(mesh, collapse,
                 symmetric ? symmetricSphereMirrors(static_cast<std::uint32_t>(mesh.vertices.size()))
                           : std::vector<std::uint32_t>{}),
      originalMirrors_(collapser_.mirrors()) {
    afterCollapse_ = measureMesh(mesh_);
    preCollapse_ = afterCollapse_;
}

InflateDemoAnimation::~InflateDemoAnimation() {
    if (brushActive_) brush_.end(mesh_);
}

bool InflateDemoAnimation::advance() {
    if (next_ == stamps_.size()) return false;
    if (!brushActive_) {
        brush_.begin(mesh_);
        brushActive_ = true;
    }
    // Diagnose compression from the same vertices immediately before and
    // immediately after this stamp, before any topology changes.
    const auto before = mesh_.vertices;
    brush_.stamp(mesh_, stamps_[next_]);
    preCollapse_ = measureMesh(mesh_, before, collapser_.minimumEdge());
    lastCollapse_ = collapser_.apply(mesh_, before);
    totalRemoved_ += lastCollapse_.removedVertices;
    afterCollapse_ = measureMesh(mesh_);
    ++next_;
    if (lastCollapse_.accepted && next_ < stamps_.size()) {
        // Incident-face lookup must follow the revised triangle connectivity.
        brush_.end(mesh_);
        brush_.begin(mesh_);
    }
    if (next_ == stamps_.size()) {
        brush_.end(mesh_);
        brushActive_ = false;
    }
    return true;
}

void InflateDemoAnimation::reset() {
    if (brushActive_) {
        brush_.end(mesh_);
        brushActive_ = false;
    }
    mesh_ = originalMesh_;
    collapser_.reset(mesh_, originalMirrors_);
    preCollapse_ = afterCollapse_ = measureMesh(mesh_);
    lastCollapse_ = {};
    totalRemoved_ = 0;
    next_ = 0;
}

std::size_t inflateHalfCircleDemo(Mesh& mesh, double step, double strength,
                                  std::uint32_t segments, double radius) {
    InflateDemoAnimation animation(mesh, step, strength, segments, radius);
    while (animation.advance()) {}
    return animation.completed();
}

} // namespace mesher

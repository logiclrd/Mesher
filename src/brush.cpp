#include "mesher/brush.hpp"

#include <algorithm>
#include <chrono>
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

InflateBrush::InflateBrush(double strength, CurvatureSettings curvature)
    : strength_(strength), curvature_(curvature) {
    if (!std::isfinite(strength))
        throw std::invalid_argument("Inflate strength must be finite");
    if (!std::isfinite(curvature.strength) || curvature.strength < 0)
        throw std::invalid_argument("Curvature strength must be finite and non-negative");
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
    spatial_.rebuild(mesh.vertices);
    movedVertices_.clear();
}

void InflateBrush::stamp(Mesh& mesh, std::span<const BrushArea> areas) {
    if(incidentFaces_.size()!=mesh.vertices.size() ||
       spatial_.indexedVertices()!=mesh.vertices.size())
        throw std::logic_error("Inflate requires begin and unchanged topology");
    movedVertices_.clear();

    // The octree-leaf equivalent makes a brush query proportional to nearby
    // vertices rather than the entire model. Exact influence and unchanged
    // movement arithmetic retain the original brush semantics.
    std::vector<std::uint32_t> candidates;
    for(const auto& area:areas) {
        validateArea(area);
        spatial_.querySphere(mesh.vertices,area.center,area.radius,candidates);
    }
    std::sort(candidates.begin(),candidates.end());
    candidates.erase(std::unique(candidates.begin(),candidates.end()),
                     candidates.end());

    struct Movement { std::uint32_t index; Vec3 offset; };
    std::vector<Movement> movements;
    movements.reserve(candidates.size());
    const bool curvature=curvature_.enabled && curvature_.strength>0;
    for(const std::uint32_t i:candidates) {
        const double influence=brushInfluence(mesh.vertices[i],areas);
        if(influence<=0 || incidentFaces_[i].empty())continue;
        Vec3 sum{},cotangentSum{};
        double vertexArea=0,edgeLengthSum=0;
        std::uint32_t edgeCount=0;
        const Vec3 point=mesh.vertices[i];
        for(const auto faceIndex:incidentFaces_[i]) {
            const auto t=mesh.triangles[faceIndex];
            const Vec3 a=mesh.vertices[t.a],b=mesh.vertices[t.b],c=mesh.vertices[t.c];
            const Vec3 n=cross(b-a,c-a);
            const double twiceArea=length(n);
            if(twiceArea>1e-20)sum=sum+n/twiceArea;
            if(!curvature || twiceArea<1e-14)continue;
            const double cotA=std::clamp(dot(b-a,c-a)/twiceArea,-20.0,20.0);
            const double cotB=std::clamp(dot(a-b,c-b)/twiceArea,-20.0,20.0);
            const double cotC=std::clamp(dot(a-c,b-c)/twiceArea,-20.0,20.0);
            const double ab=length(b-a),bc=length(c-b),ca=length(a-c);
            if(i==t.a) {
                cotangentSum=cotangentSum+(b-a)*cotC+(c-a)*cotB;
                edgeLengthSum+=ab+ca;
            }else if(i==t.b) {
                cotangentSum=cotangentSum+(a-b)*cotC+(c-b)*cotA;
                edgeLengthSum+=ab+bc;
            }else {
                cotangentSum=cotangentSum+(a-c)*cotB+(b-c)*cotA;
                edgeLengthSum+=bc+ca;
            }
            vertexArea+=twiceArea/6.0;
            edgeCount+=2;
        }
        if(length(sum)<=1e-20)continue;
        const Vec3 direction=normalized(sum);
        double multiplier=1.0;
        if(curvature && vertexArea>1e-14 && edgeCount>0) {
            const Vec3 laplacian=cotangentSum/(2*vertexArea);
            const double nominalLength=edgeLengthSum/edgeCount;
            const double signedCurvature=dot(laplacian,direction);
            multiplier=std::clamp(1.0+curvature_.strength*nominalLength*signedCurvature,
                                  0.10,3.0);
        }
        const Vec3 delta=direction*(strength_*influence*multiplier);
        if(!finite(point+delta))
            throw std::overflow_error("Inflate displacement is not finite");
        movements.push_back({i,delta});
    }
    movedVertices_.reserve(movements.size());
    for(const auto& movement:movements) {
        mesh.vertices[movement.index]=mesh.vertices[movement.index]+movement.offset;
        movedVertices_.push_back(movement.index);
    }
    spatial_.update(mesh.vertices,movedVertices_);
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
    skippedStamps_ = 0;
    timed_ = false;
    timeSpeed_ = 0;
    currentTime_ = lastStampTime_ = 0;
    stamp();
}

void BrushStroke::stamp() {
    brush_.stamp(mesh_, areas_);
    lastStamped_.clear();
    for (const auto& a : areas_) lastStamped_.push_back(a.center);
    lastStampTime_ = currentTime_;
    ++stamps_;
}

void BrushStroke::startTimed(std::vector<BrushArea> areas, double timeSpeed) {
    if(!std::isfinite(timeSpeed) || timeSpeed<=0)
        throw std::invalid_argument("Time speed must be positive and finite");
    start(std::move(areas));
    timed_ = true;
    timeSpeed_ = timeSpeed;
}

void BrushStroke::advance(std::span<const Vec3> positions, double elapsedSeconds,
                          bool realtime, bool terminal) {
    if (!active_) throw std::logic_error("Stroke has not started");
    if (positions.size() != areas_.size()) throw std::invalid_argument("Stroke point count changed");
    if (positions.empty()) throw std::logic_error("Stroke has no areas");
    if(!std::isfinite(elapsedSeconds) ||
       elapsedSeconds+1e-12*std::max(1.0,currentTime_)<currentTime_)
        throw std::invalid_argument("Stroke time must be finite and nondecreasing");
    // Time differences smaller than floating-point clock precision are zero.
    elapsedSeconds=std::max(elapsedSeconds,currentTime_);
    const double dt=elapsedSeconds-currentTime_;
    const double spatial=length(positions[0] - areas_[0].center);
    const double travel=std::hypot(spatial,timeSpeed_*dt);
    if (!std::isfinite(travel)) throw std::invalid_argument("Invalid stroke position");
    for (std::size_t i = 0; i < positions.size(); ++i) {
        if (!finite(positions[i])) throw std::invalid_argument("Invalid stroke position");
        const double d = std::hypot(length(positions[i] - areas_[i].center),timeSpeed_*dt);
        if (std::abs(d - travel) > 1e-9 * std::max({1.0, travel, step_}))
            throw std::invalid_argument("All stroke points must travel equal distances");
    }
    if (travel == 0) {
        currentTime_=elapsedSeconds;
        if(realtime && terminal) {
            // The terminal input may coincide exactly with the last stamp.
            brush_.end(mesh_);
            active_=false;
        }
        return;
    }

    const double fromTime=currentTime_;
    std::vector<Vec3> from;
    from.reserve(areas_.size());
    for (const auto& a : areas_) from.push_back(a.center);
    // Real-time painting must render between costly stamps. Do not execute
    // the full, potentially unbounded catch-up loop from a single mouse
    // update. Instead select the most recent grid sample (or the terminal
    // position on release), and account for dropped earlier samples. The
    // phase advances by *all* 4D travel, preventing feedback/backlog.
    if(realtime) {
        const double tolerance=1e-12*std::max(1.0,travel);
        std::size_t due=0;
        if(travel+tolerance>=untilNext_) {
            const double count=std::floor(std::max(0.0,
                travel-untilNext_+tolerance)/step_)+1.0;
            if(!std::isfinite(count) ||
               count>double(std::numeric_limits<std::size_t>::max()/2))
                throw std::overflow_error("Too many stroke samples for one update");
            due=static_cast<std::size_t>(count);
        }
        if(due!=0 && !terminal) {
            const double sampleDistance=untilNext_+double(due-1)*step_;
            const double t=std::clamp(sampleDistance/travel,0.0,1.0);
            for(std::size_t i=0;i<areas_.size();++i)
                areas_[i].center=from[i]+(positions[i]-from[i])*t;
            currentTime_=fromTime+dt*t;
            stamp();
            skippedStamps_+=due-1;
        } else if(due!=0) {
            // On release the newest sample is the actual endpoint instead
            // of the grid point immediately preceding it.
            skippedStamps_+=due-1;
        }
        untilNext_+=double(due)*step_-travel;
        if(untilNext_<=0)untilNext_=step_;
        for(std::size_t i=0;i<areas_.size();++i)
            areas_[i].center=positions[i];
        currentTime_=elapsedSeconds;
        if(terminal) {
            bool same=timeSpeed_*(currentTime_-lastStampTime_)<=1e-12;
            for(std::size_t i=0;i<areas_.size();++i)
                if(length(areas_[i].center-lastStamped_[i])>1e-12)same=false;
            if(!same)stamp();
            brush_.end(mesh_);
            active_=false;
        }
        return;
    }
    // Maintain the sampling phase across segment boundaries, independent
    // of the number and sizes of the incoming position updates.
    double distance = untilNext_;
    while (distance <= travel + 1e-12 * std::max(1.0, travel)) {
        const double t = std::min(distance / travel, 1.0);
        for (std::size_t i = 0; i < areas_.size(); ++i)
            areas_[i].center = from[i] + (positions[i] - from[i]) * t;
        currentTime_=fromTime+dt*t;
        stamp();
        distance += step_;
    }
    untilNext_ = distance - travel; // the overshoot into the next segment
    for (std::size_t i = 0; i < areas_.size(); ++i) areas_[i].center = positions[i];
    currentTime_=elapsedSeconds;
}

void BrushStroke::update(std::span<const Vec3> positions) {
    if(timed_)throw std::logic_error("Use updateTimed on a timed stroke");
    advance(positions,currentTime_);
}
void BrushStroke::updateTimed(std::span<const Vec3> positions, double elapsedSeconds) {
    if(!timed_)throw std::logic_error("Use startTimed before updateTimed");
    advance(positions,elapsedSeconds);
}
void BrushStroke::updateTimedRealtime(std::span<const Vec3> positions,
                                     double elapsedSeconds) {
    if(!timed_)throw std::logic_error("Use startTimed before updateTimedRealtime");
    advance(positions,elapsedSeconds,true,false);
}
void BrushStroke::finishTimedRealtime(std::span<const Vec3> positions,
                                     double elapsedSeconds) {
    if(!timed_)throw std::logic_error("Use startTimed before finishTimedRealtime");
    advance(positions,elapsedSeconds,true,true);
}
void BrushStroke::finish(std::span<const Vec3> positions) {
    if(timed_)throw std::logic_error("Use finishTimed on a timed stroke");
    advance(positions,currentTime_);
    bool atLastStamp = true;
    for (std::size_t i = 0; i < areas_.size(); ++i)
        if (length(areas_[i].center - lastStamped_[i]) > 1e-12) atLastStamp = false;
    if (!atLastStamp) stamp();
    brush_.end(mesh_);
    active_ = false;
}
void BrushStroke::finishTimed(std::span<const Vec3> positions,double elapsedSeconds) {
    if(!timed_)throw std::logic_error("Use startTimed before finishTimed");
    advance(positions,elapsedSeconds);
    bool same=(timeSpeed_*(currentTime_-lastStampTime_)<=1e-12);
    for(std::size_t i=0;i<areas_.size();++i)
        if(length(areas_[i].center-lastStamped_[i])>1e-12)same=false;
    if(!same)stamp();
    brush_.end(mesh_);
    active_=false;
}

AdaptiveInflateBrush::AdaptiveInflateBrush(const Mesh& mesh,double strength,
                                           CurvatureSettings curvature,
                                           CollapseSettings collapse,
                                           RefineSettings refine,bool symmetric)
    : inflate_(strength,curvature),
      collapser_(mesh,collapse,symmetric
            ? symmetricSphereMirrors(static_cast<std::uint32_t>(mesh.vertices.size()))
            : std::vector<std::uint32_t>{}),
      refiner_(mesh,refine,collapser_.mirrors()) {}
void AdaptiveInflateBrush::begin(Mesh& mesh) {
    inflate_.begin(mesh);
    begun_=true;
}
void AdaptiveInflateBrush::stamp(Mesh& mesh,std::span<const BrushArea> areas) {
    if(!begun_)throw std::logic_error("Adaptive Inflate has not begun");
    using Clock=std::chrono::steady_clock;
    auto start=Clock::now();
    auto before=mesh.vertices;
    inflate_.stamp(mesh,areas);
    auto afterInflate=Clock::now();
    const auto refined=refiner_.apply(mesh,before);
    if(refined.splitPairs)collapser_.setMirrors(refiner_.mirrors());
    auto afterRefine=Clock::now();
    const auto collapsed=collapser_.apply(mesh,before);
    if(collapsed.accepted)refiner_.setMirrors(collapser_.mirrors());
    auto afterCollapse=Clock::now();
    if(refined.splitPairs || refined.flipPairs || collapsed.accepted) {
        inflate_.end(mesh);
        inflate_.begin(mesh);
    }
    auto finished=Clock::now();
    auto seconds=[](auto a,auto b) {
        return std::chrono::duration<double>(b-a).count();
    };
    lastTimings_={seconds(start,afterInflate),seconds(afterInflate,afterRefine),
                  seconds(afterRefine,afterCollapse),seconds(afterCollapse,finished)};
    ++completed_;
}
void AdaptiveInflateBrush::end(Mesh& mesh) {
    if(begun_)inflate_.end(mesh);
    begun_=false;
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
                                                    double radius,bool symmetric) {
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
    std::vector<BrushArea> areas{{first,radius,.3}};
    if(symmetric)areas.push_back({{-first.x,first.y,first.z},radius,.3});
    stroke.start(std::move(areas));
    auto positions=[&](Vec3 p) {
        std::vector<Vec3> points{p};
        if(symmetric)points.push_back({-p.x,p.y,p.z});
        return points;
    };
    for (std::uint32_t i=1;i<segments;++i)
        stroke.update(positions(point(i)));
    stroke.finish(positions(point(segments)));
    return std::move(recorder.frames);
}
} // namespace

InflateDemoAnimation::InflateDemoAnimation(Mesh& mesh, double step,
                                           double strength,
                                           std::uint32_t segments, double radius,
                                           CollapseSettings collapse, bool symmetric,
                                           CurvatureSettings curvature,
                                           RefineSettings refine)
    : mesh_(mesh), originalMesh_(mesh),
      stamps_(makeDemoStamps(step, segments, radius,symmetric)), brush_(strength, curvature),
      collapser_(mesh, collapse,
                 symmetric ? symmetricSphereMirrors(static_cast<std::uint32_t>(mesh.vertices.size()))
                           : std::vector<std::uint32_t>{}),
      refiner_(mesh,refine,collapser_.mirrors()),
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
    auto before = mesh_.vertices;
    brush_.stamp(mesh_, stamps_[next_]);
    preCollapse_ = measureMesh(mesh_, before, collapser_.minimumEdge());
    lastRefine_ = refiner_.apply(mesh_, before);
    totalAdded_ += lastRefine_.newVertices;
    totalFlips_ += lastRefine_.flipPairs;
    if(lastRefine_.splitPairs)collapser_.setMirrors(refiner_.mirrors());
    lastCollapse_ = collapser_.apply(mesh_, before);
    totalRemoved_ += lastCollapse_.removedVertices;
    if(lastCollapse_.accepted)refiner_.setMirrors(collapser_.mirrors());
    afterCollapse_ = measureMesh(mesh_);
    ++next_;
    if ((lastCollapse_.accepted || lastRefine_.splitPairs || lastRefine_.flipPairs) &&
        next_ < stamps_.size()) {
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
    refiner_.reset(mesh_, originalMirrors_);
    preCollapse_ = afterCollapse_ = measureMesh(mesh_);
    lastCollapse_ = {};
    lastRefine_ = {};
    totalRemoved_ = totalAdded_ = totalFlips_ = 0;
    next_ = 0;
}

std::size_t inflateHalfCircleDemo(Mesh& mesh, double step, double strength,
                                  std::uint32_t segments, double radius) {
    InflateDemoAnimation animation(mesh, step, strength, segments, radius);
    while (animation.advance()) {}
    return animation.completed();
}

} // namespace mesher

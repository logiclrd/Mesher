#pragma once
#include "mesher/picking.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <string>
namespace mesher {
// View-only bounds: never quantizes, translates or edits saved source vertices.
struct ViewportSceneBounds {
    Vec3 minimum{}, maximum{};
    bool hasVertices=false;
    void include(Vec3 p) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            throw std::invalid_argument("View framing requires finite vertices");
        if(!hasVertices) {minimum=maximum=p;hasVertices=true;return;}
        minimum={std::min(minimum.x,p.x),std::min(minimum.y,p.y),
                 std::min(minimum.z,p.z)};
        maximum={std::max(maximum.x,p.x),std::max(maximum.y,p.y),
                 std::max(maximum.z,p.z)};
    }
    Vec3 center() const noexcept {
        if(!hasVertices)return {};
        // Avoid overflow in min+max for widely displaced source data.
        return {minimum.x*.5+maximum.x*.5,
                minimum.y*.5+maximum.y*.5,
                minimum.z*.5+maximum.z*.5};
    }
    double radius() const noexcept {
        if(!hasVertices)return 0;
        return std::hypot((maximum.x-minimum.x)*.5,
                          (maximum.y-minimum.y)*.5,
                          (maximum.z-minimum.z)*.5);
    }
};
constexpr double kViewportMinExponent=-5.0;
constexpr double kViewportMaxExponent=5.0;
inline double viewportScaleFromExponent(double exponent) noexcept {
    return std::pow(10.0,std::clamp(exponent,kViewportMinExponent,
                                    kViewportMaxExponent));
}
inline double viewportFitExponent(double radius,double eyeDistance,
                                  double aspect) noexcept {
    if(!(std::isfinite(radius)&&radius>0&&
         std::isfinite(eyeDistance)&&eyeDistance>0&&
         std::isfinite(aspect)&&aspect>0))return 0.0;
    const double visibleRadius=eyeDistance*std::tan(std::numbers::pi/8.0)*
                               std::min(1.0,aspect)*0.70;
    return std::clamp(std::log10(visibleRadius/radius),
                      kViewportMinExponent,kViewportMaxExponent);
}
// Movement is continuous in logarithmic exponent space. Ctrl makes the
// same number of mouse pixels one fifteenth as powerful.
inline double viewportScaleDrag(double exponent,double deltaY,
                                double trackHeight,bool ctrl) noexcept {
    if(!std::isfinite(deltaY)||!(trackHeight>0))return exponent;
    const double delta=-deltaY*(kViewportMaxExponent-kViewportMinExponent)/
                        (trackHeight*(ctrl?15.0:1.0));
    return std::clamp(exponent+delta,
                      kViewportMinExponent,kViewportMaxExponent);
}
inline std::string viewportScaleLabel(double exponent) {
    std::ostringstream out;
    out<<"10^"<<std::fixed<<std::setprecision(4)<<exponent;
    return out.str();
}
// The 4x4 screen-space probe is performed once per scale gesture. Each
// source hit is compared in the displayed coordinate system, to select
// the nearest visible point without changing the immutable mesh.
struct ViewportZoomProbe {
    Vec3 sourcePoint{};
    double distance=0;
    bool surface=false;
};
inline ViewportZoomProbe nearestViewportProbe(
        const std::vector<ViewportZoomProbe>& candidates,
        Vec3 fallbackPoint,double fallbackDistance) {
    ViewportZoomProbe chosen{fallbackPoint,fallbackDistance,false};
    for(const auto& candidate:candidates)
        if(candidate.surface&&std::isfinite(candidate.distance)&&
           candidate.distance>=0&&(!chosen.surface||
           candidate.distance<chosen.distance))chosen=candidate;
    return chosen;
}
constexpr double kViewportZoomIdleSeconds=2.0;
constexpr double kViewportZoomFollowAt60Fps=0.8;
inline double viewportZoomFollowFraction(double deltaSeconds) noexcept {
    if(!std::isfinite(deltaSeconds)||deltaSeconds<=0)return 0;
    return 1.0-std::pow(1.0-kViewportZoomFollowAt60Fps,
                         60.0*deltaSeconds);
}
class ViewportScaleFollow {
public:
    bool active() const noexcept {return active_;}
    Vec3 cameraOffset() const noexcept {return offset_;}
    Vec3 targetCameraOffset() const noexcept {return targetOffset_;}
    Vec3 sourceAnchor() const noexcept {return sourceAnchor_;}
    void reset() noexcept { *this=ViewportScaleFollow{}; }
    void cancel() noexcept {
        active_=false;
        targetOffset_=offset_;
    }
    void begin(Vec3 anchorSource,Vec3 anchorDisplayed,Vec3 baseEye,
               double timeSeconds) noexcept {
        sourceAnchor_=anchorSource;
        // Full vector, not a scalar distance: off-centre targets retain
        // their original position relative to the camera.
        cameraToAnchor_=(baseEye+offset_)-anchorDisplayed;
        targetOffset_=offset_;
        active_=true;
        lastChange_=timeSeconds;
    }
    void change(Vec3 anchorDisplayed,Vec3 baseEye,double timeSeconds) noexcept {
        if(!active_)return;
        targetOffset_=anchorDisplayed+cameraToAnchor_-baseEye;
        lastChange_=timeSeconds;
    }
    void advance(double deltaSeconds,double timeSeconds) noexcept {
        const double alpha=viewportZoomFollowFraction(deltaSeconds);
        offset_=offset_+(targetOffset_-offset_)*alpha;
        if(active_&&timeSeconds-lastChange_>=kViewportZoomIdleSeconds)
            active_=false;
    }
private:
    bool active_=false;
    double lastChange_=0;
    Vec3 sourceAnchor_{},cameraToAnchor_{},offset_{},targetOffset_{};
}
struct ViewportClipRange {double nearPlane{},farPlane{};};
inline ViewportClipRange viewportClipRange(Vec3 eye,Vec3 sceneCenter,
                                          double displayedRadius) noexcept {
    const double distance=length(eye-sceneCenter);
    const double radius=std::max(0.0,displayedRadius);
    if(!std::isfinite(distance)||!std::isfinite(radius))return {.05,100.0};
    // Clip adapts after dolly and zoom, including when the camera goes
    // inside a very large surface. Keep the front of the bounding sphere.
    const double front=distance-radius*1.05;
    const double near=front>0?std::clamp(front*.1,1e-4,1e8):
                                std::max(1e-4,distance*1e-5);
    const double far=std::max({100.0,distance+radius*1.5+10.0,near*10.0});
    return {near,far};
}
} // namespace mesher

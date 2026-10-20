#pragma once
#include "mesher/picking.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace mesher {

// UI-only rigid display transform. NEVER applied to a MeshRevision,
// original FaceId, mirror pair, checkpoint, or worker stroke coordinate.
// Column vectors: displayed = R * source + translation.
class ViewportOrbit {
public:
    Vec3 displayed(Vec3 p) const noexcept {
        return transformVector(p)+translation_;
    }
    Vec3 transformVector(Vec3 v) const noexcept {
        return basis_[0]*v.x+basis_[1]*v.y+basis_[2]*v.z;
    }
    Vec3 inverseVector(Vec3 v) const noexcept {
        return {dot(v,basis_[0]),dot(v,basis_[1]),dot(v,basis_[2])};
    }
    Ray pickingRay(Ray visualRay) const noexcept {
        return {inverseVector(visualRay.origin-translation_),
                inverseVector(visualRay.direction)};
    }
    const std::array<Vec3,3>& basis() const noexcept {return basis_;}
    Vec3 translation() const noexcept {return translation_;}

    // A left-to-right drag yaws the nearer (negative-Y) material toward
    // screen right. A downward drag pitches it toward screen bottom.
    // The pivot is expressed in ORIGINAL source space and is fixed for
    // this RMB gesture, even if its initial location was a ray hit.
    void drag(Vec3 pivotSource,double dxPixels,double dyPixels,
              double framebufferSpan) noexcept {
        if(!(std::isfinite(dxPixels)&&std::isfinite(dyPixels)&&
             std::isfinite(framebufferSpan)&&framebufferSpan>0))return;
        const double radiansPerPixel=2.0*std::numbers::pi/
                                     framebufferSpan;
        const double az=std::remainder(dxPixels*radiansPerPixel,
                                        2.0*std::numbers::pi);
        const double ax=std::remainder(dyPixels*radiansPerPixel,
                                        2.0*std::numbers::pi);
        const auto rotate=[](Vec3 v,double z,double x) {
            const double cz=std::cos(z),sz=std::sin(z);
            const double cx=std::cos(x),sx=std::sin(x);
            // Z yaw followed by X pitch in fixed world axes.
            const Vec3 yaw{cz*v.x-sz*v.y,sz*v.x+cz*v.y,v.z};
            return Vec3{yaw.x,cx*yaw.y-sx*yaw.z,
                              sx*yaw.y+cx*yaw.z};
        };
        const Vec3 anchor=displayed(pivotSource);
        for(auto& axis:basis_)axis=rotate(axis,az,ax);
        translation_=anchor+rotate(translation_-anchor,az,ax);
    }

    // The background pivot lies in a DISPLAY-space plane parallel to the
    // viewport, through the *transformed* current geometry centroid.
    // The exact original-source surface hit wins if the cursor is on a face.
    // Convert any plane hit back into original space for fixed-source orbit.
    Vec3 choosePivot(Ray visualRay,std::optional<Vec3> pickedSource,
                     Vec3 sourceCentroid,Vec3 viewForward) const noexcept {
        if(pickedSource)return *pickedSource;
        const Vec3 displayCentroid=displayed(sourceCentroid);
        const double denominator=dot(visualRay.direction,viewForward);
        if(!std::isfinite(denominator)||std::abs(denominator)<1e-12)
            return sourceCentroid;
        // A nonzero divisor on all paths avoids MSVC /W4 C4723 in
        // constant-folded parallel-ray regression inputs.
        const double safe=denominator==0.0?1.0:denominator;
        const double t=dot(displayCentroid-visualRay.origin,viewForward)/
                       safe;
        if(!std::isfinite(t)||t<=0)return sourceCentroid;
        return inverseVector(visualRay.origin+visualRay.direction*t-
                             translation_);
    }

    // Alt-RMB: positive mouse-X walks the *viewer* forward along the
    // current centre ray, i.e. shifts the visually displayed scene in
    // the opposite direction. This is the camera-translation equivalent
    // of the existing scene model translation, not a mesh edit.
    void dolly(Vec3 cameraForward,double dxPixels,double framebufferSpan,
               double eyeDistance) noexcept {
        if(!(std::isfinite(dxPixels)&&std::isfinite(framebufferSpan)&&
             std::isfinite(eyeDistance)&&framebufferSpan>0&&
             eyeDistance>0))return;
        const double unitsPerPixel=2.0*eyeDistance*
                                   std::tan(std::numbers::pi/8.0)/
                                   framebufferSpan;
        translation_=translation_-cameraForward*(dxPixels*unitsPerPixel);
    }

private:
    std::array<Vec3,3> basis_{{{1,0,0},{0,1,0},{0,0,1}}};
    Vec3 translation_{};
};


// Pure cursor-wrap planning, independent of GLFW/Vulkan. Coordinates are
// GLFW window-client coordinates; the caller performs platform cursor warp
// only during an active RMB gesture and only while the window is focused.
struct ViewportCursorStep {
    double dx{},dy{},warpX{},warpY{};
    bool warp{};
};
class ViewportCursorWrap {
public:
    void begin(double x,double y,double width,double height) noexcept {
        x_=x;y_=y;width_=width;height_=height;
        active_=true;allowWarp_=true;awaiting_=false;
    }
    void stop() noexcept {active_=false;awaiting_=false;}
    void disableWarp() noexcept {allowWarp_=false;awaiting_=false;}
    bool active() const noexcept {return active_;}
    ViewportCursorStep sample(double x,double y,double width,
                              double height) noexcept {
        if(!active_||!std::isfinite(x)||!std::isfinite(y)||
           !(width>12&&height>12))return {};
        if(width!=width_||height!=height_) {
            width_=width;height_=height;x_=x;y_=y;
            return {};
        }
        if(awaiting_) {
            // The OS can deliver a stale pre-warp position after
            // glfwSetCursorPos. Wait for its discontinuous warp; never
            // interpret that synthetic jump as rotation/dolly motion.
            if(std::abs(x-warpX_)>std::max(8.0,width*.20)||
               std::abs(y-warpY_)>std::max(8.0,height*.20))
                return {};
            awaiting_=false;
            x_=warpX_;y_=warpY_;
        }
        ViewportCursorStep move;
        move.dx=x-x_;
        move.dy=y-y_;
        x_=x;y_=y;
        if(!allowWarp_)return move;
        move.warpX=x;move.warpY=y;
        constexpr double edge=2.0,inset=5.0;
        if(x<=edge) {move.warpX=width-inset;move.warp=true;}
        else if(x>=width-edge) {move.warpX=inset;move.warp=true;}
        if(y<=edge) {move.warpY=height-inset;move.warp=true;}
        else if(y>=height-edge) {move.warpY=inset;move.warp=true;}
        return move;
    }
    void didWarp(double x,double y) noexcept {
        warpX_=x;warpY_=y;awaiting_=true;
    }
private:
    double x_{},y_{},width_{},height_{},warpX_{},warpY_{};
    bool active_=false,allowWarp_=true,awaiting_=false;
};

} // namespace mesher

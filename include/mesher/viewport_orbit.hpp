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

    // Turntable orbit: yaw around the invariant ORIGINAL Z-up axis,
    // pitch about fixed displayed X. These are ABSOLUTE angles, not a
    // succession of noncommuting incremental rotations (which adds roll
    // when the pointer traces a circle). R = Rx(pitch) * Rz(yaw), so the
    // transformed Z-up axis always has zero screen-right component.
    // A left-to-right drag moves near (-Y) geometry right; downward
    // movement pitches it down. Camera elevation is atan(0.33) ~= 18deg;
    // clamp the VIEWING elevation to +/-90deg instead of clamping extra
    // scene rotation. The poles have no projected world-Z-up direction;
    // yaw stays defined and moving away never inverts the view.
    // The pivot stays in ORIGINAL source coordinates for the RMB gesture.
    void drag(Vec3 pivotSource,double dxPixels,double dyPixels,
              double framebufferSpan) noexcept {
        if(!(std::isfinite(dxPixels)&&std::isfinite(dyPixels)&&
             std::isfinite(framebufferSpan)&&framebufferSpan>0))return;
        const double radiansPerPixel=2.0*std::numbers::pi/
                                     framebufferSpan;
        const double deltaYaw=dxPixels*radiansPerPixel;
        const double deltaPitch=dyPixels*radiansPerPixel;
        if(!std::isfinite(deltaYaw)||!std::isfinite(deltaPitch))return;
        const double elevation=std::atan(0.33);
        const double maxPitch=std::numbers::pi/2.0-elevation;
        const double minPitch=-std::numbers::pi/2.0-elevation;
        const double newYaw=std::remainder(
            yaw_+std::remainder(deltaYaw,2.0*std::numbers::pi),
            2.0*std::numbers::pi);
        const double newPitch=std::clamp(pitch_+deltaPitch,
                                       minPitch,maxPitch);
        if(newYaw==yaw_&&newPitch==pitch_)return;
        // Anchor the currently displayed pivot, including any Ctrl+RMB
        // dolly accumulated before this gesture, across the new rotation.
        const Vec3 anchor=displayed(pivotSource);
        yaw_=newYaw;
        pitch_=newPitch;
        const double cy=std::cos(yaw_),sy=std::sin(yaw_);
        const double cp=std::cos(pitch_),sp=std::sin(pitch_);
        basis_[0]={cy,cp*sy,sp*sy};
        basis_[1]={-sy,cp*cy,sp*cy};
        basis_[2]={0,-sp,cp};
        translation_=anchor-transformVector(pivotSource);
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

    // Ctrl-RMB: positive mouse-X walks the *viewer* forward along the
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
        const Vec3 displacement=cameraForward*(dxPixels*unitsPerPixel);
        if(std::isfinite(displacement.x)&&std::isfinite(displacement.y)&&
           std::isfinite(displacement.z))translation_=translation_-displacement;
    }

    // Ctrl-LMB: grab the displayed WORLD in the current view plane.
    // Right/down pointer movement pulls the visible world right/down,
    // equivalent to moving the camera left/up (reverse of M61).
    // The 45-degree vertical FOV gives DPI-independent world-unit scaling.
    void strafe(Vec3 viewRight,Vec3 viewUp,double dxPixels,
                double dyPixels,double framebufferHeight,
                double eyeDistance) noexcept {
        if(!(std::isfinite(dxPixels)&&std::isfinite(dyPixels)&&
             std::isfinite(framebufferHeight)&&framebufferHeight>0&&
             std::isfinite(eyeDistance)&&eyeDistance>0))return;
        const double unit=2.0*eyeDistance*
                          std::tan(std::numbers::pi/8.0)/framebufferHeight;
        const Vec3 movement=viewRight*(dxPixels*unit)-viewUp*(dyPixels*unit);
        const Vec3 next=translation_+movement;
        if(std::isfinite(next.x)&&std::isfinite(next.y)&&
           std::isfinite(next.z))translation_=next;
    }

private:
    double yaw_=0.0,pitch_=0.0;
    std::array<Vec3,3> basis_{{{1,0,0},{0,1,0},{0,0,1}}};
    Vec3 translation_{};
};


// Latch all REAL desktop camera gestures at mouse-button-down, not at
// modifier changes in mid-stroke. Suppress starts with both buttons down.
enum class ViewportCameraMode {Inactive,Orbit,Dolly,Strafe};
struct ViewportCameraGestureStep {
    ViewportCameraMode mode=ViewportCameraMode::Inactive;
    ViewportCameraMode endedMode=ViewportCameraMode::Inactive;
    bool started=false,ended=false;
};
class ViewportCameraGestures {
public:
    ViewportCameraGestureStep sample(bool left,bool right,bool ctrl,
                                     bool alt,bool allowed) noexcept {
        ViewportCameraGestureStep out;
        if(mode_!=ViewportCameraMode::Inactive) {
            // Strafe may be latched to Ctrl+LEFT or Alt+RIGHT. Only the
            // initiating button ends it; modifiers and the other mouse
            // button cannot change or terminate an active camera gesture.
            const bool initiatingHeld=
                mode_==ViewportCameraMode::Strafe?
                    (strafeOnRight_?right:left):right;
            if(!initiatingHeld) {
                out.ended=true;out.endedMode=mode_;
                mode_=ViewportCameraMode::Inactive;
                strafeOnRight_=false;
            }
        }else if(allowed) {
            if(right&&!wasRight_&&!left) {
                // Ctrl+RMB keeps dolly precedence; Alt+RMB grabs the
                // world, while bare RMB remains orbit. Alt cannot alter
                // an already latched orbit or dolly.
                const bool altRightPan=alt&&!ctrl;
                mode_=ctrl?ViewportCameraMode::Dolly:
                      altRightPan?ViewportCameraMode::Strafe:
                                  ViewportCameraMode::Orbit;
                strafeOnRight_=altRightPan;
                out.started=true;
            }else if(left&&!wasLeft_&&!right&&ctrl) {
                mode_=ViewportCameraMode::Strafe;
                strafeOnRight_=false;
                out.started=true;
            }
        }
        wasLeft_=left;wasRight_=right;
        out.mode=mode_;
        return out;
    }
    void cancel() noexcept {
        mode_=ViewportCameraMode::Inactive;
        strafeOnRight_=false;
    }
private:
    ViewportCameraMode mode_=ViewportCameraMode::Inactive;
    bool wasLeft_=false,wasRight_=false;
    bool strafeOnRight_=false;
};

// Pure cursor-wrap planning, independent of GLFW/Vulkan. Coordinates are
// GLFW window-client coordinates; the caller performs platform cursor warp
// only during an active view gesture and only while the window is focused.
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

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

    // The miss fallback is specifically the XZ plane at centroid.y,
    // *not* a z=0 ground plane. A parallel/behind ray has no valid
    // forward intersection; use the current centroid deterministically.
    static Vec3 choosePivot(Ray sourceRay,
                            std::optional<Vec3> pickedSurface,
                            Vec3 geometryCentroid) noexcept {
        if(pickedSurface)return *pickedSurface;
        if(std::abs(sourceRay.direction.y)>1e-12) {
            const double t=(geometryCentroid.y-sourceRay.origin.y)/
                            sourceRay.direction.y;
            if(std::isfinite(t)&&t>0)
                return sourceRay.origin+sourceRay.direction*t;
        }
        return geometryCentroid;
    }
private:
    std::array<Vec3,3> basis_{{{1,0,0},{0,1,0},{0,0,1}}};
    Vec3 translation_{};
};

} // namespace mesher

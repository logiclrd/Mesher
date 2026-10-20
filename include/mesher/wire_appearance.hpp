#pragma once

#include <algorithm>
#include <cmath>

namespace mesher {

// Physical FRAMEBUFFER pixels. M58 used an outer 2.0px halfwidth,
// yielding roughly 3.0px integrated opaque-equivalent wire ink. M62
// reduces this to 1.5px, roughly 2.0px ink, NOT a naive factor-of-two
// thin line. Depth clipping of one side remains geometrically valid.
inline constexpr float kWireOuterHalfWidthPixels=1.5f;

// CPU reference for the GLSL smoothstep coverage. Signed screen-space
// distance is perpendicular to the original edge centerline.
inline float wireCoverage(float signedDistancePixels,
                          float halfWidth=kWireOuterHalfWidthPixels) noexcept {
    if(!std::isfinite(signedDistancePixels)||!std::isfinite(halfWidth)||
       halfWidth<=0.0f)return 0.0f;
    const float t=std::clamp(halfWidth-std::abs(signedDistancePixels),
                             0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}

} // namespace mesher

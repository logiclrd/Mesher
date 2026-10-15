#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace mesher {

// The shortest useful spatial scale is the sphere's nominal triangle edge
// length at its *initial* resolution; it must not change after remeshing.
struct BrushRadiusRange {
    double minimum=0.0;
    double maximum=0.5;
    static BrushRadiusRange forVertexCount(std::uint32_t initialCount) {
        if(initialCount==0)throw std::invalid_argument("Initial vertex count must be positive");
        constexpr double pi=3.1415926535897932384626433832795;
        constexpr double root3=1.7320508075688772935274463415059;
        return {std::min(0.5,std::sqrt(8.0*pi/(double(initialCount)*root3))),0.5};
    }
    double clamp(double radius) const {
        if(!std::isfinite(radius))throw std::invalid_argument("Brush radius must be finite");
        return std::clamp(radius,minimum,maximum);
    }
    // y coordinate increases downwards. Larger brush sizes are at the top.
    double fromSliderFraction(double downFraction) const {
        return minimum+(maximum-minimum)*(1.0-std::clamp(downFraction,0.0,1.0));
    }
    double sliderFraction(double radius) const {
        if(maximum==minimum)return 0.0;
        return 1.0-(clamp(radius)-minimum)/(maximum-minimum);
    }
};
} // namespace mesher

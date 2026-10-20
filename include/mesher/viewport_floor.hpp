#pragma once

#include "mesher/mesh.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mesher {

// Read-only ONCE at viewport construction from the immutable imported
// startup Mesh. The floor is display scenery, never a physical part of
// the worker's COW source, first-original proof, selection or history.
inline double viewportWorkFloorZ(const Mesh& initial) {
    constexpr double legacySphereFloor=-1.22;
    constexpr double clearance=.5;
    double minimum=legacySphereFloor+clearance;
    for(const auto p:initial.vertices) {
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            throw std::invalid_argument("Nonfinite viewport initial mesh vertex");
        minimum=std::min(minimum,p.z);
    }
    // Preserve the legacy unit-sphere view exactly; move the plane ONLY
    // when the real startup geometry would otherwise intersect it.
    return std::min(legacySphereFloor,minimum-clearance);
}

} // namespace mesher

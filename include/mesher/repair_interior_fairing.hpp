#pragma once
#include "mesher/repair_mixed_stage.hpp"
#include <cstddef>
#include <map>
#include <vector>

namespace mesher {
// Pure, bounded local improvement of NEW private-closure vertices only.
// No survivor/rim vertex or face connectivity can be changed. With intrinsic
// X=0 symmetry a complete reflected position orbit is moved atomically;
// ambiguous reflection, inverted triangles, or non-improving steps are skipped.
// Return 1 move for a seam/self-mirror vertex, 2 for a reflected pair, else 0.
std::size_t fairOneConstrainedClosureVertex(
    const std::vector<MixedClosureFace>& faces,
    std::map<VertexId,Vec3>& positions,
    const std::vector<VertexId>& createdVertices,
    bool requireReflection,double minCapQuality,double minZipperQuality,
    double maxCapEdgeFactor,double maxZipperEdgeFactor);
}

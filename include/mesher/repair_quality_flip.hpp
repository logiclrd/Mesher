#pragma once
#include "mesher/repair_mixed_stage.hpp"
#include <cstddef>
#include <map>
#include <vector>

namespace mesher {
// One deterministically chosen 2->2 interior diagonal improvement.
// Only new/new faces of the SAME closure kind are eligible; never
// touch a retained R03 survivor, original rim edge, or stable FaceId.
// For intrinsic X=0 reflection, both disjoint mirrored diagonal
// neighborhoods must be proven first and flipped as one atomic orbit.
// Returns number of flipped diagonals (0, 1, or 2).
std::size_t flipOneInferiorClosureDiagonal(
    std::vector<MixedClosureFace>& faces,
    const std::map<VertexId,Vec3>& positions,
    bool requireReflection,
    double minCapQuality,double minZipperQuality,
    double maxCapEdgeFactor,double maxZipperEdgeFactor);
} // namespace mesher

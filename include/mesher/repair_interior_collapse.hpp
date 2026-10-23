#pragma once
#include "mesher/repair_mixed_stage.hpp"
#include <cstddef>
#include <map>
#include <vector>

namespace mesher {
// Undo a provably redundant valence-three NEW cap fan. All three faces
// must be new, consistently oriented CAP triangles with no original R03
// vertex retired, and their replacement must satisfy the original strict
// q/edge bound. In mirrored mode a complete reflected fan pair is changed
// atomically. Result is the number of removed NEW interior vertices (0/1/2).
std::vector<VertexId> collapseOneInferiorCapFan(
    std::vector<MixedClosureFace>& faces,
    std::map<VertexId,Vec3>& positions,
    const std::vector<VertexId>& createdVertices,
    bool requireReflection,double minCapQuality,double maxCapEdgeFactor);
}

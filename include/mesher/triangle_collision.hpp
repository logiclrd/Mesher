#pragma once

#include "mesher/versioned_mesh.hpp"

#include <array>

namespace mesher {

// V13 static, post-state collision predicate. "Intersecting" includes
// tangencies between independent faces. Shared edges/vertices alone are
// ordinary manifold contacts, but shared-vertex penetration and coplanar
// face overlap are not. Ambiguous cases must never be treated as certified
// intersection-free by a future destructive repair stage.
enum class TriangleContact {
    Disjoint,
    ExpectedAdjacency,
    Intersecting,
    Ambiguous
};

TriangleContact triangleContact(
    const std::array<Vec3,3>& a,const std::array<VertexId,3>& aIds,
    const std::array<Vec3,3>& b,const std::array<VertexId,3>& bIds);

} // namespace mesher

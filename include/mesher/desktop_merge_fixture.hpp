#pragma once

#include "mesher/mesh.hpp"

#include <array>
#include <cstdint>

namespace mesher {

// Reproducible M50 *interactive* first-original source fixture. These are
// the same two independently owned strictly transverse tetrahedra used by
// the M47/M49 accepted source-positive worker tests. This function does
// not claim proof, select original FaceIds, queue Merge or publish a Step:
// the real protected worker separately authenticates and checks everything.
inline Mesh desktopMergeAsymmetricTetraFixture() {
    Mesh mesh;
    constexpr std::array<Vec3,8> coords{{
        {0,0,0},{2,0,0},{0,2,0},
        {.75,-.5,-1},{.75,1.75,1},{.75,.25,-1},
        {.5,.5,-3},{3,.5,0}
    }};
    constexpr std::array<std::array<std::uint32_t,3>,8> faces{{
        {0,1,2},{1,0,6},{2,1,6},{0,2,6},
        {3,4,5},{4,3,7},{5,4,7},{3,5,7}
    }};
    for(const Vec3 p:coords)mesh.vertices.push_back(p);
    for(const auto& face:faces)
        mesh.triangles.push_back({face[0],face[1],face[2]});
    return mesh;
}

} // namespace mesher

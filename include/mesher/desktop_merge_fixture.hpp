#pragma once

#include "mesher/mesh.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/repair_surgery.hpp"

#include <memory>
#include <utility>
#include <vector>


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


 // Reflection is a different, independently source-owned pair of
 // closed shells, NOT a coordinate-based deduplication of the first eight.
inline Mesh desktopMergeReflectedTetraFixture() {
    const Mesh positive=desktopMergeAsymmetricTetraFixture();
    Mesh mesh;
    for(const Vec3 p:positive.vertices)
        mesh.vertices.push_back({p.x+5,p.y,p.z});
    for(const Vec3 p:positive.vertices)
        mesh.vertices.push_back({-p.x-5,p.y,p.z});
    for(const auto face:positive.triangles)
        mesh.triangles.push_back(face);
    for(const auto face:positive.triangles)
        mesh.triangles.push_back({face.a+8,face.c+8,face.b+8});
    return mesh;
}

struct DesktopMergeFirstOriginal {
    DurableCheckpoint checkpoint;
    std::vector<FaceId> firstFaces;
};

// Only for the deliberately named freshly constructed developer fixture.
// Preserve explicit first original and actual imported VertexIds; never
// claim proof merely because an arbitrary terminal geometry looks mirrored.
inline DesktopMergeFirstOriginal desktopMergeReflectedFirstOriginal() {
    auto mesh=desktopMergeReflectedTetraFixture();
    auto imported=partitionFlatMesh(mesh,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> mirrorPairs;
    for(std::size_t i=0;i<8;++i)
        mirrorPairs.emplace_back(imported.flatIndex.vertexIds[i],
                                 imported.flatIndex.vertexIds[i+8]);
    auto proof=std::make_shared<const OriginalPatchLifetime>(
        beginOriginalPatchLifetime(captureOriginalSourceAnchors(
            root,imported.flatIndex.faceIds,
            7*imported.flatIndex.faceIds.size())));
    DurableCheckpoint fresh(root,mirrorPairs,mesh.vertices.size(),true,proof);
    fresh.verifiedFirstOriginalMirrorPairs=mirrorPairs;
    return {std::move(fresh),std::move(imported.flatIndex.faceIds)};
}

} // namespace mesher

#pragma once

#include "mesher/partition_render.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace mesher {

// A DISPLAY-ONLY fixed-cost expansion at GPU upload/dirty-partition time.
// The immutable COW renderer continues to stage exactly one canonical
// two-vertex segment per topological edge. No VertexId or FaceId changes.
// Existing seven-float Vulkan binding is reused for the wire pipeline:
// position = segment A, normal.xyz = segment B, material = corner 0..3.
// The dedicated wire shader computes a screen-space AA quad from these
// originals, preserving projected depth and depth-buffer occlusion.
inline std::vector<PartitionRenderVertex> expandWireQuads(
        std::span<const PartitionRenderVertex> original) {
    if(original.size()%2!=0)
        throw std::invalid_argument("Wire staging requires complete edge pairs");
    if(original.size()>std::numeric_limits<std::size_t>::max()/3)
        throw std::length_error("Wire quad staging vertex count overflow");
    std::vector<PartitionRenderVertex> triangles;
    triangles.reserve(original.size()*3);
    constexpr std::array<unsigned,6> corners{0,1,2,0,2,3};
    for(std::size_t i=0;i<original.size();i+=2) {
        const auto a=original[i],b=original[i+1];
        for(const unsigned corner:corners) {
            PartitionRenderVertex out=a;
            out.nx=b.px;out.ny=b.py;out.nz=b.pz;
            out.material=static_cast<float>(corner);
            triangles.push_back(out);
        }
    }
    return triangles;
}

} // namespace mesher

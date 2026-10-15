#pragma once
#include "mesher/mesh.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace mesher {

// This is an experimental, local-topology pass rather than global remeshing.
// A split is triggered only when an edge is both longer than the maximum
// and has stretched in the stamp. Existing fine areas are never coarsened.
struct RefineSettings {
    bool enabled = false;
    double maximumEdgeRatio = 1.5; // multiplied by initial nominal spacing
    std::uint32_t maximumSplitPairsPerStamp = 8;
    bool optimizeFlips = true;
    std::uint32_t maximumFlipPairsPerStamp = 12;
};

struct RefineStatistics {
    std::uint32_t splitPairs = 0;    // one paired symmetry operation counts as one
    std::uint32_t newVertices = 0;
    std::uint32_t flipPairs = 0;
    std::uint32_t longGrowingEdges = 0;
};

// The mirror map is maintained by index, including newly inserted vertices.
// Any separate topology optimizer must receive this map and return its updated
// mapping after compaction.
class LocalRefiner {
public:
    LocalRefiner(const Mesh& original, RefineSettings settings,
                 std::vector<std::uint32_t> mirrors = {});
    void reset(const Mesh& mesh, std::vector<std::uint32_t> mirrors = {});
    void setMirrors(std::vector<std::uint32_t> mirrors);
    // Extends pre-stamp positions at the same new indices created by splitting.
    // The input vector must exactly match mesh.vertices before the call.
    RefineStatistics apply(Mesh& mesh, std::vector<Vec3>& beforeStamp,
                           std::span<const std::uint32_t> affectedFaces = {},
                           bool restrictToAffected = false);
    const std::vector<std::uint32_t>& mirrors() const noexcept { return mirrors_; }
    double maximumEdge() const noexcept { return maximumEdge_; }
private:
    RefineSettings settings_;
    double maximumEdge_ = 0;
    std::vector<std::uint32_t> mirrors_;
};
} // namespace mesher

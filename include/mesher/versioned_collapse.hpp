#pragma once

#include "mesher/remesh.hpp"
#include "mesher/versioned_inflate.hpp"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mesher {

struct VersionedCollapseResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    CollapseStatistics statistics{};
    std::vector<VertexId> removedVertices;
    std::vector<FaceId> removedFaces;
    std::vector<FaceId> rewrittenFaces;
    std::vector<std::pair<VertexId,VertexId>> removedMirrorPairs;
    std::size_t modifiedPartitions{};
    std::size_t copiedDirectoryNodes{};
};

// Directional edge collapse on immutable geometry. Each candidate commits an
// independent, locally validated transaction or changes nothing. Stable IDs
// are never renumbered/recycled; old snapshots retain their removed elements.
class VersionedDirectionalCollapser {
public:
    VersionedDirectionalCollapser(
        std::size_t initialVertexCount,CollapseSettings settings,
        std::vector<std::pair<VertexId,VertexId>> mirrors={},
        std::size_t maximumElementsPerPartition=256);

    VersionedCollapseResult apply(
        std::shared_ptr<const RegionTopologyIndex> postEdit,
        const LocalStampBaseline& baseline);

    // Explicit local trial; useful for independent acceptance comparisons and
    // for clients with a preselected edge. Must have shortened since baseline.
    VersionedCollapseResult tryEdge(
        std::shared_ptr<const RegionTopologyIndex> postEdit,
        const LocalStampBaseline& baseline,VertexId a,VertexId b);

    double minimumEdge() const noexcept {return minimumEdge_;}
    const std::unordered_map<VertexId,VertexId>& mirrors() const noexcept {
        return mirrors_;
    }

private:
    bool attempt(VersionedCollapseResult& result,VertexId a,VertexId b);
    double minimumEdge_{};
    CollapseSettings settings_;
    std::size_t partitionCapacity_{};
    std::unordered_map<VertexId,VertexId> mirrors_;
};

} // namespace mesher

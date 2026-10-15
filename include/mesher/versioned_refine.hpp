#pragma once

#include "mesher/refine.hpp"
#include "mesher/versioned_inflate.hpp"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mesher {

struct VersionedRefineResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    RefineStatistics statistics{};
    std::vector<VertexId> insertedVertices;
    std::vector<FaceId> changedFaces;
    std::size_t modifiedPartitions{};
    std::size_t copiedDirectoryNodes{};
};

// Stateful counterpart to LocalRefiner, but stable IDs and persistent local
// connectivity replace the flat vector indices and mesh-wide rebuilds.
// Only edges adjacent to a stamp's affected faces are considered for splits;
// flips inspect the small touched neighborhood after accepted splits.
class VersionedLocalRefiner {
public:
    VersionedLocalRefiner(std::size_t initialVertexCount,
                          RefineSettings settings,
                          std::vector<std::pair<VertexId,VertexId>> mirrors={},
                          std::size_t maximumElementsPerPartition=256);

    VersionedRefineResult apply(
        std::shared_ptr<const RegionTopologyIndex> postInflate,
        const LocalStampBaseline& baseline);

    double maximumEdge() const noexcept { return maximumEdge_; }
    const std::unordered_map<VertexId,VertexId>& mirrors() const noexcept {
        return mirrors_;
    }

private:
    RefineSettings settings_;
    double maximumEdge_{};
    std::size_t partitionCapacity_{};
    std::unordered_map<VertexId,VertexId> mirrors_;
};

} // namespace mesher

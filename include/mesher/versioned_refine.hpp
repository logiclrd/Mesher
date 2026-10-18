#pragma once

#include "mesher/refine.hpp"
#include "mesher/versioned_inflate.hpp"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mesher {

// M24: a source-owned, immutable committed edge split. Each accepted
// two-face (or reflected four-face) RegionEdit contributes one record
// per rewritten source face, never based on XYZ-nearest matching.
struct RefineSplitSourceEvidence {
    std::shared_ptr<const RegionTopologyIndex> before,after;
    FaceId sourceFace{},appendedFace{};
    VertexId midpoint{},edgeA{},edgeB{};
};

struct VersionedRefineResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    // The real immutable pre-stamp original and post-Inflate input; an
    // independent verifier can reject stale/mismatched source chains.
    std::shared_ptr<const RegionTopologyIndex> stampOriginal,sourceIndex;
    std::vector<RefineSplitSourceEvidence> splitSources;
    RefineStatistics statistics{};
    std::vector<VertexId> insertedVertices;
    std::vector<std::pair<VertexId,VertexId>> insertedMirrorPairs;
    std::vector<FaceId> changedFaces;
    std::size_t modifiedPartitions{};
    std::size_t copiedDirectoryNodes{};
    std::size_t clonedPartitionBytes{};
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

    // Apply the V7 collapse result's retired mirror pairs before the next stamp.
    void removeMirrorPair(VertexId first,VertexId second);
    void addMirrorPair(VertexId first,VertexId second);
    // A Step journals only changed mirror pairs, not the O(all vertices)
    // correspondence map. Rejecting a proposed Step replays inverses.
    void beginStep();
    void commitStep() noexcept;
    void rollbackStep();
    double maximumEdge() const noexcept { return maximumEdge_; }
    const std::unordered_map<VertexId,VertexId>& mirrors() const noexcept {
        return mirrors_;
    }

private:
    RefineSettings settings_;
    double maximumEdge_{};
    std::size_t partitionCapacity_{};
    std::unordered_map<VertexId,VertexId> mirrors_;
    struct MirrorChange {bool added;VertexId first,second;};
    std::vector<MirrorChange> pendingMirrors_;
    bool stepOpen_=false;
};

} // namespace mesher

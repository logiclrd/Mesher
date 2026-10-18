#pragma once

#include "mesher/remesh.hpp"
#include "mesher/versioned_inflate.hpp"

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mesher {

// One true committed local edge-collapse transaction. Its complete
// source-face retirement/rewrite set is captured only AFTER RegionEdit
// accepts both mirrored sides and their permanent ID changes.
struct CollapseSourceEvidence {
    std::shared_ptr<const RegionTopologyIndex> before,after;
    VertexId survivor{},retired{},mirrorSurvivor{},mirrorRetired{};
    bool symmetric{},paired{};
    std::vector<FaceId> retiredFaces,rewrittenFaces;
};

struct VersionedCollapseResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::shared_ptr<const RegionTopologyIndex> stampOriginal,sourceIndex;
    std::vector<CollapseSourceEvidence> collapseSources;
    CollapseStatistics statistics{};
    std::vector<VertexId> removedVertices;
    std::vector<FaceId> removedFaces;
    std::vector<FaceId> rewrittenFaces;
    std::vector<std::pair<VertexId,VertexId>> removedMirrorPairs;
    std::size_t modifiedPartitions{};
    std::size_t copiedDirectoryNodes{};
    std::size_t clonedPartitionBytes{};
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

    // Refinement may introduce a new mirrored midpoint before collapse.
    void addMirrorPair(VertexId first,VertexId second);
    void removeMirrorPair(VertexId first,VertexId second);
    void beginStep();
    void commitStep() noexcept;
    void rollbackStep();
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
    struct MirrorChange {bool added;VertexId first,second;};
    std::vector<MirrorChange> pendingMirrors_;
    bool stepOpen_=false;
};

} // namespace mesher

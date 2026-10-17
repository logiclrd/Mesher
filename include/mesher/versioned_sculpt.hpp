#pragma once

#include "mesher/brush.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/versioned_collapse.hpp"
#include "mesher/versioned_refine.hpp"
#include "mesher/revision_history.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/repair_surgery.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace mesher {

// Explicitly expensive V8 bridge to the unchanged flat Vulkan renderer.
// Preserves the original ordering for surviving imported elements and appends
// new stable IDs deterministically. Never called by the sculpt worker.
struct ViewportFlattenStatistics {
    double milliseconds{};
    std::size_t vertices{};
    std::size_t triangles{};
    std::size_t bytes{};
};
Mesh flattenRevisionForViewport(const RegionTopologyIndex& index,
                                const FlatMeshIndex& originalOrder,
                                ViewportFlattenStatistics* statistics=nullptr);

// A candidate validator runs only on the worker thread, after all
// Inflate/refine/collapse edits, but before publishing the original-root CAS.
// Empty reason accepts; a nonempty reason rejects the ENTIRE Step.
// The spans are borrowed for this synchronous call only.
struct ProposedSculptStep {
    std::shared_ptr<const RegionTopologyIndex> before;
    std::shared_ptr<const RegionTopologyIndex> candidate;
    std::span<const VertexId> changedVertices;
    std::span<const FaceId> changedFaces;
};
using SculptStepValidator=std::function<std::string(const ProposedSculptStep&)>;

// V17d1: opt-in private classified collision evaluation. The desktop
// Merge geometry UI remains disabled, with current sculpt default unchanged.
struct SculptCollisionSettings {
    bool enabled=false;
    RepairSurgerySettings surgery{};
};
struct SculptCollisionEvaluation {
    RepairPlan plan{};
    std::optional<RepairSurgeryResult> repair;
    std::string refusal;
    std::size_t finalNarrowPhaseTests{};
    bool safe() const noexcept {return refusal.empty();}
};
// A self-contained, unpublished post-state evaluation. Optional seeds are
// explicitly known NEW closed components; the normal brush passes none.
SculptCollisionEvaluation evaluateSculptCollisionCandidate(
    const ProposedSculptStep& proposed,bool symmetric,
    const SculptCollisionSettings& settings,
    const std::vector<FaceId>& isolatedComponentSeeds={});

// The BrushStroke owns sampling and queue semantics; this brush owns *all*
// editable geometry and uses a dummy Mesh solely to satisfy BrushStroke's
// existing callback signature. Each stamp publishes one immutable index/root
// pair; it never creates or publishes a flat Mesh snapshot.
class VersionedSculptBrush final : public Brush {
public:
    VersionedSculptBrush(const Mesh& initial,double strength,
                         CurvatureSettings curvature,CollapseSettings collapse,
                         RefineSettings refine,bool symmetric,
                         std::size_t partitionCapacity=256,
                         SculptStepValidator validator={},
                         SculptCollisionSettings collision={});
    VersionedSculptBrush(DurableCheckpoint restored,double strength,
                         CurvatureSettings curvature,CollapseSettings collapse,
                         RefineSettings refine,
                         std::size_t partitionCapacity=256,
                         SculptStepValidator validator={},
                         SculptCollisionSettings collision={});
    void stamp(Mesh&,std::span<const BrushArea> areas) override;
    bool undo();
    bool redo();
    HistoryStatistics historyStatistics() const noexcept {
        return history_.statistics();
    }
    DurableCheckpoint checkpoint() const;

    std::shared_ptr<const RegionTopologyIndex> snapshot() const noexcept {
        return published_.snapshot();
    }
    std::shared_ptr<const FlatMeshIndex> originalOrder() const noexcept {
        return flatOrder_;
    }
    std::size_t completedStamps() const noexcept {return completed_;}
    std::size_t rejectedStamps() const noexcept {return rejected_;}
    std::size_t collisionCheckedStamps() const noexcept {return collisionChecked_;}
    std::size_t collisionRepairedStamps() const noexcept {return collisionRepaired_;}
    const std::string& lastRejection() const noexcept {return lastRejection_;}
    StampTimings lastTimings() const noexcept {return timings_;}
    const StampDetails& lastDetails() const noexcept {return details_;}
    std::size_t publishedRevisions() const noexcept {return publishedRevisions_;}
    std::size_t modifiedPartitions() const noexcept {return modifiedPartitions_;}
    std::size_t copiedDirectoryNodes() const noexcept {return copiedDirectoryNodes_;}
    std::size_t clonedPartitionBytes() const noexcept {return clonedPartitionBytes_;}
    const std::vector<PartitionId>& lastDirtyPartitions() const noexcept {
        return lastDirtyPartitions_;
    }

private:
    VersionedSculptBrush(PartitionedMeshImport import,std::size_t initialCount,
                         double strength,CurvatureSettings curvature,
                         CollapseSettings collapse,RefineSettings refine,
                         bool symmetric,std::size_t partitionCapacity,
                         SculptStepValidator validator,
                         SculptCollisionSettings collision);
    std::shared_ptr<const FlatMeshIndex> flatOrder_;
    IndexedMeshRepository published_;
    RevisionRetirementQueue historyRetirement_{64};
    RevisionHistory history_;
    VersionedInflateBrush inflate_;
    VersionedLocalRefiner refiner_;
    VersionedDirectionalCollapser collapser_;
    std::size_t partitionCapacity_;
    std::size_t initialVertexCount_{};
    std::size_t vertices_{},faces_{};
    std::size_t completed_{},publishedRevisions_{},rejected_{};
    SculptStepValidator validator_;
    SculptCollisionSettings collision_{};
    bool symmetric_=false;
    std::size_t collisionChecked_{},collisionRepaired_{};
    std::string lastRejection_;
    std::size_t modifiedPartitions_{},copiedDirectoryNodes_{};
    std::size_t clonedPartitionBytes_{};
    StampTimings timings_{};
    StampDetails details_{};
    std::vector<PartitionId> lastDirtyPartitions_;
};

} // namespace mesher

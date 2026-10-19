#pragma once

#include "mesher/brush.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/versioned_collapse.hpp"
#include "mesher/versioned_refine.hpp"
#include "mesher/revision_history.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/repair_surgery.hpp"
#include "mesher/repair_intersection.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

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

// Externally nominated ORIGINAL surface witnesses. A client selects
// preserved exposed features; deliberately sculpted surfaces must not be
// in this set. The guard compares to one fixed original reference across
// all Steps, not to the previous accepted Step. Candidate FaceIds are a
// bounded stable local search domain (not the whole mesh).
struct SculptAnchoredSurfaceGuard {
    std::vector<Vec3> originalWitnesses;
    std::vector<FaceId> candidateFaces;
    double originalEdgeLength=1.0;
    double maximumDistanceInEdges=0.01;
    std::size_t maxDistanceTests=131072;
};

// M17: read-only complete ORIGINAL-FaceId V13 discovery on the ACTUAL
// unpublished post-Inflate/refine/collapse candidate. This uses the
// candidate as the source root, never the prior published brush root or
// a selected dirty-face subset. No Boolean cut, welding, CAS or history.
// Every result is either the complete strictly finite source-pair graph,
// conclusively clean, or an empty refused certificate with diagnostics.
enum class SculptSourceContactStatus {Clean,CompleteTransverse,Refused};
struct SculptSourceContactLimits {
    std::size_t maxVertices=16384;
    std::size_t maxFaces=32768;
    RegionTopologyIndex::CollisionQueryLimits query{
        32768,524288,131072,262144,131072,128};
    std::size_t maxSectionTests=128;
};
struct SculptSourceContactAssessment {
    SculptSourceContactStatus status{SculptSourceContactStatus::Refused};
    std::shared_ptr<const RegionTopologyIndex> originalCandidate;
    // Populated only for fully certified transverse original V13 pairs.
    std::vector<RegionTopologyIndex::CollisionPair> contacts;
    RegionTopologyIndex::CollisionStatistics work{};
    std::size_t discoveredPairs{};
    std::string refusal;
    bool ready() const noexcept {
        return status==SculptSourceContactStatus::CompleteTransverse;
    }
};
SculptSourceContactAssessment assessPostSculptOriginalContacts(
    const ProposedSculptStep& proposed,bool symmetric,
    const SculptSourceContactLimits& limits={},
    const std::unordered_map<VertexId,VertexId>* mirrors=nullptr);

// V17d1: opt-in private classified collision evaluation. The desktop
// Merge geometry UI remains disabled, with current sculpt default unchanged.
// M31: finite caller INPUT validation. The active terminal gate is M32/M33.
struct SculptOriginalIntentInputLimits {
    std::size_t maxOriginalWitnesses=131072;
    std::size_t maxDeclaredSources=16384;
};
// M32: finite original-surface work and first-root protected-fidelity policy.
// Intentionally separate from M17 contact limits and V16 anchoredSurface.
struct SculptOriginalTerminalLimits {
    double maximumProtectedDeviationInEdges=1e-7;
    std::size_t maxDistanceTests=131072;
    std::size_t maxLivingDescendants=16384;
    std::size_t maxCumulativeFaceEvidence=16384;
    std::size_t maxInflateVertices=16384;
    std::size_t maxInflateFaces=32768;
};
struct SculptCollisionSettings {
    bool enabled=false; // V16 approximate classifier; independent policy
    // M18: worker-only exact physical sculpt union, DEFAULT OFF. The desktop
    // never enables this before M15. No V16 scar surgery in exact mode.
    bool physicalExact=false;
    SculptOriginalIntentInputLimits originalIntentInput{};
    SculptOriginalTerminalLimits originalTerminal{};
    SculptSourceContactLimits physicalSourceLimits{};
    ExactCutStepLimits physicalExactLimits{};
    RepairSurgerySettings surgery{};
    // Explicit newly isolated closed-component seeds supplied by a caller.
    // No global component sweep is inferred from a brush stamp. Retired
    // seeds are ignored on later stamps; empty by default in the UI.
    std::vector<FaceId> isolatedComponentSeeds{};
    // Diagnostic hysteresis only: never disables a collision query or
    // authorizes a previously rejected candidate. Quiet *conclusive*
    // assessments close an episode; ambiguous/budget queries do not.
    std::size_t quietChecksToEndContact=3;
    // Optional cumulative erosion guard, not required for legacy callers.
    std::optional<SculptAnchoredSurfaceGuard> anchoredSurface;
};
// Hysteresis groups a run of contact attempts for diagnostics. It does
// NOT cache geometry decisions or skip expensive V13/V14 verification.
struct SculptContactStatistics {
    std::size_t episodes{},contactChecks{},cleanChecks{},uncertainChecks{};
    std::size_t quietRun{},contactRun{},longestContactRun{};
    bool active{};
};
class SculptContactTracker {
public:
    explicit SculptContactTracker(std::size_t quietChecks=3);
    // nullopt means classification could not establish clean/contact;
    // unknown does not terminate an active episode.
    void observe(std::optional<bool> contact) noexcept;
    const SculptContactStatistics& statistics() const noexcept {
        return stats_;
    }
private:
    std::size_t quietChecks_;
    SculptContactStatistics stats_{};
};
struct SculptCollisionEvaluation {
    RepairPlan plan{};
    std::optional<RepairSurgeryResult> repair;
    // Net mirror changes in this PRIVATE repair only. The V17c journal
    // stages them before the original CAS, then history/checkpoints commit.
    std::vector<MirrorPair> insertedMirrors;
    std::vector<MirrorPair> retiredMirrors;
    std::string refusal;
    std::size_t finalNarrowPhaseTests{};
    std::optional<SurfaceWitnessAudit> anchoredSurfaceAudit;
    bool safe() const noexcept {return refusal.empty();}
};
// Derive and verify net new/retired mirror orbits for an already validated
// private mirrored surgery. Useful for explicit CPU repair acceptance too;
// this never mutates the index or publishes a root.
void deriveMirroredRepairDeltas(
    SculptCollisionEvaluation& evaluation,
    const RegionTopologyIndex& candidate,
    const std::unordered_map<VertexId,VertexId>& livingMirrors);
// A self-contained, unpublished post-state evaluation. Optional seeds are
// explicitly known NEW closed components; the normal brush passes none.
SculptCollisionEvaluation evaluateSculptCollisionCandidate(
    const ProposedSculptStep& proposed,bool symmetric,
    const SculptCollisionSettings& settings,
    const std::vector<FaceId>& isolatedComponentSeeds={},
    const std::unordered_map<VertexId,VertexId>* vertexMirrors=nullptr);

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
    // Explicit FIRST-original FaceIds for this stroke; never proximity-guessed.
    // Invalid scope is a recoverable Step refusal, not a worker exception.
    void setOriginalPatchIntent(std::vector<FaceId> originals);
    const std::vector<FaceId>& originalPatchIntent() const noexcept {
        return physicalIntendedOriginals_;
    }
    std::shared_ptr<const RegionTopologyIndex> firstOriginalSourceRoot() const noexcept {
        return firstOriginalAuthority_?firstOriginalAuthority_->anchors.original:nullptr;
    }
    void stamp(Mesh&,std::span<const BrushArea> areas) override;
    bool undo();
    bool redo();
    // Explicit, opt-in V17e2b2d3b worker-only command, never called by
    // the desktop Merge geometry UI (which remains disabled). The expected
    // root is captured when contacts are selected, not guessed when queued
    // commands eventually execute. A failed/stale Step publishes NOTHING.
    bool mergeExactContacts(
        std::shared_ptr<const RegionTopologyIndex> expected,
        const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
        const ExactCutStepLimits& limits={},
        const std::vector<FaceId>& intendedOriginals={});
    std::size_t completedUnions() const noexcept {return completedUnions_;}
    std::size_t rejectedUnions() const noexcept {return rejectedUnions_;}
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
    SculptContactStatistics contactStatistics() const noexcept {
        return contact_.statistics();
    }
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
    std::string validateOriginalIntentSources(
        const std::vector<FaceId>& originals) const;
    VersionedSculptBrush(PartitionedMeshImport import,std::size_t initialCount,
                         double strength,CurvatureSettings curvature,
                         CollapseSettings collapse,RefineSettings refine,
                         bool symmetric,std::size_t partitionCapacity,
                         SculptStepValidator validator,
                         SculptCollisionSettings collision);
    std::shared_ptr<const FlatMeshIndex> flatOrder_;
    IndexedMeshRepository published_;
    RevisionRetirementQueue historyRetirement_{64};
    // Construct genuine first-root sealed authority BEFORE M29 history:
    // tracked history must start at its original root, never retrofit.
    std::shared_ptr<const OriginalPatchLifetime> firstOriginalAuthority_;
    RevisionHistory history_;
    VersionedInflateBrush inflate_;
    VersionedLocalRefiner refiner_;
    VersionedDirectionalCollapser collapser_;
    std::size_t partitionCapacity_;
    std::size_t initialVertexCount_{};
    std::size_t vertices_{},faces_{};
    std::size_t completed_{},publishedRevisions_{},rejected_{};
    std::size_t completedUnions_{},rejectedUnions_{};
    SculptStepValidator validator_;
    SculptCollisionSettings collision_{};
    bool symmetric_=false;
    std::size_t collisionChecked_{},collisionRepaired_{};
    SculptContactTracker contact_{};
    std::string lastRejection_;
    std::size_t modifiedPartitions_{},copiedDirectoryNodes_{};
    std::size_t clonedPartitionBytes_{};
    StampTimings timings_{};
    StampDetails details_{};
    // A checkpoint restored without an authenticated FIRST-original
    // certificate must never automatically restart fidelity at its
    // current terminal root. M30 installs the active pre-CAS guard.
    bool originalFidelityRequired_{};
    // Immutable FIRST-source authority and original reflected-ID involution.
    std::unordered_map<VertexId,VertexId> firstOriginalMirrors_;
    std::vector<FaceId> physicalIntendedOriginals_;
    std::string physicalIntentRefusal_;
    std::vector<PartitionId> lastDirtyPartitions_;
};

} // namespace mesher

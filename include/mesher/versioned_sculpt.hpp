#pragma once

#include "mesher/brush.hpp"
#include "mesher/vertex_paint.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/versioned_collapse.hpp"
#include "mesher/versioned_refine.hpp"
#include "mesher/revision_history.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/repair_surgery.hpp"
#include "mesher/repair_intersection.hpp"
#include "mesher/exact_stage_work.hpp"
#include "mesher/offmode_collision.hpp"
#include "mesher/offmode_clearance.hpp"
#include "mesher/offmode_mirror.hpp"

#include <atomic>
#include <chrono>
#include <exception>
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
enum class SculptSourceContactStatus {
    Clean,CompleteTransverse,CompleteIntersecting,Refused
};
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
    // Fully bounded contacts: protected V13 requires finite transverse
    // splits; R07 AUTO permits non-ambiguous face intersection pairs for
    // whole-face R02/R03 excision, which does not use V13 split patches.
    std::vector<RegionTopologyIndex::CollisionPair> contacts;
    RegionTopologyIndex::CollisionStatistics work{};
    std::size_t discoveredPairs{};
    std::string refusal;
    bool ready() const noexcept {
        return status==SculptSourceContactStatus::CompleteTransverse||
               status==SculptSourceContactStatus::CompleteIntersecting;
    }
};
SculptSourceContactAssessment assessPostSculptOriginalContacts(
    const ProposedSculptStep& proposed,bool symmetric,
    const SculptSourceContactLimits& limits={},
    const std::unordered_map<VertexId,VertexId>* mirrors=nullptr,
    std::size_t groupedSeeds=1,
    bool requireFiniteTransverseSections=true);

// R07 live AUTO: classify *new contacts involving this stamp's changed
// neighborhood*, then exclude pairs already present in the immutable
// before root. Full post-state mirror and element-budget validation remains.
// This is NOT the protected V13 whole-original contact discovery above.
SculptSourceContactAssessment assessPostSculptRepairContacts(
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
// M40: ONE pre-publication resource envelope across every private COW
// edit of an opt-in physical or queued exact Step. These are conservative
// memory COPYING WORK proxies, not peak RSS or general process limits.
struct SculptStepResourceLimits {
    std::size_t maxPrivateCopiedBytes=1073741824;
    std::size_t maxChangedIdReferences=131072;
    std::size_t maxFinalDirtyPartitions=4096;
};
enum class SculptStepResourcePath {None, PhysicalStamp, QueuedExact};
enum class SculptPostCasFaultPoint {
    AfterRootCas, AfterMirrorState, AfterHistoryAppend, AfterAuxCommit
};
enum class SculptRefusalCategory {None,CallerOrProvenance,GeometryOrTopology,FiniteWorkBudget,StaleExpectedRoot};
struct SculptStageWallObservations {
    // Wall-clock OBSERVATIONS, not deterministic time upper bounds.
    double initialGeometryMs{},sourceGeometryMs{},originalProofMs{};
    double admissionMs{},publicationAndHistoryMs{};
};
// V18-M04: bounded private full-geometry trials, never partial CAS.
struct SculptOffModeBacktrackLimits {
    bool enabled=true; // only active when the enclosing OFF policy is ON
    std::size_t maxTrials=5; // fractions 1, 1/2, 1/4, 1/8, 1/16
    double maxNormalStepInLocalEdges=0.5; // conservative endpoint/tunnelling bound
    std::size_t maxTrialCopiedBytes=536870912;
    std::size_t maxTrialChangedIdReferences=131072;
    std::size_t maxTrialDistanceTests=32768;
};
struct SculptBacktrackWork {
    std::size_t trials{},declined{};
    std::size_t privateCopiedBytes{},changedIdReferences{},distanceTests{};
    double chosenNormalFraction=0;
    bool refused{};
};

struct SculptStepResourceWork {
    SculptStepResourcePath path{SculptStepResourcePath::None};
    bool evaluated{}; // a complete candidate was measured
    bool published{}; // exact final original-root CAS succeeded
    bool refused{};   // no root CAS; budget or other source policy
    bool postCasFailed{}; // irrevocable CAS, quarantined auxiliary state
    std::size_t privateCopiedBytes{}; // all private edits, including intermediates
    std::size_t changedIdReferences{}; // reference count, not unique stable IDs
    std::size_t finalDirtyPartitions{}; // final renderer owner partitions
    ExactStageWork exactStage{}; // all actual M41–M45 geometry and original-source proof operations
    SculptBacktrackWork backtracking{}; // rejected and accepted private V18 trials
    RegionTopologyIndex::CollisionStatistics autoDetectionWork{}; // R07 local source query only
    double elapsedMilliseconds{}; // observed monotonic wall time, not budget work
    SculptStageWallObservations stageWall{};
    SculptRefusalCategory refusalCategory{SculptRefusalCategory::None};
    std::string refusal; // empty on a published/accepted candidate
};
// Current in-memory worker or direct-brush session only. Disk checkpoints
// never authenticate or restore these telemetry accumulators.
struct SculptResourceTotals {
    std::size_t attempts{},physicalAttempts{},queuedExactAttempts{};
    std::size_t published{},refused{},unchanged{},postCasFailures{};
    std::size_t provenanceRefusals{},geometryRefusals{},budgetRefusals{},staleRefusals{};
    std::size_t attemptedPrivateCopiedBytes{},publishedPrivateCopiedBytes{};
    std::size_t attemptedChangedIdReferences{},publishedChangedIdReferences{};
    std::size_t attemptedDirtyPartitions{},publishedDirtyPartitions{};
    ExactStageWork attemptedExact{},publishedExact{};
    double observedAttemptMilliseconds{},observedPublishedMilliseconds{};
};
// V18-M02: new, explicitly enabled OFF-mode **private admission gate**.
// M03/M04 will supply mesh-scale clearance and constrained displacement.
// M06 alone will expose a desktop opt-in. Not V17 source-certified Merge.
struct SculptOffModeConstraintSettings {
    bool enabled=false;
    OffModeContactLimits contactLimits{};
    OffModeClearanceLimits clearanceLimits{};
    SculptOffModeBacktrackLimits backtrack{};
};

struct SculptCollisionSettings {
    bool enabled=false; // V16 approximate classifier; independent policy
    // M18: worker-only exact physical sculpt union, DEFAULT OFF. The desktop
    // never enables this before M15. No V16 scar surgery in exact mode.
    bool physicalExact=false;
    // R07: opt-in live R06 AUTO, using certified source-backed private
    // closure and the worker's ordinary final-root CAS/history/checkpoint.
    // The read-only R06 inspector and replay debugger never set this.
    bool certifiedAutoClosure=false;
    bool certifiedAutoUnifiedZipper=true;
    // R07's per-stamp changed-neighborhood BVH has different costs than
    // protected whole-original Merge. Retain finite limits for both, but
    // do not globally relax the strict protected Merge source budget.
    SculptSourceContactLimits certifiedAutoSourceLimits{
        262144,524288,{16384,4194304,1048576,524288,262144,2048},2048};
    // Only after the *local* classifier finds a new transverse contact,
    // collect the entire source contact graph for R02/R03/M03. This is
    // never charged to ordinary clean brush stamps. Protected Merge keeps
    // its separate strict V13 limits.
    SculptSourceContactLimits certifiedAutoCompleteSourceLimits{
        262144,524288,{262144,33554432,8388608,2097152,1048576,2048},2048};
    SculptOffModeConstraintSettings offModeConstraints{};
    // Test-only exceptional callback; unset in production and desktop.
    std::function<void(SculptPostCasFaultPoint)> postCasFaultInjection{};
    SculptOriginalIntentInputLimits originalIntentInput{};
    SculptOriginalTerminalLimits originalTerminal{};
    SculptStepResourceLimits stepResources{};
    ExactStageWorkLimits exactStageLimits{}; // M41 original-root geometry work
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
// A fully classified Contact starts/extends an episode, and only three
// CONSECUTIVE fully accepted Clean decisions close it. Rejected, ambiguous
// or budget-exhausted checks reset the Clean streak without forging a Clean.
// This diagnostic state NEVER caches weld approval or skips V13/c1/c2/c3/d2
// or first-original M21/M29 terminal verification on the next Step.
struct SculptContactStatistics {
    std::size_t episodes{},contactChecks{},cleanChecks{},uncertainChecks{};
    std::size_t closedEpisodes{};
    std::size_t quietRun{},contactRun{},longestContactRun{};
    bool active{};
};
class SculptContactTracker {
public:
    explicit SculptContactTracker(std::size_t quietChecks=3);
    // nullopt is an uncertain/refused/exhausted check. It interrupts
    // consecutive Clean or Contact streaks, but never ends an episode.
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
    // Worker-thread only; latched before a new timed stroke starts.
    void setStrengthScale(double scale) {inflate_.setStrengthScale(scale);}
    const std::vector<FaceId>& originalPatchIntent() const noexcept {
        return physicalIntendedOriginals_;
    }
    std::shared_ptr<const RegionTopologyIndex> firstOriginalSourceRoot() const noexcept {
        return firstOriginalAuthority_?firstOriginalAuthority_->anchors.original:nullptr;
    }
    void stamp(Mesh&,std::span<const BrushArea> areas) override;
    bool undo();
    bool redo();
    // Worker-thread COW paint publication, independent of Inflate geometry.
    bool paint(const std::vector<PaintDab>& dabs);
    void beginPaintStroke();
    void endPaintStroke();
    // Explicit, opt-in V17e2b2d3b worker-only command, never called by
    // the desktop Merge geometry UI (which remains disabled). The expected
    // root is captured when contacts are selected, not guessed when queued
    // commands eventually execute. A failed/stale Step publishes NOTHING.
    bool mergeExactContacts(
        std::shared_ptr<const RegionTopologyIndex> expected,
        const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
        const ExactCutStepLimits& limits={},
        const std::vector<FaceId>& intendedOriginals={});
    // M47: bounded READ-ONLY complete source contact discovery. The
    // worker calls this only on its serial edit thread for the exact
    // original-root candidate; OFF-mode or unauthenticated disk state
    // cannot obtain an accepted contact certificate. No partial contacts.
    SculptSourceContactAssessment discoverLiveMergeContacts(
        const SculptSourceContactLimits& limits={}) const;
    std::size_t completedUnions() const noexcept {return completedUnions_;}
    std::size_t rejectedUnions() const noexcept {return rejectedUnions_;}
    HistoryStatistics historyStatistics() const noexcept {
        return history_.statistics();
    }
    DurableCheckpoint checkpoint() const;

    bool terminalFailure() const noexcept {
        return terminalFailure_.load(std::memory_order_acquire);
    }
    const std::string& terminalFailureReason() const noexcept {
        return terminalFailureReason_;
    }
    // Quarantine a half-committed original-root transaction. Never hand a
    // root with uncertain mirrors or history to a new reader.
    std::shared_ptr<const RegionTopologyIndex> snapshot() const noexcept {
        if(terminalFailure()||postCasInFlight_.load(std::memory_order_acquire))
            return nullptr;
        auto result=published_.snapshot();
        // A reader racing the CAS must not acquire an unpublished auxiliary
        // root even if it observed the gate open immediately beforehand.
        if(terminalFailure()||postCasInFlight_.load(std::memory_order_acquire))
            return nullptr;
        return result;
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
    const SculptStepResourceWork& lastStepResourceWork() const noexcept {
        return lastStepResourceWork_;
    }
    SculptResourceTotals resourceTotals() const noexcept {
        return resourceTotals_;
    }
    const StampDetails& lastDetails() const noexcept {return details_;}
    std::size_t publishedRevisions() const noexcept {return publishedRevisions_;}
    std::size_t modifiedPartitions() const noexcept {return modifiedPartitions_;}
    std::size_t copiedDirectoryNodes() const noexcept {return copiedDirectoryNodes_;}
    std::size_t clonedPartitionBytes() const noexcept {return clonedPartitionBytes_;}
    const std::vector<PartitionId>& lastDirtyPartitions() const noexcept {
        return lastDirtyPartitions_;
    }

private:
    void ensureOperational() const;
    void markPostCasFailure(std::exception_ptr error);
    void injectPostCasFault(SculptPostCasFaultPoint point) const;
    void recordStepDiagnostics() noexcept;
    struct BacktrackChoice {
        double fraction=1;
        std::string refusal;
        SculptBacktrackWork work{};
    };
    BacktrackChoice probeOffModeNormalFraction(
        const std::shared_ptr<const RegionTopologyIndex>& before,
        std::span<const BrushArea> areas);
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
    SculptStepResourceWork lastStepResourceWork_{};
    SculptResourceTotals resourceTotals_{};
    std::atomic<bool> postCasInFlight_{false};
    std::atomic<bool> terminalFailure_{false};
    std::string terminalFailureReason_;
    // A checkpoint restored without an authenticated FIRST-original
    // certificate must never automatically restart fidelity at its
    // current terminal root. M30 installs the active pre-CAS guard.
    bool originalFidelityRequired_{};
    // Immutable FIRST-source authority and original reflected-ID involution.
    std::unordered_map<VertexId,VertexId> firstOriginalMirrors_;
    std::vector<FaceId> physicalIntendedOriginals_;
    std::string physicalIntentRefusal_;
    std::vector<PartitionId> lastDirtyPartitions_;
    bool paintStrokeActive_{},paintStrokeHasRevision_{};
};

} // namespace mesher

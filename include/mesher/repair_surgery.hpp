#pragma once

#include "mesher/repair_plan.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mesher {

// Read-only V14 provenance for a nominated excision set. Counts are
// SAMPLED fragment classifications, not a certified polygon partition.
// Exposed and reversed boundaries carry occupancy information; buried
// and outside samples do not require reverse proximity to the new scar.
// Ambiguity must never be converted into confident feature preservation.
struct RemovalSurfaceProvenance {
    std::size_t sampledFaces{};
    std::size_t exposedFragments{};
    std::size_t reversedBoundaryFragments{};
    std::size_t buriedFragments{};
    std::size_t outsideFragments{};
    std::size_t ambiguousFragments{};
    std::size_t featureFragments() const noexcept {
        return exposedFragments+reversedBoundaryFragments;
    }
    std::size_t totalFragments() const noexcept {
        return featureFragments()+buriedFragments+outsideFragments+
               ambiguousFragments;
    }
};
RemovalSurfaceProvenance inspectRemovalSurfaceProvenance(
    const RegionTopologyIndex& baseline,
    const std::vector<FaceId>& removed,
    unsigned sampleDepth=2,
    std::size_t maxFragments=2048,
    bool allowReferenceFallback=true);

// Cross-revision geometry sampling for detecting cumulative local drift.
struct SurfaceWitnessAudit {
    std::size_t witnessCount{};
    std::size_t candidateFaces{};
    std::size_t distanceTests{};
    double maximumDistanceInEdges{};
    double meanDistanceInEdges{};
    double rmsDistanceInEdges{};
};
// Seven samples per original triangle: vertices, edge midpoints, center.
std::vector<Vec3> sampleSurfaceWitnesses(
    const RegionTopologyIndex& original,
    const std::vector<FaceId>& faces,
    std::size_t maxWitnesses=4096);
// A bounded read-only closest-triangle audit. The reference edge length
// comes from original geometry, independently of the brush radius.
SurfaceWitnessAudit auditSurfaceWitnesses(
    const RegionTopologyIndex& current,
    const std::vector<Vec3>& witnesses,
    const std::vector<FaceId>& candidateFaces,
    double originalEdgeLength,
    std::size_t maxDistanceTests=131072);

// V17-M19: a fixed ORIGINAL source-FaceId anchor, not nearest-world-space
// equivalence. The reference root and seven witnesses survive later edits.
// M19 audits only original faces retaining EXACT original vertex incidence:
// a split, flip, retirement or unproven rewrite must refuse instead of
// silently matching another coincident surface. M20 owns source-certified
// descendant mapping for those later topology changes.
struct OriginalSourceFaceAnchor {
    FaceId face{};
    std::array<VertexId,3> originalVertices{};
    std::array<Vec3,7> witnesses{};
    double originalEdgeLength{};
};
struct OriginalSourceSurfaceAnchors {
    std::shared_ptr<const RegionTopologyIndex> original;
    std::vector<OriginalSourceFaceAnchor> faces;
};
// Read-only snapshot capture; faces are explicit protected nominations,
// excluding deliberately moved features under the later M21 policy.
OriginalSourceSurfaceAnchors captureOriginalSourceAnchors(
    std::shared_ptr<const RegionTopologyIndex> original,
    const std::vector<FaceId>& protectedFaces,
    std::size_t maxWitnesses=4096);
// Audit against the bound original, not the prior published revision.
// expectedReference must be the EXACT original root retained by the owner;
// a stale/different reference or unproven face ancestry fails closed.
// Each source face uses only its own surviving FaceId, 7 distance tests.
// Returns finite sampled one-sided distances in each ORIGINAL local edge
// scale: no two-sided Hausdorff, cavity or Boolean permission is implied.
SurfaceWitnessAudit auditOriginalSourceAnchors(
    const OriginalSourceSurfaceAnchors& anchors,
    const std::shared_ptr<const RegionTopologyIndex>& expectedReference,
    const RegionTopologyIndex& candidate,
    std::size_t maxDistanceTests=131072);

// M24: persistent first-original protected FaceId -> living FaceIds,
// advanced ONLY by exact committed local split records. This limited
// positive domain does not yet accept source-area transfer in flips,
// deleted patches under collapse, or exact Boolean child c3/d2 retirement.
struct VersionedRefineResult;
struct SourceSplitLineage {
    std::shared_ptr<const RegionTopologyIndex> original,terminal;
    std::vector<std::pair<FaceId,std::vector<FaceId>>> living;
    std::size_t verifiedFaceSplits{};
};
SourceSplitLineage beginSourceSplitLineage(
    const OriginalSourceSurfaceAnchors& anchors);
// Rechecks EVERY emitted parent/child against original immutable pre/post
// roots and the exact oriented midpoint subdivision. A real stamp may
// move existing vertices before splitting, but must name the precise
// previous terminal root through the LocalStampBaseline. No history or
// publication mutation. Refuses stale/forged, incomplete, flip-bearing
// or over-budget evidence, not just errors involving protected faces.
SourceSplitLineage advanceCertifiedSplitLineage(
    const SourceSplitLineage& prior,
    const VersionedRefineResult& refined,
    std::size_t maxFaceEvidence=1024,
    std::size_t maxLivingDescendants=4096);

struct VersionedCollapseResult;
// V17-M25: verify actual validated 2→2 face swaps atop the M24
// source-proven split chain. A protected flip is permitted only when
// BOTH faces have the SAME unambiguous original protected source.
// Different protected owners and mixed protected/unprotected patches
// refuse rather than inferring ancestry from coordinates.
SourceSplitLineage advanceCertifiedRefineLineage(
    const SourceSplitLineage& prior,
    const VersionedRefineResult& refined,
    std::size_t maxFaceEvidence=1024,
    std::size_t maxLivingDescendants=4096);
// A source-certified collapse can advance when it leaves ALL nominated
// original protected descendant faces and their XYZ unchanged. Removing
// or moving protected Exposed/ReversedBoundary material is not allowed
// without M21's intentional displacement policy; no such policy is
// invented here. Every actual deleted/rewritten FaceId is independently
// checked against its committed pre/post transaction and stable IDs.
SourceSplitLineage advanceCertifiedCollapseLineage(
    const SourceSplitLineage& prior,
    const VersionedCollapseResult& collapsed,
    std::size_t maxFaceEvidence=1024,
    std::size_t maxLivingDescendants=4096);

// V17-M26: source-exact Boolean cut/union survivor disposition.
// The one first-original root and permanent original FaceId ownership
// stay fixed. A fully c3/M03-certified Buried/Outside original may have
// NO live descendants; this explicitly recorded retirement is distinct
// from a missing exposed or ReversedBoundary cavity feature.
struct ExactCutStep;
struct SourceExactCutLineage {
    SourceSplitLineage survivors;
    std::vector<FaceId> certifiedRetiredOrigins;
    std::size_t classifiedChildren{},protectedChildren{},retiredChildren{};
    std::size_t certifiedUncutRetirements{};
    std::size_t checkedFaceEvidence{};
};
// Read-only: independently recompute c1/c2/c3 and M03 with the ORIGINAL
// limits/mirror involution against the step's immutable PRIVATE cut root;
// match complete typed child decision and final d2 journals. A missing
// protected child, unrelated coincident source, stale root, forged c3
// classification, mirror mismatch or one-under evidence limit refuses.
// M27 owns cumulative witness comparison across these survivors, and
// M21 owns any intended sculpt source exemptions.
SourceExactCutLineage advanceCertifiedExactCutLineage(
    const SourceSplitLineage& prior,
    const ExactCutStep& step,
    const std::unordered_map<VertexId,VertexId>* originalMirrors=nullptr,
    std::size_t maxFaceEvidence=16384,
    std::size_t maxLivingDescendants=16384);

// V17-M27: a PRIVATE, read-only lifetime source patch view. The
// first ORIGINAL anchors never advance: only current terminal and
// source-owned living FaceIds change, after M24/M25/M26 verification.
// Completely buried/outside source retirement must be explicitly
// certified by M26, never inferred from zero geometric proximity.
// No policy threshold / CAS / UI authorization is implemented here.
struct OriginalPatchLifetime {
    OriginalSourceSurfaceAnchors anchors;
    SourceSplitLineage lineage;
    // Immutable verification snapshot published ONLY by begin/advance.
    // A mutable public lineage view cannot substitute coincident FaceIds
    // for independently certified owners at witness-audit time.
    std::shared_ptr<const std::vector<std::pair<FaceId,std::vector<FaceId>>>>
        verifiedOwnership;
    std::vector<FaceId> certifiedRetiredOriginals;
    std::size_t acceptedTransitions{};
    std::size_t sourceFaceEvidence{}; // cumulative finite verification work
};
struct OriginalPatchWitness {
    FaceId original{};
    SurfaceWitnessAudit sampled;
    std::size_t livingDescendants{};
};
struct OriginalPatchLifetimeAudit {
    SurfaceWitnessAudit total;
    std::vector<OriginalPatchWitness> perOriginal;
    std::vector<FaceId> certifiedRetiredOriginals;
    std::size_t livingOriginals{};
    std::size_t originalSources{};
};
OriginalPatchLifetime beginOriginalPatchLifetime(
    const OriginalSourceSurfaceAnchors& anchors);
// maxCumulativeFaceEvidence is a SHARED cap across all accepted stages,
// not a fresh per-Step cap. maxLivingDescendants bounds one complete
// candidate; exact equality accepts, one below refuses. No mutation on
// failure, including a stage whose COW root was accepted independently.
OriginalPatchLifetime advanceOriginalPatchLifetime(
    const OriginalPatchLifetime& previous,
    const VersionedRefineResult& refined,
    std::size_t maxCumulativeFaceEvidence=16384,
    std::size_t maxLivingDescendants=16384);
OriginalPatchLifetime advanceOriginalPatchLifetime(
    const OriginalPatchLifetime& previous,
    const VersionedCollapseResult& collapsed,
    std::size_t maxCumulativeFaceEvidence=16384,
    std::size_t maxLivingDescendants=16384);
OriginalPatchLifetime advanceOriginalPatchLifetime(
    const OriginalPatchLifetime& previous,
    const ExactCutStep& step,
    const std::unordered_map<VertexId,VertexId>* originalMirrors=nullptr,
    std::size_t maxCumulativeFaceEvidence=16384,
    std::size_t maxLivingDescendants=16384);
// Exactly SEVEN immutable first-original witnesses per surviving
// first-original source, each compared ONLY against its certified live
// permanent-ID descendants (not other coincident surfaces). Distances
// are normalized per ORIGINAL shortest edge. Retired, fully certified
// Buried/Outside sources are reported separately, never silently
// discarded or counted as geometry-preserved. This is finite ONE-SIDED
// sampled evidence, NOT a continuous two-sided Hausdorff bound.
OriginalPatchLifetimeAudit auditOriginalPatchLifetime(
    const OriginalPatchLifetime& lifetime,
    const std::shared_ptr<const RegionTopologyIndex>& expectedOriginal,
    std::size_t maxDistanceTests=131072,
    std::size_t maxLivingDescendants=16384);

// Transactional closure surgery on a private region revision.
// A successful result remains unpublished; the caller may perform an
// IndexedMeshRepository::publishIfCurrent() against the original snapshot.
// Mirrored surgery is available below; unsupported complex topology
// remains fail-closed before live Merge Geometry integration.
struct RepairSurgerySettings {
    RepairPlanSettings planning{};
    std::size_t partitionCapacity=256;
    std::size_t maxNewVertices=1024;
    std::size_t maxNewFaces=4096;
    std::size_t maxContourVertices=512;
    // Local target length is the smaller median ORIGINAL ring edge length,
    // independent of brush radius. Long boundary edges are divided; every
    // adjacent surviving face must be divided in that SAME transaction.
    double maximumBoundaryEdgeFactor=1.65;
    double maximumNewEdgeFactor=4.0;
    double minimumTriangleQuality=0.025;
    std::size_t maximumAddedFanDegree=20;
    // Every new scar point must remain near the actually excised geometry
    // at the local ORIGINAL contour resolution, not the brush radius.
    double maximumSurfaceDeviationInEdges=2.5;
    // Optional reverse-distance guard for EXPOSED surfaces known to be
    // preserved by the operation. Do not enable blindly for buried red
    // faces: intentionally discarded internal material has no geometric
    // counterpart and a two-sided Hausdorff condition is inappropriate.
    bool requireReverseSurfaceFidelity=false;
    double maximumReverseDeviationInEdges=2.5;
    // Alternative opt-in: preserve ONLY V14-sampled Exposed and
    // ReversedBoundary fragments. Buried/Outside pieces may disappear.
    // Ambiguous classification refuses privately. Mutually exclusive
    // with requireReverseSurfaceFidelity (which checks ALL removed faces).
    bool requireExposedFragmentFidelity=false;
    std::size_t maxProvenanceFragments=2048;
    std::size_t maxReverseDistanceTests=131072;
    // Limit post-commit residual checks to an explicitly bounded local
    // neighborhood. Over-budget validation rejects the private revision.
    std::size_t maxResidualFaces=4096;
    // Bound the BVH visit/candidate/contact work for the final private scar.
    // A refusal throws length_error without publishing any proposed root.
    RegionTopologyIndex::CollisionQueryLimits residualCollisionLimits{};
};
struct RepairSurgeryResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    RevisionCommit commit;
    std::vector<VertexId> insertedVertices;
    std::vector<FaceId> insertedFaces;
    std::vector<FaceId> retiredFaces;
    std::vector<VertexId> retiredVertices;
    std::size_t splitBoundaryEdges{};
    std::size_t capFaces{},zipperFaces{};
    std::size_t verifiedLocalFaces{};
    double maximumSurfaceDeviationInEdges{};
    double maximumReverseDeviationInEdges{};
    RemovalSurfaceProvenance removalProvenance{};
    std::size_t checkedReverseFragments{};
    std::size_t reverseDistanceTests{};
};

// Throws before returning a result if any topology or candidate geometry
// check fails. Neither the passed snapshot nor any repository is modified.
// This overload accepts a validated non-symmetric plan; all geometry
// quality checks and private publication guards still apply.
RepairSurgeryResult applyRepairSurgery(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const RepairPlan& plan,const RepairSurgerySettings& settings={});

// Symmetric V16b variant: all affected vertices must have an explicit
// involutive reflection pairing. Plans can contain independent mirrored
// cap pairs, paired mirrored zippers, or a zipper between two contours
// which reflect into one another (built through ONE shared X=0 ring).
// Self-reflected single capped loops with exactly two existing X=0
// endpoints are supported. More complex cross-plane cases fail closed.
RepairSurgeryResult applyMirroredRepairSurgery(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const RepairPlan& plan,
    const std::vector<std::pair<VertexId,VertexId>>& vertexMirrors,
    const RepairSurgerySettings& settings={});


// Normalize one self-reflected edge which crosses X=0 and is bounded by
// exactly two self-reflected triangles. Both triangles are split against
// the SAME newly inserted, self-mirrored seam vertex. The operation
// returns a validated, still-unpublished private revision; it does not
// attempt the later cap/zipper excision or publish a worker Step.
// General plane crossings through unpaired faces and multi-contact
// contours remain unsupported and fail closed.
struct PlaneCrossingNormalization {
    std::shared_ptr<const RegionTopologyIndex> index;
    RevisionCommit commit;
    VertexId seamVertex{};
    std::array<FaceId,2> insertedFaces{};
    std::array<FaceId,2> preservedFaces{};
};
PlaneCrossingNormalization normalizeSelfMirroredPlaneEdge(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    VertexId first,VertexId second,
    const std::vector<std::pair<VertexId,VertexId>>& vertexMirrors,
    std::size_t partitionCapacity=256);


// V16c2b2: normalize every supported crossing of the ORIGINAL removal
// contours, then RE-PLAN the expanded removal set against the resulting
// private immutable revision and apply its cap/zipper surgery. The only
// publication step is an optional caller CAS against the ORIGINAL snapshot:
// neither intermediate revision is ever independently published.
struct MirroredCrossingRepair {
    RepairSurgeryResult surgery;          // final revision, never published here
    RepairPlan normalizedPlan;            // refers to the normalized revision
    std::vector<VertexId> planeSeamVertices;
    std::vector<FaceId> normalizationInsertedFaces;
    std::size_t normalizedCrossingEdges{};
    // Summation over every private normalization plus the last surgery;
    // counts transactions' work, not unique modified partitions.
    RevisionCommitStatistics accumulatedStatistics{};
};
MirroredCrossingRepair repairMirroredContoursAcrossPlane(
    std::shared_ptr<const RegionTopologyIndex> original,
    const RepairPlan& originalPlan,
    const std::vector<std::pair<VertexId,VertexId>>& vertexMirrors,
    const RepairSurgerySettings& settings={},
    std::size_t maximumCrossings=16);


// This is the CLASSIFICATION-AWARE entry point, unlike low-level
// planRemovalContours(), which accepts ANY caller-proposed deletion.
// Caller chooses a policy explicitly: no new behavior is inferred from
// triangle winding, and no worker publication is performed here.
enum class ClassifiedFeaturePolicy {
    TopologyOnly,
    PreserveSampledExposedFeatures
};
struct ClassifiedExcisionRepair {
    RepairPlan plan; // includes NoChanges, ambiguous, or budget refusals
    std::optional<RepairSurgeryResult> surgery; // only for a Ready plan
};
ClassifiedExcisionRepair applyClassifiedExcisionRepair(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const std::vector<VertexId>& changedVertices,
    const std::vector<FaceId>& changedFaces,
    const std::vector<FaceId>& componentSeeds,
    ClassifiedFeaturePolicy policy,
    const RepairSurgerySettings& settings={});


} // namespace mesher

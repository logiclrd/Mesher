#pragma once

#include "mesher/repair_plan.hpp"

#include <cstddef>
#include <memory>
#include <optional>
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
    std::size_t maxFragments=2048);

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

#pragma once

#include "mesher/repair_plan.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace mesher {

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


} // namespace mesher

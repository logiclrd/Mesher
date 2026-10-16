#pragma once

#include "mesher/repair_plan.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace mesher {

// V16a: atomic, NON-symmetric closure surgery on a private region revision.
// A successful result remains unpublished; the caller may perform an
// IndexedMeshRepository::publishIfCurrent() against the original snapshot.
// The symmetric topology path (including X=0 seam sharing) is a separate
// prerequisite before live Merge Geometry integration.
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
};

// Throws before returning a result if any topology or candidate geometry
// check fails. Neither the passed snapshot nor any repository is modified.
// V16a only accepts previously validated, non-symmetric plans and supported
// non-self-overlapping local cuts. V16b will add mirrored topology changes.
RepairSurgeryResult applyRepairSurgery(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const RepairPlan& plan,const RepairSurgerySettings& settings={});

// Symmetric V16b variant: all affected vertices must have an explicit
// involutive reflection pairing. Plans can contain independent mirrored
// cap pairs, paired mirrored zippers, or a zipper between two contours
// which reflect into one another (built through ONE shared X=0 ring).
// Self-reflected individual loops and cases requiring unequal boundary
// subdivision fail closed until supported by a symmetric refinement.
RepairSurgeryResult applyMirroredRepairSurgery(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const RepairPlan& plan,
    const std::vector<std::pair<VertexId,VertexId>>& vertexMirrors,
    const RepairSurgerySettings& settings={});


} // namespace mesher

#pragma once

#include "mesher/repair_auto_plan.hpp"
#include "mesher/repair_mixed_primitives.hpp"
#include "mesher/repair_mixed_source_view.hpp"
#include "mesher/repair_mixed_positions.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace mesher {

// R06-M04A stages BOTH closure kinds before any private COW modification.
// No intermediate mesh is published, committed or assumed closed. Actual
// quality-guided splitting and one final COW transaction belong to M04B.
enum class MixedClosureFaceKind {Survivor,Cap,Zipper};
struct MixedClosureFace {
    FaceId stableId{}; // zero -> new, assigned only in later M04B
    PartitionId owner{};
    PrivateRepairTriangle vertices{};
    MixedClosureFaceKind kind{MixedClosureFaceKind::Survivor};
    double targetEdge{};
    // For original R03 survivors this is a FaceId handle, NOT a duplicate
    // editable triangle. Unbound records are transaction-new faces, or
    // standalone legacy helper fixtures that retain local vertices.
    std::shared_ptr<MixedClosureSourceView> sourceView{};

    PrivateRepairTriangle triangle() const {
        if(stableId&&sourceView)
            return sourceView->face(stableId).vertices;
        return vertices;
    }
    void setTriangle(PrivateRepairTriangle next) {
        if(stableId&&sourceView)
            sourceView->overrideFace(stableId,next);
        else vertices=next;
    }
};
struct MixedClosureStaging {
    // M15A foundation: bind one EXACT immutable R03 open mesh and its
    // directed hole rims. Read-only boundary queries reflect subsequently
    // inserted faces and source-face overrides; no stale yellow path.
    MixedClosureStaging()=default; // staged fixture compatibility until M15C
    MixedClosureStaging(
        std::shared_ptr<const RegionTopologyIndex> source,
        const std::vector<RepairContour>& contours);
    std::size_t trackSurvivor(FaceId id,std::size_t maxFaces);
    // Allocate an identity only inside the private COW revision. This is
    // intentionally NOT idempotent: coordinate coincidence is not welding.
    VertexId addVertex(MutableMeshView& privateView,Vec3 position,
                       PartitionId owner,std::size_t maxGeneratedVertices=1024);
    // Oriented face-existence request. Cyclic rotations are identical;
    // reverse orientation conflicts; a repeat returns the same ordinal.
    // A generated face's permanent FaceId is assigned at consolidation.
    std::size_t ensureFace(
        VertexId a,VertexId b,VertexId c,MixedClosureFaceKind kind,
        PartitionId owner,double targetEdge=0,
        std::size_t maxTrackedFaces=1000000);
    bool hasEffectiveEdge(VertexId a,VertexId b) const;
    // Current directed, closed walks of one-sided effective edges.
    // Incomplete/forked boundary topology fails instead of rendering a
    // stale R03 loop. Empty when completely capped/zippered.
    std::vector<std::vector<VertexId>> currentBoundaryLoops() const;
    // The source and its transactional overrides are authoritative for
    // every original survivor face and vertex. New triangles remain local.
    std::shared_ptr<MixedClosureSourceView> sourceView;
    std::vector<FaceId> survivingFaceIds;
    std::vector<MixedClosureFace> faces;
    // Initially immutable source contour walks (not the mutable displayed
    // paths); currentBoundaryLoops derives new walks from edge incidence.
    std::vector<std::vector<VertexId>> initialRimPaths;
    // Source-backed survivor handles plus owned new vertex positions.
    MixedClosurePositions positions;
    std::map<VertexId,PartitionId> vertexOwners;
    std::size_t capLoops{};
    std::size_t zipperPairs{};
    std::size_t capTriangles{};
    std::size_t zipperTriangles{};
    std::size_t boundarySidesCovered{};
    // Temporary IDs are NEVER published. M04B replaces them with actual
    // monotonically allocated immutable IDs before its conforming audit.
    std::vector<VertexId> provisionalSpatialVertices;
    bool usedSpatialCapFallback{};
    bool usedMirroredSeamZipper{};
    std::size_t mirroredSeamZipperPairs{};
    std::size_t mirroredSeamZipperVertices{};
    std::size_t spatialCapLoops{};
    std::size_t mirroredRibbonLoops{};
    std::size_t spatialPoissonIterations{};
    double minimumSpatialInitialQuality{1};
};

struct MixedClosureStageSettings {
    bool requireReflectedGeometry=false;
    std::size_t maxLoops=16;
    std::size_t maxNewTriangles=4096;
    std::size_t maxTrackedFaces=1000000;
    ForcedZipperSettings zipper{};
};

// One immutable R03 root and exact global M03 contour decisions. Rejects
// duplicated, missing, misoriented or cross-stitched rims before returning a
// deterministic list of pre-refinement triangles and retained face records.
MixedClosureStaging stagePrivateMixedClosure(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const MixedClosureStageSettings& bounds={});

} // namespace mesher

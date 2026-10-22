#pragma once

#include "mesher/repair_auto_plan.hpp"
#include "mesher/repair_mixed_primitives.hpp"

#include <cstddef>
#include <map>
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
};
struct MixedClosureStaging {
    std::vector<MixedClosureFace> faces;
    std::map<VertexId,Vec3> positions;
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
    std::size_t spatialCapLoops{};
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

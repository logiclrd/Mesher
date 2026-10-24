#pragma once
#include "mesher/repair_zipper.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace mesher {

// R05 never picks zipper or refuses a valid single hole merely because no
// partner exists. Each oriented R03 contour is capped independently in a
// private, frozen transaction. Contours which do not support a simple
// projected disk are refused explicitly, not silently guessed.
struct ForcedCapSettings {
    std::size_t maxLoops=16;
    std::size_t maxContourVertices=512;
    std::size_t maxInsertedVertices=1024;
    std::size_t maxNewFaces=4096;
    std::size_t maxRefinementSteps=512;
    std::size_t maxAuditedFaces=1000000;
    double maxCapEdgeFactor=2.5;
    double minCapQuality=0.10;
    double minSurvivorQuality=0.012;
};

struct ForcedCapResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::vector<FaceId> greenFaces;
    std::vector<VertexId> insertedVertices;
    std::vector<PartitionId> touchedPartitions;
    std::size_t cappedLoops{};
    std::size_t totalNewFaces{};
    std::size_t refinementSteps{};
    double minimumCapQuality{1};
};

// Takes a real R03 open root, caps every independent contour, keeps
// original FaceIds/VertexIds stable and tombstones unchanged, rebuilds
// the private index, and certifies one globally closed oriented manifold
// (possibly with multiple disconnected closed components).
ForcedCapResult applyForcedPrivateCap(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const ForcedCapSettings& limits={});

} // namespace mesher

#pragma once

#include "mesher/repair_mixed_stage.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace mesher {

struct MixedPrivateClosureLimits {
    MixedClosureStageSettings staging{};
    std::size_t maxInsertedVertices=1024;
    std::size_t maxNewFaces=4096;
    std::size_t maxRefinementSteps=512;
    std::size_t maxAuditedFaces=1000000;
    double minSurvivorQuality=0.012;
    double minCapQuality=0.10;
    double minZipperQuality=0.10;
    double maxCapEdgeFactor=2.5;
    double maxZipperEdgeFactor=2.5;
    // Conforming pre-refinement of zipper boundary/retained-face edges.
    double maxZipperBoundaryEdgeFactor=0.5;
};

struct MixedPrivateClosureResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::vector<FaceId> greenFaces;
    std::vector<FaceId> blueFaces;
    std::vector<VertexId> insertedVertices;
    std::vector<PartitionId> touchedPartitions;
    std::size_t cappedLoops{};
    std::size_t zipperPairs{};
    std::size_t newFaces{};
    std::size_t refinementSteps{};
    double minimumCapQuality{1};
    double minimumZipperQuality{1};
};

// R06-M04B: one private COW transaction for the ENTIRE M03 mixed plan.
// All cap+zip faces share the same old-neighbor face ledger; every split
// propagates to both tracked incident sides. Failures commit no result.
// A complete, oriented, closed vertex-manifold audit precedes return.
// R06-M04C may subsequently retire a reliably enclosed whole component.
MixedPrivateClosureResult applyPrivateMixedClosure(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const MixedPrivateClosureLimits& limits={});

} // namespace mesher

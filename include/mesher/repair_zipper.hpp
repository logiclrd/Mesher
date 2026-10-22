#pragma once
#include "mesher/repair_excision.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mesher {

// R04 is a forced-zipper-only PRIVATE inspection of R03's open cut.
// It never decides cap-versus-zipper from V15's planar-frustum heuristic.
// Unpaired/ambiguous contours, invalid seams and exhausted budgets refuse.
struct ForcedZipperSettings {
    std::size_t maxLoops=8;
    std::size_t maxContourVertices=512;
    std::size_t maxNewVertices=1024;
    std::size_t maxNewFaces=4096;
    std::size_t maxRefinementSteps=512;
    std::size_t maxAuditedFaces=1000000;
    double maxPairDistanceInEdges=9.0;
    double maximumPerimeterRatio=4.0;
    double minMatchingAdvantage=0.12;
    double maxBoundaryEdgeFactor=1.25;
    double maxSeamEdgeFactor=2.5;
    double minSeamQuality=0.045;
    double minSurvivorQuality=0.012;
};

struct ForcedZipperResult {
    std::shared_ptr<const RegionTopologyIndex> index;
    std::vector<FaceId> blueFaces; // actual inserted scar triangles
    std::vector<VertexId> insertedVertices;
    std::vector<std::pair<std::size_t,std::size_t>> matchedLoops;
    std::vector<PartitionId> touchedPartitions;
    std::size_t splitBoundaryEdges{};
    std::size_t refinementSteps{};
    std::size_t totalNewFaces{};
    double minimumSeamQuality{1};
    std::size_t closedEdges{};
};

// This consumes the actual privately OPEN R03 root, never the published
// worker candidate. It applies matched zipper(s) and bounded adaptive
// local split propagation across new scar and old retained neighbor faces,
// then independently audits a closed oriented two-manifold.
ForcedZipperResult applyForcedPrivateZipper(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const ForcedZipperSettings& limits={});

} // namespace mesher

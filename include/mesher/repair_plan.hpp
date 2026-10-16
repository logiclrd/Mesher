#pragma once

#include "mesher/region_edit.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mesher {

// V15 describes, but NEVER applies, whole-triangle approximate excision.
// The caller may inspect rejected plans; no returned result authorizes
// deleting geometry until V16 has independently validated a transaction.
enum class RepairPlanStatus {
    NoChanges,
    Ready,
    AmbiguousClassification,
    BudgetExceeded,
    InvalidBoundary,
    UnmatchedContours,
    ExcessiveDeviation
};
struct RepairPlanSettings {
    // Hard per-query limits; neither changed brush radius nor model scale
    // changes these bounds. The target h derives from LOCAL surviving edges.
    std::size_t maxClassifiedFaces=512;
    std::size_t maxRemovedFaces=256;
    std::size_t maxBoundaryEdges=2048;
    std::size_t maxContours=16;
    unsigned sampleDepth=2; // V14 4^depth, bounded there at depth<=4
    // Whole-triangle removal is an approximation: a mixed triangle can be
    // selected only if at least this fraction of samples is buried/outside.
    double minimumHiddenFraction=0.625;
    // Measured contour gap / local mean edge length, not a hard Hausdorff
    // error bound; near-tangent/uneven-density cases can be rejected.
    double maximumGapInEdges=2.5;
    double maximumPerimeterRatio=3.0;
};
struct RepairContour {
    // Each directed edge follows the retained/surviving face's winding;
    // the final edge returns to vertexIds.front() (not duplicated here).
    std::vector<VertexId> vertexIds;
    std::vector<FaceId> survivingFaces;
    double perimeter{};
    double meanEdgeLength{};
};
struct RepairContourPair {
    std::size_t first{},second{};
    // How the second contour was aligned when comparing perimeter samples;
    // actual zipper orientation/triangulation is deferred to V16.
    bool reverseSecond{};
    std::size_t sampleOffset{};
    double maxSampleDistance{};
    double gapInEdges{};
};
struct RepairPlan {
    RepairPlanStatus status{RepairPlanStatus::NoChanges};
    std::string reason;
    std::vector<FaceId> removedFaces;
    std::vector<RepairContour> contours;
    std::vector<RepairContourPair> pairs;
    std::size_t intersections{},classifiedFaces{},uncertainFaces{};
    std::size_t mixedFaces{},boundaryEdges{};
    // Approximation heuristics, NOT certified geometric error bounds.
    double maximumSeamGapInEdges{};
    double maximumRemovedEdgeLength{};
    bool ready() const noexcept {return status==RepairPlanStatus::Ready;}
};

// Low-level, read-only contour/pair constructor. This takes an ALREADY
// proposed removal set, irrespective of whether the classification was
// justified. Useful for testing loop topology independently of V14;
// callers must not interpret Ready as permission for actual deletion.
RepairPlan planRemovalContours(const RegionTopologyIndex& revision,
    const std::vector<FaceId>& proposedRemoval,
    const RepairPlanSettings& settings={});

// Classification-aware front end. Collision and winding tests both refer
// only to the complete candidate revision. The dirty IDs identify the
// V13 search region; optional newly isolated closed-component seeds allow
// no-intersection internal bubbles to be considered independently.
RepairPlan planApproximateExcision(const RegionTopologyIndex& revision,
    const std::vector<VertexId>& changedVertices,
    const std::vector<FaceId>& changedFaces,
    const std::vector<FaceId>& componentSeeds={},
    const RepairPlanSettings& settings={});

} // namespace mesher

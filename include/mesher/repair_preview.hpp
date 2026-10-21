#pragma once
#include "mesher/repair_plan.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mesher {

// R02: diagnostic-only decision evidence, not a deletion authorization.
// Classifications are V14 face-fragment *samples*, not exact material cuts.
// Independent intersection islands, face-excision components, and surviving
// directed boundary contours have distinct cardinalities and relationships.
struct RepairExcisionPreview {
    std::vector<FaceId> proposedFaces;
    std::vector<FaceId> uncertainFaces;
    std::vector<FaceId> mixedFaces;
    std::vector<RepairContour> contours;
    std::size_t contactIslands{};
    std::size_t excisionComponents{};
    std::size_t classifiedFaces{};
    std::size_t boundaryEdges{};
    RepairPlanStatus contourStatus{RepairPlanStatus::NoChanges};
    std::string diagnostic; // classification/contour extraction blocker
    std::string closureAdvisory; // later R04–R06 matching/quality only
    bool complete{}; // classified and extracted valid loops; no claim of closure
};

struct RepairPreviewLimits {
    std::size_t maxClassifiedFaces=128;
    std::size_t maxProposedFaces=96;
    std::size_t maxContours=24;
    unsigned sampleDepth=1;
    double minimumHiddenFraction=.625;
};

// Uses the V14 fragment classifier and the V15 oriented-contour planner,
// but NEVER performs any RegionEdit or publishes/removes mesh topology.
// A failed or undecidable classification remains explicitly uncertain;
// an ambiguous contour or closure does not become a "ready" repair.
RepairExcisionPreview previewApproximateExcision(
    const RegionTopologyIndex& candidate,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const RepairPreviewLimits& limits={});

} // namespace mesher

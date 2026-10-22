#pragma once
#include "mesher/repair_plan.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace mesher {

// R02: read-only preview of the ORIGINAL vertex-excision contract.
// Select incident vertices of colliding faces; remove every incident face
// of every selected vertex, with bounded boundary-pinch cleanup.
// No RegionEdit is performed, no mesh root or history is published.
// V14 winding/fragment classifications are informational ONLY and never
// shrink the candidate deletion to a few buried triangle samples.
struct RepairExcisionPreview {
    std::vector<VertexId> proposedVertices;
    std::vector<VertexId> expandedBoundaryVertices;
    std::vector<FaceId> proposedFaces;
    std::vector<FaceId> uncertainFaces;
    std::vector<FaceId> mixedFaces;
    std::vector<RepairContour> contours;
    std::size_t contactIslands{};
    std::size_t excisionComponents{};
    std::size_t classifiedFaces{};
    std::size_t boundaryEdges{};
    std::size_t boundaryRepairs{};
    RepairPlanStatus contourStatus{RepairPlanStatus::NoChanges};
    std::string diagnostic; // a necessary topological/preflight condition failed
    std::string classificationAdvisory; // V14 samples are NOT deletion evidence
    std::string closureAdvisory; // R04-R06 closure fitness is not R02's job
    bool complete{}; // bounded full vertex fans AND valid directed boundary loops
};
struct RepairPreviewLimits {
    std::size_t maxSeedVertices=512;
    std::size_t maxProposedVertices=768;
    std::size_t maxProposedFaces=4096;
    std::size_t maxContours=64;
    std::size_t maxBoundaryRepairs=24;
    std::size_t maxClassifiedFaces=128; // informational V14-only query
    unsigned sampleDepth=1;
};
// Trusted contact pairs refer to the same complete private candidate.
// Selecting vertices is conservative: intersection triangle endpoints may
// include non-penetrating vertices, exactly as the R01 red overlay does.
RepairExcisionPreview previewApproximateExcision(
    const RegionTopologyIndex& candidate,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const RepairPreviewLimits& limits={});

} // namespace mesher

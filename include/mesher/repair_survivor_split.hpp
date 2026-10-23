#pragma once
#include "mesher/repair_mixed_stage.hpp"

#include <map>
#include <utility>
#include <vector>

namespace mesher {
// Pure read-only preflight: candidate edge may be split only when EVERY
// incident original-survivor child triangle remains legal. Mandatory
// splitting remains conditional on a complete, conforming mirror orbit;
// the caller must validate BOTH edges before mutating either one.
struct SurvivorSplitQuality {
    bool safe=true;
    bool touchesSurvivor=false;
    FaceId originalFace{};
    double worstChildQuality=1.0;
};
// Exact-position collision guard. Never add a vertex which collocates
// with an existing vertex (including endpoints), since it creates zero
// area children and makes exact reflected partner lookup ambiguous.
struct MidpointCollision {
    bool unique=true;
    VertexId existingVertex{};
    Vec3 point{};
};
MidpointCollision assessUniqueConformingMidpoint(
    const std::map<VertexId,Vec3>& positions,
    std::pair<VertexId,VertexId> edge);
SurvivorSplitQuality assessSurvivorSplit(
    const std::vector<MixedClosureFace>& faces,
    const std::map<VertexId,Vec3>& positions,
    std::pair<VertexId,VertexId> splitEdge,
    double minSurvivorQuality);
MidpointCollision assessUniqueConformingMidpoint(
    const MixedClosurePositions& positions,std::pair<VertexId,VertexId> edge);
SurvivorSplitQuality assessSurvivorSplit(
    const std::vector<MixedClosureFace>& faces,
    const MixedClosurePositions& positions,
    std::pair<VertexId,VertexId> splitEdge,double minSurvivorQuality);
} // namespace mesher

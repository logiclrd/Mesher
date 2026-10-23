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
SurvivorSplitQuality assessSurvivorSplit(
    const std::vector<MixedClosureFace>& faces,
    const std::map<VertexId,Vec3>& positions,
    std::pair<VertexId,VertexId> splitEdge,
    double minSurvivorQuality);
} // namespace mesher

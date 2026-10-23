#pragma once
#include "mesher/repair_mixed_stage.hpp"
#include "mesher/repair_preview.hpp"
#include <map>
#include <optional>

namespace mesher {
// Certificate for a *fixed planar rim-tip policy*, not a claim about all
// possible freely folded 3D surfaces. Both non-tip face vertices must lie
// on the TWO original, unchanged R03 contour edge segments; the planar
// wedge's sharp apex bounds the maximum possible triangle quality.
struct ConstrainedRimTipQuality {
    VertexId fixedTip{};
    double upperBound{};
    double actualQuality{};
};
std::optional<ConstrainedRimTipQuality> certifyPlanarRimTipQuality(
    const MixedClosureFace& face,
    const std::map<VertexId,Vec3>& positions,
    const std::vector<RepairContourLoop>& contours,
    double standardMinimumQuality);
}

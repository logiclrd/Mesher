#pragma once

#include "mesher/repair_cap.hpp"

#include <array>
#include <map>
#include <vector>

namespace mesher {

// R06-M04 shared oriented triangle builders. These wrappers call the exact
// R04/R05 algorithms, leaving forced-mode behavior unchanged. IDs are the
// existing immutable R03 vertices (or staged splitter vertices).
using PrivateRepairTriangle=std::array<VertexId,3>;
std::vector<PrivateRepairTriangle> triangulatePrivateCapDisk(
    const RepairContour& contour,
    const std::map<VertexId,Vec3>& positions,
    double localEdgeLength);
std::vector<PrivateRepairTriangle> triangulatePrivateZipperStrip(
    const std::vector<VertexId>& first,
    const std::vector<VertexId>& second,
    const std::map<VertexId,Vec3>& positions,
    const ForcedZipperSettings& bounds);

} // namespace mesher

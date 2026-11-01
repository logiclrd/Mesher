#pragma once

#include "mesher/mesh.hpp"

#include <cstdint>
#include <vector>

namespace mesher {

// Optional diagnostics for the dual-graph facet-colour diffusion algorithm.
struct StlFacetColourMetrics {
    double preparationMs=0;
    double assignmentMs=0;
    double rgbRms=0;       // area-weighted error versus continuous facet target
    double rgbBias=0;      // magnitude of the area-weighted mean RGB residual
    double edgeContrast=0; // edge-length-weighted adjacent facet RGB variation
};

// Convert an editable vertex-RGB surface to one quantized RGB555 colour per
// triangle, selecting colours from each triangle's original vertex palette
// while diffusing residuals across neighbouring faces on the topological dual.
// Output is R-low 5:5:5, independent of VisCAM or Materialise Magics packing.
// The validated source mesh must be a closed, consistently wound manifold.
// RGB alpha is not representable in STL facet colours.
std::vector<std::uint16_t> stlFacetColourRGB555(
    const Mesh& mesh,std::uint64_t seed=42,
    StlFacetColourMetrics* metrics=nullptr);

} // namespace mesher

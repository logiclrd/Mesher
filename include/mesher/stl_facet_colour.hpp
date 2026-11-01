#pragma once

#include "mesher/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mesher {

// Experimental vertex-RGB -> facet-RGB555 strategies. Mean is the historical
// baseline; Palette and Diffusion deliberately retain source-palette contrast.
// No STL facet colour encoding can retain editable alpha.
enum class StlFacetColourStrategy {
    Mean,
    Palette,
    Diffusion,
};

struct StlFacetColourMetrics {
    double preparationMs=0;
    double assignmentMs=0;
    double rgbRms=0;       // area-weighted error versus continuous facet means
    double rgbBias=0;      // length of the area-weighted mean RGB residual
    double edgeContrast=0; // edge-length-weighted adjacent facet RGB variation
};

// Each result is a 5:5:5 value with red in the low five bits, independent of
// Magics/VisCAM byte layout. Inputs must have complete, finite vertex RGB.
// The topology must be a closed manifold for dual-graph diffusion; the caller
// validates this before entering the STL output stream.
std::vector<std::uint16_t> stlFacetColourRGB555(
    const Mesh& mesh,StlFacetColourStrategy strategy,
    std::uint64_t seed=42,StlFacetColourMetrics* metrics=nullptr);

} // namespace mesher

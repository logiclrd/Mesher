#pragma once
#include "mesher/mesh.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace mesher {
enum class StlColourMethod {Average,Stochastic,Diffusion};
const char* stlColourMethodName(StlColourMethod method) noexcept;
struct StlColourMethodMetrics {
    StlColourMethod method=StlColourMethod::Diffusion;
    double conversionMs=0,evaluationMs=0,rgbRms=0,alphaRms=0,contrastError=0;
};
struct StlColourComparison {
    double graphPreparationMs=0;
    std::size_t vertices=0,faces=0,uncolouredFacets=0;
    std::uint64_t seed=1;
    StlColourMethod selected=StlColourMethod::Diffusion;
    std::array<StlColourMethodMetrics,3> methods{};
};
// Production path: compute and profile only the selected method.
// Diffusion is the default for STL imports; Average and Stochastic are
// available explicitly as diagnostic alternatives.
std::vector<VertexRGBA> convertStlFacetColours(
    const Mesh& geometry,
    const std::vector<std::optional<VertexRGBA>>& facets,
    StlColourMethod selected,std::uint64_t seed,
    StlColourMethodMetrics* profile=nullptr);

// Explicit A/B/C benchmark: compute all three candidates and their metrics.
std::vector<VertexRGBA> compareStlFacetColours(
    const Mesh& geometry,
    const std::vector<std::optional<VertexRGBA>>& facets,
    StlColourMethod selected,std::uint64_t seed,
    StlColourComparison* profile=nullptr);
} // namespace mesher

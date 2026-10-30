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
    StlColourMethod method=StlColourMethod::Average;
    double conversionMs=0,evaluationMs=0,rgbRms=0,alphaRms=0,contrastError=0;
};
struct StlColourComparison {
    double graphPreparationMs=0;
    std::size_t vertices=0,faces=0,uncolouredFacets=0;
    std::uint64_t seed=1;
    StlColourMethod selected=StlColourMethod::Diffusion;
    std::array<StlColourMethodMetrics,3> methods{};
};
std::vector<VertexRGBA> compareStlFacetColours(
    const Mesh& geometry,
    const std::vector<std::optional<VertexRGBA>>& facets,
    StlColourMethod selected,std::uint64_t seed,
    StlColourComparison* profile=nullptr);
} // namespace mesher

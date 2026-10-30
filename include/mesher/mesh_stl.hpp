#pragma once

#include "mesher/mesh.hpp"
#include "mesher/mesh_stl_color.hpp"
#include <filesystem>

namespace mesher {
// Strict STL import: exact-coordinate facet welding, optional supported
// binary facet colours, and no publication or source-state mutation.
struct StlImportOptions {
    StlColourMethod method=StlColourMethod::Diffusion;
    std::uint64_t seed=1;
};
Mesh parseStlMesh(const std::filesystem::path& path,
                  StlImportOptions options={});
} // namespace mesher

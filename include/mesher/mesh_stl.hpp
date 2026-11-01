#pragma once

#include "mesher/mesh.hpp"
#include "mesher/mesh_stl_color.hpp"
#include <filesystem>

namespace mesher {
// Strict STL import: exact-coordinate facet welding, optional supported
// binary facet colours, and no publication or source-state mutation.
// Detected from STL data, never from the file's extension or picker filter.
// Order parallels the five explicitly supported export codecs.
enum class StlDetectedEncoding {
    Ascii, BinaryGeometry, BinaryViscam, BinaryMagicsFacet, BinaryMagicsObject
};
struct StlImportOptions {
    // Visual winner of the castle STL comparison; alternatives require
    // an explicit --stl-colour-method selection.
    StlColourMethod method=StlColourMethod::Diffusion;
    std::uint64_t seed=1;
};
Mesh parseStlMesh(const std::filesystem::path& path,
                  StlImportOptions options={},
                  StlDetectedEncoding* detected=nullptr);
} // namespace mesher

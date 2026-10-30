#pragma once

#include "mesher/mesh.hpp"
#include <filesystem>

namespace mesher {
// Strict STL import: exact-coordinate facet welding, optional supported
// binary facet colours, and no publication or source-state mutation.
Mesh parseStlMesh(const std::filesystem::path& path);
} // namespace mesher

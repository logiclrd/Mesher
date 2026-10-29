#pragma once
#include "mesher/mesh.hpp"
#include <filesystem>

namespace mesher {
// Standalone, bounded PLY reader. Does not modify any live revision,
// worker, renderer, picking state, checkpoint path or history.
Mesh parsePlyMesh(const std::filesystem::path& path);
} // namespace mesher

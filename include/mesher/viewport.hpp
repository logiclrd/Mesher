#pragma once
#include "mesher/mesh.hpp"
namespace mesher {
// Opens the primary monitor in full-screen mode and runs until Escape.
void runViewport(const Mesh& mesh);
}

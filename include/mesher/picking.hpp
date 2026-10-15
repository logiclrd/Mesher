#pragma once
#include "mesher/mesh.hpp"

#include <optional>

namespace mesher {
struct Ray {
    Vec3 origin;
    Vec3 direction; // normalized
};
// Shares the camera framing/projection constants used by the Vulkan viewport.
// Cursor coordinates and dimensions are in framebuffer pixels, with Y down.
Vec3 viewportCameraEye(double width,double height);
Ray viewportRay(double x,double y,double width,double height);
std::optional<Vec3> firstMeshHit(const Mesh& mesh,Ray ray);
} // namespace mesher

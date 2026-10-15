#pragma once

#include <cstdint>
#include <vector>

namespace mesher {

struct Vec3 {
    double x{}, y{}, z{};
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
constexpr Vec3 operator*(Vec3 a, double s) { return {a.x*s,a.y*s,a.z*s}; }
constexpr Vec3 operator/(Vec3 a, double s) { return {a.x/s,a.y/s,a.z/s}; }
constexpr double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
constexpr Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
double length(Vec3 v);
Vec3 normalized(Vec3 v);

struct Triangle { std::uint32_t a{}, b{}, c{}; };
struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
};

struct SphereOptions {
    std::uint32_t vertexCount = 5000;
    bool symmetric = true;
    std::uint64_t seed = 1;
    std::uint32_t relaxationIterations = 65;
    // The reflection plane is perpendicular to this vector. For the
    // initial camera (looking along +Y), +X is perpendicular to view.
    Vec3 seamNormal = {1.0, 0.0, 0.0};
};

// Creates an outward-wound, watertight triangle mesh with exactly the
// requested vertex count. Throws std::invalid_argument on invalid inputs
// and std::runtime_error if topology construction fails.
Mesh generateSphere(const SphereOptions& options);

// Checks index validity, outward winding, exactly two incident triangles
// per edge, connectivity and Euler characteristic V-E+F=2.
bool validateSphere(const Mesh& mesh);

} // namespace mesher

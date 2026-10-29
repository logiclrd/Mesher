#pragma once

#include <cstdint>
#include <optional>
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

// Linear, unpremultiplied RGBA. White is the default for unpainted meshes.
struct VertexRGBA {
    float r=1.f,g=1.f,b=1.f,a=1.f;
    // Uninterpreted source alpha is independent of editable paint opacity.
    std::optional<float> sourceAlpha{};
    constexpr bool operator==(const VertexRGBA&) const = default;
};
// Provenance follows the same interpolation factor as RGBA. Never quantize
// until export; a mixed-presence edge inherits the present scalar.
inline VertexRGBA interpolateVertexColor(VertexRGBA a,VertexRGBA b,double t) {
    const auto mix=[t](float x,float y) {
        return static_cast<float>(double(x)*(1.0-t)+double(y)*t);
    };
    VertexRGBA result{mix(a.r,b.r),mix(a.g,b.g),mix(a.b,b.b),mix(a.a,b.a)};
    if(a.sourceAlpha&&b.sourceAlpha)
        result.sourceAlpha=mix(*a.sourceAlpha,*b.sourceAlpha);
    else result.sourceAlpha=a.sourceAlpha?a.sourceAlpha:b.sourceAlpha;
    return result;
}
struct Triangle { std::uint32_t a{}, b{}, c{}; };
struct Mesh {
    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
    // Optional parallel colour stream; empty means every vertex is white.
    std::vector<VertexRGBA> colors{};
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

struct SphereGenerationTimings {
    double relaxationSeconds = 0;
    double triangulationSeconds = 0;
    double validationSeconds = 0;
    double totalSeconds = 0;
};

// Runtime diagnostics make optional dependencies and build performance visible.
const char* sphereHullBackend() noexcept;
unsigned sphereRelaxationThreads() noexcept;

// Creates an outward-wound, watertight triangle mesh with exactly the
// requested vertex count. Large meshes require Quickhull: the old
// quadratic fallback is only appropriate for small, dependency-free tests.
// Optional timings separate relaxation, hull and topology validation.
Mesh generateSphere(const SphereOptions& options,
                    SphereGenerationTimings* timings = nullptr);

// Checks index validity, outward winding, exactly two incident triangles
// per edge, connectivity and Euler characteristic V-E+F=2.
bool validateSphere(const Mesh& mesh);

} // namespace mesher

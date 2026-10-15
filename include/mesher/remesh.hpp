#pragma once

#include "mesher/mesh.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mesher {

// Mesh-wide diagnostics are intentionally separate from the stricter
// validateSphere() routine: radial inversion may be present during experiments.
struct MeshDiagnostics {
    double minimumEdgeLength = 0.0;
    double minimumTriangleAltitude = 0.0;
    std::size_t invertedFaces = 0;
    std::size_t shrinkingShortEdges = 0;
};

MeshDiagnostics measureMesh(const Mesh& mesh,
                            std::span<const Vec3> beforeStamp = {},
                            double minimumAllowedEdge = 0.0);

// Checks manifold indices and topology (including consistent edge orientation),
// without assuming a convex, outward-wound embedding.
bool validateClosedSphereTopology(const Mesh& mesh);

// Only for meshes produced by generateSphere({.symmetric=true}) with the
// generator's original ordering. No geometric nearest-neighbour guessing.
std::vector<std::uint32_t> symmetricSphereMirrors(std::uint32_t vertexCount);

struct CollapseSettings {
    bool enabled = false;
    double minimumEdgeRatio = 0.65; // relative to initial nominal sphere edge
    std::uint32_t maximumPairsPerStamp = 12;
};

struct CollapseStatistics {
    std::uint32_t attempted = 0;
    std::uint32_t accepted = 0;        // distinct symmetry-preserving operations
    std::uint32_t removedVertices = 0; // mirrored pairs count as two
};

// Experimental, conservative edge-collapse-only pass. It never introduces
// new geometry, never collapses an edge solely because it was already short,
// and does not relax/reposition vertices except at selected edge midpoints.
// With mirrors, a collapse and its reflection are one atomic operation.
// This is NOT yet collision prevention or a production half-edge remesher.
class DirectionalCollapser {
public:
    DirectionalCollapser(const Mesh& mesh, CollapseSettings settings,
                         std::vector<std::uint32_t> mirrors = {});
    CollapseStatistics apply(Mesh& mesh, std::span<const Vec3> beforeStamp);
    void reset(const Mesh& mesh, std::vector<std::uint32_t> mirrors = {});
    double minimumEdge() const noexcept { return minimumEdge_; }
    const std::vector<std::uint32_t>& mirrors() const noexcept { return mirrors_; }
private:
    bool attempt(Mesh& mesh, std::uint32_t a, std::uint32_t b);
    CollapseSettings settings_;
    double minimumEdge_;
    std::vector<std::uint32_t> mirrors_;
};

} // namespace mesher

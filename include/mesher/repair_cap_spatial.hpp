#pragma once

#include "mesher/triangle_collision.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mesher {

// Private LOCAL coordinates/indices: the first boundaryCount positions
// correspond to the R03 directed rim in REVERSED (cap-face) order.
// All subsequent vertices are synthetic interior vertices, not stable IDs.
struct SpatialCapPatch {
    std::size_t boundaryCount{};
    std::vector<Vec3> positions;
    std::vector<std::array<std::uint32_t,3>> faces;
    std::size_t poissonIterations{};
    double poissonResidual{};
    double minimumQuality{1};
};

struct SpatialCapSettings {
    std::size_t maxBoundaryVertices=256;
    std::size_t maxInteriorVertices=1024;
    std::size_t maxFaces=4096;
    std::size_t maxPoissonIterations=256;
    std::size_t maxIntersectionTests=50000;
    double poissonTolerance=1e-9;
    double maximumInitialEdgeFactor=12; // loose gate; M04 owns quality limits
    double normalGuidance=0.04;  // fraction of median original rim edge
};

// Bounded, nonprojective advancing-front construction + fixed-rim Poisson
// fairing. Intentionally no source collisions/immutable ID allocation here:
// callers MUST perform candidate-mesh intersections and M04 closed-manifold,
// quality and worker-root audits before accepting the PRIVATE result.
// Throws rather than returning a self-intersecting/degenerate candidate.
SpatialCapPatch buildSpatialCapFrontPoisson(
    const std::vector<Vec3>& orientedCapRim,
    const std::vector<Vec3>& survivingFaceNormals,
    const SpatialCapSettings& settings={});

} // namespace mesher

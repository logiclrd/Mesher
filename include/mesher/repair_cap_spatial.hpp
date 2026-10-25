#pragma once

#include "mesher/triangle_collision.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mesher {

struct SurvivorDirectionalCurvature;

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
    bool usedMirroredRibbonFallback{}; // alternate exact X=0 seam strip; no Poisson sweeps
};

struct SpatialCapSettings {
    std::size_t maxBoundaryVertices=256;
    std::size_t maxInteriorVertices=1024;
    std::size_t maxFaces=4096;
    std::size_t maxPoissonIterations=256;
    std::size_t maxIntersectionTests=50000;
    double poissonTolerance=1e-9;
    double maximumInitialEdgeFactor=12; // loose gate; M04 owns quality limits
    bool requireMirrorPlane=false; // exact reflection about X=0; no coordinate welding
    bool preferMirroredRibbonForTesting=false; // local regression only, no production caller
    // M17D candidate experiment: mirror-paired opposite diagonal in each
    // outward strip quad. False preserves legacy exact staging topology.
    bool alternateBandDiagonals=false;
    // M17D opt-in: correct Poisson-settled inward-ring points which shoot
    // past a neighboring fixed boundary edge, without moving R03 vertices.
    bool repairTangentialOvershoot=false;
    // M18D independent opt-in: bounded mirror-orbit first-ring placement
    // for M18C measured boundary direction/fairness. Disabled by default.
    bool optimizeBoundaryContinuity=false;
    std::size_t maxContinuityRounds=4;
    std::size_t maxContinuityTrials=40;
    double desiredMinimumQuality=0.10;
    std::size_t maxCorrectionRounds=4;
    std::size_t maxCorrectionTrials=24;
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
    const SpatialCapSettings& settings={},
    const std::vector<SurvivorDirectionalCurvature>* incomingReference=nullptr);

} // namespace mesher

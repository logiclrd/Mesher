#pragma once

#include "mesher/triangle_collision.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
    bool usedAdaptiveFront{}; // arbitrary indexed connectivity; M17-M21 ring-only probes disabled
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
    bool useAdvancingFront=false; // R06 staging enables the new primary constructor; old direct CPU fixtures remain available
    double nearSeamSnapFraction=0.25; // candidate heuristic, not a waiver of front manifold checks
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
    // M18E independent opt-in: confidence-weighted survivor curvature
    // enters the converged fixed-rim Poisson objective (not initial seeds).
    // M19A diagnostic only; the counterfactual best patch is NEVER staged.
    bool probeFirstRingFeasibility=false;
    bool stageFirstRingFeasibilityPreview=false; // step-debug ONLY; not a certified closure result
    bool probeBandDiagonalFeasibility=false; // M19C read-only counterfactual
    bool stageBandDiagonalPreview=false; // M19D step-debug-only PRIVATE face candidate
    bool stageWholeCapFairingPreview=false; // M21A optional post-M19 whole-cap fairing
    bool curvatureGuidedPoisson=false;
    double curvaturePoissonStrength=1.5; // relative to graph vertex degree
    double curvaturePoissonAnchorWeight=1.5; // retains legal M17D in-ring topology scale
    double curvaturePoissonMaxNormalShift=0.65; // * median rim edge
    std::size_t maxCurvaturePoissonIterations=128;
    std::size_t maxContinuityRounds=4;
    std::size_t maxContinuityTrials=40;
    double desiredMinimumQuality=0.10;
    std::size_t maxCorrectionRounds=4;
    std::size_t maxCorrectionTrials=24;
    double normalGuidance=0.04;  // fraction of median original rim edge
    // M23B zipper-only positional retention; zero preserves old Poisson.
    double placementAnchorWeight=0.0;
};

// M23A: optional geometric proposal hook for ordinary advancing-front
// vertex INSERTIONS only. A null hook is the exact pre-M23 path.
// The caller owns the std::function; the pointer is borrowed only during
// this synchronous construction (no registration or deferred execution).
// Front IDs are patch-local, not permanent mesh VertexIds. The selected
// front occurrence and neighbors identify the sector being expanded.
// This is a PROPOSAL, never authorization to bypass collision, finite,
// triangle quality, orientation, X=0 mirror or manifold checks.
struct SpatialCapPlacementProposal {
    Vec3 projectedPosition{};
    Vec3 frontPosition{};
    Vec3 previousPosition{};
    Vec3 nextPosition{};
    double localEdgeScale{};
    std::size_t frontOccurrence{};
    std::uint32_t frontVertex{};
    std::uint32_t previousVertex{};
    std::uint32_t nextVertex{};
    unsigned faceMode{}; // 2 -> one new vertex; 3 -> two new vertices
    std::size_t proposalOrdinal{}; // 0 or 1 in projected-front order
    // M23B: only initialized for a non-null caller functor. These views
    // refer to live, accepted FRONT state, never to an uncommitted trial.
    // The callback may inspect but MUST NOT retain or mutate them.
    std::uint8_t originatingSide{}; // 0 neutral, 1 or 2 seed contour
    const std::vector<Vec3>* activePositions{};
    const std::vector<std::uint32_t>* activeFront{};
    const std::vector<std::uint8_t>* activeSides{};
    // M23B: current geometric normal used for the zipper-only tangent
    // projection BEFORE optional gravity. Zero when not applicable.
    Vec3 projectionNormal{};
};
using SpatialCapPlacementAdjustment =
    std::function<Vec3(const SpatialCapPlacementProposal&)>;

// M22A: topology-agnostic, fixed-R03-rim graph Poisson solve.
// patch.positions[0..boundaryCount) are fixed and never overwritten.
// interiorGuidance is indexed in the SAME order as generated patch positions,
// NOT by original boundary ordinal or an assumed inset ring/hub layout.
// For mirrored sources pass an exact involutive mirror partner for EVERY
// local vertex, including all fixed boundary and synthetic interior IDs.
// The supplied patch and source-boundary snapshot are otherwise unchanged
// unless the finite solver converges; the caller performs contact/quality
// and whole-survivor source-backed certification separately.
void solveSpatialCapFixedRimPoisson(
    SpatialCapPatch& patch,
    const std::vector<Vec3>& interiorGuidance,
    const SpatialCapSettings& settings,
    const std::vector<std::size_t>& mirrorPartners={});

// Current R06 cap entry point: for now it still constructs a fixed
// n-vertex inset ring and hub, then delegates to the generic fixed-rim
// solver above. M22B replaces ONLY the geometry-construction phase in place.
// Existing M17–M21 post-processing still contains ring-specific assumptions.
// No source stable-ID allocation here; callers perform the complete M04
// source-backed quality/contact/manifold audit before PRIVATE acceptance.
SpatialCapPatch buildSpatialCapFrontPoisson(
    const std::vector<Vec3>& orientedCapRim,
    const std::vector<Vec3>& survivingFaceNormals,
    const SpatialCapSettings& settings={},
    const std::vector<SurvivorDirectionalCurvature>* incomingReference=nullptr,
    const SpatialCapPlacementAdjustment* placementAdjustment=nullptr,
    const std::vector<std::uint8_t>* boundarySides=nullptr);

} // namespace mesher

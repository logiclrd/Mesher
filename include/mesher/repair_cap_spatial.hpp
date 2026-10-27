#pragma once

#include "mesher/triangle_collision.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace mesher {

struct SurvivorDirectionalCurvature;

// M23B diagnostic only: one record per ACCEPTED generated AFM vertex.
// Vertex indices are patch-local and remapped to provisional VertexIds in
// mixed staging. A replay ENSUREFACE references those provisional IDs.
// The recorded supporting faces are the already-built AFM faces at the
// moment of proposal; an empty set means fallback guidance was used.
// Original support XYZ is retained because Poisson can later move its
// vertices. None of these records changes candidate acceptance.
struct SpatialCapPlacementTrace {
    std::uint32_t patchVertex{};
    std::uint32_t frontVertex{},frontPrev{},frontNext{};
    std::size_t proposalOrdinal{};
    std::size_t frontOperation{};
    std::uint8_t originatingSide{};
    Vec3 tangentNormal{};
    Vec3 rawProposal{},tangentProposal{},gravityProposal{};
    Vec3 stagedProposal{},poissonPosition{};
    std::array<std::array<std::uint32_t,3>,2> supportFaces{};
    std::array<std::array<Vec3,3>,2> supportAtProposal{};
    std::size_t supportCount{};
};

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
    bool usedMirroredRibbonFallback{}; // compatibility metric: always false for AFM
    bool usedAdaptiveFront{}; // generated with arbitrary indexed advancing-front connectivity
    std::vector<SpatialCapPlacementTrace> placementTrace; // zipper-only debug
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
    bool useAdvancingFront=true; // production and default: AFM only; false is an explicit retired-mode error
    double nearSeamSnapFraction=0.25; // candidate heuristic, not a waiver of front manifold checks
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

// Current R06 cap entry point: topology-independent advancing-front
// construction followed by the shared fixed-rim Poisson solver. It never
// falls back to the retired ring-and-hub triangulation. Final source-backed
// geometry, symmetry, collision and quality certification belong to M04B.
// incomingReference is a retained compatibility argument, ignored by AFM.
SpatialCapPatch buildSpatialCapFrontPoisson(
    const std::vector<Vec3>& orientedCapRim,
    const std::vector<Vec3>& survivingFaceNormals,
    const SpatialCapSettings& settings={},
    const std::vector<SurvivorDirectionalCurvature>* incomingReference=nullptr,
    const SpatialCapPlacementAdjustment* placementAdjustment=nullptr,
    const std::vector<std::uint8_t>* boundarySides=nullptr);

} // namespace mesher

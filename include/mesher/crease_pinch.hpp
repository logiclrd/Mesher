#pragma once
#include "mesher/versioned_inflate.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace mesher {

// Both modes draw XY geometry toward the fitted line. Only the local-Z sign
// and which side of the plane counts as existing depth evidence differ.
enum class SculptBrushMode {Inflate,Crease,Pinch};

// Latched at worker Begin, never mid-stroke. Width/depth are in brush-radius
// units so the same settings work on small and large imported geometry.
struct SculptDeformation {
    SculptBrushMode mode=SculptBrushMode::Inflate;
    double widthFraction=0.20;
    double depthFraction=0.20;
    Vec3 towardCamera{0,-1,0}; // positive fitted local Z
};

struct CreasePinchDiagnostics {
    double confidence{};
    double linearity{};
    double realEvidence{};
    double principalVariance{};
    double transverseVariance{};
    std::size_t supportVertices{};
    std::size_t exploredEdges{};
    std::size_t displacedVertices{};
};

struct CreasePinchMovement {
    VertexId vertex{};
    Vec3 position{};
    double surfaceDistance{};
};

struct CreasePinchProposal {
    std::vector<CreasePinchMovement> movements;
    CreasePinchDiagnostics diagnostics;
};

// Pure read-only geometry proposal. No COW, no publication, no history edits.
// contactFace=0 asks for a deterministic nearest visible candidate vertex
// fallback; callers with source FaceId should always supply it. This support
// graph cannot jump between disconnected-but-nearby surface sheets.
CreasePinchProposal proposeCreasePinch(
    const RegionTopologyIndex& before,Vec3 contact,double radius,
    double width,double depth,double strength,Vec3 towardCamera,
    SculptBrushMode mode,FaceId contactFace=0,
    std::size_t maximumVertices=32768,
    std::size_t maximumEdges=196608);

// COW adapter yielding exactly the usual VersionedInflateStamp contract.
// Mirrored input areas are supplied by the worker; solving one authoritative
// area and reflecting stable VertexIds guarantees exact X=0 symmetry.
VersionedInflateStamp versionedCreasePinchStamp(
    std::shared_ptr<const RegionTopologyIndex> before,
    std::span<const BrushArea> areas,const SculptDeformation& settings,
    double strength,
    const std::unordered_map<VertexId,VertexId>& mirrors,
    std::size_t maximumElementsPerPartition=256);

} // namespace mesher

#pragma once

#include "mesher/triangle_collision.hpp"

#include <array>
#include <cstddef>
#include <vector>

namespace mesher {

// V17e2b1 extracts one finite, transverse segment from the intersection
// of two triangles and triangulates EACH original triangle along that
// exact shared segment, still entirely in private local coordinates.
//
// This is deliberately NOT an executable topology change. V17e2b2 must
// reconcile shared edge subdivisions across adjacent surviving triangles,
// classify the resulting fragments and pair reflected sections BEFORE
// any repair is authorized. Unsupported contacts remain fail-closed.
enum class IntersectionSectionStatus {
    Disjoint,
    TransverseSegment,
    UnsupportedContact
};
struct IntersectionPatch {
    // 0,1,2 are the triangle's original vertices. 3,4 are the same
    // intersection endpoints in both patches, even if interior to a face.
    std::array<Vec3,5> positions{};
    std::array<std::array<double,3>,2> endpointBarycentrics{};
    std::vector<std::array<unsigned,3>> triangles;
    // The endpoint is interior when no barycentric coordinate vanishes.
    std::array<bool,2> interior{};
};
struct IntersectionSection {
    IntersectionSectionStatus status{IntersectionSectionStatus::Disjoint};
    std::array<Vec3,2> endpoints{};
    std::array<IntersectionPatch,2> patches{};
};
// Uses V13 contact semantics. No answer contains a partly truncated
// intersection: parallel, coplanar, grazing, near-vertex, ill-conditioned
// or quality-degenerate cases report UnsupportedContact, never Disjoint.
IntersectionSection splitTransverseTriangleContact(
    const std::array<Vec3,3>& a,const std::array<VertexId,3>& aIds,
    const std::array<Vec3,3>& b,const std::array<VertexId,3>& bIds);

// The caller must bound the discovered V13 pair set before calling this.
// Each pair is independent; the returned patches are NOT conformingly
// merged into a mesh. A refused member refuses the whole batch rather
// than returning a partial collection.
struct IntersectionSectionBatch {
    IntersectionSectionStatus status{IntersectionSectionStatus::Disjoint};
    std::vector<IntersectionSection> sections;
};
IntersectionSectionBatch splitTransverseContactBatch(
    const RegionTopologyIndex& index,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    std::size_t maxPairs=128);

} // namespace mesher

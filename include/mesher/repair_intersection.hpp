#pragma once

#include "mesher/triangle_collision.hpp"
#include "mesher/region_edit.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
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

 
// V17e2b2a: a read-only, bounded plan for conforming EVERY original
// incident face to the edge vertices created by V17e2b1. Plan indices
// are ephemeral and are NOT VertexIds or FaceIds in a MeshRevision.
// Two intersecting sheets retain DISTINCT cut vertices at their common
// geometrical coordinates until classified removal authorizes welding.
struct ConformingCutLimits {
    std::size_t maxContacts=128;
    std::size_t maxAffectedFaces=1024;
    std::size_t maxNewPoints=4096;
    std::size_t maxTriangles=8192;
    // The multiple-curve face arrangement has quadratic intersection work.
    std::size_t maxArrangementPairTests=16384;
    std::size_t maxArrangementEdges=16384;
};
struct ConformingCutPoint {
    Vec3 position{};
    std::optional<VertexId> existingVertex;
    std::optional<RegionTopologyIndex::Edge> boundaryEdge;
    FaceId owningFace{}; // meaningful for interior points only
};
struct ConformingCutFace {
    FaceId sourceFace{};
    std::vector<std::array<std::size_t,3>> triangles;
};
struct ConformingCutSegment {
    FaceId firstFace{},secondFace{};
    std::array<std::size_t,2> firstSide{},secondSide{};
};
struct ConformingCutPlan {
    IntersectionSectionStatus status{IntersectionSectionStatus::Disjoint};
    std::vector<ConformingCutPoint> points;
    std::vector<ConformingCutFace> faces;
    std::vector<ConformingCutSegment> segments;
    std::vector<std::pair<std::size_t,std::size_t>> mirroredCutPoints;
    std::string refusal;
};
// Each boundary edge cut is represented by ONE ephemeral point index in
// BOTH original incident faces; an independent intersecting sheet retains
// its own distinct point. Faces from outside the original collision pair
// are added to the plan if their shared edge needs a conforming split.
// Unsupported intersections, invalid overlapping/coincident curve segments,
// absent mirror companions and nonmanifold boundary edges refuse the whole plan.
// Does not perform a RegionEdit, winding classification, or root CAS.
ConformingCutPlan buildConformingCutPlan(
    const RegionTopologyIndex& index,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ConformingCutLimits& limits={},
    const std::unordered_map<VertexId,VertexId>* mirrors=nullptr);


// V17e2b2b1: materialize a fully validated conforming cut graph in ONE
// PRIVATE RegionEdit on closed orientable input meshes. This does NOT
// classify, excise, heal, validate residual collisions, or publish the
// result. Contact surfaces retain distinct cut vertex identities.
struct ConformingCutCandidate {
    RevisionCommit commit;
    std::shared_ptr<const RegionTopologyIndex> index;
    std::vector<VertexId> insertedVertices;
    std::vector<FaceId> insertedFaces;
    std::vector<FaceId> rewrittenFaces;
    std::vector<std::array<VertexId,2>> sectionFirst;
    std::vector<std::array<VertexId,2>> sectionSecond;
};
// Always REBUILDS the cut plan from the current immutable revision and
// exact caller-supplied V13 contact pairs; never trusts an arbitrary
// precomputed point/face graph. For now symmetric RegionEdit construction
// needs a separate paired-face staging protocol and is explicitly refused.
// An unsupported or ambiguous multiple-section arrangement still refuses
// without returning partial topology. Failure leaves old roots untouched.
ConformingCutCandidate materializeTransverseCutCandidate(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ConformingCutLimits& limits={},
    std::size_t partitionCapacity=256,
    const std::unordered_map<VertexId,VertexId>* mirrors=nullptr);

} // namespace mesher

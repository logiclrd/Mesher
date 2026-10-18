#pragma once

#include "mesher/triangle_collision.hpp"
#include "mesher/region_edit.hpp"
#include "mesher/repair_plan.hpp"
#include "mesher/repair_surgery.hpp"

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
    // Bounds scaffold visibility and ear-clipping predicates, separately
    // from the section-pair intersection budget.
    std::size_t maxArrangementPredicates=262144;
    // Bounded original-face contact-triple enumeration and source
    // plane/finite-section certification before any junction is staged.
    std::size_t maxJunctionCandidates=16384;
    // One complete reciprocal mirror-point/section matching pass.
    std::size_t maxMirroredPairTests=16384;
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
// One physical point jointly proven on THREE original source triangles
// and their THREE finite V13 section pairs. The three point indices remain
// topologically separate (different sheet IDs) until later c3/d2 authority.
// A plan never exposes a partly filled junction.
struct ConformingCutJunction {
    std::array<FaceId,3> sourceFaces{};
    Vec3 position{};
    std::array<std::size_t,3> facePoints{};
};
struct ConformingCutPlan {
    IntersectionSectionStatus status{IntersectionSectionStatus::Disjoint};
    std::vector<ConformingCutPoint> points;
    std::vector<ConformingCutFace> faces;
    std::vector<ConformingCutSegment> segments;
    std::vector<ConformingCutJunction> junctions;
    // Verified reflected point pairs (distinct sheets never coalesce).
    std::vector<std::pair<std::size_t,std::size_t>> mirroredCutPoints;
    // Source-proven ON-plane fixed-point cut orbits; one point/VertexId,
    // never two coincident reflected IDs.
    std::vector<std::size_t> mirroredSeamCutPoints;
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
    // Exact original-face provenance for EVERY child triangle, including
    // rewritten old FaceIds; never inferred from coordinate proximity.
    std::vector<std::pair<FaceId,FaceId>> childSources;
    std::shared_ptr<const RegionTopologyIndex> originalIndex;
    // V17e2b2b3b: complete NET inserted-vertex and rewritten/inserted
    // face mirror orbits, ready for a future original-root Step journal.
    // Empty for the nonmirrored path; no published map is mutated here.
    std::vector<std::pair<VertexId,VertexId>> mirroredVertices;
    std::vector<VertexId> mirroredSeamVertices;
    std::vector<std::pair<FaceId,FaceId>> mirroredFaces;
    // Exact caller-supplied original contact order, retained so an
    // independent certification pass can compare with ALL V13 local hits.
    std::vector<RegionTopologyIndex::CollisionPair> sourceContacts;
    std::vector<std::array<VertexId,2>> sectionFirst;
    std::vector<std::array<VertexId,2>> sectionSecond;
};
// Always REBUILDS the cut plan from the current immutable revision and
// exact caller-supplied V13 contact pairs; never trusts an arbitrary
// precomputed point/face graph. For an explicit mirror map, the V17e2b2b3b
// branch stages unique new vertex and child-face orbits in ONE private
// paired RegionEdit. New X=0 cuts and invariant source faces still refuse;
// the returned root is NOT a Boolean union or live accepted sculpt Step.
// An unsupported or ambiguous multiple-section arrangement still refuses
// without returning partial topology. Failure leaves old roots untouched.
ConformingCutCandidate materializeTransverseCutCandidate(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ConformingCutLimits& limits={},
    std::size_t partitionCapacity=256,
    const std::unordered_map<VertexId,VertexId>* mirrors=nullptr);


 // V17e2b2c: inspect the ACTUAL conforming child faces rather than a
 // sampled subdivision of their original unsplit parents. All original
 // source FaceIds are explicit and the entire test is read-only.
 // No finite set of interior witnesses proves a continuous Boolean winding
 // bound on its own: this is a conservative occupancy ASSESSMENT, not a
 // deletion permit. Uncertain/disagreeing children refuse the whole batch.
struct CutFragmentLimits {
    std::size_t maxFragments=512;
    std::size_t maxWindingQueries=8192;
    std::size_t maxWindingBoxTests=1048576;
    std::size_t maxWindingPartitionTests=131072;
    std::size_t maxWindingTriangleTests=262144;
    std::size_t maxWindingRays=24576;
};
struct CutFragmentOccupancy {
    FaceId child{};
    FaceId original{};
    RegionTopologyIndex::FragmentKind kind{
        RegionTopologyIndex::FragmentKind::Ambiguous};
    int negativeWinding{},positiveWinding{};
};
struct CutFragmentAssessment {
    bool complete{};
    std::string refusal;
    std::vector<CutFragmentOccupancy> fragments;
    std::size_t windingQueries{};
    RegionTopologyIndex::WindingStatistics windingWork{};
};
// Uses exact REAL child FaceIds, original per-child source provenance,
// multiple strictly interior witnesses and offsets on BOTH normal sides,
// positive signed winding (w>0, NOT parity or nonzero), and bounded
// accelerated ray queries with NO whole-mesh solid-angle fallback.
// No faces are removed or nominated for V15 deletion by this API.
CutFragmentAssessment assessConformingCutFragments(
    const ConformingCutCandidate& cut,const CutFragmentLimits& limits={});


// V17e2b2c2: bounded, read-only certification that every V13 contact
// involving a changed ORIGINAL face is declared, every declared transverse
// section is traced by child edges on BOTH sheets, and no child of either
// contact source crosses the other source's FINITE transverse contact
// interval through its open interior (the infinite plane alone is not enough).
// The resulting certificate covers the supported finite-tolerance model;
// no Boolean trimming, seam welding, history entry or deletion occurs.
struct CutCoverageLimits {
    CollisionQueryLimits originalCollisionLimits{
        1024,131072,16384,131072,65536,2048};
    std::size_t maxContacts=128;
    std::size_t maxChildren=8192;
    std::size_t maxChildPairTests=262144;
    std::size_t maxEdgeTraceTests=262144;
};
struct CutCoverageCertificate {
    bool complete{};
    std::string refusal;
    std::size_t checkedContacts{};
    std::size_t checkedChildren{};
    std::size_t childPairTests{};
    std::size_t edgeTraceTests{};
    RegionTopologyIndex::CollisionStatistics discoveryWork{};
    // Exact source pairs exhaustively rediscovered from the ORIGINAL root.
    // Empty upon ANY refusal; never a partial authorization to trim.
    std::vector<RegionTopologyIndex::CollisionPair> coveredContacts;
};
CutCoverageCertificate certifyConformingCutCoverage(
    const ConformingCutCandidate& cut,const CutCoverageLimits& limits={});


// V17e2b2c3: a PRIVATE, read-only authorization proposal. Evidence from
// V17e2b2c1 and c2 is RECOMPUTED against the SAME immutable candidate
// (never accepted as caller-supplied labels/certificates). No actual face
// retirement, stitching, mirror journal application or publication occurs.
enum class CutRemovalPolicy {
    PositiveWindingUnionPreserveCavities
};
enum class CutRemovalStatus {
    Refused, BudgetExceeded, NoChanges, Ready
};
struct CutRemovalSettings {
    CutCoverageLimits coverage{};
    CutFragmentLimits occupancy{};
    RepairPlanSettings contours{};
    std::size_t maxProposedRemoval=256;
    CutRemovalPolicy policy{
        CutRemovalPolicy::PositiveWindingUnionPreserveCavities};
};
struct CutRemovalProposal {
    CutRemovalStatus status{CutRemovalStatus::Refused};
    std::string refusal;
    std::shared_ptr<const RegionTopologyIndex> originalIndex;
    std::shared_ptr<const RegionTopologyIndex> candidateIndex;
    // Complete, immutable snapshot of the actual per-child decision,
    // populated ONLY on NoChanges/Ready. Protected means it CANNOT be
    // removed by this policy; reversed cavity boundaries stay protected.
    std::vector<CutFragmentOccupancy> classifiedChildren;
    std::vector<FaceId> proposedRemoval;
    std::vector<FaceId> protectedChildren;
    std::vector<std::pair<FaceId,FaceId>> mirroredRemoval;
    // V15 contour-index pair orbits, each output once (i<partner).
    // Self-paired seam contours remain unsupported in this first gate.
    std::vector<std::pair<std::size_t,std::size_t>> mirroredContours;
    // V15 checks contour shape and closure options, but does not perform
    // surgery; its low-level readiness is NOT a publication authorization.
    RepairPlan contourPlan;
    std::size_t buried{},outside{},exposed{},reversedBoundary{};
    std::size_t checkedContacts{},windingQueries{};
    bool ready() const noexcept {return status==CutRemovalStatus::Ready;}
};
// Treat any V13 coverage refusal, uncertain occupancy, incomplete child or
// mirror orbit, unsupported cavity configuration, or invalid V15 closure
// contour as an atomic refusal. No PARTIAL removal set escapes. Even Ready
// can only be consumed by a later V16 whole-candidate surgery validator.
CutRemovalProposal planExactCutRemoval(
    const ConformingCutCandidate& cut,const CutRemovalSettings& settings={});

// V17-M03: independently authorize whole ORIGINAL faces which were never
// cut, but form a complete, bounded connected no-contact patch adjoining
// an actual c2-certified original contact source. No caller-supplied V15
// Ready contour or precomputed winding labels can authorize these IDs.
// This READ-ONLY proposal does NOT modify any COW revision or publish.
struct UncutFaceRetirementLimits {
    std::size_t maxOriginalFaces=512;
    std::size_t maxAdjacencyTests=8192;
    std::size_t maxWindingQueries=8192;
    std::size_t maxWindingBoxTests=1048576;
    std::size_t maxWindingPartitionTests=131072;
    std::size_t maxWindingTriangleTests=262144;
    std::size_t maxWindingRays=24576;
    std::size_t maxCollisionBoxTests=131072;
    std::size_t maxCollisionPartitionTests=16384;
    std::size_t maxCollisionCandidateTriangles=131072;
    std::size_t maxCollisionNarrowPhaseTests=65536;
};
struct UncutFaceRetirementProposal {
    CutRemovalStatus status{CutRemovalStatus::Refused};
    std::string refusal;
    // Every ID in authorizedUncut must exist UNCHANGED in both the
    // original and private cut indices, with no original V13 contacts.
    std::vector<FaceId> authorizedUncut;
    std::vector<FaceId> protectedUncut;
    // Reciprocal explicitly witnessed, off-plane original FaceId orbits;
    // never populate on partial or refused retirement.
    std::vector<std::pair<FaceId,FaceId>> mirroredAuthorizedUncut;
    std::vector<std::pair<FaceId,FaceId>> mirroredProtectedUncut;
    // Complete, sorted union of c1/c2/c3-authorized CHILD FaceIds and
    // independently certified untouched original FaceIds.
    std::vector<FaceId> combinedRemoval;
    std::vector<FaceId> protectedChildren;
    RepairPlan contourPlan;
    std::size_t checkedOriginalFaces{},checkedOriginalContacts{};
    std::size_t windingQueries{};
    RegionTopologyIndex::WindingStatistics windingWork{};
    RegionTopologyIndex::CollisionStatistics discoveryWork{};
    bool ready() const noexcept {return status==CutRemovalStatus::Ready;}
};
UncutFaceRetirementProposal planUncutFaceRetirement(
    const ConformingCutCandidate& cut,
    const CutRemovalSettings& settings={},
    const UncutFaceRetirementLimits& limits={},
    const std::unordered_map<VertexId,VertexId>* originalMirrors=nullptr);



// V17e2b2d1: strictly private handoff to existing V16 geometry surgery.
// Even a V15 Ready nomination is NEVER considered a Boolean union until
// V16's own private closure/manifold/residual/fidelity audit, a second
// full modified-child collision check, and protected-child reverse
// surface fidelity have ALL accepted the SAME immutable revision chain.
// This function does not access the repository CAS, undo, renderer, or
// persistent mirror/history journals. Refusals return NO surgery root.
enum class ExactCutSurgeryStatus {
    Refused,
    NoChanges,
    PrivateValidated
};
struct ExactCutSurgerySettings {
    CutRemovalSettings removal{};
    RepairSurgerySettings surgery{};
    std::size_t maxFinalCollisionFaces=8192;
    RegionTopologyIndex::CollisionQueryLimits finalCollisionLimits{
        16384,131072,16384,131072,65536,4096};
    std::size_t maxProtectedWitnesses=4096;
    std::size_t maxProtectedDistanceTests=262144;
    double maxProtectedDeviationInEdges=1e-7;
};
struct ExactCutSurgeryAttempt {
    ExactCutSurgeryStatus status{ExactCutSurgeryStatus::Refused};
    std::string refusal;
    // Exists ONLY on PrivateValidated. The original immutable baseline
    // stays untouched even if a later validation gate rejects this root.
    std::optional<RepairSurgeryResult> surgery;
    std::size_t selectedFaces{},protectedFaces{};
    std::size_t checkedFinalContacts{},protectedWitnesses{};
    double maximumProtectedDeviationInEdges{};
};
// For mirrored cuts the caller MUST supply the immutable pre-cut vertex
// involution; the private candidate contributes every NEW mirrored vertex.
// Reflection maps are checked by stable IDs, never guessed by position
// for potentially coincident, independent contact sheets.
ExactCutSurgeryAttempt attemptPrivateExactCutSurgery(
    const ConformingCutCandidate& cut,
    const ExactCutSurgerySettings& settings={},
    const std::unordered_map<VertexId,VertexId>* originalMirrors=nullptr);


// V17e2b2d2: A proven contact-edge seam join is a different operation
// from V15 cap/zipper closure. All geometric section vertices are paired
// only by c2-certified original contact provenance, never nearest-neighbor
// global coincidence. It retires only c3-authorized child faces and
// redundant counterpart seam VertexIds in one UNPUBLISHED RegionEdit.
// No live publication or history mutation occurs, even on success.
struct ExactCutJoinLimits {
    CutRemovalSettings removal{};
    std::size_t maxBoundaryEdges=8192;
    std::size_t maxSeamPairs=8192;
    std::size_t maxTraceTests=262144; // bounded original-contact x boundary probes
    std::size_t maxRewrittenFaces=8192;
    std::size_t maxCandidateFaces=16384;
    CollisionQueryLimits residualCollisionLimits{
        16384,131072,16384,131072,65536,4096};
    std::size_t partitionCapacity=256;
};
struct ExactCutJoinResult {
    bool complete{};
    std::string refusal;
    std::shared_ptr<const RegionTopologyIndex> index;
    std::optional<RevisionCommit> commit;
    std::vector<FaceId> retiredFaces;
    std::vector<FaceId> retiredUncut,protectedUncut;
    std::vector<VertexId> retiredVertices;
    std::vector<FaceId> rewrittenFaces;
    std::vector<std::pair<VertexId,VertexId>> weldedPairs; // retired -> retained
    std::size_t checkedBoundaryEdges{},checkedContacts{};
};
// If mirrors are supplied, enforce exact reflected unions of complete
// stable-ID vertex orbits and paired child face retirement. The join
// still uses ONE RegionEdit for BOTH reflected halves (no self-plane seams
// yet); unsupported mirror configurations refuse the ENTIRE result.
ExactCutJoinResult joinCertifiedCutSections(
    const ConformingCutCandidate& cut,
    const ExactCutJoinLimits& limits={},
    const std::unordered_map<VertexId,VertexId>* originalMirrors=nullptr,
    bool includeAuthorizedUncutOriginals=false,
    const UncutFaceRetirementLimits& uncutLimits={});

// V17-M02: Reconcile DIFFERENT exact source-section edge subdivisions
// between two independently staged cut sheets. This is a private COW
// preflight, not welding, history, worker input or publication.
// Every split point is taken from a vertex on the OPPOSITE side of the
// SAME original V13 contact section. All incident source-mesh faces are
// split together, and c1/c2/c3 are revalidated on the refined root.
// Mirrored NEW splits remain unsupported until V17-M05.
struct ExactSeamRefinementLimits {
    ExactCutJoinLimits join{};
    std::size_t maxCandidateTests=262144;
    std::size_t maxNewVertices=2048;
    std::size_t maxNewFaces=4096;
};
struct ExactSeamRefinementResult {
    bool complete{};
    std::string refusal;
    std::optional<ConformingCutCandidate> refined;
    std::size_t insertedVertices{},insertedFaces{},matchedIntervals{};
};
ExactSeamRefinementResult reconcileCertifiedSeamSubdivisions(
    const ConformingCutCandidate& cut,
    const ExactSeamRefinementLimits& limits={},
    bool includeAuthorizedUncutOriginals=false,
    const UncutFaceRetirementLimits& uncutLimits={});


// V17e2b2d3: Stage the entire subdivision + certified seam join as
// ONE original-root Step. Two private RegionEdits are implementation
// details: the returned FINAL immutable root is the ONLY publishable
// candidate. Nothing in this API publishes or appends to history.
struct ExactCutStepLimits {
    ConformingCutLimits cut{};
    ExactCutJoinLimits join{};
    ExactSeamRefinementLimits refinement{};
    UncutFaceRetirementLimits uncut{};
    std::size_t partitionCapacity=256;
    bool requireMirrors=false; // explicit symmetric Step intent; no fallback
    std::size_t maxNetChangedIds=32768;
    std::size_t maxDirtyPartitions=4096;
    std::size_t maxFinalCollisionFaces=16384;
    CollisionQueryLimits finalCollisionLimits{
        16384,131072,16384,131072,65536,4096};
};
struct ExactCutStep {
    bool complete{};
    std::string refusal;
    // The caller MUST CAS from expected to final, NEVER from the cut
    // intermediate. Both pointers share the original identity domain.
    std::shared_ptr<const RegionTopologyIndex> expected;
    std::shared_ptr<const RegionTopologyIndex> final;
    RevisionCommit commit;
    // NET stable-ID deltas relative to expected. IDs born AND retired
    // inside the two private edits are excluded from these journals.
    std::vector<VertexId> addedVertices,retiredVertices;
    std::vector<FaceId> addedFaces,rewrittenFaces,retiredFaces;
    std::vector<std::pair<VertexId,VertexId>> addedMirrors,removedMirrors;
    std::vector<std::pair<FaceId,FaceId>> survivingMirroredFaces;
    std::vector<PartitionId> dirtyPartitions;
    std::size_t copiedPayloadBytes{}; // cost of ALL private COW edits
    std::size_t finalCollisionSeeds{},checkedProtectedFaces{};
    // Relative counts; callers append one History entry using their
    // existing absolute counts. Does not include born-then-retired IDs.
    std::ptrdiff_t vertexCountDelta{},faceCountDelta{};
};
// A mirrored cut requires the original stable-ID involution. Any
// unsupported geometry, mismatch, stale provenance or bounded-work
// exhaustion returns NO final/commit, journals or publication permission.
ExactCutStep prepareExactCutStep(
    std::shared_ptr<const RegionTopologyIndex> original,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ExactCutStepLimits& limits={},
    const std::unordered_map<VertexId,VertexId>* originalMirrors=nullptr);

} // namespace mesher

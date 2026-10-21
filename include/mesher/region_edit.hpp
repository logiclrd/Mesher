#pragma once

#include "mesher/versioned_mesh.hpp"
#include "mesher/persistent_index_map.hpp"
#include "mesher/picking.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mesher {

// Persistent immutable lookup bound to exactly one mesh revision.
// Initial import builds it once; subsequent RegionEdit commits update only
// touched records and share all unchanged spatial/connectivity index nodes.
// Publishing a new index root never scans the mesh.
// Bounded per-query post-state collision discovery. Declared outside the
// index class so all C++20 compilers can bind default-initialized limits.
struct CollisionQueryLimits {
    std::size_t maxSeedFaces=std::numeric_limits<std::size_t>::max();
    std::size_t maxBoxTests=std::numeric_limits<std::size_t>::max();
    std::size_t maxPartitionTests=std::numeric_limits<std::size_t>::max();
    std::size_t maxCandidateTriangles=std::numeric_limits<std::size_t>::max();
    std::size_t maxNarrowPhaseTests=std::numeric_limits<std::size_t>::max();
    std::size_t maxReportedContacts=std::numeric_limits<std::size_t>::max();
};
// Bounded BVH work for EACH positive-winding query. The caller can pass
// remaining aggregate allowances across multiple fragment samples.
struct WindingQueryLimits {
    std::size_t maxBoxTests=std::numeric_limits<std::size_t>::max();
    std::size_t maxPartitionTests=std::numeric_limits<std::size_t>::max();
    std::size_t maxTriangleTests=std::numeric_limits<std::size_t>::max();
    std::size_t maxRays=std::numeric_limits<std::size_t>::max();
};
class RevisionPickingIndex;
class RegionTopologyIndex {
public:
    struct Vertex {
        PartitionId owner{};
        Vec3 position{};
        std::vector<FaceId> incidentFaces;
    };
    struct Face {
        PartitionId owner{};
        std::array<VertexId,3> vertices{};
    };
    struct Edge {
        VertexId a{},b{};
        auto operator<=>(const Edge&) const = default;
    };
    struct EdgeHash {
        std::size_t operator()(Edge edge) const noexcept;
    };
    struct EdgeSide {
        FaceId face{};
        bool forward{};
    };
    struct Cell {
        std::int64_t x{},y{},z{};
        auto operator<=>(const Cell&) const = default;
    };
    struct CellHash {
        std::size_t operator()(Cell key) const noexcept;
    };
    struct UpdateStatistics {
        std::size_t changedVertexRecords{};
        std::size_t changedFaceRecords{};
        std::size_t changedEdgeRecords{};
        std::size_t changedCellRecords{};
        std::size_t copiedTreeNodes{};
        std::size_t fullMeshTraversals{};
        std::size_t rebuiltPickingPartitions{};
        std::size_t copiedPickingDirectoryNodes{};
    };

    // Full index construction happens only once for an imported mesh.
    static std::shared_ptr<const RegionTopologyIndex> build(
        std::shared_ptr<const MeshRevision> revision,double cellSize=.15);
    static std::shared_ptr<const RegionTopologyIndex> restore(
        std::shared_ptr<const MeshRevision> revision,
        const std::vector<VertexId>& retiredVertices,
        const std::vector<FaceId>& retiredFaces,double cellSize=.15);
    std::vector<VertexId> retiredVertexIds() const;
    std::vector<FaceId> retiredFaceIds() const;

    ~RegionTopologyIndex();
    static std::size_t liveRevisionCount() noexcept;
    static std::size_t peakLiveRevisionCount() noexcept;
    const MeshRevision& revision() const noexcept {return *revision_;}
    const Vertex& vertex(VertexId id) const {return vertices_.at(id);}
    const Face& face(FaceId id) const {return faces_.at(id);}
    // Read-only presence query for COW revisions and net surgery deltas.
    std::shared_ptr<const Face> maybeFace(FaceId id) const {
        return faces_.find(id);
    }
    std::shared_ptr<const Vertex> maybeVertex(VertexId id) const {
        return vertices_.find(id);
    }
    bool retiredVertex(VertexId id) const {return retiredVertices_.find(id)!=nullptr;}
    bool retiredFace(FaceId id) const {return retiredFaces_.find(id)!=nullptr;}
    std::size_t retiredVertexCount() const noexcept {return retiredVertices_.size();}
    std::size_t retiredFaceCount() const noexcept {return retiredFaces_.size();}
    std::shared_ptr<const std::vector<EdgeSide>> maybeEdgeSides(Edge key) const {
        return edges_.find(key);
    }
    const std::vector<EdgeSide>& edgeSides(Edge key) const {
        return edges_.at(key);
    }
    const UpdateStatistics& updateStatistics() const noexcept {return statistics_;}
    // Exact squared-distance filtering after spatial bucket lookup. This is
    // scoped to this immutable revision, even after newer revisions publish.
    std::vector<VertexId> querySphere(Vec3 center,double radius) const;
    struct PickStatistics {
        std::size_t boxesTested{};
        std::size_t partitionsTested{};
        std::size_t trianglesTested{};
    };
    // Exact ray picking against this revision's path-shared triangle BVHs.
    struct FaceRayHit {Vec3 point{}; FaceId face{};};
    // Same immutable revision/BVH used for regular picks; carries the stable
    // FaceId, NEVER a nearest-coordinate inference from later topology.
    std::optional<FaceRayHit> firstRayFaceHit(
        Ray ray,PickStatistics* statistics=nullptr) const;
    std::optional<Vec3> firstRayHit(Ray ray,PickStatistics* statistics=nullptr) const;
    const void* pickingRootIdentity() const noexcept;

    struct CollisionPair {
        FaceId first{},second{}; // Canonical ascending stable FaceIds
        // True for ordinary intersections and contacts; ambiguous pairs are
        // reported separately rather than certified as collision-free.
        bool ambiguous{};
        auto operator<=>(const CollisionPair&) const = default;
    };
    struct CollisionStatistics {
        std::size_t seedFaces{},boxesTested{},partitionsTested{};
        std::size_t candidateTriangles{},narrowPhaseTests{},reportedContacts{};
    };
    // Work limits apply to ONE complete post-state collision query. Defaults
    // preserve historical unbounded CPU callers; V17 live callers must supply
    // explicit finite limits. Zero forbids that kind of work. The query throws
    // length_error on exhaustion, NEVER returns a truncated collision list.
    using CollisionQueryLimits=mesher::CollisionQueryLimits;
    // All candidates and narrow-phase tests refer to THIS complete immutable
    // post-deformation revision. changedVertices and changedFaces are a local
    // dirty set, not a comparison against geometry from the previous revision.
    // Retired face IDs may be supplied in changedFaces and are ignored.
    std::vector<CollisionPair> triangleIntersections(
        const std::vector<VertexId>& changedVertices,
        const std::vector<FaceId>& changedFaces,
        CollisionStatistics* stats=nullptr,
        const CollisionQueryLimits& limits={}) const;

    // V18-M03: read-only pair broad phase over the same persistent
    // partition BVH. Each seed AABB expands by ratio * ITS OWN shortest
    // edge. Actual clearance uses min(seed,target) local edge scales;
    // expanding by the seed therefore cannot omit a pair requiring
    // mesh-scale clearance. Returned pairs are stable FaceIds, not
    // geometric equivalences, and are limited by maxReportedContacts.
    // Limits meter all directory/partition traversals and pair visits.
    std::vector<std::pair<FaceId,FaceId>> nearbyTrianglePairs(
        const std::vector<VertexId>& changedVertices,
        const std::vector<FaceId>& changedFaces,
        double localEdgeRatio,
        CollisionStatistics* stats=nullptr,
        const CollisionQueryLimits& limits={}) const;

    struct WindingStatistics {
        std::size_t boxesTested{}, partitionsTested{}, trianglesTested{};
        std::size_t raysTested{}, referenceFallbacks{};
        std::size_t referenceFallbackRefusals{};
    };
    struct WindingSample {
        int winding{};
        bool reliable{};
    };
    // Signed crossing count of a ray from this point to infinity. At least
    // two independent rays must agree. Grazing/on-surface input is uncertain;
    // the exceptional solid-angle fallback is deliberately a full traversal.
    // A caller preparing a live Step can disable that O(all faces) fallback:
    // exhausted ray evidence then reports unreliable, never false certainty.
    WindingSample positiveWindingAt(Vec3 point,
        WindingStatistics* statistics=nullptr,
        bool allowReferenceFallback=true,
        const WindingQueryLimits& limits={}) const;
    // Independent O(all faces) signed-solid-angle reference, for testing and
    // the exceptional case when every chosen ray passes near a mesh feature.
    long double referenceWindingAt(Vec3 point) const;

    enum class FragmentKind {
        Exposed,         // positive-winding occupancy changes across patch
        Buried,          // both sides have strictly positive winding
        Outside,         // both sides are nonpositive
        ReversedBoundary,// occupancy changes in the opposite orientation
        Ambiguous
    };
    struct SurfaceFragment {
        FaceId face{};
        // Barycentric coordinates of the patch's three corners, in original
        // face vertex order. These are *sampled*, not exact cut fragments.
        std::array<std::array<double,3>,3> corners{};
        FragmentKind kind{FragmentKind::Ambiguous};
        int negativeWinding{},positiveWinding{};
    };
    // Read-only sampled subtriangle classification. Requested depth splits a
    // triangle 4^depth ways to reveal mixed inside/outside portions. No patch
    // is certified as an exact intersection-trimmed polygon: V15 owns cuts.
    // Bounded depth prevents unbounded processing of pathological geometry.
    std::vector<SurfaceFragment> classifyFaceFragments(
        FaceId face,unsigned depth=2,
        bool allowReferenceFallback=true) const;

    enum class ComponentKind {Redundant,Boundary,PotentialCavity,Ambiguous};
    struct ComponentClassification {
        ComponentKind kind{ComponentKind::Ambiguous};
        std::size_t facesVisited{};
        bool complete{};
    };
    // Inspect one topologically connected CLOSED face component, limited to
    // maxFaces; classification is conservative if components self-intersect,
    // contain near-coincident surfaces, or cannot be fully enumerated.
    // An oriented enclosed cavity boundary is policy-dependent, never erased.
    ComponentClassification classifyClosedComponent(
        FaceId seed,std::size_t maxFaces=10000,
        bool allowReferenceFallback=true,
        const CollisionQueryLimits& collisionLimits={}) const;

private:
    friend class RegionEdit;
    explicit RegionTopologyIndex(std::shared_ptr<const MeshRevision> revision,
                                 double cellSize);
    static std::shared_ptr<const RegionTopologyIndex> advance(
        const RegionTopologyIndex& previous,
        std::shared_ptr<const MeshRevision> next,
        const std::unordered_map<VertexId,std::optional<Vec3>>& vertices,
        const std::unordered_map<FaceId,
            std::optional<std::array<VertexId,3>>>& faces,
        const std::unordered_map<VertexId,PartitionId>& addedVertices,
        const std::unordered_map<FaceId,PartitionId>& addedFaces);

    static Cell cellFor(Vec3 point,double cellSize);
    std::shared_ptr<const MeshRevision> revision_;
    double cellSize_;
    PersistentIndexMap<VertexId,Vertex> vertices_;
    PersistentIndexMap<FaceId,Face> faces_;
    PersistentIndexMap<Edge,std::vector<EdgeSide>> edges_;
    PersistentIndexMap<Cell,std::vector<VertexId>> cells_;
    // Immutable tombstones survive local ID erasure; IDs never recycle.
    PersistentIndexMap<VertexId,bool> retiredVertices_;
    PersistentIndexMap<FaceId,bool> retiredFaces_;
    UpdateStatistics statistics_{};
    std::shared_ptr<const RevisionPickingIndex> picking_;
};

class IndexedMeshRepository {
public:
    explicit IndexedMeshRepository(std::shared_ptr<const RegionTopologyIndex> initial);
    std::shared_ptr<const RegionTopologyIndex> snapshot() const noexcept;
    bool publishIfCurrent(const std::shared_ptr<const RegionTopologyIndex>& expected,
                          std::shared_ptr<const RegionTopologyIndex> next);
private:
    // One atomic pointer owns both the immutable revision and its index.
    std::atomic<std::shared_ptr<const RegionTopologyIndex>> published_;
};

// A seed vertex's write halo comprises its owner plus owners of incident
// faces. The read halo includes every vertex of those faces and faces sharing
// their edges; it is immutable, and is not cloned for reading.
struct RegionFootprint {
    std::vector<VertexId> editableVertices;
    std::vector<FaceId> editableFaces;
    std::vector<PartitionId> readPartitions;
    std::vector<PartitionId> writePartitions;
};

// Topological operations must be applied to an entire region transaction,
// never published a face at a time. No raw writable partition reference escapes.
class RegionEdit {
public:
    // Mirror pairs are an involution. Self-pairs represent vertices on X=0.
    // When supplied, every seed must have a mirror, and a move updates both
    // partners (or constrains a seam vertex) in the same transaction.
    RegionEdit(std::shared_ptr<const RegionTopologyIndex> index,
               std::vector<VertexId> seedVertices,
               std::vector<std::pair<VertexId,VertexId>> mirrors = {},
               std::size_t maximumElementsPerPartition = 256);

    RegionEdit(const RegionEdit&) = delete;
    RegionEdit& operator=(const RegionEdit&) = delete;

    const RegionFootprint& footprint() const noexcept { return footprint_; }

    void moveVertex(VertexId id, Vec3 position);
    VertexId addVertex(PartitionId owner, Vec3 position);
    FaceId addFace(PartitionId owner, std::array<VertexId,3> vertices);
    // V6: bounded local insertion, preferring a writable existing partition;
    // if full, reserve a branch-unique empty overflow partition (no scans).
    VertexId appendLocalVertex(PartitionId preferred, Vec3 position);
    FaceId appendLocalFace(PartitionId preferred, std::array<VertexId,3> vertices);
    void replaceFace(FaceId id, std::array<VertexId,3> vertices);
    void removeFace(FaceId id);
    void removeVertex(VertexId id);

    // Explicit paired face operation; the second winding is the reflection
    // (reversed orientation) of the first. Requires mirror mappings for all
    // vertices referenced by both existing and replacement faces.
    void replaceMirroredFaces(FaceId first,FaceId second,
                              std::array<VertexId,3> replacement);

    // Symmetric topology primitives. Distinct off-plane vertices/faces are
    // created/deleted in reflected pairs. A vertex on the common X=0 seam
    // is represented by ONE self-mirrored ID, not duplicate coincident IDs.
    std::pair<VertexId,VertexId> appendMirroredVertices(
        PartitionId firstOwner,PartitionId secondOwner,Vec3 firstPosition);
    VertexId appendSeamVertex(PartitionId owner,Vec3 onPlane);
    std::pair<FaceId,FaceId> appendMirroredFaces(
        PartitionId firstOwner,PartitionId secondOwner,
        std::array<VertexId,3> firstTriangle);
    // A face whose oriented vertex cycle maps to ITSELF under X
    // reflection cannot be edited as two pre-existing face IDs.
    // If its directed edge (from,to) joins a reflected vertex pair
    // and its third vertex lies on X=0, split that edge at an already
    // allocated self-mirrored seam midpoint, retaining the original
    // FaceId for the first triangle and adding exactly ONE reflected
    // partner face. This does not publish or independently repair an
    // entire edge; all incident faces must be split in one transaction.
    std::pair<FaceId,FaceId> splitSelfMirroredFace(
        FaceId face,VertexId from,VertexId to,VertexId midpoint);
    // General bounded invariant-source fan; prove all child triangles
    // occur in DISTINCT, reverse-winding reflected pairs. Keep one
    // original FaceId and allocate only the other child FaceIds.
    std::vector<std::pair<FaceId,FaceId>> replaceSelfMirroredFace(
        FaceId source,const std::vector<std::array<VertexId,3>>& children);
    void removeMirroredFaces(FaceId first,FaceId second);
    void removeMirroredVertices(VertexId first,VertexId second);
    VertexId mirroredVertex(VertexId vertex) const;

    // Validates all changed edges/vertex fans using the snapshot index plus
    // local deltas, then commits only the pre-cloned writable partitions.
    // A failure leaves the base revision and any repository unchanged.
    RevisionCommit commit();
    // Available after a successful commit; retains exact revision pairing.
    std::shared_ptr<const RegionTopologyIndex> updatedIndex() const noexcept {
        return updatedIndex_;
    }

private:
    void setPosition(VertexId id,Vec3 position);
    void setFace(FaceId id,std::optional<std::array<VertexId,3>> vertices);
    void ensureWritable(PartitionId id) const;
    PartitionId chooseInsertionPartition(PartitionId preferred,bool vertex);
    void ensureReferences(const std::array<VertexId,3>& ids) const;
    void validate() const;

    std::shared_ptr<const RegionTopologyIndex> index_;
    std::shared_ptr<const RegionTopologyIndex> updatedIndex_;
    MutableMeshView view_;
    RegionFootprint footprint_;
    std::unordered_set<VertexId> selected_;
    std::unordered_set<FaceId> faces_;
    std::unordered_set<PartitionId> writers_, readers_;
    std::unordered_set<VertexId> accessibleVertices_;
    std::unordered_map<VertexId,VertexId> mirror_;
    std::unordered_map<FaceId,FaceId> faceMirror_;
    bool poisoned_{};
    std::unordered_map<VertexId,std::optional<Vec3>> vertexChanges_;
    std::unordered_map<FaceId,std::optional<std::array<VertexId,3>>> faceChanges_;
    std::unordered_map<VertexId,PartitionId> newVertices_;
    std::unordered_map<FaceId,PartitionId> newFaces_;
    std::size_t maximumElementsPerPartition_{};
    bool committed_{};
};

} // namespace mesher

#pragma once

#include "mesher/versioned_mesh.hpp"
#include "mesher/persistent_index_map.hpp"
#include "mesher/picking.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
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
    };

    // Full index construction happens only once for an imported mesh.
    static std::shared_ptr<const RegionTopologyIndex> build(
        std::shared_ptr<const MeshRevision> revision,double cellSize=.15);

    const MeshRevision& revision() const noexcept {return *revision_;}
    const Vertex& vertex(VertexId id) const {return vertices_.at(id);}
    const Face& face(FaceId id) const {return faces_.at(id);}
    std::shared_ptr<const Vertex> maybeVertex(VertexId id) const {
        return vertices_.find(id);
    }
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
    // Exact immutable-revision picking; accelerated triangle BVH is V10.
    std::optional<Vec3> firstRayHit(Ray ray) const;

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
    UpdateStatistics statistics_{};
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
    std::unordered_map<VertexId,std::optional<Vec3>> vertexChanges_;
    std::unordered_map<FaceId,std::optional<std::array<VertexId,3>>> faceChanges_;
    std::unordered_map<VertexId,PartitionId> newVertices_;
    std::unordered_map<FaceId,PartitionId> newFaces_;
    std::size_t maximumElementsPerPartition_{};
    bool committed_{};
};

} // namespace mesher

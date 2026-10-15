#pragma once

#include "mesher/versioned_mesh.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mesher {

// An immutable, explicitly constructed lookup of a *particular* revision.
// V3 builds this with one O(N) scan; V4 will replace that full build with
// revision-aware incremental spatial and connectivity indexes. Neither
// transaction commit nor revision publication traverses this whole index.
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
        VertexId a{}, b{};
        bool operator==(const Edge&) const = default;
    };
    struct EdgeHash {
        std::size_t operator()(Edge edge) const noexcept;
    };
    struct EdgeSide {
        FaceId face{};
        bool forward{};
    };

    static std::shared_ptr<const RegionTopologyIndex> build(
        std::shared_ptr<const MeshRevision> revision);

    const MeshRevision& revision() const noexcept { return *revision_; }
    const Vertex& vertex(VertexId id) const;
    const Face& face(FaceId id) const;

private:
    friend class RegionEdit;
    explicit RegionTopologyIndex(std::shared_ptr<const MeshRevision> revision);
    std::shared_ptr<const MeshRevision> revision_;
    std::unordered_map<VertexId,Vertex> vertices_;
    std::unordered_map<FaceId,Face> faces_;
    std::unordered_map<Edge,std::vector<EdgeSide>,EdgeHash> edges_;
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
               std::vector<std::pair<VertexId,VertexId>> mirrors = {});

    RegionEdit(const RegionEdit&) = delete;
    RegionEdit& operator=(const RegionEdit&) = delete;

    const RegionFootprint& footprint() const noexcept { return footprint_; }

    void moveVertex(VertexId id, Vec3 position);
    VertexId addVertex(PartitionId owner, Vec3 position);
    FaceId addFace(PartitionId owner, std::array<VertexId,3> vertices);
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

private:
    void setPosition(VertexId id,Vec3 position);
    void setFace(FaceId id,std::optional<std::array<VertexId,3>> vertices);
    void ensureWritable(PartitionId id) const;
    void ensureReferences(const std::array<VertexId,3>& ids) const;
    void validate() const;

    std::shared_ptr<const RegionTopologyIndex> index_;
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
    bool committed_{};
};

} // namespace mesher

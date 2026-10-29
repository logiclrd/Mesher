#pragma once

#include "mesher/mesh.hpp"
#include "mesher/portable_concurrency.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace mesher {

// IDs are monotonically allocated within a mesh's shared identity domain.
// Zero is reserved; deleted/aborted IDs are never recycled. IDs do NOT
// encode an address, partition, mutable array offset, or flat-mesh index.
using PartitionId = std::uint32_t;
using VertexId = std::uint64_t;
using FaceId = std::uint64_t;

struct PartitionVertex {
    VertexId id{};
    Vec3 position{};
    VertexRGBA color{};
};
struct PartitionFace {
    FaceId id{};
    std::array<VertexId,3> vertices{};
};
struct MeshPartition {
    std::vector<PartitionVertex> vertices;
    std::vector<PartitionFace> faces;
};
struct MeshIdentityWatermarks {
    std::uint64_t nextVertex{1},nextFace{1},nextOverflow{1};
};

class MutableMeshView;
class MeshIdentityDomain;
struct MeshDirectoryNode;

// A revision owns an immutable root into a persistent 16-way radix directory.
// Unchanged partitions (and directory branches) retain identical object identity.
// A separate, shared atomic ID domain is not geometry and is never copied.
class MeshRevision {
public:
    static std::shared_ptr<const MeshRevision> empty();
    // Full checkpoint import only: restores stable IDs and allocator high
    // waters, including IDs consumed in abandoned/rolled-back branches.
    static std::shared_ptr<const MeshRevision> restore(
        std::vector<std::pair<PartitionId,MeshPartition>> partitions,
        MeshIdentityWatermarks watermarks);
    MeshIdentityWatermarks identityWatermarks() const noexcept;
    std::shared_ptr<const MeshPartition> partition(PartitionId id) const;
    std::size_t partitionCount() const noexcept { return partitionCount_; }
    MutableMeshView createMutableView() const;
    // Fresh, branch-unique overflow partition ID for bounded local growth.
    // Imported partitions occupy the low half of the 32-bit ID space.
    PartitionId allocateOverflowPartition() const;
    // Explicit O(N) traversal: for import/export/validation only, not commit.
    void forEachPartition(const std::function<void(PartitionId,const MeshPartition&)>& visit) const;

private:
    friend class MutableMeshView;
    MeshRevision(std::shared_ptr<const MeshDirectoryNode> root,
                 std::size_t count, std::shared_ptr<MeshIdentityDomain> identities);
    std::shared_ptr<const MeshDirectoryNode> root_;
    std::size_t partitionCount_{};
    std::shared_ptr<MeshIdentityDomain> identities_;
};

struct RevisionCommitStatistics {
    std::size_t modifiedPartitions{};
    std::size_t clonedPartitions{};
    std::size_t clonedPartitionBytes{}; // actual copied vertex+face payloads
    std::size_t copiedDirectoryNodes{};
};
struct RevisionCommit {
    std::shared_ptr<const MeshRevision> revision;
    RevisionCommitStatistics statistics;
};

// Single-writer, copy-on-write transaction. Mutable references obtained from
// editPartition() must NOT outlive the transaction or escape to other threads.
// After commit(), this view cannot be edited or committed again.
class MutableMeshView {
public:
    MutableMeshView(const MutableMeshView&) = delete;
    MutableMeshView& operator=(const MutableMeshView&) = delete;
    MutableMeshView(MutableMeshView&&) = default;
    MutableMeshView& operator=(MutableMeshView&&) = default;

    MeshPartition& editPartition(PartitionId id);
    void erasePartition(PartitionId id);
    // Shared atomic source ensures unique IDs across branches/aborted views.
    // Low-level direct writes are not topology-validated until V3.
    VertexId addVertex(PartitionId owner, Vec3 position,
                       VertexRGBA color={});
    void setVertexColor(PartitionId owner,VertexId vertex,VertexRGBA color);
    FaceId addFace(PartitionId owner, std::array<VertexId,3> vertices);
    // Copies only modified directory paths and publishes no data itself.
    // The caller may atomically publish the returned immutable revision.
    RevisionCommit commit();

private:
    friend class MeshRevision;
    explicit MutableMeshView(const MeshRevision& base);
    struct Change {
        std::shared_ptr<MeshPartition> partition;
        bool removed{};
    };
    std::shared_ptr<const MeshDirectoryNode> root_;
    std::size_t partitionCount_{};
    std::shared_ptr<MeshIdentityDomain> identities_;
    std::map<PartitionId,Change> changes_;
    std::size_t clonedPartitions_{};
    std::size_t clonedPartitionBytes_{};
    bool committed_{};
};

// Readers retain their revisions independently. Only the root shared_ptr
// changes atomically; painting never locks the renderer's geometry reads.
class MeshRepository {
public:
    explicit MeshRepository(std::shared_ptr<const MeshRevision> initial);
    std::shared_ptr<const MeshRevision> snapshot() const noexcept;
    void publish(std::shared_ptr<const MeshRevision> next);
    // Optimistic, single-root compare-and-swap: a transaction based on an
    // outdated revision must not overwrite another publisher's newer work.
    bool publishIfCurrent(const std::shared_ptr<const MeshRevision>& expected,
                          std::shared_ptr<const MeshRevision> next);

private:
    AtomicSharedPtr<const MeshRevision> published_;
};

} // namespace mesher

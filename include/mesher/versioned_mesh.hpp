#pragma once

#include "mesher/mesh.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace mesher {

// Stable element identifiers are independent of their storage partition.
// Allocation, validation, and cross-partition connectivity are later milestones.
using PartitionId = std::uint32_t;
using VertexId = std::uint64_t;
using FaceId = std::uint64_t;

struct PartitionVertex {
    VertexId id{};
    Vec3 position{};
};
struct PartitionFace {
    FaceId id{};
    std::array<VertexId,3> vertices{};
};
struct MeshPartition {
    std::vector<PartitionVertex> vertices;
    std::vector<PartitionFace> faces;
};

class MutableMeshView;
struct MeshDirectoryNode;

// A revision owns an immutable root into a persistent 16-way radix directory.
// Unchanged partitions (and directory branches) retain identical object identity.
class MeshRevision {
public:
    static std::shared_ptr<const MeshRevision> empty();
    std::shared_ptr<const MeshPartition> partition(PartitionId id) const;
    std::size_t partitionCount() const noexcept { return partitionCount_; }
    MutableMeshView createMutableView() const;

private:
    friend class MutableMeshView;
    MeshRevision(std::shared_ptr<const MeshDirectoryNode> root,
                 std::size_t count);
    std::shared_ptr<const MeshDirectoryNode> root_;
    std::size_t partitionCount_{};
};

struct RevisionCommitStatistics {
    std::size_t modifiedPartitions{};
    std::size_t clonedPartitions{};
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
    std::map<PartitionId,Change> changes_;
    std::size_t clonedPartitions_{};
    bool committed_{};
};

// Readers retain their revisions independently. Only the root shared_ptr
// changes atomically; painting never locks the renderer's geometry reads.
class MeshRepository {
public:
    explicit MeshRepository(std::shared_ptr<const MeshRevision> initial);
    std::shared_ptr<const MeshRevision> snapshot() const noexcept;
    void publish(std::shared_ptr<const MeshRevision> next);

private:
    std::atomic<std::shared_ptr<const MeshRevision>> published_;
};

} // namespace mesher

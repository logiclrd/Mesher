#pragma once

#include "mesher/region_edit.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>
#include <utility>

namespace mesher {

// Vulkan vertex format retains the original seven floats and adds RGBA.
struct PartitionRenderVertex {
    float px{},py{},pz{},nx{},ny{},nz{},material{};
    float r=1.f,g=1.f,b=1.f,a=1.f;
};
struct PartitionRenderGeometry {
    std::vector<PartitionRenderVertex> solid;
    std::vector<PartitionRenderVertex> wire;
};
struct PartitionRenderUpdate {
    std::size_t rebuiltPartitions{};
    std::size_t rebuiltFaces{};
    std::size_t uploadedBytes{};
};

// Discover face-owner partitions whose geometry, normals or uniquely owned
// wires can change. Operates on affected vertices/faces and their one-ring
// before AND after the edit, not on an entire revision/partition directory.
std::vector<PartitionId> dirtyRenderPartitions(
    const RegionTopologyIndex& before,const RegionTopologyIndex& after,
    std::span<const VertexId> changedVertices,
    std::span<const FaceId> changedFaces);

// Each face is represented in its own owner partition. An edge is emitted
// by the incident face with the smallest stable FaceId, independent of
// physical vertex and face partition ownership. This prevents missing or
// doubled wires across partition boundaries.
PartitionRenderGeometry buildPartitionRenderGeometry(
    const RegionTopologyIndex& index,PartitionId owner,Vec3 renderOrigin={});

// UI-owned CPU staging/cache, not part of worker publication. It preserves
// identical geometry pointers for untouched partitions and rebuilds only
// named dirty partition IDs; it never enumerates the mesh on an update.
class RevisionRetirementQueue;
class PartitionRenderCache {
public:
    void initialize(std::shared_ptr<const RegionTopologyIndex> initial,
                    Vec3 renderOrigin={});
    // Only during a complete independent-file rebind (all owners dirty).
    void setRenderOrigin(Vec3 value) noexcept {renderOrigin_=value;}
    PartitionRenderUpdate advance(
        std::shared_ptr<const RegionTopologyIndex> revision,
        std::span<const PartitionId> dirty,
        RevisionRetirementQueue* retirement=nullptr);
    const std::map<PartitionId,std::shared_ptr<const PartitionRenderGeometry>>&
    partitions() const noexcept {return partitions_;}
    std::shared_ptr<const RegionTopologyIndex> revision() const noexcept {
        return revision_;
    }
    // Shutdown path: hand the last retained cache root to the reaper.
    std::shared_ptr<const RegionTopologyIndex> detachRevision() noexcept {
        return std::move(revision_);
    }
private:
    std::map<PartitionId,std::shared_ptr<const PartitionRenderGeometry>> partitions_;
    std::shared_ptr<const RegionTopologyIndex> revision_;
    Vec3 renderOrigin_{};
};

} // namespace mesher

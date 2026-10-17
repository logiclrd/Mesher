#pragma once

#include "mesher/region_edit.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <optional>
#include <vector>

namespace mesher {

// Revision-owned immutable two-level ray acceleration: bounded per-partition
// triangle BVHs under a persistent, 8-level, 16-way partition directory.
// Each local edit rebuilds only face-owner partitions affected by changed
// face IDs or incident to moved vertices. Unchanged nodes are shared.
class RevisionPickingIndex {
public:
    struct Bounds {
        Vec3 low{},high{};
        bool valid=false;
    };
    struct Triangle {
        FaceId id{};
        std::array<Vec3,3> vertices{};
        Bounds bounds{};
        Vec3 center{};
    };
    struct Node {
        Bounds bounds{};
        std::int32_t left=-1,right=-1;
        std::size_t begin=0,count=0;
    };
    struct Partition {
        std::vector<Triangle> triangles;
        std::vector<Node> nodes;
        Bounds bounds{};
    };
    struct Directory {
        Bounds bounds{};
        std::array<std::shared_ptr<const Directory>,16> children{};
        std::shared_ptr<const Partition> partition;
    };

    static std::shared_ptr<const RevisionPickingIndex> build(
        const RegionTopologyIndex& index);
    static std::shared_ptr<const RevisionPickingIndex> advance(
        const RevisionPickingIndex& old,
        const RegionTopologyIndex& before,
        const RegionTopologyIndex& after,
        const std::unordered_map<VertexId,std::optional<Vec3>>& vertexChanges,
        const std::unordered_map<FaceId,
            std::optional<std::array<VertexId,3>>>& faceChanges,
        std::size_t& touchedPartitions,std::size_t& copiedNodes);

    std::optional<Vec3> firstRayHit(Ray ray,
                                    RegionTopologyIndex::PickStatistics* statistics) const;
    const void* identity() const noexcept {return root_.get();}
    // Visit all triangles with boxes overlapping an arbitrary query AABB.
    // Visitor pointers/references are valid while this immutable root lives.
    void forEachOverlappingTriangle(
        const Bounds& bounds,const std::function<void(const Triangle&)>& visitor,
        RegionTopologyIndex::CollisionStatistics& stats,
        const RegionTopologyIndex::CollisionQueryLimits& limits={}) const;
    // Signed intersection sum for a single direction. Ambiguity includes
    // grazing vertices/edges, starting on the surface and near-parallel hits.
    int signedRayCrossings(Ray ray,
        RegionTopologyIndex::WindingStatistics& stats,bool& ambiguous) const;
private:
    std::shared_ptr<const Directory> root_;
    static std::shared_ptr<const Partition> buildPartition(
        const RegionTopologyIndex& index,PartitionId id);
    static std::shared_ptr<const Directory> replace(
        const std::shared_ptr<const Directory>& before,
        PartitionId id,unsigned depth,
        std::shared_ptr<const Partition> newPartition,std::size_t& copied);
};
} // namespace mesher

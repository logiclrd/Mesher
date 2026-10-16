#pragma once

#include "mesher/brush.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/versioned_collapse.hpp"
#include "mesher/versioned_refine.hpp"

#include <chrono>
#include <memory>

namespace mesher {

// Explicitly expensive V8 bridge to the unchanged flat Vulkan renderer.
// Preserves the original ordering for surviving imported elements and appends
// new stable IDs deterministically. Never called by the sculpt worker.
struct ViewportFlattenStatistics {
    double milliseconds{};
    std::size_t vertices{};
    std::size_t triangles{};
    std::size_t bytes{};
};
Mesh flattenRevisionForViewport(const RegionTopologyIndex& index,
                                const FlatMeshIndex& originalOrder,
                                ViewportFlattenStatistics* statistics=nullptr);

// The BrushStroke owns sampling and queue semantics; this brush owns *all*
// editable geometry and uses a dummy Mesh solely to satisfy BrushStroke's
// existing callback signature. Each stamp publishes one immutable index/root
// pair; it never creates or publishes a flat Mesh snapshot.
class VersionedSculptBrush final : public Brush {
public:
    VersionedSculptBrush(const Mesh& initial,double strength,
                         CurvatureSettings curvature,CollapseSettings collapse,
                         RefineSettings refine,bool symmetric,
                         std::size_t partitionCapacity=256);
    void stamp(Mesh&,std::span<const BrushArea> areas) override;
    std::shared_ptr<const RegionTopologyIndex> snapshot() const noexcept {
        return published_.snapshot();
    }
    std::shared_ptr<const FlatMeshIndex> originalOrder() const noexcept {
        return flatOrder_;
    }
    std::size_t completedStamps() const noexcept {return completed_;}
    StampTimings lastTimings() const noexcept {return timings_;}
    const StampDetails& lastDetails() const noexcept {return details_;}
    std::size_t publishedRevisions() const noexcept {return publishedRevisions_;}
    std::size_t modifiedPartitions() const noexcept {return modifiedPartitions_;}
    std::size_t copiedDirectoryNodes() const noexcept {return copiedDirectoryNodes_;}

private:
    VersionedSculptBrush(PartitionedMeshImport import,std::size_t initialCount,
                         double strength,CurvatureSettings curvature,
                         CollapseSettings collapse,RefineSettings refine,
                         bool symmetric,std::size_t partitionCapacity);
    std::shared_ptr<const FlatMeshIndex> flatOrder_;
    IndexedMeshRepository published_;
    VersionedInflateBrush inflate_;
    VersionedLocalRefiner refiner_;
    VersionedDirectionalCollapser collapser_;
    std::size_t partitionCapacity_;
    std::size_t vertices_{},faces_{};
    std::size_t completed_{},publishedRevisions_{};
    std::size_t modifiedPartitions_{},copiedDirectoryNodes_{};
    StampTimings timings_{};
    StampDetails details_{};
};

} // namespace mesher

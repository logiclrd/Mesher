#pragma once

#include "mesher/brush.hpp"
#include "mesher/region_edit.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mesher {

// The immutable pre-stamp revision IS the baseline: no copying of the mesh's
// vertex array, and no mutable post-stamp position may contaminate normals or
// directional edge-length measurements. V6/V7 consume this for remeshing.
struct LocalStampBaseline {
    std::shared_ptr<const RegionTopologyIndex> before;
    std::vector<VertexId> movedVertices;
    std::vector<FaceId> affectedFaces;

    Vec3 positionBefore(VertexId id) const { return before->vertex(id).position; }
    double edgeLengthBefore(VertexId a,VertexId b) const {
        return length(positionBefore(a)-positionBefore(b));
    }
    bool edgeWasGrowing(const RegionTopologyIndex& after,VertexId a,
                        VertexId b,double epsilon=1e-12) const {
        return length(after.vertex(a).position-after.vertex(b).position)>
               edgeLengthBefore(a,b)+epsilon;
    }
    bool edgeWasShrinking(const RegionTopologyIndex& after,VertexId a,
                          VertexId b,double epsilon=1e-12) const {
        return length(after.vertex(a).position-after.vertex(b).position)+epsilon<
               edgeLengthBefore(a,b);
    }
};

struct VersionedInflateStamp {
    std::shared_ptr<const RegionTopologyIndex> index;
    LocalStampBaseline baseline;
    RevisionCommitStatistics commitStatistics{};
    std::size_t candidates{};
    std::size_t moved{};
};

// Performs precisely one simultaneous Inflate stamp, including optional
// curvature correction. All normals, cotangents, influence and movement
// decisions use the old immutable revision. Only influenced partitions are
// copied on write. This is the V5 deformation path; V6/V7 will integrate
// split/flip/collapse, V8 will put it into the asynchronous worker.
class VersionedInflateBrush {
public:
    explicit VersionedInflateBrush(
        double strength,CurvatureSettings curvature={},
        std::vector<std::pair<VertexId,VertexId>> mirrorPairs={});

    VersionedInflateStamp stamp(
        std::shared_ptr<const RegionTopologyIndex> before,
        std::span<const BrushArea> areas,
        std::size_t maximumElementsPerPartition=256) const;

private:
    double strength_;
    CurvatureSettings curvature_;
    std::unordered_map<VertexId,VertexId> mirrors_;
};

} // namespace mesher

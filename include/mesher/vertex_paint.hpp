#pragma once
#include "mesher/region_edit.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace mesher {

// This is a colour-only immutable COW edit, never a geometry mutation.
// The exact original surface is used for line-of-sight visibility.
struct PaintDab {
    Vec3 point{};
    Vec3 outwardNormal{};
    double radius{};
    double nominalEdgeLength{};
    VertexRGBA rgba{};
    double hardness=0.5; // 0: linear soft falloff, 1: hard edge
    FaceId sourceFace{}; // optional picked triangle for full-strength centre interpolation
};
struct PaintPreview {
    std::vector<VertexId> visibleVertices;
    std::vector<FaceId> affectedFaces;
};
// Mirror semantics are identical for actual dabs and passive preview.
std::vector<PaintDab> paintDabsWithMirror(PaintDab dab,bool symmetric);
// Preview uses exact paint ray visibility and surface radius, but deliberately
// ignores alpha/hardness and never publishes any indexed COW revision.
PaintPreview collectPaintPreview(
    std::shared_ptr<const RegionTopologyIndex> source,
    const std::vector<PaintDab>& dabs,
    std::size_t maxVertices=65536,
    std::size_t maxRayTriangles=1048576,
    std::size_t maxFaces=65536);
struct PaintDabResult {
    std::shared_ptr<const RegionTopologyIndex> colored;
    std::vector<VertexId> changedVertices;
    std::vector<PartitionId> dirtyPartitions;
    std::size_t copiedPayloadBytes{};
};
// Current immutable colour at the stable ID, obtained from its owner partition.
VertexRGBA vertexColorAt(const RegionTopologyIndex& source,VertexId id);
// At most maxVertices candidates and maxRayTriangles primitive tests.
// Any exhaustion throws before publishing ANY colour change.
PaintDabResult applyVertexPaintDabs(
    std::shared_ptr<const RegionTopologyIndex> source,
    const std::vector<PaintDab>& dabs,
    std::size_t maxVertices=65536,
    std::size_t maxRayTriangles=1048576);
}

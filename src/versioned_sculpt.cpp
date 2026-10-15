#include "mesher/versioned_sculpt.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point a,Clock::time_point b) {
    return std::chrono::duration<double>(b-a).count();
}
std::vector<std::pair<VertexId,VertexId>> makeMirrors(
    const FlatMeshIndex& order,std::size_t count,bool symmetric) {
    if(!symmetric)return {};
    const auto correspondences=symmetricSphereMirrors(
        static_cast<std::uint32_t>(count));
    std::vector<std::pair<VertexId,VertexId>> pairs;
    pairs.reserve(count);
    for(std::size_t i=0;i<count;++i)
        pairs.push_back({order.vertexIds.at(i),
                         order.vertexIds.at(correspondences[i])});
    return pairs;
}
}

Mesh flattenRevisionForViewport(
    const RegionTopologyIndex& index,const FlatMeshIndex& originalOrder,
    ViewportFlattenStatistics* statistics) {
    const auto start=Clock::now();
    std::unordered_map<VertexId,Vec3> positions;
    std::unordered_map<FaceId,std::array<VertexId,3>> faces;
    index.revision().forEachPartition(
        [&](PartitionId,const MeshPartition& partition) {
            for(const auto& v:partition.vertices)
                if(!positions.emplace(v.id,v.position).second)
                    throw std::logic_error("Duplicate vertex in viewport adapter");
            for(const auto& f:partition.faces)
                if(!faces.emplace(f.id,f.vertices).second)
                    throw std::logic_error("Duplicate face in viewport adapter");
        });
    if(positions.size()>std::numeric_limits<std::uint32_t>::max())
        throw std::length_error("Flat Vulkan viewport exceeds 32-bit vertex index range");
    Mesh output;
    output.vertices.reserve(positions.size());
    output.triangles.reserve(faces.size());
    std::unordered_map<VertexId,std::uint32_t> indices;
    indices.reserve(positions.size());
    auto putVertex=[&](VertexId id,Vec3 value) {
        const auto indexValue=static_cast<std::uint32_t>(output.vertices.size());
        indices.emplace(id,indexValue);
        output.vertices.push_back(value);
    };
    // Preserve original flat indices for every surviving imported vertex.
    // Retired vertices are simply absent. Stable-ID additions are appended
    // after the surviving originals in increasing ID order.
    for(const auto id:originalOrder.vertexIds) {
        const auto it=positions.find(id);
        if(it==positions.end())continue;
        putVertex(it->first,it->second);
        positions.erase(it);
    }
    std::vector<std::pair<VertexId,Vec3>> extraVertices(
        positions.begin(),positions.end());
    std::sort(extraVertices.begin(),extraVertices.end(),
              [](const auto& a,const auto& b){return a.first<b.first;});
    for(const auto& [id,p]:extraVertices)putVertex(id,p);
    auto putFace=[&](std::array<VertexId,3> f) {
        output.triangles.push_back({
            indices.at(f[0]),indices.at(f[1]),indices.at(f[2])});
    };
    for(const auto id:originalOrder.faceIds) {
        const auto it=faces.find(id);
        if(it==faces.end())continue;
        putFace(it->second);
        faces.erase(it);
    }
    std::vector<std::pair<FaceId,std::array<VertexId,3>>> extraFaces(
        faces.begin(),faces.end());
    std::sort(extraFaces.begin(),extraFaces.end(),
              [](const auto& a,const auto& b){return a.first<b.first;});
    for(const auto& [_,vertices]:extraFaces)putFace(vertices);
    if(statistics) {
        statistics->milliseconds=seconds(start,Clock::now())*1000;
        statistics->vertices=output.vertices.size();
        statistics->triangles=output.triangles.size();
        statistics->bytes=output.vertices.size()*sizeof(Vec3)+
                          output.triangles.size()*sizeof(Triangle);
    }
    return output;
}

VersionedSculptBrush::VersionedSculptBrush(
    const Mesh& initial,double strength,CurvatureSettings curvature,
    CollapseSettings collapse,RefineSettings refine,bool symmetric,
    std::size_t partitionCapacity)
    :VersionedSculptBrush(partitionFlatMesh(initial,partitionCapacity),
                          initial.vertices.size(),strength,curvature,collapse,
                          refine,symmetric,partitionCapacity) {}

VersionedSculptBrush::VersionedSculptBrush(
    PartitionedMeshImport import,std::size_t initialCount,double strength,
    CurvatureSettings curvature,CollapseSettings collapse,RefineSettings refine,
    bool symmetric,std::size_t partitionCapacity)
    :flatOrder_(std::make_shared<const FlatMeshIndex>(
         std::move(import.flatIndex))),
     published_(RegionTopologyIndex::build(std::move(import.revision))),
     inflate_(strength,curvature,makeMirrors(*flatOrder_,initialCount,symmetric)),
     refiner_(initialCount,refine,
              makeMirrors(*flatOrder_,initialCount,symmetric),partitionCapacity),
     collapser_(initialCount,collapse,
                makeMirrors(*flatOrder_,initialCount,symmetric),partitionCapacity),
     partitionCapacity_(partitionCapacity),
     vertices_(initialCount),faces_(flatOrder_->faceIds.size()) {
    if(!partitionCapacity_)throw std::invalid_argument("Invalid partition capacity");
    details_.vertices=vertices_;
    details_.triangles=faces_;
}

void VersionedSculptBrush::stamp(
    Mesh&,std::span<const BrushArea> areas) {
    const auto old=published_.snapshot();
    const auto start=Clock::now();
    const auto inflated=inflate_.stamp(old,areas,partitionCapacity_);
    const auto afterInflate=Clock::now();
    const auto refined=refiner_.apply(inflated.index,inflated.baseline);
    // Collapse must see V6's newly inserted stable mirror IDs immediately.
    for(const auto& pair:refined.insertedMirrorPairs)
        collapser_.addMirrorPair(pair.first,pair.second);
    const auto afterRefine=Clock::now();
    const auto collapsed=collapser_.apply(refined.index,inflated.baseline);
    const auto afterCollapse=Clock::now();
    const auto updated=collapsed.index;

    // All geometry and connectivity are already fully validated. Publish
    // the exact index + mesh revision pair atomically once per completed
    // Step, never publishing partially refined intermediate topology.
    if(updated!=old) {
        if(!published_.publishIfCurrent(old,updated))
            throw std::logic_error("Single writer lost indexed root");
        ++publishedRevisions_;
    }
    // Mirror-ID changes are cheap local deltas, not a global map copy.
    for(const auto& pair:refined.insertedMirrorPairs) {
        inflate_.addMirrorPair(pair.first,pair.second);
        // V6 itself has already added the pair; V7 sees the same live IDs.
        // V7's persistent collapse map needs the added mirror pair too.
        collapser_.addMirrorPair(pair.first,pair.second);
    }
    for(const auto& pair:collapsed.removedMirrorPairs) {
        inflate_.removeMirrorPair(pair.first,pair.second);
        refiner_.removeMirrorPair(pair.first,pair.second);
    }
    vertices_+=refined.insertedVertices.size();
    faces_+=2*refined.insertedVertices.size();
    vertices_-=collapsed.removedVertices.size();
    faces_-=collapsed.removedFaces.size();
    modifiedPartitions_+=inflated.commitStatistics.modifiedPartitions+
                         refined.modifiedPartitions+collapsed.modifiedPartitions;
    copiedDirectoryNodes_+=inflated.commitStatistics.copiedDirectoryNodes+
                            refined.copiedDirectoryNodes+
                            collapsed.copiedDirectoryNodes;
    details_.areas.assign(areas.begin(),areas.end());
    details_.influenced=inflated.candidates;
    details_.moved=inflated.moved;
    details_.preStampCopiedVertices=0;
    details_.splitPairs=refined.statistics.splitPairs;
    details_.flipPairs=refined.statistics.flipPairs;
    details_.collapsedPairs=collapsed.statistics.accepted;
    details_.vertices=vertices_;
    details_.triangles=faces_;
    timings_={seconds(start,afterInflate),seconds(afterInflate,afterRefine),
              seconds(afterRefine,afterCollapse),0};
    ++completed_;
}

} // namespace mesher

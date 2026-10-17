#include "mesher/versioned_sculpt.hpp"
#include "mesher/partition_render.hpp"

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
std::size_t elementCount(const RegionTopologyIndex& index,bool vertices) {
    std::size_t count=0;
    index.revision().forEachPartition([&](PartitionId,const MeshPartition& p) {
        count+=vertices?p.vertices.size():p.faces.size();
    });
    return count;
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
    std::size_t partitionCapacity,SculptStepValidator validator)
    :VersionedSculptBrush(partitionFlatMesh(initial,partitionCapacity),
                          initial.vertices.size(),strength,curvature,collapse,
                          refine,symmetric,partitionCapacity,std::move(validator)) {}

VersionedSculptBrush::VersionedSculptBrush(
    PartitionedMeshImport import,std::size_t initialCount,double strength,
    CurvatureSettings curvature,CollapseSettings collapse,RefineSettings refine,
    bool symmetric,std::size_t partitionCapacity,
    SculptStepValidator validator)
    :flatOrder_(std::make_shared<const FlatMeshIndex>(
         std::move(import.flatIndex))),
     published_(RegionTopologyIndex::build(std::move(import.revision))),
     history_(published_.snapshot(),64,32*1024*1024,&historyRetirement_,
              initialCount,flatOrder_->faceIds.size()),
     inflate_(strength,curvature,makeMirrors(*flatOrder_,initialCount,symmetric)),
     refiner_(initialCount,refine,
              makeMirrors(*flatOrder_,initialCount,symmetric),partitionCapacity),
     collapser_(initialCount,collapse,
                makeMirrors(*flatOrder_,initialCount,symmetric),partitionCapacity),
     partitionCapacity_(partitionCapacity),
     initialVertexCount_(initialCount),
     vertices_(initialCount),faces_(flatOrder_->faceIds.size()),
     validator_(std::move(validator)) {
    if(!partitionCapacity_)throw std::invalid_argument("Invalid partition capacity");
    details_.vertices=vertices_;
    details_.triangles=faces_;
}

VersionedSculptBrush::VersionedSculptBrush(
    DurableCheckpoint recovered,double strength,CurvatureSettings curvature,
    CollapseSettings collapse,RefineSettings refine,
    std::size_t partitionCapacity,SculptStepValidator validator)
    :flatOrder_(std::make_shared<const FlatMeshIndex>()),
     published_(recovered.index),
     history_(published_.snapshot(),64,32*1024*1024,&historyRetirement_,
              elementCount(*recovered.index,true),
              elementCount(*recovered.index,false)),
     inflate_(strength,curvature,recovered.mirrorPairs),
     refiner_(recovered.initialVertexCount?recovered.initialVertexCount:
              elementCount(*recovered.index,true),refine,
              recovered.mirrorPairs,partitionCapacity),
     collapser_(recovered.initialVertexCount?recovered.initialVertexCount:
                elementCount(*recovered.index,true),collapse,
                recovered.mirrorPairs,partitionCapacity),
     partitionCapacity_(partitionCapacity),
     initialVertexCount_(recovered.initialVertexCount?recovered.initialVertexCount:
                         elementCount(*recovered.index,true)),
     vertices_(elementCount(*recovered.index,true)),
     faces_(elementCount(*recovered.index,false)),
     validator_(std::move(validator)) {
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
    refiner_.beginStep();
    try {collapser_.beginStep();}
    catch(...) {refiner_.rollbackStep();throw;}
    try {
    const auto refined=refiner_.apply(inflated.index,inflated.baseline);
    // Collapse must see V6's newly inserted stable mirror IDs immediately.
    for(const auto& pair:refined.insertedMirrorPairs)
        collapser_.addMirrorPair(pair.first,pair.second);
    const auto afterRefine=Clock::now();
    const auto collapsed=collapser_.apply(refined.index,inflated.baseline);
    const auto afterCollapse=Clock::now();
    const auto updated=collapsed.index;

    // Identify every face-owner partition whose triangles, flat normals or
    // canonical edge lines may have changed, using only old/new local halos.
    // This set spans the entire stamp even when split/flip and collapse both
    // occur, so a rendered frame never receives a half-updated surface.
    std::vector<VertexId> changedVertices=inflated.baseline.movedVertices;
    changedVertices.insert(changedVertices.end(),
        refined.insertedVertices.begin(),refined.insertedVertices.end());
    changedVertices.insert(changedVertices.end(),
        collapsed.removedVertices.begin(),collapsed.removedVertices.end());
    std::vector<FaceId> changedFaces=inflated.baseline.affectedFaces;
    changedFaces.insert(changedFaces.end(),
        refined.changedFaces.begin(),refined.changedFaces.end());
    changedFaces.insert(changedFaces.end(),
        collapsed.removedFaces.begin(),collapsed.removedFaces.end());
    changedFaces.insert(changedFaces.end(),
        collapsed.rewrittenFaces.begin(),collapsed.rewrittenFaces.end());
    // The decision examines the complete unpublished post-refine/collapse
    // Step and its exact dirty face/vertex set. Classification budget and
    // geometric refusals raised by this opt-in callback are recoverable.
    std::string rejection;
    if(validator_&&updated!=old) {
        try {
            rejection=validator_({old,updated,
                std::span<const VertexId>(changedVertices),
                std::span<const FaceId>(changedFaces)});
        }catch(const std::length_error& error) {
            rejection=error.what();
        }catch(const std::logic_error& error) {
            rejection=error.what();
        }
    }
    if(!rejection.empty()) {
        collapser_.rollbackStep();
        refiner_.rollbackStep();
        lastDirtyPartitions_.clear();
        lastRejection_=std::move(rejection);
        ++rejected_;
        details_={};
        details_.areas.assign(areas.begin(),areas.end());
        details_.influenced=inflated.candidates;
        details_.vertices=vertices_;
        details_.triangles=faces_;
        timings_={seconds(start,afterInflate),seconds(afterInflate,afterRefine),
                  seconds(afterRefine,afterCollapse),0};
        ++completed_;
        return;
    }
    lastDirtyPartitions_=updated==old?std::vector<PartitionId>{}:
        dirtyRenderPartitions(*old,*updated,changedVertices,changedFaces);

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
        // V6 itself and V7's pre-collapse state already own this pair.
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
    clonedPartitionBytes_+=inflated.commitStatistics.clonedPartitionBytes+
                           refined.clonedPartitionBytes+collapsed.clonedPartitionBytes;
    // One COW root per actual geometry-changing Step, within both a
    // fixed entry count and copied-payload budget. Mirror changes are stored
    // only as incremental transitions, never a full per-stamp map copy.
    history_.append(updated,lastDirtyPartitions_,
                    refined.insertedMirrorPairs,collapsed.removedMirrorPairs,
                    inflated.commitStatistics.clonedPartitionBytes+
                    refined.clonedPartitionBytes+collapsed.clonedPartitionBytes,
                    vertices_,faces_);
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
    collapser_.commitStep();
    refiner_.commitStep();
    }catch(...) {
        collapser_.rollbackStep();
        refiner_.rollbackStep();
        throw;
    }
}

bool VersionedSculptBrush::undo() {
    if(!history_.canUndo())return false;
    const auto previous=published_.snapshot();
    const auto step=history_.undo();
    for(const auto& [a,b]:step.removeMirrors) {
        inflate_.removeMirrorPair(a,b);
        refiner_.removeMirrorPair(a,b);
        collapser_.removeMirrorPair(a,b);
    }
    for(const auto& [a,b]:step.addMirrors) {
        inflate_.addMirrorPair(a,b);
        refiner_.addMirrorPair(a,b);
        collapser_.addMirrorPair(a,b);
    }
    if(!published_.publishIfCurrent(previous,step.index))
        throw std::logic_error("History undo base revision changed");
    lastDirtyPartitions_=step.dirtyPartitions;
    vertices_=step.vertices;
    faces_=step.faces;
    details_.vertices=vertices_;
    details_.triangles=faces_;
    details_.moved=details_.splitPairs=details_.flipPairs=details_.collapsedPairs=0;
    ++publishedRevisions_;
    return true;
}
bool VersionedSculptBrush::redo() {
    if(!history_.canRedo())return false;
    const auto previous=published_.snapshot();
    const auto step=history_.redo();
    for(const auto& [a,b]:step.removeMirrors) {
        inflate_.removeMirrorPair(a,b);
        refiner_.removeMirrorPair(a,b);
        collapser_.removeMirrorPair(a,b);
    }
    for(const auto& [a,b]:step.addMirrors) {
        inflate_.addMirrorPair(a,b);
        refiner_.addMirrorPair(a,b);
        collapser_.addMirrorPair(a,b);
    }
    if(!published_.publishIfCurrent(previous,step.index))
        throw std::logic_error("History redo base revision changed");
    lastDirtyPartitions_=step.dirtyPartitions;
    vertices_=step.vertices;
    faces_=step.faces;
    details_.vertices=vertices_;
    details_.triangles=faces_;
    details_.moved=details_.splitPairs=details_.flipPairs=details_.collapsedPairs=0;
    ++publishedRevisions_;
    return true;
}
DurableCheckpoint VersionedSculptBrush::checkpoint() const {
    DurableCheckpoint snapshot;
    snapshot.index=published_.snapshot();
    snapshot.initialVertexCount=initialVertexCount_;
    for(const auto& [a,b]:inflate_.mirrors())
        if(a<=b)snapshot.mirrorPairs.push_back({a,b});
    std::sort(snapshot.mirrorPairs.begin(),snapshot.mirrorPairs.end());
    return snapshot;
}

} // namespace mesher

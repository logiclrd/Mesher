#include "mesher/partition_render.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
PartitionRenderVertex vertex(Vec3 p,Vec3 n={}) {
    return {float(p.x),float(p.y),float(p.z),
            float(n.x),float(n.y),float(n.z),0};
}
using Edge=RegionTopologyIndex::Edge;
Edge edge(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
}

std::vector<PartitionId> dirtyRenderPartitions(
    const RegionTopologyIndex& before,const RegionTopologyIndex& after,
    std::span<const VertexId> changedVertices,
    std::span<const FaceId> changedFaces) {
    std::set<PartitionId> owners;
    for(const auto* index:{&before,&after}) {
        std::set<FaceId> faces(changedFaces.begin(),changedFaces.end());
        std::set<VertexId> vertices(changedVertices.begin(),changedVertices.end());
        for(const auto face:changedFaces) {
            if(index->retiredFace(face))continue;
            try {
                const auto& f=index->face(face);
                owners.insert(f.owner);
                vertices.insert(f.vertices.begin(),f.vertices.end());
            }catch(const std::out_of_range&) {
                // A new face only exists in the after revision; a removed
                // face only exists in before. Neither is a global scan.
            }
        }
        // Include the one-ring of old and new vertex IDs. Face owners can
        // differ from the owners of their referenced vertex partitions.
        for(const auto id:vertices) {
            const auto v=index->maybeVertex(id);
            if(!v)continue;
            for(const auto face:v->incidentFaces)faces.insert(face);
        }
        // A second face neighborhood includes adjacent cross-partition
        // faces responsible for a changed edge's canonical wire owner.
        for(const auto face:faces) {
            if(index->retiredFace(face))continue;
            try {
                const auto& f=index->face(face);
                owners.insert(f.owner);
                for(const auto id:f.vertices) {
                    const auto v=index->maybeVertex(id);
                    if(v)for(const auto adjacent:v->incidentFaces)
                        owners.insert(index->face(adjacent).owner);
                }
            }catch(const std::out_of_range&) { }
        }
    }
    return {owners.begin(),owners.end()};
}

PartitionRenderGeometry buildPartitionRenderGeometry(
    const RegionTopologyIndex& index,PartitionId owner) {
    PartitionRenderGeometry out;
    const auto p=index.revision().partition(owner);
    if(!p)return out;
    out.solid.reserve(p->faces.size()*3);
    out.wire.reserve(p->faces.size()*6);
    std::set<Edge> emitted;
    for(const auto& face:p->faces) {
        const auto& t=face.vertices;
        const auto a=index.vertex(t[0]).position;
        const auto b=index.vertex(t[1]).position;
        const auto c=index.vertex(t[2]).position;
        const Vec3 n=cross(b-a,c-a);
        const Vec3 normal=length(n)>1e-20?normalized(n):Vec3{};
        out.solid.push_back(vertex(a,normal));
        out.solid.push_back(vertex(b,normal));
        out.solid.push_back(vertex(c,normal));
        for(unsigned i=0;i<3;++i) {
            const auto e=edge(t[i],t[(i+1)%3]);
            const auto sides=index.maybeEdgeSides(e);
            if(!sides||sides->empty())
                throw std::logic_error("Partition wire references nonexistent edge");
            auto smallest=sides->front().face;
            for(const auto& side:*sides)
                smallest=std::min(smallest,side.face);
            if(smallest!=face.id||!emitted.insert(e).second)continue;
            out.wire.push_back(vertex(index.vertex(e.a).position));
            out.wire.push_back(vertex(index.vertex(e.b).position));
        }
    }
    return out;
}

void PartitionRenderCache::initialize(
    std::shared_ptr<const RegionTopologyIndex> initial) {
    if(!initial)throw std::invalid_argument("Partition renderer needs a revision");
    partitions_.clear();
    revision_=std::move(initial);
    revision_->revision().forEachPartition(
        [&](PartitionId id,const MeshPartition& partition) {
            if(partition.faces.empty())return;
            partitions_.emplace(id,std::make_shared<const PartitionRenderGeometry>(
                buildPartitionRenderGeometry(*revision_,id)));
        });
}

PartitionRenderUpdate PartitionRenderCache::advance(
    std::shared_ptr<const RegionTopologyIndex> revision,
    std::span<const PartitionId> dirty) {
    if(!revision)throw std::invalid_argument("Partition renderer needs revision");
    if(!revision_)throw std::logic_error("Partition renderer not initialized");
    PartitionRenderUpdate stats;
    std::set<PartitionId> unique(dirty.begin(),dirty.end());
    // Preserve every unchanged allocation even if two revisions coalesced.
    for(const auto id:unique) {
        const auto partition=revision->revision().partition(id);
        ++stats.rebuiltPartitions;
        if(!partition||partition->faces.empty()) {
            partitions_.erase(id);
            continue;
        }
        auto geometry=std::make_shared<const PartitionRenderGeometry>(
            buildPartitionRenderGeometry(*revision,id));
        stats.rebuiltFaces+=partition->faces.size();
        stats.uploadedBytes+=(geometry->solid.size()+geometry->wire.size())*
                             sizeof(PartitionRenderVertex);
        partitions_[id]=std::move(geometry);
    }
    revision_=std::move(revision);
    return stats;
}
} // namespace mesher

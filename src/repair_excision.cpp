#include "mesher/repair_excision.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=RegionTopologyIndex::Edge;
Edge edge(VertexId a,VertexId b) {
    if(b<a)std::swap(a,b);
    return {a,b};
}
struct EdgeInfo {std::size_t count{}; bool firstForward{};};
}

PrivateExcisionResult applyPrivateExcision(
    std::shared_ptr<const RegionTopologyIndex> candidate,
    const RepairExcisionPreview& proposal,
    const PrivateExcisionLimits& limits) {
    if(!candidate || !limits.maxVertices || !limits.maxFaces ||
       !limits.maxModifiedPartitions || !limits.maxSurvivingFaces)
        throw std::invalid_argument("R03 requires candidate and finite positive budgets");
    if(!proposal.complete || proposal.proposedFaces.empty() ||
       proposal.proposedVertices.empty())
        throw std::logic_error("R03 refuses incomplete/empty R02 removal proposal");
    const std::set<VertexId> vertices(
        proposal.proposedVertices.begin(),proposal.proposedVertices.end());
    const std::set<FaceId> faces(
        proposal.proposedFaces.begin(),proposal.proposedFaces.end());
    if(vertices.size()!=proposal.proposedVertices.size() ||
       faces.size()!=proposal.proposedFaces.size() ||
       vertices.size()>limits.maxVertices || faces.size()>limits.maxFaces)
        throw std::length_error("R03 duplicate IDs or finite excision budget exceeded");

    std::set<PartitionId> owners;
    for(const auto id:vertices) {
        const auto old=candidate->maybeVertex(id);
        if(!old)throw std::logic_error("R03 excision references missing vertex");
        owners.insert(old->owner);
        for(const auto f:old->incidentFaces)
            if(!faces.contains(f))
                throw std::logic_error("R03 cannot leave a selected vertex referenced by a surviving face");
    }
    std::set<VertexId> affected;
    for(const auto id:faces) {
        const auto old=candidate->maybeFace(id);
        if(!old)throw std::logic_error("R03 excision references missing face");
        owners.insert(old->owner);
        affected.insert(old->vertices.begin(),old->vertices.end());
    }
    for(const auto id:affected) {
        if(vertices.contains(id))continue;
        const auto old=candidate->maybeVertex(id);
        if(!old)throw std::logic_error("R03 local face references a missing vertex");
        bool survives=false;
        for(const auto f:old->incidentFaces)
            if(!faces.contains(f)) {survives=true;break;}
        if(!survives)
            throw std::logic_error("R03 plan omitted an orphaned vertex");
    }
    if(owners.size()>limits.maxModifiedPartitions)
        throw std::length_error("R03 modified partition budget exceeded");

    // COW debug transaction. This low-level view deliberately has NO
    // production closed-manifold requirement; each partition is touched
    // once, and its surviving stable IDs and face winding are unchanged.
    auto view=candidate->revision().createMutableView();
    std::size_t actualFaces=0,actualVertices=0;
    for(const auto owner:owners) {
        auto& part=view.editPartition(owner);
        const auto oldFaces=part.faces.size(),oldVertices=part.vertices.size();
        std::erase_if(part.faces,[&](const PartitionFace& f) {
            return faces.contains(f.id);
        });
        std::erase_if(part.vertices,[&](const PartitionVertex& v) {
            return vertices.contains(v.id);
        });
        actualFaces+=oldFaces-part.faces.size();
        actualVertices+=oldVertices-part.vertices.size();
    }
    if(actualFaces!=faces.size()||actualVertices!=vertices.size())
        throw std::logic_error("R03 COW excision did not retire the exact selected stable IDs");

    // Everything from here on remains private. No worker publication.
    const auto revision=view.commit().revision;
    auto vertexTombstones=candidate->retiredVertexIds();
    auto faceTombstones=candidate->retiredFaceIds();
    vertexTombstones.insert(vertexTombstones.end(),vertices.begin(),vertices.end());
    faceTombstones.insert(faceTombstones.end(),faces.begin(),faces.end());
    auto cut=RegionTopologyIndex::restore(
        revision,vertexTombstones,faceTombstones);

    std::map<Edge,EdgeInfo> edges;
    std::size_t surviving=0;
    cut->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            surviving+=part.faces.size();
            if(surviving>limits.maxSurvivingFaces)
                throw std::length_error("R03 audit work exceeded surviving-face limit");
            for(const auto& f:part.faces) {
                for(const auto v:f.vertices)
                    if(!cut->maybeVertex(v))
                        throw std::logic_error("R03 private cut has a dangling face vertex reference");
                for(unsigned i=0;i<3;++i) {
                    const auto a=f.vertices[i],b=f.vertices[(i+1)%3];
                    const auto key=edge(a,b);
                    auto& info=edges[key];
                    const bool direction=a==key.a;
                    if(info.count && (info.count!=1||info.firstForward==direction))
                        throw std::logic_error("R03 private cut has a nonmanifold or misoriented edge");
                    if(!info.count)info.firstForward=direction;
                    ++info.count;
                }
            }
        });
    // Only R02's surviving-face contours may become one-sided edges.
    // Closed-component removal is valid with zero new boundary edges.
    std::set<Edge> expected;
    for(const auto& loop:proposal.contours) {
        if(loop.vertexIds.size()<3)
            throw std::logic_error("R03 contour has fewer than three vertices");
        for(std::size_t i=0;i<loop.vertexIds.size();++i) {
            const auto a=loop.vertexIds[i];
            const auto b=loop.vertexIds[(i+1)%loop.vertexIds.size()];
            if(vertices.contains(a)||vertices.contains(b))
                throw std::logic_error("R03 contour touches retired vertex");
            if(!expected.insert(edge(a,b)).second)
                throw std::logic_error("R03 contour repeats one boundary edge");
        }
    }
    std::set<Edge> observed;
    for(const auto& [id,info]:edges)
        if(info.count==1)observed.insert(id);
    if(observed!=expected || observed.size()!=proposal.boundaryEdges)
        throw std::logic_error("R03 actual open edges differ from R02 directed contours");

    // An edge-count audit alone can miss two disjoint triangle fans meeting
    // at one surviving vertex. A legitimate open manifold has ONE connected
    // vertex link: a chain at a hole rim (two degree-1 link endpoints), or a
    // cycle elsewhere (all degree-2). Audit the complete affected neighborhood
    // under the existing finite face budget, not the entire global mesh.
    std::set<VertexId> boundaryVertices;
    for(const auto e:observed) {
        boundaryVertices.insert(e.a);
        boundaryVertices.insert(e.b);
    }
    for(const auto id:affected) {
        const auto vertex=cut->maybeVertex(id);
        if(!vertex)continue;
        if(vertex->incidentFaces.empty())
            throw std::logic_error("R03 cut left an unretired orphan vertex");
        std::map<VertexId,std::set<VertexId>> link;
        for(const auto faceId:vertex->incidentFaces) {
            const auto& tri=cut->face(faceId).vertices;
            std::array<VertexId,2> others{};
            unsigned count=0;
            for(const auto v:tri)
                if(v!=id)others[count++]=v;
            if(count!=2)
                throw std::logic_error("R03 remaining face does not contain center vertex");
            link[others[0]].insert(others[1]);
            link[others[1]].insert(others[0]);
        }
        std::size_t endpoints=0;
        for(const auto& [_,adjacent]:link) {
            if(adjacent.size()==1)++endpoints;
            else if(adjacent.size()!=2)
                throw std::logic_error("R03 private cut left branching vertex link");
        }
        const auto isBoundary=boundaryVertices.contains(id);
        if(endpoints!=(isBoundary?2u:0u))
            throw std::logic_error("R03 vertex link is not one open fan or closed cycle");
        std::set<VertexId> visited;
        std::vector<VertexId> pending{link.begin()->first};
        while(!pending.empty()) {
            const auto current=pending.back();pending.pop_back();
            if(!visited.insert(current).second)continue;
            for(const auto next:link.at(current))pending.push_back(next);
        }
        if(visited.size()!=link.size())
            throw std::logic_error("R03 surviving vertex has disconnected fans");
    }

    for(const auto id:vertices)
        if(cut->maybeVertex(id)||!cut->retiredVertex(id))
            throw std::logic_error("R03 vertex tombstone was not retained");
    for(const auto id:faces)
        if(cut->maybeFace(id)||!cut->retiredFace(id))
            throw std::logic_error("R03 face tombstone was not retained");

    return {std::move(cut),vertices.size(),faces.size(),owners.size(),
            surviving,observed.size(),proposal.contours.size()};
}
} // namespace mesher

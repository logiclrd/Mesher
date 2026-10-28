#pragma once
#include "mesher/region_edit.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mesher {

// R07: a CLOSED and certified R06 index is not automatically a publishable
// sculpt revision. This bounded read-only transition audit derives exact
// changed stable IDs and mirror-orbit deltas for the existing single-CAS,
// history and checkpoint publisher. No coordinate coincidence grants an
// existing source ID a new mirror association.
struct CertifiedAutoPublicationDelta {
    std::vector<VertexId> addedVertices,retiredVertices,changedVertices;
    std::vector<FaceId> addedFaces,retiredFaces,changedFaces;
    std::vector<std::pair<VertexId,VertexId>> addedMirrors,removedMirrors;
    std::size_t liveVertices{},liveFaces{};
};

inline CertifiedAutoPublicationDelta inspectCertifiedAutoPublication(
    const RegionTopologyIndex& source,
    const RegionTopologyIndex& certified,
    const std::unordered_map<VertexId,VertexId>* currentMirrors=nullptr,
    std::size_t maxElements=1000000) {
    if(!maxElements)throw std::invalid_argument("R07 publication element budget is zero");
    using Point=std::tuple<double,double,double>;
    const auto key=[](Vec3 p){return Point{p.x,p.y,p.z};};
    std::map<VertexId,Vec3> oldVertices,newVertices;
    std::map<FaceId,std::array<VertexId,3>> oldFaces,newFaces;
    const auto collect=[&](const RegionTopologyIndex& index,
                           auto& vertices,auto& faces) {
        index.revision().forEachPartition(
            [&](PartitionId,const MeshPartition& partition) {
                if(partition.vertices.size()>maxElements-vertices.size()||
                   partition.faces.size()>maxElements-faces.size())
                    throw std::length_error("R07 publication transition exceeds finite element budget");
                for(const auto& v:partition.vertices)
                    if(!vertices.emplace(v.id,v.position).second)
                        throw std::logic_error("R07 duplicate stable vertex ID");
                for(const auto& f:partition.faces)
                    if(!faces.emplace(f.id,f.vertices).second)
                        throw std::logic_error("R07 duplicate stable face ID");
            });
    };
    collect(source,oldVertices,oldFaces);
    collect(certified,newVertices,newFaces);
    CertifiedAutoPublicationDelta out;
    out.liveVertices=newVertices.size();
    out.liveFaces=newFaces.size();
    std::set<VertexId> added,retired;
    for(const auto& [id,p]:oldVertices) {
        const auto next=newVertices.find(id);
        if(next==newVertices.end()) {
            retired.insert(id);
            out.retiredVertices.push_back(id);
            out.changedVertices.push_back(id);
        }else if(key(p)!=key(next->second))
            throw std::logic_error(
                "R07 certified R06 closure unexpectedly moved a surviving source vertex");
    }
    for(const auto& [id,p]:newVertices) {
        (void)p;
        if(oldVertices.contains(id))continue;
        added.insert(id);
        out.addedVertices.push_back(id);
        out.changedVertices.push_back(id);
    }
    for(const auto& [id,face]:oldFaces) {
        const auto next=newFaces.find(id);
        if(next==newFaces.end()) {
            out.retiredFaces.push_back(id);
            out.changedFaces.push_back(id);
        }else if(next->second!=face)out.changedFaces.push_back(id);
    }
    for(const auto& [id,face]:newFaces) {
        (void)face;
        if(!oldFaces.contains(id)) {
            out.addedFaces.push_back(id);
            out.changedFaces.push_back(id);
        }
    }
    // Source-backed split children and retired vertices must all remain
    // internally referenced and certified. No orphan new vertex is allowed.
    std::set<VertexId> referenced;
    for(const auto& [id,tri]:newFaces) {
        (void)id;
        for(const auto v:tri) {
            if(!newVertices.contains(v))
                throw std::logic_error("R07 output face refers to a missing vertex ID");
            referenced.insert(v);
        }
    }
    for(const auto id:added)
        if(!referenced.contains(id))
            throw std::logic_error("R07 closure introduced an unreferenced new vertex");

    if(currentMirrors) {
        if(currentMirrors->size()!=oldVertices.size())
            throw std::logic_error("R07 source mirror map is not complete");
        for(const auto& [id,p]:oldVertices) {
            const auto m=currentMirrors->find(id);
            if(m==currentMirrors->end()||
               !oldVertices.contains(m->second)||
               !currentMirrors->contains(m->second)||
               currentMirrors->at(m->second)!=id)
                throw std::logic_error("R07 source mirror map is not reciprocal");
            const auto q=oldVertices.at(m->second);
            if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
               ((m->second==id)!=(p.x==0)))
                throw std::logic_error("R07 input mirror orbit is geometrically inconsistent");
            if(retired.contains(id)!=retired.contains(m->second))
                throw std::logic_error("R07 closure retired only half a mirrored source orbit");
            if(retired.contains(id)&&id<=m->second)
                out.removedMirrors.emplace_back(id,m->second);
            if(!retired.contains(id)) {
                const auto& finalP=newVertices.at(id);
                const auto& finalQ=newVertices.at(m->second);
                if(finalP.x!=-finalQ.x||finalP.y!=finalQ.y||
                   finalP.z!=finalQ.z)
                    throw std::logic_error("R07 preserved mirror source orbit was disrupted");
            }
        }
        std::map<Point,VertexId> newPositions;
        for(const auto id:added)
            if(!newPositions.emplace(key(newVertices.at(id)),id).second)
                throw std::logic_error("R07 inserted mirrored vertices overlap");
        for(const auto id:added) {
            const auto p=newVertices.at(id);
            const auto found=newPositions.find(Point{-p.x,p.y,p.z});
            if(found==newPositions.end())
                throw std::logic_error("R07 newly inserted vertex has no exact reflected twin");
            const auto twin=found->second;
            if((id==twin)!=(p.x==0))
                throw std::logic_error("R07 newly inserted seam vertex has incorrect reflection");
            if(id<=twin)out.addedMirrors.emplace_back(id,twin);
        }
    }
    return out;
}

} // namespace mesher

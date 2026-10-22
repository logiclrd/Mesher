#include "mesher/repair_survivors.hpp"

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
struct Side {FaceId face{}; bool forward{};};
Edge canonical(VertexId a,VertexId b) {
    if(b<a)std::swap(a,b);
    return {a,b};
}
}

RepairSurvivorInventory inventoryPrivateExcision(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const RepairSurvivorInventoryLimits& limits) {
    if(!cut.index || !limits.maxFaces || !limits.maxEdges ||
       !limits.maxBoundaryLoops)
        throw std::invalid_argument("R06-M01 requires private cut and finite positive limits");
    if(!proposal.complete || proposal.contours.size()>limits.maxBoundaryLoops ||
       proposal.contours.size()!=cut.boundaryLoops ||
       proposal.boundaryEdges!=cut.boundaryEdges)
        throw std::logic_error("R06-M01 requires a complete R03-matching boundary plan");

    const auto& index=*cut.index;
    RepairSurvivorInventory result;
    std::map<FaceId,std::vector<FaceId>> adjacency;
    std::map<VertexId,std::vector<FaceId>> incident;
    std::map<Edge,std::vector<Side>> edges;
    index.revision().forEachPartition([&](PartitionId,const MeshPartition& part) {
        for(const auto& face:part.faces) {
            if(++result.faces>limits.maxFaces)
                throw std::length_error("R06-M01 face inventory budget exhausted");
            if(!adjacency.emplace(face.id,std::vector<FaceId>{}).second)
                throw std::logic_error("R06-M01 duplicate surviving FaceId");
            if(!index.maybeFace(face.id))
                throw std::logic_error("R06-M01 indexed face absent from private revision");
            std::set<VertexId> distinct;
            for(const auto v:face.vertices) {
                if(!index.maybeVertex(v) || !distinct.insert(v).second)
                    throw std::logic_error("R06-M01 degenerate or dangling face");
                incident[v].push_back(face.id);
            }
            for(unsigned i=0;i<3;++i) {
                const auto a=face.vertices[i],b=face.vertices[(i+1)%3];
                const auto key=canonical(a,b);
                auto [it,inserted]=edges.try_emplace(key);
                if(inserted && edges.size()>limits.maxEdges)
                    throw std::length_error("R06-M01 edge inventory budget exhausted");
                auto& sides=it->second;
                sides.push_back({face.id,a==key.a});
                if(sides.size()>2)
                    throw std::logic_error("R06-M01 edge has more than two incident faces");
            }
        }
    });
    if(result.faces!=cut.survivingFaces)
        throw std::logic_error("R06-M01 private cut surviving-face count disagrees");
    result.edges=edges.size();
    for(const auto& [_,sides]:edges) {
        if(sides.size()==2) {
            if(sides[0].forward==sides[1].forward)
                throw std::logic_error("R06-M01 two-sided edge winding disagrees");
            adjacency.at(sides[0].face).push_back(sides[1].face);
            adjacency.at(sides[1].face).push_back(sides[0].face);
        }else ++result.boundaryEdges;
    }
    if(result.boundaryEdges!=cut.boundaryEdges)
        throw std::logic_error("R06-M01 actual boundary edge count differs from R03");

    std::map<FaceId,std::size_t> owner;
    std::map<VertexId,std::size_t> vertexOwner;
    for(const auto& [seed,_]:adjacency) {
        if(owner.contains(seed))continue;
        const auto componentId=result.components.size();
        auto& component=result.components.emplace_back();
        std::vector<FaceId> pending{seed};
        while(!pending.empty()) {
            const auto faceId=pending.back();
            pending.pop_back();
            if(owner.contains(faceId)) {
                if(owner.at(faceId)!=componentId)
                    throw std::logic_error("R06-M01 edge-connected face has multiple owners");
                continue;
            }
            owner.emplace(faceId,componentId);
            component.faces.push_back(faceId);
            const auto& tri=index.face(faceId).vertices;
            for(const auto v:tri) {
                auto [it,inserted]=vertexOwner.emplace(v,componentId);
                if(!inserted && it->second!=componentId)
                    throw std::logic_error("R06-M01 separate surface components touch at vertex");
            }
            for(const auto neighbor:adjacency.at(faceId))
                if(!owner.contains(neighbor))pending.push_back(neighbor);
        }
        std::sort(component.faces.begin(),component.faces.end());
    }
    for(const auto& [vertex,faces]:incident) {
        const auto componentId=vertexOwner.at(vertex);
        result.components[componentId].vertices.push_back(vertex);
        // A single component can still have disjoint fans at one vertex.
        // Their links must be connected through actual shared edges.
        std::set<FaceId> atVertex(faces.begin(),faces.end());
        std::set<FaceId> reached;
        std::vector<FaceId> queue{faces.front()};
        while(!queue.empty()) {
            const auto current=queue.back();queue.pop_back();
            if(!reached.insert(current).second)continue;
            for(const auto neighbor:adjacency.at(current))
                if(atVertex.contains(neighbor)&&!reached.contains(neighbor))
                    queue.push_back(neighbor);
        }
        if(reached.size()!=atVertex.size())
            throw std::logic_error("R06-M01 disconnected vertex fan");
    }

    std::map<Edge,Side> openEdges;
    for(const auto& [edge,sides]:edges) {
        const auto componentId=owner.at(sides.front().face);
        auto& component=result.components[componentId];
        ++component.edges;
        if(sides.size()==1) {
            ++component.boundaryEdges;
            openEdges.emplace(edge,sides.front());
        }else if(owner.at(sides.back().face)!=componentId)
            throw std::logic_error("R06-M01 two sides of one edge have different owners");
    }
    std::set<Edge> consumedEdges;
    std::set<VertexId> loopVertices;
    for(std::size_t loopId=0;loopId<proposal.contours.size();++loopId) {
        const auto& loop=proposal.contours[loopId];
        const auto n=loop.vertexIds.size();
        if(n<3 || n!=loop.survivingFaces.size())
            throw std::logic_error("R06-M01 contour has inconsistent directed sides");
        std::set<VertexId> thisLoop;
        std::size_t componentId=result.components.size();
        for(std::size_t i=0;i<n;++i) {
            const auto a=loop.vertexIds[i],b=loop.vertexIds[(i+1)%n];
            if(!thisLoop.insert(a).second || !loopVertices.insert(a).second)
                throw std::logic_error("R06-M01 contour repeats or shares a boundary vertex");
            const auto key=canonical(a,b);
            const auto found=openEdges.find(key);
            if(found==openEdges.end() || !consumedEdges.insert(key).second ||
               found->second.face!=loop.survivingFaces[i] ||
               found->second.forward!=(a==key.a))
                throw std::logic_error("R06-M01 contour edge not an actual oriented R03 rim");
            const auto current=owner.at(found->second.face);
            if(componentId==result.components.size())componentId=current;
            else if(componentId!=current)
                throw std::logic_error("R06-M01 one contour crosses surface components");
        }
        result.components[componentId].contourIndices.push_back(loopId);
        ++result.boundaryLoops;
    }
    if(consumedEdges.size()!=openEdges.size() ||
       result.boundaryLoops!=cut.boundaryLoops)
        throw std::logic_error("R06-M01 missing or unowned cut boundary");
    for(auto& component:result.components) {
        component.closed=component.boundaryEdges==0;
        component.eulerCharacteristic=
            static_cast<std::int64_t>(component.vertices.size())-
            static_cast<std::int64_t>(component.edges)+
            static_cast<std::int64_t>(component.faces.size());
        if(component.closed!=component.contourIndices.empty())
            throw std::logic_error("R06-M01 component boundary/contour mismatch");
    }
    return result;
}
} // namespace mesher

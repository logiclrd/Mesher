#include "mesher/repair_inspection.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
RegionTopologyIndex::Edge canonical(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
PartitionRenderVertex marker(Vec3 p,float material) {
    return {float(p.x),float(p.y),float(p.z),0,0,1,material};
}
void triangle(std::vector<PartitionRenderVertex>& out,
              Vec3 a,Vec3 b,Vec3 c,float material) {
    out.push_back(marker(a,material));
    out.push_back(marker(b,material));
    out.push_back(marker(c,material));
}
void dot(std::vector<PartitionRenderVertex>& out,Vec3 c,double r) {
    const std::array<Vec3,6> points{{
        c+Vec3{r,0,0},c+Vec3{-r,0,0},
        c+Vec3{0,r,0},c+Vec3{0,-r,0},
        c+Vec3{0,0,r},c+Vec3{0,0,-r}
    }};
    for(unsigned i=0;i<4;++i) {
        const unsigned j=(i+1)%4;
        const auto a=points[i==0?0:i==1?2:i==2?1:3];
        const auto b=points[j==0?0:j==1?2:j==2?1:3];
        triangle(out,points[4],a,b,2.0f);
        triangle(out,points[5],b,a,2.0f);
    }
}
void rod(std::vector<PartitionRenderVertex>& out,Vec3 a,Vec3 b,double radius) {
    const auto delta=b-a;
    if(length(delta)<=1e-15)return;
    const auto axis=normalized(delta);
    const auto ref=std::abs(axis.z)<0.9?Vec3{0,0,1}:Vec3{0,1,0};
    const auto u=normalized(cross(axis,ref))*radius;
    const auto v=normalized(cross(axis,u))*radius;
    const std::array<Vec3,4> ring{{u,v,u*-1.0,v*-1.0}};
    for(unsigned i=0;i<4;++i) {
        const auto j=(i+1)%4;
        triangle(out,a+ring[i],a+ring[j],b+ring[i],3.0f);
        triangle(out,b+ring[i],a+ring[j],b+ring[j],3.0f);
    }
}
}

std::string RepairInspectionSession::inspect(const ProposedSculptStep& proposed) {
    if(frozen())return "R01 diagnostic view is frozen; relaunch to sculpt again";
    if(!proposed.candidate||!proposed.before)
        return "R01 diagnostic requires valid immutable before/candidate roots";
    if(proposed.candidate==proposed.before)return {};
    auto result=std::make_shared<RepairInspectionSnapshot>();
    result->candidate=proposed.candidate;
    const std::vector<VertexId> vertices(
        proposed.changedVertices.begin(),proposed.changedVertices.end());
    const std::vector<FaceId> faces(
        proposed.changedFaces.begin(),proposed.changedFaces.end());
    // Finite speculative work, NEVER a partial collision list.
    RegionTopologyIndex::CollisionQueryLimits limits;
    limits.maxSeedFaces=16384;
    limits.maxBoxTests=131072;
    limits.maxPartitionTests=131072;
    limits.maxCandidateTriangles=131072;
    limits.maxNarrowPhaseTests=65536;
    limits.maxReportedContacts=8192;
    if(vertices.size()>65536||faces.size()>65536)
        result->diagnostic="R01 changed-ID input budget exhausted";
    else {
        try {
            result->contacts=proposed.candidate->triangleIntersections(
                vertices,faces,&result->work,limits);
        }catch(const std::length_error& e) {
            result->diagnostic=std::string("R01 collision query budget exhausted: ")+e.what();
        }
    }
    if(result->contacts.empty()&&result->diagnostic.empty())return {};
    std::set<FaceId> markedFaces;
    std::set<VertexId> markedVertices;
    std::set<RegionTopologyIndex::Edge> markedEdges;
    for(const auto& contact:result->contacts) {
        markedFaces.insert(contact.first);
        markedFaces.insert(contact.second);
    }
    for(const auto face:markedFaces) {
        const auto f=proposed.candidate->maybeFace(face);
        if(!f)throw std::logic_error("R01 contact references missing candidate FaceId");
        for(unsigned i=0;i<3;++i) {
            markedVertices.insert(f->vertices[i]);
            markedEdges.insert(canonical(f->vertices[i],f->vertices[(i+1)%3]));
        }
    }
    result->affectedFaces.assign(markedFaces.begin(),markedFaces.end());
    result->incidentVertices.assign(markedVertices.begin(),markedVertices.end());
    result->incidentEdges.assign(markedEdges.begin(),markedEdges.end());
    {
        std::lock_guard lock(mutex_);
        if(frozen_)return "R01 diagnostic view is frozen; relaunch to sculpt again";
        snapshot_=std::move(result);
        frozen_.store(true,std::memory_order_release);
    }
    return "R01 diagnostic intersection detected; private candidate retained for read-only inspection";
}

std::shared_ptr<const RepairInspectionSnapshot>
RepairInspectionSession::frozenSnapshot() const {
    if(!frozen())return {};
    std::lock_guard lock(mutex_);
    return snapshot_;
}

std::vector<PartitionRenderVertex> buildRepairInspectionOverlay(
    const RepairInspectionSnapshot& snapshot) {
    std::vector<PartitionRenderVertex> overlay;
    if(!snapshot.candidate)return overlay;
    // Real incident edge lengths keep markers visible on both coarse
    // fixture geometry and ordinary higher-density sphere meshes.
    std::map<VertexId,double> radius;
    for(const auto vertex:snapshot.incidentVertices)
        radius.emplace(vertex,std::numeric_limits<double>::infinity());
    for(const auto edge:snapshot.incidentEdges) {
        const auto a=snapshot.candidate->vertex(edge.a).position;
        const auto b=snapshot.candidate->vertex(edge.b).position;
        const double distance=length(b-a);
        if(auto it=radius.find(edge.a);it!=radius.end())
            it->second=std::min(it->second,distance);
        if(auto it=radius.find(edge.b);it!=radius.end())
            it->second=std::min(it->second,distance);
    }
    const auto markerRadius=[&](VertexId id) {
        const auto it=radius.find(id);
        if(it==radius.end()||!std::isfinite(it->second))return .005;
        return std::clamp(it->second*.10,.004,.045);
    };
    overlay.reserve(snapshot.incidentVertices.size()*24+
                    snapshot.incidentEdges.size()*24);
    for(const auto edge:snapshot.incidentEdges)
        rod(overlay,snapshot.candidate->vertex(edge.a).position,
            snapshot.candidate->vertex(edge.b).position,
            std::min(markerRadius(edge.a),markerRadius(edge.b))*.34);
    for(const auto id:snapshot.incidentVertices)
        dot(overlay,snapshot.candidate->vertex(id).position,markerRadius(id));
    return overlay;
}

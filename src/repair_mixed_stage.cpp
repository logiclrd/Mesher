#include "mesher/repair_mixed_stage.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
using Edge=std::pair<VertexId,VertexId>;
Edge edge(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
double medianEdge(const RegionTopologyIndex& mesh,const RepairContour& loop) {
    std::vector<double> lengths;
    lengths.reserve(loop.vertexIds.size());
    for(std::size_t k=0;k<loop.vertexIds.size();++k) {
        const auto a=mesh.vertex(loop.vertexIds[k]).position;
        const auto b=mesh.vertex(loop.vertexIds[(k+1)%loop.vertexIds.size()]).position;
        lengths.push_back(length(a-b));
    }
    std::sort(lengths.begin(),lengths.end());
    return lengths[lengths.size()/2];
}
struct Side {bool forward{};bool inserted{};};
}

MixedClosureStaging stagePrivateMixedClosure(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const MixedClosureStageSettings& bounds) {
    if(!cut.index||!proposal.complete||!plan.ready()||
       cut.boundaryLoops!=proposal.contours.size()||
       cut.boundaryEdges!=proposal.boundaryEdges)
        throw std::logic_error("R06-M04A requires M03 ready and an exact R03 open cut");
    if(!bounds.maxLoops||!bounds.maxTrackedFaces||!bounds.maxNewTriangles||
       proposal.contours.size()>bounds.maxLoops)
        throw std::length_error("R06-M04A finite staging budget exceeded");
    if(plan.cappedContours.size()+2*plan.zipperPairs.size()!=
       proposal.contours.size())
        throw std::logic_error("R06-M04A every R03 directed contour must be used exactly once");
    MixedClosureStaging result;
    const auto& mesh=*cut.index;
    std::vector<bool> used(proposal.contours.size());
    std::set<FaceId> loadedFaces;
    const auto loadVertex=[&](VertexId id) {
        const auto& v=mesh.vertex(id);
        if(!std::isfinite(v.position.x)||!std::isfinite(v.position.y)||
           !std::isfinite(v.position.z))
            throw std::logic_error("R06-M04A nonfinite survivor position");
        result.positions.emplace(id,v.position);
        result.vertexOwners.emplace(id,v.owner);
    };
    const auto addFace=[&](MixedClosureFace rec) {
        if(result.faces.size()>=bounds.maxTrackedFaces)
            throw std::length_error("R06-M04A tracked-face budget exceeded");
        result.faces.push_back(rec);
        if(rec.kind==MixedClosureFaceKind::Cap)++result.capTriangles;
        if(rec.kind==MixedClosureFaceKind::Zipper)++result.zipperTriangles;
        if(result.capTriangles+result.zipperTriangles>bounds.maxNewTriangles)
            throw std::length_error("R06-M04A new-triangle budget exceeded");
    };
    for(const auto& loop:proposal.contours) {
        if(loop.vertexIds.size()<3||
           loop.vertexIds.size()>bounds.zipper.maxContourVertices||
           loop.vertexIds.size()!=loop.survivingFaces.size())
            throw std::length_error("R06-M04A invalid contour length");
        for(const auto id:loop.vertexIds)loadVertex(id);
        for(const auto id:loop.survivingFaces) {
            if(!loadedFaces.insert(id).second)continue;
            const auto& face=mesh.face(id);
            for(const auto vertex:face.vertices)loadVertex(vertex);
            addFace({id,face.owner,face.vertices,
                     MixedClosureFaceKind::Survivor,0});
        }
    }
    const auto consume=[&](std::size_t loop) {
        if(loop>=used.size()||used[loop])
            throw std::logic_error("R06-M04A duplicate or out-of-range selected contour");
        used[loop]=true;
    };
    for(const auto id:plan.cappedContours) {
        consume(id);
        const auto& loop=proposal.contours[id];
        const auto h=medianEdge(mesh,loop);
        if(!(h>0)||!std::isfinite(h))
            throw std::logic_error("R06-M04A degenerate cap local edge scale");
        const auto owner=mesh.face(loop.survivingFaces.front()).owner;
        for(const auto triangle:triangulatePrivateCapDisk(
                loop,result.positions,h))
            addFace({0,owner,triangle,MixedClosureFaceKind::Cap,h});
        ++result.capLoops;
    }
    for(const auto& pair:plan.zipperPairs) {
        consume(pair.first);
        consume(pair.second);
        const auto& a=proposal.contours.at(pair.first);
        const auto& b=proposal.contours.at(pair.second);
        const auto h=std::min(medianEdge(mesh,a),medianEdge(mesh,b));
        if(!(h>0)||!std::isfinite(h))
            throw std::logic_error("R06-M04A degenerate zipper local edge scale");
        const auto owner=mesh.face(a.survivingFaces.front()).owner;
        for(const auto triangle:triangulatePrivateZipperStrip(
                a.vertexIds,b.vertexIds,result.positions,bounds.zipper))
            addFace({0,owner,triangle,MixedClosureFaceKind::Zipper,h});
        ++result.zipperPairs;
    }
    if(std::any_of(used.begin(),used.end(),[](bool v){return !v;}))
        throw std::logic_error("R06-M04A missing directed R03 contour");
    // The region ledger is part of the plan's safety contract, not merely a
    // diagnostic. Reject a forged global choice crossing independent regions.
    std::set<std::size_t> regionCaps;
    std::set<std::pair<std::size_t,std::size_t>> regionZippers;
    for(const auto& region:plan.regions) {
        for(const auto cap:region.caps)
            if(!regionCaps.insert(cap).second)
                throw std::logic_error("R06-M04A duplicated regional cap");
        for(const auto& pair:region.zippers) {
            auto a=pair.first,b=pair.second;
            if(a>b)std::swap(a,b);
            if(!regionZippers.insert({a,b}).second)
                throw std::logic_error("R06-M04A duplicated regional zipper");
        }
    }
    if(regionCaps!=std::set<std::size_t>(
         plan.cappedContours.begin(),plan.cappedContours.end()))
        throw std::logic_error("R06-M04A global caps disagree with regional selections");
    std::set<std::pair<std::size_t,std::size_t>> actualPairs;
    for(const auto& pair:plan.zipperPairs) {
        auto a=pair.first,b=pair.second;
        if(a>b)std::swap(a,b);
        actualPairs.insert({a,b});
    }
    if(regionZippers!=actualPairs)
        throw std::logic_error("R06-M04A cross-region zipper not authorized");

    std::map<Edge,std::vector<Side>> stagedEdges;
    for(const auto& face:result.faces) {
        for(unsigned i=0;i<3;++i) {
            const auto a=face.vertices[i],b=face.vertices[(i+1)%3];
            if(a==b)throw std::logic_error("R06-M04A degenerate staged edge");
            auto& sides=stagedEdges[edge(a,b)];
            sides.push_back({a<b,face.kind!=MixedClosureFaceKind::Survivor});
            if(sides.size()>2)
                throw std::logic_error("R06-M04A staged edge has three or more faces");
        }
    }
    std::set<Edge> rimEdges;
    for(const auto& loop:proposal.contours)
        for(std::size_t i=0;i<loop.vertexIds.size();++i)
            rimEdges.insert(edge(loop.vertexIds[i],
                                 loop.vertexIds[(i+1)%loop.vertexIds.size()]));
    for(const auto& [key,sides]:stagedEdges) {
        if(sides.size()==2&&sides[0].forward==sides[1].forward)
            throw std::logic_error("R06-M04A staged edges have inconsistent winding");
        if(rimEdges.contains(key)) {
            if(sides.size()!=2||sides[0].inserted==sides[1].inserted)
                throw std::logic_error("R06-M04A did not close an actual rim with one inserted opposite face");
            ++result.boundarySidesCovered;
        }else if(sides.size()==1&&sides.front().inserted)
            throw std::logic_error("R06-M04A generated an open seam/cap interior edge");
    }
    if(result.boundarySidesCovered!=cut.boundaryEdges)
        throw std::logic_error("R06-M04A no exact one-to-one rim coverage");
    return result;
}
} // namespace mesher

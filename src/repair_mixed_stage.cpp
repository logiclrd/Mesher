#include "mesher/repair_mixed_stage.hpp"
#include "mesher/repair_cap_spatial.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace mesher {
namespace {
using Edge=std::pair<VertexId,VertexId>;
Edge edge(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
double medianEdge(const MixedClosureSourceView& mesh,const RepairContour& loop) {
    std::vector<double> lengths;
    lengths.reserve(loop.vertexIds.size());
    for(std::size_t k=0;k<loop.vertexIds.size();++k) {
        const auto a=mesh.position(loop.vertexIds[k]);
        const auto b=mesh.position(loop.vertexIds[(k+1)%loop.vertexIds.size()]);
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
    MixedClosureStaging result(cut.index,proposal.contours);
    result.setOperationObserver(bounds.operationObserver);
    const auto& mesh=*cut.index;
    std::vector<bool> used(proposal.contours.size());
    std::set<FaceId> loadedFaces;
    const auto loadVertex=[&](VertexId id) {
        const auto p=result.sourceView()->position(id);
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            throw std::logic_error("R06-M04A nonfinite survivor position");
        // Original survivor registration belongs to the staging editor.
        result.trackOriginalVertex(id);
    };
    // Generators describe desired faces; only the staging editor may
    // introduce their topology. Repeated cyclic face requests are no-ops.
    const auto addFace=[&](MixedClosureFace rec) {
        if(rec.stableId)
            throw std::logic_error("R06-M15C generator cannot create a source face");
        const auto t=rec.triangle();
        (void)result.ensureFace(t[0],t[1],t[2],rec.kind,
            rec.owner,rec.targetEdge,
            bounds.maxTrackedFaces,bounds.maxNewTriangles);
    };
    for(const auto& loop:proposal.contours) {
        if(loop.vertexIds.size()<3||
           loop.vertexIds.size()>bounds.zipper.maxContourVertices||
           loop.vertexIds.size()!=loop.survivingFaces.size())
            throw std::length_error("R06-M04A invalid contour length");
        for(const auto id:loop.vertexIds)loadVertex(id);
        for(const auto id:loop.survivingFaces) {
            if(!loadedFaces.insert(id).second)continue;
            // Every original R03 face becomes a handle through the
            // staging editor, including survivor geometry outside the
            // initial write halo imported by later edge operations.
            (void)result.trackSurvivor(id,bounds.maxTrackedFaces);
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
        const auto h=medianEdge(*result.sourceView(),loop);
        if(!(h>0)||!std::isfinite(h))
            throw std::logic_error("R06-M04A degenerate cap local edge scale");
        const auto owner=result.sourceView()->face(loop.survivingFaces.front()).owner;
        bool forceSpatial=false;
        if(bounds.requireReflectedGeometry) {
            bool positive=false,negative=false;
            for(const auto vertex:loop.vertexIds) {
                const auto x=result.positions().at(vertex).x;
                positive=positive||x>0;
                negative=negative||x<0;
            }
            forceSpatial=positive&&negative;
        }
        std::string projectedReason="mirror-required crossing-plane cap";
        if(!forceSpatial) {
            try {
                for(const auto triangle:triangulatePrivateCapDisk(
                        loop,result.positions().materialize(),h))
                    addFace({0,owner,triangle,MixedClosureFaceKind::Cap,h});
            }catch(const std::logic_error& e) {
                forceSpatial=true;
                projectedReason=e.what();
            }
        }
        if(forceSpatial) {
            // The original R05 projected-ear builder remains unchanged and
            // first-choice. For genuinely folded contours, advance a fully
            // spatial oriented front, add synthetic private interior vertices
            // and fair by fixed-rim normal-guided Poisson. M04B later
            // allocates permanent IDs and certifies true mesh embedding.
            std::vector<Vec3> ordered;
            std::vector<Vec3> guidance;
            const auto n=loop.vertexIds.size();
            ordered.reserve(n);
            guidance.reserve(n);
            for(std::size_t k=0;k<n;++k) {
                const auto r=n-k-1;
                ordered.push_back(result.positions().at(loop.vertexIds[r]));
                const auto& adjacent=result.sourceView()->face(loop.survivingFaces.at(r));
                const auto& v=adjacent.vertices;
                const auto normal=cross(
                    result.positions().at(v[1])-result.positions().at(v[0]),
                    result.positions().at(v[2])-result.positions().at(v[0]));
                guidance.push_back(normal);
            }
            SpatialCapPatch patch;
            try {
                SpatialCapSettings spatial;
                spatial.repairTangentialOvershoot=bounds.optimizeSpatialFront;
                spatial.desiredMinimumQuality=bounds.targetInitialCapQuality;
                spatial.requireMirrorPlane=bounds.requireReflectedGeometry&&
                    std::any_of(ordered.begin(),ordered.end(),
                                [](Vec3 v){return v.x<0;})&&
                    std::any_of(ordered.begin(),ordered.end(),
                                [](Vec3 v){return v.x>0;});
                patch=buildSpatialCapFrontPoisson(ordered,guidance,spatial);
            }catch(const std::exception& failed) {
                throw std::logic_error(
                    std::string("R06-M06C projected cap unavailable: ")+
                    projectedReason+"; spatial front refused: "+
                    failed.what());
            }
            std::vector<VertexId> ids;
            ids.reserve(patch.positions.size());
            for(std::size_t k=0;k<n;++k)
                ids.push_back(loop.vertexIds[n-k-1]);
            for(std::size_t k=n;k<patch.positions.size();++k) {
                if(result.provisionalSpatialVertices().size()>=
                   bounds.maxNewTriangles)
                    throw std::length_error(
                        "R06-M06C spatial provisional vertex budget exceeded");
                const auto provisional=std::numeric_limits<VertexId>::max()-
                    static_cast<VertexId>(result.provisionalSpatialVertices().size());
                if(mesh.maybeVertex(provisional)||
                   result.positions().contains(provisional))
                    throw std::logic_error(
                        "R06-M06C temporary ID collides with immutable vertex");
                result.stageProvisionalVertex(provisional,
                    patch.positions[k],owner,bounds.maxNewTriangles);
                ids.push_back(provisional);
            }
            for(const auto& t:patch.faces)
                addFace({0,owner,{ids[t[0]],ids[t[1]],ids[t[2]]},
                         MixedClosureFaceKind::Cap,h});
            result.usedSpatialCapFallback=true;
            ++result.spatialCapLoops;
            if(patch.usedMirroredRibbonFallback)++result.mirroredRibbonLoops;
            result.spatialPoissonIterations+=patch.poissonIterations;
            result.minimumSpatialInitialQuality=std::min(
                result.minimumSpatialInitialQuality,patch.minimumQuality);
        }
        ++result.capLoops;
    }
    for(const auto& pair:plan.zipperPairs) {
        consume(pair.first);
        consume(pair.second);
        const auto& a=proposal.contours.at(pair.first);
        const auto& b=proposal.contours.at(pair.second);
        const auto h=std::min(medianEdge(*result.sourceView(),a),medianEdge(*result.sourceView(),b));
        if(!(h>0)||!std::isfinite(h))
            throw std::logic_error("R06-M04A degenerate zipper local edge scale");
        const auto owner=result.sourceView()->face(a.survivingFaces.front()).owner;
        const auto otherOwner=result.sourceView()->face(b.survivingFaces.front()).owner;
        // Exact opposite-side reflected loops require a seam, not a DP
        // shortcut which crosses X=0 with unpaired triangle interiors.
        const auto entirely=[&](const RepairContour& loop,int sign) {
            return std::all_of(loop.vertexIds.begin(),loop.vertexIds.end(),
                [&](VertexId v){return result.positions().at(v).x*sign>0;});
        };
        const bool mirroredPair=bounds.requireReflectedGeometry&&
            ((entirely(a,-1)&&entirely(b,+1))||
             (entirely(a,+1)&&entirely(b,-1)));
        if(mirroredPair) {
            if(a.vertexIds.size()!=b.vertexIds.size())
                throw std::logic_error(
                    "R06-M06C4c mirrored zipper rim counts disagree");
            const auto n=a.vertexIds.size();
            std::vector<VertexId> reflected;
            std::vector<std::size_t> bIndices;
            reflected.reserve(n);
            bIndices.reserve(n);
            std::set<std::size_t> foundIndices;
            for(const auto first:a.vertexIds) {
                const auto p=result.positions().at(first);
                std::size_t found=n;
                for(std::size_t k=0;k<n;++k) {
                    const auto q=result.positions().at(b.vertexIds[k]);
                    if(q.x!=-p.x||q.y!=p.y||q.z!=p.z)continue;
                    if(found!=n)
                        throw std::logic_error(
                            "R06-M06C4c duplicate exact reflected zipper vertex");
                    found=k;
                }
                if(found==n||!foundIndices.insert(found).second)
                    throw std::logic_error(
                        "R06-M06C4c mirrored zipper lacks unique reflected contour orbit");
                reflected.push_back(b.vertexIds[found]);
                bIndices.push_back(found);
            }
            for(std::size_t i=0;i<n;++i)
                if(bIndices[(i+1)%n]!=(bIndices[i]+n-1)%n)
                    throw std::logic_error(
                        "R06-M06C4c mirrored zipper contour winding not reversed");
            // M04B grants true immutable IDs to the new self-mirrored
            // seam vertices. No original IDs are welded by position.
            std::vector<VertexId> seam;
            seam.reserve(n);
            for(std::size_t i=0;i<n;++i) {
                if(result.provisionalSpatialVertices().size()>=
                   bounds.maxNewTriangles)
                    throw std::length_error(
                        "R06-M06C4c mirrored zipper seam vertex budget");
                const auto p=result.positions().at(a.vertexIds[i]);
                const VertexId id=std::numeric_limits<VertexId>::max()-
                    static_cast<VertexId>(result.provisionalSpatialVertices().size());
                if(mesh.maybeVertex(id)||result.positions().contains(id))
                    throw std::logic_error(
                        "R06-M06C4c provisional mirror seam vertex ID collision");
                result.stageProvisionalVertex(id,
                    Vec3{0,p.y,p.z},owner,bounds.maxNewTriangles);
                seam.push_back(id);
            }
            for(std::size_t i=0;i<n;++i) {
                const auto j=(i+1)%n;
                const auto a0=a.vertexIds[i],a1=a.vertexIds[j],
                           b0=reflected[i],b1=reflected[j],
                           s0=seam[i],s1=seam[j];
                addFace({0,owner,{a1,a0,s0},
                         MixedClosureFaceKind::Zipper,h});
                addFace({0,owner,{a1,s0,s1},
                         MixedClosureFaceKind::Zipper,h});
                addFace({0,otherOwner,{b1,s0,b0},
                         MixedClosureFaceKind::Zipper,h});
                addFace({0,otherOwner,{b1,s1,s0},
                         MixedClosureFaceKind::Zipper,h});
            }
            result.usedMirroredSeamZipper=true;
            ++result.mirroredSeamZipperPairs;
            result.mirroredSeamZipperVertices+=n;
        }else {
            for(const auto triangle:triangulatePrivateZipperStrip(
                    a.vertexIds,b.vertexIds,result.positions().materialize(),bounds.zipper))
                addFace({0,owner,triangle,MixedClosureFaceKind::Zipper,h});
        }
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
    for(const auto& face:result.faces()) {
        const auto vertices=face.triangle();
        for(unsigned i=0;i<3;++i) {
            const auto a=vertices[i],b=vertices[(i+1)%3];
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

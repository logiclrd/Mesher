#include "mesher/repair_mixed_stage.hpp"
#include "mesher/repair_cap_spatial.hpp"
#include "mesher/repair_zipper_front.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
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
// M22B3: local O(F log F) diagnostic, independent of whole-mesh source
// validation. Every completed candidate is measured before staging.
template<class Id,class Lookup>
void logCapCandidate(const char* method,std::size_t loop,
                     std::size_t boundaryVertices,
                     const std::vector<std::array<Id,3>>& faces,
                     Lookup&& position,std::size_t iterations,
                     double residual,const char* status) {
    std::set<Id> vertices;
    std::set<std::pair<Id,Id>> edges;
    std::vector<double> qValues,edgeLengths;
    qValues.reserve(faces.size());
    std::size_t underQuality=0,zeroArea=0;
    for(const auto& f:faces) {
        const auto a=position(f[0]),b=position(f[1]),d=position(f[2]);
        vertices.insert(f.begin(),f.end());
        const auto sum=dot(a-b,a-b)+dot(b-d,b-d)+dot(d-a,d-a);
        const auto q=sum>0?
            2*std::sqrt(3.)*length(cross(b-a,d-a))/sum:0.;
        qValues.push_back(q);
        if(q<.10)++underQuality;
        if(!(q>1e-8)||!std::isfinite(q))++zeroArea;
        for(unsigned k=0;k<3;++k) {
            const auto x=f[k],y=f[(k+1)%3];
            const auto pair=std::minmax(x,y);
            if(edges.emplace(pair.first,pair.second).second)
                edgeLengths.push_back(length(position(x)-position(y)));
        }
    }
    std::map<std::pair<Id,Id>,unsigned> incidence;
    for(const auto& f:faces)
        for(unsigned k=0;k<3;++k) {
            const auto pair=std::minmax(f[k],f[(k+1)%3]);
            ++incidence[{pair.first,pair.second}];
        }
    std::size_t boundaryEdges=0,nonmanifoldEdges=0;
    for(const auto& [edge,count]:incidence) {
        (void)edge;
        if(count==1)++boundaryEdges;
        if(count>2)++nonmanifoldEdges;
    }
    std::sort(qValues.begin(),qValues.end());
    std::sort(edgeLengths.begin(),edgeLengths.end());
    const auto percentile=[](const std::vector<double>& v,double fraction) {
        if(v.empty())return 0.;
        const auto index=static_cast<std::size_t>(fraction*(v.size()-1));
        return v[index];
    };
    const auto decileCount=(qValues.size()+9)/10;
    double lowSum=0,highSum=0,mean=0;
    for(std::size_t i=0;i<qValues.size();++i) {
        mean+=qValues[i];
        if(i<decileCount)lowSum+=qValues[i];
        if(i>=qValues.size()-decileCount)highSum+=qValues[i];
    }
    if(!qValues.empty())mean/=static_cast<double>(qValues.size());
    const auto decile=static_cast<double>(decileCount);
    std::cerr<<std::setprecision(10)
             <<"[repair-r06-cap-metrics] method="<<method
             <<" loop="<<loop<<" status="<<status
             <<" fixed_boundary_vertices="<<boundaryVertices
             <<" vertices="<<vertices.size()
             <<" generated_vertices="<<(vertices.size()>boundaryVertices?
                 vertices.size()-boundaryVertices:0)
             <<" edges="<<edges.size()
             <<" faces="<<faces.size()
             <<" boundary_edges="<<boundaryEdges
             <<" overincident_edges="<<nonmanifoldEdges
             <<" euler="<<(static_cast<std::int64_t>(vertices.size())-
                 static_cast<std::int64_t>(edges.size())+
                 static_cast<std::int64_t>(faces.size()))
             <<" q_min="<<percentile(qValues,0)
             <<" q_p10="<<percentile(qValues,.10)
             <<" q_median="<<percentile(qValues,.5)
             <<" q_p90="<<percentile(qValues,.9)
             <<" q_max="<<percentile(qValues,1)
             <<" q_mean="<<mean
             <<" bottom_decile_faces="<<decileCount
             <<" bottom_decile_mean_q="<<(decileCount?lowSum/decile:0.)
             <<" top_decile_faces="<<decileCount
             <<" top_decile_mean_q="<<(decileCount?highSum/decile:0.)
             <<" q_below_0_10="<<underQuality
             <<" degenerate_faces="<<zeroArea
             <<" edge_min="<<percentile(edgeLengths,0)
             <<" edge_median="<<percentile(edgeLengths,.5)
             <<" edge_max="<<percentile(edgeLengths,1)
             <<" poisson_iterations="<<iterations
             <<" poisson_residual="<<residual
             <<" source_certified=0\n";
}
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
    std::set<std::size_t> mirroredCapsAlreadyGenerated;
    for(const auto id:plan.cappedContours) {
        if(used.at(id)) {
            if(mirroredCapsAlreadyGenerated.erase(id))continue;
            throw std::logic_error("R06-M04A duplicate selected cap contour");
        }
        consume(id);
        const auto& loop=proposal.contours[id];
        // Independent AFM runs on disjoint reflected rims can choose
        // different triangulations/interior positions. Generate only ONE
        // side of an exact two-disk orbit, then reflect its ENTIRE patch.
        // Source rim VertexIds remain distinct; only NEW provisional IDs
        // are introduced for the reflected interior.
        std::size_t mirrorCap=proposal.contours.size();
        std::vector<std::size_t> mirroredRimIndexes;
        if(bounds.requireReflectedGeometry) {
            int sign=0;
            for(const auto v:loop.vertexIds) {
                const auto x=result.positions().at(v).x;
                if(x==0) {sign=0;break;}
                const int next=x>0?1:-1;
                if(sign&&sign!=next) {sign=0;break;}
                sign=next;
            }
            if(sign) {
                for(const auto other:plan.cappedContours) {
                    if(other==id)continue;
                    const auto& candidate=proposal.contours.at(other);
                    const auto n=loop.vertexIds.size();
                    if(candidate.vertexIds.size()!=n)continue;
                    std::vector<std::size_t> indices;
                    std::set<std::size_t> seen;
                    bool matches=true;
                    for(const auto sourceVertex:loop.vertexIds) {
                        const auto p=result.positions().at(sourceVertex);
                        std::size_t located=n;
                        for(std::size_t j=0;j<n;++j) {
                            const auto q=result.positions().at(candidate.vertexIds[j]);
                            if(q.x==-p.x&&q.y==p.y&&q.z==p.z) {
                                if(located!=n) {matches=false;break;}
                                located=j;
                            }
                        }
                        if(!matches||located==n||!seen.insert(located).second) {
                            matches=false;break;
                        }
                        indices.push_back(located);
                    }
                    if(!matches)continue;
                    // A reflection reverses directed contour winding.
                    // Accept any cyclic start vertex, not only ordinal 0.
                    for(std::size_t k=0;k<n;++k)
                        if(indices[(k+1)%n]!=(indices[k]+n-1)%n)
                            matches=false;
                    if(!matches)continue;
                    if(mirrorCap!=proposal.contours.size())
                        throw std::logic_error(
                            "R06-M06C mirrored cap rim has multiple exact partner contours");
                    mirrorCap=other;
                    mirroredRimIndexes=std::move(indices);
                }
                if(mirrorCap==proposal.contours.size())
                    throw std::logic_error(
                        "R06-M06C one-sided cap lacks a separate exact reflected cap rim");
                if(used[mirrorCap])
                    throw std::logic_error(
                        "R06-M06C reflected cap rim already consumed by another closure");
                consume(mirrorCap);
                mirroredCapsAlreadyGenerated.insert(mirrorCap);
            }
        }
        const auto h=medianEdge(*result.sourceView(),loop);
        if(!(h>0)||!std::isfinite(h))
            throw std::logic_error("R06-M04A degenerate cap local edge scale");
        const auto owner=result.sourceView()->face(loop.survivingFaces.front()).owner;
        // AFM is the default R06 cap generator. Projected ear clipping
        // is a diagnostic/compatibility first-pass only when requested.
        bool forceSpatial=!bounds.tryProjectedEarClipping ||
            mirrorCap!=proposal.contours.size();
        if(bounds.requireReflectedGeometry) {
            bool positive=false,negative=false;
            for(const auto vertex:loop.vertexIds) {
                const auto x=result.positions().at(vertex).x;
                positive=positive||x>0;
                negative=negative||x<0;
            }
            forceSpatial=forceSpatial||(positive&&negative);
        }
        std::string projectedReason=bounds.tryProjectedEarClipping?
            "mirror-required crossing-plane cap":"projected-ear disabled by default";
        if(!forceSpatial) {
            std::vector<PrivateRepairTriangle> triangles;
            try {
                triangles=triangulatePrivateCapDisk(
                    loop,result.positions().materialize(),h);
                logCapCandidate("projected-ear",id,loop.vertexIds.size(),
                    triangles,[&](VertexId vertex) {
                        return result.positions().at(vertex);
                    },0,0.,"CANDIDATE_READY");
            }catch(const std::logic_error& e) {
                forceSpatial=true;
                projectedReason=e.what();
                std::cerr<<"[repair-r06-cap-metrics] method=projected-ear"
                         <<" loop="<<id
                         <<" status=GENERATOR_REFUSED"
                         <<" metrics=unavailable"
                         <<" reason="<<e.what()
                         <<" next=advancing-front\n";
            }
            // Any failure to STAGE a generated ear is not an ordinary
            // projection failure. The outer atomic M04 retry must start
            // again from the immutable R03 source, with AFM enabled.
            if(!forceSpatial)
                for(const auto& triangle:triangles)
                    addFace({0,owner,triangle,MixedClosureFaceKind::Cap,h});
        }else if(bounds.tryProjectedEarClipping) {
            std::cerr<<"[repair-r06-cap-metrics] method=projected-ear"
                     <<" loop="<<id<<" status=SKIPPED"
                     <<" reason=mirror_requires_spatial_cap"
                     <<" next=advancing-front\n";
        }
        if(forceSpatial) {
            // AFM is the production spatial cap generator. It creates
            // provisional interior vertices, then settles them with the
            // topology-independent, fixed-rim graph Poisson solver. M04B
            // allocates stable IDs and certifies full mesh embedding.
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
                spatial.useAdvancingFront=true;
                spatial.desiredMinimumQuality=bounds.targetInitialCapQuality;
                spatial.requireMirrorPlane=bounds.requireReflectedGeometry&&
                    std::any_of(ordered.begin(),ordered.end(),
                                [](Vec3 v){return v.x<0;})&&
                    std::any_of(ordered.begin(),ordered.end(),
                                [](Vec3 v){return v.x>0;});
                patch=buildSpatialCapFrontPoisson(
                    ordered,guidance,spatial);
                logCapCandidate("advancing-front",id,n,
                    patch.faces,[&](std::uint32_t vertex) {
                        return patch.positions.at(vertex);
                    },patch.poissonIterations,patch.poissonResidual,
                    "CANDIDATE_READY");
            }catch(const std::exception& failed) {
                std::cerr<<"[repair-r06-cap-metrics] method=advancing-front"
                         <<" loop="<<id<<" status=GENERATOR_REFUSED"
                         <<" metrics=unavailable reason="<<failed.what()
                         <<" prior_projected_ear="<<projectedReason
                         <<" source_certified=0\n";
                throw std::logic_error(
                    std::string("R06-M22B advancing-front cap unavailable: ")+
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
            if(mirrorCap!=proposal.contours.size()) {
                const auto& reflectedLoop=proposal.contours[mirrorCap];
                const auto reflectedOwner=result.sourceView()->face(
                    reflectedLoop.survivingFaces.front()).owner;
                std::vector<VertexId> reflectedIds;
                reflectedIds.reserve(patch.positions.size());
                // The AFM uses reversed directed rim order.
                for(std::size_t k=0;k<n;++k)
                    reflectedIds.push_back(reflectedLoop.vertexIds.at(
                        mirroredRimIndexes.at(n-k-1)));
                for(std::size_t k=n;k<patch.positions.size();++k) {
                    if(result.provisionalSpatialVertices().size()>=
                       bounds.maxNewTriangles)
                        throw std::length_error(
                            "R06-M06C mirrored cap provisional vertex budget");
                    const auto p=patch.positions[k];
                    const auto provisional=std::numeric_limits<VertexId>::max()-
                        static_cast<VertexId>(result.provisionalSpatialVertices().size());
                    if(mesh.maybeVertex(provisional)||
                       result.positions().contains(provisional))
                        throw std::logic_error(
                            "R06-M06C mirrored provisional vertex ID collision");
                    result.stageProvisionalVertex(
                        provisional,{-p.x,p.y,p.z},
                        reflectedOwner,bounds.maxNewTriangles);
                    reflectedIds.push_back(provisional);
                }
                for(const auto& t:patch.faces)
                    addFace({0,reflectedOwner,
                             {reflectedIds[t[0]],reflectedIds[t[2]],
                              reflectedIds[t[1]]},
                             MixedClosureFaceKind::Cap,h});
                logCapCandidate("mirrored-advancing-front",mirrorCap,
                    reflectedLoop.vertexIds.size(),patch.faces,
                    [&](std::uint32_t vertex) {
                        const auto p=patch.positions.at(vertex);
                        return Vec3{-p.x,p.y,p.z};
                    },patch.poissonIterations,patch.poissonResidual,
                    "CANDIDATE_READY");
                ++result.spatialCapLoops;
                ++result.capLoops;
            }
            result.usedSpatialCapFallback=true;
            ++result.spatialCapLoops;
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
        if(bounds.useUnifiedZipper) {
            const auto toCapFacingRim=[&](const RepairContour& loop) {
                UnifiedZipperRim output;
                const auto n=loop.vertexIds.size();
                output.ids.reserve(n);
                output.positions.reserve(n);
                output.outgoingNormals.reserve(n);
                for(std::size_t k=0;k<n;++k) {
                    const auto r=n-1-k;
                    const auto id=loop.vertexIds[r];
                    output.ids.push_back(id);
                    output.positions.push_back(result.positions().at(id));
                    const auto face=result.sourceView()->face(
                        loop.survivingFaces.at(r));
                    const auto& v=face.vertices;
                    output.outgoingNormals.push_back(cross(
                        result.positions().at(v[1])-result.positions().at(v[0]),
                        result.positions().at(v[2])-result.positions().at(v[0])));
                }
                return output;
            };
            SpatialCapSettings spatial;
            spatial.useAdvancingFront=true;
            spatial.maxBoundaryVertices=std::min<std::size_t>(
                512,bounds.zipper.maxContourVertices*2+2);
            spatial.maxInteriorVertices=bounds.maxNewTriangles;
            spatial.maxFaces=bounds.maxNewTriangles;
            spatial.maxIntersectionTests=50000;
            spatial.desiredMinimumQuality=.10;
            const auto patch=buildUnifiedZipperFront(
                toCapFacingRim(a),toCapFacingRim(b),
                mirroredPair,spatial);
            std::vector<VertexId> map;
            map.reserve(patch.positions.size());
            for(std::size_t k=0;k<patch.positions.size();++k) {
                if(k<patch.boundaryIds.size()&&patch.boundaryIds[k]) {
                    const auto id=patch.boundaryIds[k];
                    const auto source=result.positions().at(id);
                    const auto p=patch.positions[k];
                    if(source.x!=p.x||source.y!=p.y||source.z!=p.z)
                        throw std::logic_error(
                            "R06-M23B changed original R03 rim coordinates");
                    map.push_back(id);
                }else {
                    const VertexId id=std::numeric_limits<VertexId>::max()-
                        static_cast<VertexId>(
                            result.provisionalSpatialVertices().size());
                    if(mesh.maybeVertex(id)||result.positions().contains(id))
                        throw std::logic_error(
                            "R06-M23B provisional zipper ID collision");
                    result.stageProvisionalVertex(
                        id,patch.positions[k],owner,bounds.maxNewTriangles);
                    map.push_back(id);
                }
            }
            // M23B placement provenance: step replay runs AFTER AFM and
            // Poisson, so its ENSUREFACE arguments contain provisional mesh
            // IDs, not AFM's patch-local indices. Emit this exact mapping
            // only for the accepted bridge/cap patch. Each trace also keeps
            // the original supporting triangles at proposal time, allowing
            // comparisons before and after their vertices move in Poisson.
            const auto vecText=[](Vec3 p) {
                std::ostringstream o;
                o<<std::setprecision(17)
                 <<p.x<<","<<p.y<<","<<p.z;
                return o.str();
            };
            const auto signedPlaneDistance=[](
                Vec3 p,const std::array<Vec3,3>& tri) {
                const auto nFace=cross(tri[1]-tri[0],tri[2]-tri[0]);
                const auto magnitude=length(nFace);
                if(!(magnitude>1e-14))
                    return std::numeric_limits<double>::quiet_NaN();
                return dot(p-tri[0],nFace)/magnitude;
            };
            if(bounds.emitTrace) {
            for(const auto& trace:patch.placementTrace) {
                const auto i=trace.patchVertex;
                if(i>=map.size()||
                   trace.poissonPosition.x!=patch.positions[i].x||
                   trace.poissonPosition.y!=patch.positions[i].y||
                   trace.poissonPosition.z!=patch.positions[i].z)
                    throw std::logic_error(
                        "R06-M23B trace/accepted vertex index mismatch");
                std::ostringstream o;
                o<<std::setprecision(17)
                 <<"[repair-r06-m23b-placement]"
                 <<" pair_a="<<pair.first<<" pair_b="<<pair.second
                 <<" patch_vertex="<<i<<" provisional="<<map[i]
                 <<" front="<<map.at(trace.frontVertex)
                 <<" prev="<<map.at(trace.frontPrev)
                 <<" next="<<map.at(trace.frontNext)
                 <<" operation_face_start="<<(patch.bridgeFaceCount+trace.frontOperation)
                 <<" proposal_ordinal="<<trace.proposalOrdinal
                 <<" side="<<static_cast<unsigned>(trace.originatingSide)
                 <<" raw="<<vecText(trace.rawProposal)
                 <<" tangent="<<vecText(trace.tangentProposal)
                 <<" gravity="<<vecText(trace.gravityProposal)
                 <<" staged="<<vecText(trace.stagedProposal)
                 <<" poisson="<<vecText(trace.poissonPosition)
                 <<" local_normal="<<vecText(trace.tangentNormal)
                 <<" gravity_shift="<<length(
                     trace.gravityProposal-trace.tangentProposal)
                 <<" poisson_shift="<<length(
                     trace.poissonPosition-trace.stagedProposal)
                 <<" afm_support_faces="<<trace.supportCount;
                for(std::size_t k=0;k<trace.supportCount;++k) {
                    const auto ids=trace.supportFaces[k];
                    const auto& initial=trace.supportAtProposal[k];
                    const std::array<Vec3,3> final{
                        patch.positions.at(ids[0]),
                        patch.positions.at(ids[1]),
                        patch.positions.at(ids[2])};
                    o<<" support"<<k<<"_patch="
                     <<ids[0]<<","<<ids[1]<<","<<ids[2]
                     <<" support"<<k<<"_mesh="
                     <<map.at(ids[0])<<","<<map.at(ids[1])<<","
                     <<map.at(ids[2])
                     <<" support"<<k<<"_raw_dist="
                     <<signedPlaneDistance(trace.rawProposal,initial)
                     <<" support"<<k<<"_tangent_dist="
                     <<signedPlaneDistance(trace.tangentProposal,initial)
                     <<" support"<<k<<"_gravity_dist="
                     <<signedPlaneDistance(trace.gravityProposal,initial)
                     <<" support"<<k<<"_poisson_dist_old_plane="
                     <<signedPlaneDistance(trace.poissonPosition,initial)
                     <<" support"<<k<<"_poisson_dist_final_plane="
                     <<signedPlaneDistance(trace.poissonPosition,final);
                }
                // The first bridge seed faces live outside the AFM
                // constructor. Explicitly test them too, so a replay
                // vertex attached to an initial D/K triangle can still
                // be measured against that real adjacent face.
                std::size_t seedSupports=0;
                for(std::size_t faceIndex=0;
                    faceIndex<patch.bridgeFaceCount;++faceIndex) {
                    const auto ids=patch.faces.at(faceIndex);
                    const auto hasEdge=[&](std::uint32_t a,
                                          std::uint32_t b) {
                        bool hasA=false,hasB=false;
                        for(const auto v:ids) {
                            hasA=hasA||v==a;
                            hasB=hasB||v==b;
                        }
                        return hasA&&hasB;
                    };
                    if(!hasEdge(trace.frontPrev,trace.frontVertex)&&
                       !hasEdge(trace.frontVertex,trace.frontNext))
                        continue;
                    const std::array<Vec3,3> triangle{
                        patch.positions.at(ids[0]),
                        patch.positions.at(ids[1]),
                        patch.positions.at(ids[2])};
                    o<<" seed_support"<<seedSupports<<"_face="<<faceIndex
                     <<" seed_support"<<seedSupports<<"_mesh="
                     <<map.at(ids[0])<<","<<map.at(ids[1])<<","
                     <<map.at(ids[2])
                     <<" seed_support"<<seedSupports<<"_raw_dist="
                     <<signedPlaneDistance(trace.rawProposal,triangle)
                     <<" seed_support"<<seedSupports<<"_tangent_dist="
                     <<signedPlaneDistance(trace.tangentProposal,triangle)
                     <<" seed_support"<<seedSupports<<"_gravity_dist="
                     <<signedPlaneDistance(trace.gravityProposal,triangle)
                     <<" seed_support"<<seedSupports<<"_poisson_dist="
                     <<signedPlaneDistance(trace.poissonPosition,triangle);
                    ++seedSupports;
                }
                o<<" seed_support_faces="<<seedSupports;
                std::cerr<<o.str()<<"\n";
            }
            for(std::size_t faceIndex=0;faceIndex<patch.faces.size();++faceIndex) {
                const auto& face=patch.faces[faceIndex];
                std::cerr<<"[repair-r06-m23b-face-map]"
                         <<" pair_a="<<pair.first<<" pair_b="<<pair.second
                         <<" patch_face="<<faceIndex
                         <<" kind="<<(faceIndex<patch.bridgeFaceCount?
                            "bridge":"advancing_front")
                         <<" A="<<map.at(face[0])
                         <<" B="<<map.at(face[1])
                         <<" C="<<map.at(face[2])<<"\n";
            }
            }
            for(const auto& triangle:patch.faces) {
                const auto x=(patch.positions[triangle[0]].x+
                              patch.positions[triangle[1]].x+
                              patch.positions[triangle[2]].x)/3.;
                const auto firstX=result.positions().at(a.vertexIds[0]).x;
                const auto faceOwner=mirroredPair&&x*firstX<0?
                    otherOwner:owner;
                addFace({0,faceOwner,
                         {map[triangle[0]],map[triangle[1]],map[triangle[2]]},
                         MixedClosureFaceKind::Zipper,h});
            }
            result.spatialPoissonIterations+=patch.poissonIterations;
            result.minimumSpatialInitialQuality=std::min(
                result.minimumSpatialInitialQuality,patch.minimumSeedQuality);
            if(mirroredPair) {
                result.usedMirroredSeamZipper=true;
                ++result.mirroredSeamZipperPairs;
                result.mirroredSeamZipperVertices+=patch.bridgeVertexCount;
            }
            if(bounds.emitTrace)
            std::cerr<<"[repair-r06-zipper-unified]"
                     <<" rim_a="<<a.vertexIds.size()
                     <<" rim_b="<<b.vertexIds.size()
                     <<" seed_faces="<<patch.bridgeFaceCount
                     <<" generated_faces="<<patch.faces.size()
                     <<" generated_vertices="<<patch.positions.size()-
                         (a.vertexIds.size()+b.vertexIds.size())
                     <<" min_seed_q="<<patch.minimumSeedQuality
                     <<" tangent_seed="<<patch.tangentGuidedBridge
                     <<" seam_D_offset="<<patch.seamSeedOffset
                     <<" poisson_iterations="<<patch.poissonIterations
                     <<" source_certified=0\n";
        }else if(mirroredPair) {
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

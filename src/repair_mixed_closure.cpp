#include "mesher/repair_mixed_closure.hpp"
#include "mesher/repair_quality_flip.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=std::pair<VertexId,VertexId>;
Edge edge(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
double quality(Vec3 a,Vec3 b,Vec3 c) {
    const double sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}
double edgeSpan(const std::map<VertexId,Vec3>& pos,Edge e) {
    return length(pos.at(e.first)-pos.at(e.second));
}
struct QualityInfo {
    double threshold{},edgeFactor{};
};
QualityInfo accepted(MixedClosureFaceKind kind,
                     const MixedPrivateClosureLimits& limits) {
    if(kind==MixedClosureFaceKind::Cap)
        return {limits.minCapQuality,limits.maxCapEdgeFactor};
    if(kind==MixedClosureFaceKind::Zipper)
        return {limits.minZipperQuality,limits.maxZipperEdgeFactor};
    return {limits.minSurvivorQuality,
            std::numeric_limits<double>::infinity()};
}
void validate(const MixedPrivateClosureLimits& settings) {
    if(!settings.maxInsertedVertices||!settings.maxNewFaces||
       !settings.maxRefinementSteps||!settings.maxAuditedFaces||
       !std::isfinite(settings.minSurvivorQuality)||
       !std::isfinite(settings.minCapQuality)||
       !std::isfinite(settings.minZipperQuality)||
       !std::isfinite(settings.maxCapEdgeFactor)||
       !std::isfinite(settings.spatialCapDensityEdgeFactor)||
       settings.spatialCapDensityEdgeFactor<=1||
       settings.spatialCapDensityEdgeFactor>settings.maxCapEdgeFactor||
       !std::isfinite(settings.maxZipperEdgeFactor)||
       !std::isfinite(settings.maxZipperBoundaryEdgeFactor)||
       settings.maxZipperBoundaryEdgeFactor<=0||
       settings.maxZipperBoundaryEdgeFactor>1||
       settings.minSurvivorQuality<=0||
       settings.minCapQuality<=0||settings.minCapQuality>=1||
       settings.minZipperQuality<=0||settings.minZipperQuality>=1||
       settings.maxCapEdgeFactor<=1||settings.maxZipperEdgeFactor<=1)
        throw std::invalid_argument("R06-M04B invalid finite quality/refinement limits");
}
} // namespace

MixedPrivateClosureResult applyPrivateMixedClosure(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const MixedPrivateClosureLimits& limits) {
    validate(limits);
    auto stageLimits=limits.staging;
    stageLimits.requireReflectedGeometry=limits.requireReflectedGeometry;
    auto staged=stagePrivateMixedClosure(cut,proposal,plan,stageLimits);
    MixedPrivateClosureResult out;
    out.cappedLoops=staged.capLoops;
    out.zipperPairs=staged.zipperPairs;
    out.spatialCapLoops=staged.spatialCapLoops;
    out.mirroredRibbonLoops=staged.mirroredRibbonLoops;
    out.mirroredSeamZipperPairs=staged.mirroredSeamZipperPairs;
    out.mirroredSeamVertices=staged.mirroredSeamZipperVertices;
    out.spatialPoissonIterations=staged.spatialPoissonIterations;
    out.minimumSpatialInitialQuality=staged.minimumSpatialInitialQuality;
    auto view=cut.index->revision().createMutableView();
    auto& faces=staged.faces;
    auto& positions=staged.positions;
    auto& vertexOwners=staged.vertexOwners;
    std::set<PartitionId> owners;

    // Resolve synthetic spatial front IDs ONLY inside this never-published
    // private transaction. Boundary vertex IDs are retained exactly;
    // interior vertices receive monotonically allocated immutable IDs.
    for(const auto provisional:staged.provisionalSpatialVertices) {
        if(out.insertedVertices.size()>=limits.maxInsertedVertices)
            throw std::length_error(
                "R06-M06C mixed front interior-vertex budget exceeded");
        const auto position=positions.at(provisional);
        const auto owner=vertexOwners.at(provisional);
        const auto stable=view.addVertex(owner,position);
        if(positions.contains(stable))
            throw std::logic_error(
                "R06-M06C newly allocated permanent front ID already present");
        positions.emplace(stable,position);
        vertexOwners.emplace(stable,owner);
        owners.insert(owner);
        out.insertedVertices.push_back(stable);
        for(auto& face:faces)
            for(auto& vertex:face.vertices)
                if(vertex==provisional)vertex=stable;
        positions.erase(provisional);
        vertexOwners.erase(provisional);
    }

    const auto incident=[&](Edge e) {
        std::vector<std::size_t> result;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& v=faces[i].vertices;
            for(unsigned k=0;k<3;++k) {
                if(edge(v[k],v[(k+1)%3])==e) {
                    result.push_back(i);
                    break;
                }
            }
        }
        return result;
    };
    const auto splitQuality=[&](const MixedClosureFace& f,Edge candidate) {
        const auto& v=f.vertices;
        for(unsigned k=0;k<3;++k) {
            const auto a=v[k],b=v[(k+1)%3],c=v[(k+2)%3];
            if(edge(a,b)!=candidate)continue;
            const auto p=(positions.at(a)+positions.at(b))*.5;
            return std::min(quality(positions.at(a),p,positions.at(c)),
                            quality(p,positions.at(b),positions.at(c)));
        }
        throw std::logic_error("R06-M04B tracked face does not contain chosen edge");
    };
    const auto splitEdge=[&](Edge chosen) {
        const auto adjacent=incident(chosen);
        if(adjacent.size()!=2)
            throw std::logic_error("R06-M04B cannot split an edge without exactly two tracked faces");
        if(out.insertedVertices.size()>=limits.maxInsertedVertices)
            throw std::length_error("R06-M04B inserted vertex limit exceeded");
        const auto p=(positions.at(chosen.first)+positions.at(chosen.second))*.5;
        const auto owner=vertexOwners.at(chosen.first);
        const auto middle=view.addVertex(owner,p);
        out.insertedVertices.push_back(middle);
        positions.emplace(middle,p);
        vertexOwners.emplace(middle,owner);
        owners.insert(owner);
        for(const auto index:adjacent) {
            const auto parent=faces.at(index);
            bool matched=false;
            for(unsigned k=0;k<3;++k) {
                const auto a=parent.vertices[k];
                const auto b=parent.vertices[(k+1)%3];
                if(edge(a,b)!=chosen)continue;
                const auto c=parent.vertices[(k+2)%3];
                faces[index].vertices={a,middle,c};
                if(faces.size()>=limits.maxAuditedFaces+limits.maxNewFaces)
                    throw std::length_error("R06-M04B tracked face work limit");
                faces.push_back({0,parent.owner,{middle,b,c},
                                 parent.kind,parent.targetEdge});
                matched=true;
                break;
            }
            if(!matched)
                throw std::logic_error("R06-M04B lost one side of conforming edge split");
        }
    };

    // If this immutable source carries intrinsic X=0 reflection, every
    // conforming split is one complete mirror orbit, not an isolated
    // triangle on whichever side happened to be visited first.
    // The lookup uses EXACT positions only as a private staging sanity
    // check. R07 still requires authoritative source mirror-ID provenance.
    const auto reflectedEdge=[&](Edge chosen)->std::optional<Edge> {
        const auto findPartner=[&](VertexId vertex)->std::optional<VertexId> {
            const auto p=positions.at(vertex);
            std::optional<VertexId> match;
            for(const auto& [id,q]:positions) {
                if(q.x!=-p.x||q.y!=p.y||q.z!=p.z)continue;
                if(match)return std::nullopt;
                match=id;
            }
            return match;
        };
        const auto a=findPartner(chosen.first);
        const auto b=findPartner(chosen.second);
        if(!a||!b)return std::nullopt;
        return edge(*a,*b);
    };
    const auto splitMirrored=[&](Edge chosen,bool optional)->std::size_t {
        Edge paired=chosen;
        if(limits.requireReflectedGeometry) {
            const auto found=reflectedEdge(chosen);
            if(!found) {
                if(optional)return 0;
                throw std::logic_error(
                    "R06-M06C cannot find exact mirror orbit for conforming edge");
            }
            paired=*found;
        }
        const auto count=paired==chosen?std::size_t{1}:std::size_t{2};
        // No speculative pair split is allowed to consume half an orbit.
        if(out.refinementSteps+count>limits.maxRefinementSteps||
           out.insertedVertices.size()+count>limits.maxInsertedVertices||
           faces.size()+2*count>limits.maxAuditedFaces+limits.maxNewFaces) {
            if(optional)return 0;
            throw std::length_error(
                "R06-M06C complete mirrored edge-split orbit exceeds finite budget; "
                "refinements="+std::to_string(out.refinementSteps)+
                " interior_quality_flips="+std::to_string(out.interiorQualityFlips));
        }
        if(incident(chosen).size()!=2||incident(paired).size()!=2) {
            if(optional)return 0;
            throw std::logic_error(
                "R06-M06C mirrored conforming edge lacks two incident tracked faces");
        }
        if(optional) {
            for(const auto& e:{chosen,paired})
                for(const auto index:incident(e)) {
                    const auto q=splitQuality(faces[index],e);
                    if(!std::isfinite(q)||
                       q+1e-12<accepted(faces[index].kind,limits).threshold)
                        return 0;
                }
        }
        splitEdge(chosen);
        if(paired!=chosen)splitEdge(paired);
        return count;
    };

    // R04-like conforming PRE-subdivision along the true old-boundary /
    // new-zipper seam. A narrow zipper across sparsely sampled loops cannot
    // be fixed reliably by repeatedly bisecting skinny interior needles:
    // first shorten BOTH incident surface sides on the same actual edge.
    // This shares the single transaction and the same finite vertex/step
    // budget with later adaptive refinement (no private half-publication).
    while(true) {
        Edge worst{};
        double largestRatio=1;
        for(const auto& face:faces) {
            if(face.kind!=MixedClosureFaceKind::Zipper)continue;
            const auto& v=face.vertices;
            for(unsigned k=0;k<3;++k) {
                const auto candidate=edge(v[k],v[(k+1)%3]);
                const auto adjacent=incident(candidate);
                if(adjacent.size()!=2)continue;
                const auto first=faces[adjacent[0]].kind;
                const auto second=faces[adjacent[1]].kind;
                if(!((first==MixedClosureFaceKind::Survivor&&
                       second==MixedClosureFaceKind::Zipper)||
                      (first==MixedClosureFaceKind::Zipper&&
                       second==MixedClosureFaceKind::Survivor)))
                    continue;
                const auto ratio=edgeSpan(positions,candidate)/
                    (limits.maxZipperBoundaryEdgeFactor*face.targetEdge);
                if(ratio>largestRatio) {
                    largestRatio=ratio;
                    worst=candidate;
                }
            }
        }
        if(largestRatio<=1+1e-8)break;
        if(out.refinementSteps>=limits.maxRefinementSteps)
            throw std::length_error("R06-M04B zipper boundary density requires too many subdivisions");
        out.refinementSteps+=splitMirrored(worst,false);
    }

    const auto improveQuality=[&]() -> bool {
        const auto orbit=limits.requireReflectedGeometry?std::size_t{2}:std::size_t{1};
        if(out.interiorQualityFlips+orbit>limits.maxInteriorQualityFlips)
            return false;
        const auto count=flipOneInferiorClosureDiagonal(
            faces,positions,limits.requireReflectedGeometry,
            limits.minCapQuality,limits.minZipperQuality,
            limits.maxCapEdgeFactor,limits.maxZipperEdgeFactor);
        if(!count)return false;
        out.interiorQualityFlips+=count;
        return true;
    };
    std::size_t stagnant=0;
    while(out.refinementSteps<limits.maxRefinementSteps) {
        Edge longest{};
        double largestRatio=1;
        std::size_t lowest=faces.size();
        double smallest=1;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& f=faces[i];
            if(f.kind==MixedClosureFaceKind::Survivor)continue;
            const auto score=accepted(f.kind,limits);
            const auto& v=f.vertices;
            const auto q=quality(positions.at(v[0]),positions.at(v[1]),
                                 positions.at(v[2]));
            if(q<score.threshold&&q<smallest) {
                smallest=q;lowest=i;
            }
            for(unsigned k=0;k<3;++k) {
                const auto e=edge(v[k],v[(k+1)%3]);
                const auto ratio=edgeSpan(positions,e)/
                    (score.edgeFactor*f.targetEdge);
                if(ratio>largestRatio) {
                    largestRatio=ratio;
                    longest=e;
                }
            }
        }
        if(largestRatio<=1&&lowest==faces.size())break;
        // A bad local diagonal is not repairable merely by bisecting an
        // already skinny needle. Try a bounded topology-preserving 2->2
        // improvement before burning one of the finite subdivision steps.
        if(lowest!=faces.size()&&largestRatio<=1&&improveQuality()) {
            stagnant=0;
            continue;
        }
        Edge chosen=longest;
        if(largestRatio<=1) {
            double best=-1;
            const auto& candidate=faces[lowest];
            const auto v=candidate.vertices;
            for(unsigned k=0;k<3;++k) {
                const auto e=edge(v[k],v[(k+1)%3]);
                const auto adjacent=incident(e);
                if(adjacent.size()!=2)continue;
                double score=1;
                for(const auto index:adjacent)
                    score=std::min(score,splitQuality(faces[index],e));
                score+=1e-7*edgeSpan(positions,e)/candidate.targetEdge;
                if(score>best){best=score;chosen=e;}
            }
            if(best<0)
                throw std::logic_error("R06-M04B skinny cap/seam has no conforming two-sided split");
            if(best<=smallest*(1+1e-6))++stagnant;
            else stagnant=0;
        }else stagnant=0;
        if(stagnant>64)
            throw std::logic_error("R06-M04B refinement stagnation");
        out.refinementSteps+=splitMirrored(chosen,false);
    }
    // Mandatory split budget can be exhausted with q just below .10.
    // Flips need no new vertices and may rescue such local bad
    // diagonals, but remain a separate finite allowance.
    while(improveQuality()){}
    // R06-M06C4a: optional density balancing ONLY after the mandatory
    // bounded M04 quality/length pass. The spatial front can have long
    // spokes even if its legal triangles satisfy the relaxed 2.5*h
    // hard limit. Prefer a tighter 1.5*h mesh-scale detail for display
    // and downstream remeshing, without ever weakening safety or
    // converting a valid closure into a density-budget refusal.
    if(staged.usedSpatialCapFallback&&
       limits.maxOptionalSpatialDensitySplits>0) {
        for(std::size_t optional=0;
            optional<limits.maxOptionalSpatialDensitySplits&&
            out.refinementSteps<limits.maxRefinementSteps&&
            out.insertedVertices.size()<limits.maxInsertedVertices;
            ++optional) {
            // Rebuilt after every conforming split to include both old
            // survivors and all newly generated triangles.
            std::map<Edge,std::vector<std::size_t>> adjacency;
            std::size_t newFaceCount=0;
            for(std::size_t i=0;i<faces.size();++i) {
                const auto& f=faces[i];
                if(f.stableId==0)++newFaceCount;
                for(unsigned k=0;k<3;++k)
                    adjacency[edge(f.vertices[k],
                                   f.vertices[(k+1)%3])].push_back(i);
            }
            if(newFaceCount+2>limits.maxNewFaces||
               faces.size()+2>limits.maxAuditedFaces+limits.maxNewFaces)
                break;
            Edge chosen{};
            double highestRatio=1;
            for(const auto& f:faces) {
                if(f.kind!=MixedClosureFaceKind::Cap)continue;
                const auto& v=f.vertices;
                for(unsigned k=0;k<3;++k) {
                    const auto candidate=edge(v[k],v[(k+1)%3]);
                    const auto ratio=edgeSpan(positions,candidate)/
                        (limits.spatialCapDensityEdgeFactor*f.targetEdge);
                    if(ratio<=highestRatio+1e-10)continue;
                    const auto& adjacent=adjacency.at(candidate);
                    if(adjacent.size()!=2)continue;
                    // A midpoint split must improve the local density
                    // without producing a NEW under-quality cap, zipper
                    // or survivor. M04's hard thresholds remain intact.
                    bool qualitySafe=true;
                    for(const auto index:adjacent) {
                        const auto& side=faces[index];
                        const auto q=splitQuality(side,candidate);
                        if(!std::isfinite(q)||
                           q+1e-12<accepted(side.kind,limits).threshold) {
                            qualitySafe=false;break;
                        }
                    }
                    if(!qualitySafe)continue;
                    highestRatio=ratio;
                    chosen=candidate;
                }
            }
            if(highestRatio<=1)break;
            const auto count=splitMirrored(chosen,true);
            if(!count)break;
            out.refinementSteps+=count;
            out.spatialDensitySplits+=count;
        }
    }
    if(staged.usedSpatialCapFallback)
        for(const auto& f:faces) {
            if(f.kind!=MixedClosureFaceKind::Cap)continue;
            for(unsigned k=0;k<3;++k) {
                const auto e=edge(f.vertices[k],f.vertices[(k+1)%3]);
                const auto ratio=edgeSpan(positions,e)/f.targetEdge;
                out.maximumFinalSpatialCapEdgeRatio=std::max(
                    out.maximumFinalSpatialCapEdgeRatio,ratio);
                if(ratio>limits.spatialCapDensityEdgeFactor*(1+1e-8))
                    ++out.spatialDensityRemainingLongEdges;
            }
        }

    for(const auto& f:faces) {
        const auto& v=f.vertices;
        const auto q=quality(positions.at(v[0]),positions.at(v[1]),
                             positions.at(v[2]));
        const auto requirements=accepted(f.kind,limits);
        if(f.kind==MixedClosureFaceKind::Survivor) {
            if(q<requirements.threshold)
                throw std::logic_error("R06-M04B conforming split left skinny original survivor");
            continue;
        }
        const auto largest=std::max({
            edgeSpan(positions,edge(v[0],v[1])),
            edgeSpan(positions,edge(v[1],v[2])),
            edgeSpan(positions,edge(v[2],v[0]))
        });
        if(q<requirements.threshold ||
           largest>f.targetEdge*requirements.edgeFactor*(1+1e-8))
            throw std::logic_error(
                "R06-M04B mixed closure remains skinny/elongated after finite refinement: kind="+
                std::string(f.kind==MixedClosureFaceKind::Cap?"CAP":"ZIPPER")+
                " vertices="+std::to_string(v[0])+","+
                              std::to_string(v[1])+","+
                              std::to_string(v[2])+
                " q="+std::to_string(q)+
                " required="+std::to_string(requirements.threshold)+
                " length_ratio="+
                std::to_string(largest/(f.targetEdge*requirements.edgeFactor))+
                " refinements="+std::to_string(out.refinementSteps)+
                " inserted_vertices="+std::to_string(out.insertedVertices.size())+
                " interior_quality_flips="+std::to_string(out.interiorQualityFlips));
        if(f.kind==MixedClosureFaceKind::Cap)
            out.minimumCapQuality=std::min(out.minimumCapQuality,q);
        else
            out.minimumZipperQuality=std::min(out.minimumZipperQuality,q);
    }
    for(const auto& face:faces) {
        owners.insert(face.owner);
        if(face.stableId) {
            auto& partition=view.editPartition(face.owner);
            const auto it=std::find_if(partition.faces.begin(),
                                       partition.faces.end(),
                [&](const PartitionFace& old){return old.id==face.stableId;});
            if(it==partition.faces.end())
                throw std::logic_error("R06-M04B stable original FaceId missing");
            it->vertices=face.vertices;
        }else {
            if(out.newFaces>=limits.maxNewFaces)
                throw std::length_error("R06-M04B finite new-face limit exceeded");
            const auto id=view.addFace(face.owner,face.vertices);
            ++out.newFaces;
            if(face.kind==MixedClosureFaceKind::Cap)
                out.greenFaces.push_back(id);
            else if(face.kind==MixedClosureFaceKind::Zipper)
                out.blueFaces.push_back(id);
        }
    }
    out.touchedPartitions.assign(owners.begin(),owners.end());
    const auto revision=view.commit().revision;
    auto closed=RegionTopologyIndex::restore(
        revision,cut.index->retiredVertexIds(),cut.index->retiredFaceIds());
    validatePrivateClosedManifold(*closed,limits.maxAuditedFaces);
    if(staged.usedSpatialCapFallback||staged.usedMirroredSeamZipper||
       out.interiorQualityFlips>0) {
        // Neither the spatial front nor the explicit mirrored zipper
        // seam can see the full surrounding mesh during local staging.
        // Demand a separate, bounded exact triangle contact query against
        // ALL candidate geometry after final conforming refinement. A
        // manifold by incidence can still self-intersect in 3D.
        RegionTopologyIndex::CollisionQueryLimits collision;
        collision.maxSeedFaces=16384;
        collision.maxBoxTests=4194304;
        collision.maxPartitionTests=1048576;
        collision.maxCandidateTriangles=524288;
        collision.maxNarrowPhaseTests=262144;
        collision.maxReportedContacts=1;
        RegionTopologyIndex::CollisionStatistics diagnostics;
        std::vector<FaceId> newSurface=out.greenFaces;
        newSurface.insert(newSurface.end(),
                          out.blueFaces.begin(),out.blueFaces.end());
        const auto unexpected=closed->triangleIntersections(
            std::vector<VertexId>{},newSurface,&diagnostics,collision);
        if(!unexpected.empty())
            throw std::logic_error(
                "R06-M06C spatial front intersects surviving/cap geometry "
                "after final 3D private refinement");
    }
    for(const auto id:proposal.proposedFaces)
        if(closed->maybeFace(id)||!closed->retiredFace(id))
            throw std::logic_error("R06-M04B resurrected a retired R03 FaceId");
    out.index=std::move(closed);
    return out;
}
} // namespace mesher

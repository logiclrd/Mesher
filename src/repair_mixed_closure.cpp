#include "mesher/repair_mixed_closure.hpp"
#include "mesher/repair_quality_flip.hpp"
#include "mesher/repair_interior_fairing.hpp"
#include "mesher/repair_interior_collapse.hpp"
#include "mesher/repair_rim_quality.hpp"
#include "mesher/repair_survivor_split.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <iomanip>
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
double edgeSpan(const MixedClosurePositions& pos,Edge e) {
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
    // Warning mode is used only for an untrusted private inspection rerun.
    const auto warn=[&](const std::string& reason) {
        if(out.qualityWarningReason.find(reason)!=std::string::npos)return;
        // One label per distinct failure class, bounded even when a patch
        // contains thousands of under-quality faces.
        if(out.qualityWarningReason.size()+reason.size()>4096)return;
        if(!out.qualityWarningReason.empty())out.qualityWarningReason+="; ";
        out.qualityWarningReason+=reason;
    };

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

    // Undo only fully NEW redundant 3-triangle interior cap fans,
    // BEFORE spending scarce subdivision/reflection-orbit budget. The
    // common outer triangle must already satisfy the original strict
    // q and length bounds; no original surviving/rim vertex moves.
    constexpr std::size_t maxPrivateCapFanRetirements=16;
    while(out.interiorCapCollapses<maxPrivateCapFanRetirements) {
        auto collapsePositions=positions.materialize();
        const auto retired=collapseOneInferiorCapFan(
            faces,collapsePositions,out.insertedVertices,
            limits.requireReflectedGeometry,
            limits.minCapQuality,limits.maxCapEdgeFactor);
        if(retired.empty())break;
        for(const auto id:retired) {
            positions.erase(id);
            const auto owner=vertexOwners.at(id);
            auto& partition=view.editPartition(owner);
            const auto count=std::erase_if(partition.vertices,
                [id](const PartitionVertex& v){return v.id==id;});
            if(count!=1)
                throw std::logic_error(
                    "R06-M11A3b redundant interior cap fan retirement must remove one new vertex");
            std::erase(out.insertedVertices,id);
            vertexOwners.erase(id);
        }
        out.interiorCapCollapses+=retired.size();
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
            for(const auto& [id,q]:positions.materialize()) {
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

    // Read-only complete mirror-orbit preflight. A conforming split can
    // bisect a skinny ORIGINAL survivor as well as the new cap/zipper.
    // Never discover that damage only after spending the split budget.
    const auto survivorSafeOrbit=[&](Edge chosen)->bool {
        if(!assessSurvivorSplit(
                faces,positions.materialize(),chosen,limits.minSurvivorQuality).safe)
            return false;
        const auto midpoint=assessUniqueConformingMidpoint(positions.materialize(),chosen);
        if(!midpoint.unique)return false;
        if(!limits.requireReflectedGeometry)return true;
        const auto reflected=reflectedEdge(chosen);
        if(!reflected||!assessSurvivorSplit(
                faces,positions.materialize(),*reflected,limits.minSurvivorQuality).safe)
            return false;
        if(*reflected==chosen)return true;
        const auto peer=assessUniqueConformingMidpoint(positions.materialize(),*reflected);
        // Distinct reflected orbits must not insert the SAME plane vertex
        // under two different IDs, even if both were previously absent.
        return peer.unique&&
            !(peer.point.x==midpoint.point.x&&
              peer.point.y==midpoint.point.y&&
              peer.point.z==midpoint.point.z);
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

        // BOTH surviving face stars must remain legal before EITHER
        // new midpoint is allocated, even for mandatory quality passes.
        for(const auto& e:{chosen,paired}) {
            const auto check=assessSurvivorSplit(
                faces,positions.materialize(),e,limits.minSurvivorQuality);
            if(!check.safe) {
                if(optional)return 0;
                throw std::logic_error(
                    "R06-M11A unsafe original-survivor midpoint split"
                    " edge="+std::to_string(e.first)+","+
                              std::to_string(e.second)+
                    " stable_face="+std::to_string(check.originalFace)+
                    " child_q="+std::to_string(check.worstChildQuality)+
                    " required="+std::to_string(limits.minSurvivorQuality));
            }
        }
        const auto firstPoint=assessUniqueConformingMidpoint(
            positions.materialize(),chosen);
        const auto secondPoint=paired==chosen?firstPoint:
            assessUniqueConformingMidpoint(positions.materialize(),paired);
        const bool midpointsCollide=paired!=chosen&&
            firstPoint.point.x==secondPoint.point.x&&
            firstPoint.point.y==secondPoint.point.y&&
            firstPoint.point.z==secondPoint.point.z;
        if(!firstPoint.unique||!secondPoint.unique||midpointsCollide) {
            if(optional)return 0;
            throw std::logic_error(
                "R06-M11A2 complete mirrored midpoint orbit would "
                "create coincident stable vertices: edge="+
                std::to_string(chosen.first)+","+
                std::to_string(chosen.second)+
                " first_collision="+
                std::to_string(firstPoint.existingVertex)+
                " reflected_collision="+
                std::to_string(secondPoint.existingVertex)+
                " orbit_self_collision="+
                std::to_string(midpointsCollide));
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
        double largestUnsafeRatio=1;
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
                if(ratio>largestUnsafeRatio)
                    largestUnsafeRatio=ratio;
                if(ratio>largestRatio&&survivorSafeOrbit(candidate)) {
                    largestRatio=ratio;
                    worst=candidate;
                }
            }
        }
        if(largestUnsafeRatio<=1+1e-8)break;
        if(largestRatio<=1+1e-8) {
            if(limits.inspectionWarnQuality) {
                warn("zipper boundary density has no survivor-safe split");
                break;
            }
            throw std::logic_error(
                "R06-M11A zipper boundary length deficit has no "
                "survivor-safe conforming mirror-orbit split");
        }
        if(out.refinementSteps>=limits.maxRefinementSteps) {
            if(limits.inspectionWarnQuality) {
                warn("zipper boundary density reached finite split budget");
                break;
            }
            throw std::length_error("R06-M04B zipper boundary density requires too many subdivisions");
        }
        try {
            out.refinementSteps+=splitMirrored(worst,false);
        }catch(const std::length_error&) {
            if(!limits.inspectionWarnQuality)throw;
            warn("zipper boundary density exceeded complete mirror-orbit budget");
            break;
        }
    }

    const auto improveQuality=[&]() -> bool {
        const auto orbit=limits.requireReflectedGeometry?std::size_t{2}:std::size_t{1};
        if(out.interiorQualityFlips+orbit>limits.maxInteriorQualityFlips)
            return false;
        const auto count=flipOneInferiorClosureDiagonal(
            faces,positions.materialize(),limits.requireReflectedGeometry,
            limits.minCapQuality,limits.minZipperQuality,
            limits.maxCapEdgeFactor,limits.maxZipperEdgeFactor);
        if(!count)return false;
        out.interiorQualityFlips+=count;
        return true;
    };
    constexpr std::size_t maxInteriorFairingMoves=32;
    const auto improveFairing=[&]() -> bool {
        const auto orbit=limits.requireReflectedGeometry?
            std::size_t{2}:std::size_t{1};
        if(out.interiorFairingMoves+orbit>maxInteriorFairingMoves)return false;
        auto fairPositions=positions.materialize();
        const auto moved=fairOneConstrainedClosureVertex(
            faces,fairPositions,out.insertedVertices,
            limits.requireReflectedGeometry,
            limits.minCapQuality,limits.minZipperQuality,
            limits.maxCapEdgeFactor,limits.maxZipperEdgeFactor);
        if(!moved)return false;
        std::size_t updated=0;
        for(const auto id:out.insertedVertices) {
            const auto it=fairPositions.find(id);
            if(it==fairPositions.end())continue;
            const auto original=positions.at(id);
            const auto p=it->second;
            if(p.x!=original.x||p.y!=original.y||p.z!=original.z) {
                positions.set(id,p);
                ++updated;
            }
        }
        if(updated!=moved)
            throw std::logic_error(
                "M14B1 fairing attempted non-transactional vertex changes");
        out.interiorFairingMoves+=moved;
        return true;
    };
    std::size_t stagnant=0;
    while(out.refinementSteps<limits.maxRefinementSteps) {
        Edge longest{};
        double largestRatio=1;
        double largestUnsafeRatio=1;
        std::vector<std::pair<double,Edge>> longCandidates;
        std::size_t lowest=faces.size();
        double smallest=1;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& f=faces[i];
            if(f.kind==MixedClosureFaceKind::Survivor)continue;
            const auto score=accepted(f.kind,limits);
            const auto& v=f.vertices;
            const auto q=quality(positions.at(v[0]),positions.at(v[1]),
                                 positions.at(v[2]));
            // A face spanning both unmodified R03 rays at a certified
            // acute planar tip cannot meet q=.10 without changing the
            // fixed corner. Do not split it forever just to repeat the
            // same unavoidable angle. No waiver for off-ray/folded fans.
            const bool constrainedTip=
                f.kind==MixedClosureFaceKind::Cap&&
                certifyPlanarRimTipQuality(
                    f,positions.materialize(),proposal.contours,score.threshold).has_value();
            if(q<score.threshold&&!constrainedTip&&q<smallest) {
                smallest=q;lowest=i;
            }
            for(unsigned k=0;k<3;++k) {
                const auto e=edge(v[k],v[(k+1)%3]);
                const auto ratio=edgeSpan(positions,e)/
                    (score.edgeFactor*f.targetEdge);
                if(ratio>1) {
                    largestUnsafeRatio=std::max(largestUnsafeRatio,ratio);
                    longCandidates.emplace_back(ratio,e);
                }
            }
        }
        // Preserve the original longest-first ordering where it is legal,
        // but skip candidates whose complete mirrored orbit would create
        // an under-quality ORIGINAL survivor child. Sort by score then IDs.
        std::sort(longCandidates.begin(),longCandidates.end(),
            [](const auto& a,const auto& b) {
                if(a.first!=b.first)return a.first>b.first;
                return a.second<b.second;
            });
        for(const auto& [ratio,e]:longCandidates) {
            if(!survivorSafeOrbit(e))continue;
            largestRatio=ratio;
            longest=e;
            break;
        }
        if(largestUnsafeRatio<=1&&lowest==faces.size())break;
        if(largestRatio<=1&&lowest==faces.size()) {
            if(improveQuality())continue;
            if(limits.inspectionWarnQuality) {
                warn("closure edge length cannot be refined with safe mirror orbit");
                break;
            }
            throw std::logic_error(
                "R06-M11A remaining closure edge-length deficit has "
                "no survivor-safe conforming mirror-orbit split");
        }
        // When quality, rather than edge length, is the obstacle,
        // reposition NEW interior vertices before further subdivisions
        // simply propagate a sharp near-zero-angle needle.
        if(lowest!=faces.size()&&largestRatio<=1&&improveFairing()) {
            stagnant=0;
            continue;
        }
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
                if(adjacent.size()!=2||!survivorSafeOrbit(e))continue;
                double score=1;
                for(const auto index:adjacent)
                    score=std::min(score,splitQuality(faces[index],e));
                score+=1e-7*edgeSpan(positions,e)/candidate.targetEdge;
                if(score>best){best=score;chosen=e;}
            }
            if(best<0) {
                std::ostringstream evidence;
                evidence<<std::setprecision(17)
                        <<"R06-M11A skinny cap/seam has no survivor-safe "
                          "conforming two-sided split face="
                        <<v[0]<<","<<v[1]<<","<<v[2]
                        <<" current_q="<<smallest;
                for(const auto vertex:v) {
                    const auto p=positions.at(vertex);
                    std::size_t reflectedMatches=0;
                    for(const auto& [id,q]:positions.materialize()) {
                        (void)id;
                        if(q.x==-p.x&&q.y==p.y&&q.z==p.z)
                            ++reflectedMatches;
                    }
                    evidence<<" vertex="<<vertex
                            <<" xyz="<<p.x<<","<<p.y<<","<<p.z
                            <<" exact_mirror_candidates="<<reflectedMatches;
                }
                for(unsigned k=0;k<3;++k) {
                    const auto e=edge(v[k],v[(k+1)%3]);
                    const auto neighboring=incident(e);
                    const auto local=assessSurvivorSplit(
                        faces,positions.materialize(),e,limits.minSurvivorQuality);
                    evidence<<" edge="<<e.first<<","<<e.second
                            <<" incident="<<neighboring.size()
                            <<" original_face="<<local.originalFace
                            <<" survivor_child_q="<<local.worstChildQuality
                            <<" survivor_safe="<<local.safe;
                    if(limits.requireReflectedGeometry) {
                        const auto mirror=reflectedEdge(e);
                        evidence<<" mirror="<<bool(mirror.has_value());
                        if(mirror) {
                            const auto other=assessSurvivorSplit(
                                faces,positions.materialize(),*mirror,
                                limits.minSurvivorQuality);
                            evidence<<" mirror_original_face="<<other.originalFace
                                    <<" mirror_child_q="<<other.worstChildQuality
                                    <<" mirror_safe="<<other.safe;
                        }
                    }
                }
                evidence<<" fairing_moves="<<out.interiorFairingMoves;
                if(limits.inspectionWarnQuality) {
                    warn(evidence.str());
                    break;
                }
                throw std::logic_error(evidence.str());
            }
            if(best<=smallest*(1+1e-6))++stagnant;
            else stagnant=0;
        }else {
            stagnant=0;
            if(largestRatio<=1) {
                // Every long edge was unsafe for an existing survivor.
                // A shorter safe split may still improve the skinny
                // closure, but never waive the hard density constraint.
                if(improveQuality())continue;
                if(limits.inspectionWarnQuality) {
                    warn("remaining closure edge-length deficit has no survivor-safe split");
                    break;
                }
                throw std::logic_error(
                    "R06-M11A no survivor-safe split for remaining "
                    "closure edge-length deficit");
            }
        }
        if(stagnant>64) {
            if(limits.inspectionWarnQuality) {
                warn("refinement stagnation after finite work");
                break;
            }
            throw std::logic_error("R06-M04B refinement stagnation");
        }
        try {
            out.refinementSteps+=splitMirrored(chosen,false);
        }catch(const std::length_error&) {
            if(!limits.inspectionWarnQuality)throw;
            warn("closure refinement exceeded complete mirror-orbit budget");
            break;
        }
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

    // One final bounded local merit pass may rescue quality deficits
    // remaining after the subdivision budget, including incomplete
    // symmetric split orbits. Never touch original R03/survivor vertices.
    while(improveFairing()) {}
    if(out.interiorFairingMoves)
        for(const auto id:out.insertedVertices) {
            const auto owner=vertexOwners.at(id);
            auto& partition=view.editPartition(owner);
            const auto it=std::find_if(partition.vertices.begin(),
                                       partition.vertices.end(),
                [id](const PartitionVertex& v){return v.id==id;});
            if(it==partition.vertices.end())
                throw std::logic_error("R06-M11A3a moved private vertex missing from owner");
            it->position=positions.at(id);
        }
    std::vector<bool> deficient(faces.size(),false);
    std::vector<bool> certifiedTip(faces.size(),false);
    for(std::size_t faceOrdinal=0;faceOrdinal<faces.size();++faceOrdinal) {
        const auto& f=faces[faceOrdinal];
        const auto& v=f.vertices;
        const auto q=quality(positions.at(v[0]),positions.at(v[1]),
                             positions.at(v[2]));
        const auto requirements=accepted(f.kind,limits);
        if(f.kind==MixedClosureFaceKind::Survivor) {
            if(!std::isfinite(q)||q<requirements.threshold) {
                if(limits.inspectionWarnQuality) {
                    deficient[faceOrdinal]=true;
                    warn("under-quality original survivor triangle after refinement");
                }else throw std::logic_error(
                    "R06-M04B conforming split left skinny original survivor"
                    " vertices="+std::to_string(v[0])+","+
                                 std::to_string(v[1])+","+
                                 std::to_string(v[2])+
                    " q="+std::to_string(q)+
                    " required="+std::to_string(requirements.threshold)+
                    " refinements="+std::to_string(out.refinementSteps)+
                    " interior_quality_flips="+std::to_string(out.interiorQualityFlips));
            }
            continue;
        }
        const auto largest=std::max({
            edgeSpan(positions,edge(v[0],v[1])),
            edgeSpan(positions,edge(v[1],v[2])),
            edgeSpan(positions,edge(v[2],v[0]))
        });
        const bool overlong=largest>f.targetEdge*requirements.edgeFactor*(1+1e-8);
        if(!overlong&&std::isfinite(q)&&q<requirements.threshold&&
           f.kind==MixedClosureFaceKind::Cap&&
           certifyPlanarRimTipQuality(
               f,positions.materialize(),proposal.contours,requirements.threshold)) {
            certifiedTip[faceOrdinal]=true;
        }
        if(!certifiedTip[faceOrdinal]&&
           (!std::isfinite(q)||q<requirements.threshold||overlong)) {
            if(limits.inspectionWarnQuality) {
                deficient[faceOrdinal]=true;
                warn("under-quality or overlong cap/zipper triangle");
            }else throw std::logic_error(
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
        }
        if(f.kind==MixedClosureFaceKind::Cap)
            out.minimumCapQuality=std::min(out.minimumCapQuality,q);
        else
            out.minimumZipperQuality=std::min(out.minimumZipperQuality,q);
    }
    for(std::size_t i=0;i<faces.size();++i) {
        const auto& face=faces[i];
        owners.insert(face.owner);
        if(face.stableId) {
            auto& partition=view.editPartition(face.owner);
            const auto it=std::find_if(partition.faces.begin(),
                                       partition.faces.end(),
                [&](const PartitionFace& old){return old.id==face.stableId;});
            if(it==partition.faces.end())
                throw std::logic_error("R06-M04B stable original FaceId missing");
            it->vertices=face.vertices;
            if(deficient[i])out.qualityWarningFaces.push_back(face.stableId);
            if(certifiedTip[i])out.boundaryTipQualityExemptFaces.push_back(face.stableId);
        }else {
            if(out.newFaces>=limits.maxNewFaces)
                throw std::length_error("R06-M04B finite new-face limit exceeded");
            const auto id=view.addFace(face.owner,face.vertices);
            if(deficient[i])out.qualityWarningFaces.push_back(id);
            if(certifiedTip[i])out.boundaryTipQualityExemptFaces.push_back(id);
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
       out.interiorQualityFlips>0||out.interiorFairingMoves>0||
       out.interiorCapCollapses>0||!out.boundaryTipQualityExemptFaces.empty()) {
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
        try {
            const auto unexpected=closed->triangleIntersections(
                std::vector<VertexId>{},newSurface,&diagnostics,collision);
            if(!unexpected.empty()) {
                if(limits.inspectionWarnQuality)
                    warn("UNTRUSTED PREVIEW: final geometry has unexpected "
                         "triangle intersections (strict AUTO still refuses)");
                else throw std::logic_error(
                    "R06-M06C spatial front intersects surviving/cap geometry "
                    "after final 3D private refinement");
            }
        }catch(const std::length_error& e) {
            if(!limits.inspectionWarnQuality)throw;
            warn(std::string("UNTRUSTED PREVIEW: finite collision audit "
                             "incomplete: ")+e.what());
        }
    }
    for(const auto id:proposal.proposedFaces)
        if(closed->maybeFace(id)||!closed->retiredFace(id))
            throw std::logic_error("R06-M04B resurrected a retired R03 FaceId");
    out.index=std::move(closed);
    return out;
}
} // namespace mesher

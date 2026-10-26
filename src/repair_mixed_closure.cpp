#include "mesher/repair_mixed_closure.hpp"
#include "mesher/repair_step_gate.hpp"
#include "mesher/repair_quality_flip.hpp"
#include "mesher/repair_interior_fairing.hpp"
#include "mesher/repair_interior_collapse.hpp"
#include "mesher/repair_rim_quality.hpp"
#include "mesher/repair_survivor_split.hpp"
#include "mesher/repair_split_template.hpp"
#include "mesher/repair_scale_aware_policy.hpp"
#include "mesher/repair_mixed_edges.hpp"
#include "mesher/repair_mixed_patch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <iomanip>
#include <iostream>
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
       settings.maxCapEdgeFactor<=1||settings.maxZipperEdgeFactor<=1||
       !std::isfinite(settings.shapeRepairScaleRatio)||
       settings.shapeRepairScaleRatio<=1||
       !settings.maxScaleAwareSplitOperations||
       !settings.maxScaleAwareSplitDepth||
       settings.maxScaleAwareSplitDepth>8)
        throw std::invalid_argument("R06-M04B invalid finite quality/refinement limits");
}
} // namespace

static MixedPrivateClosureResult applyPrivateMixedClosureOnce(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const MixedPrivateClosureLimits& limits) {
    std::string phase="validate";
    try {
    validate(limits);
    phase="initial_staging";
    auto stageLimits=limits.staging;
    stageLimits.requireReflectedGeometry=limits.requireReflectedGeometry;
    stageLimits.targetInitialCapQuality=limits.minCapQuality;
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
    phase="provisional_resolution_and_refinement";
    auto view=cut.index->revision().createMutableView();
    const auto& faces=staged.faces();
    const auto& positions=staged.positions();
    const auto& vertexOwners=staged.vertexOwners();
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

    // All synthetic front ID replacement is an editor operation; caller
    // never rewrites a survivor/cap triangle directly.
    for(const auto stable:staged.resolveProvisionalVertices(
            view,limits.maxInsertedVertices)) {
        out.insertedVertices.push_back(stable);
        owners.insert(staged.vertexOwner(stable));
    }

    // Undo only fully NEW redundant 3-triangle interior cap fans,
    // BEFORE spending scarce subdivision/reflection-orbit budget. The
    // common outer triangle must already satisfy the original strict
    // q and length bounds; no original surviving/rim vertex moves.
    constexpr std::size_t maxPrivateCapFanRetirements=16;
    while(out.interiorCapCollapses<maxPrivateCapFanRetirements) {
        const auto retired=staged.collapseInteriorCapFan(
            out.insertedVertices,
            limits.requireReflectedGeometry,
            limits.minCapQuality,limits.maxCapEdgeFactor);
        if(retired.empty())break;
        for(const auto id:retired) {
            staged.retirePrivateVertex(view,id);
            std::erase(out.insertedVertices,id);
        }
        out.interiorCapCollapses+=retired.size();
    }

    const auto incident=[&](Edge e) {
        std::vector<std::size_t> result;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& v=faces[i].triangle();
            for(unsigned k=0;k<3;++k) {
                if(edge(v[k],v[(k+1)%3])==e) {
                    result.push_back(i);
                    break;
                }
            }
        }
        return result;
    };
    // Inspect both the generated triangles and the complete effective R03
    // source topology. Missing source sides are visible without copying
    // their faces into the editable worklist.
    const auto neighborhood=[&](Edge e) {
        return inspectMixedClosureEdge(faces,*staged.sourceView(),e);
    };
    const auto effectiveSides=[&](Edge e) {
        const auto n=neighborhood(e);
        std::vector<MixedClosureFace> result;
        result.reserve(n.sides());
        for(const auto i:n.localFaceIndexes)result.push_back(faces.at(i));
        for(const auto id:n.missingSurvivorFaces) {
            const auto& original=staged.sourceView()->face(id);
            result.push_back({id,original.owner,{},MixedClosureFaceKind::Survivor,
                              0,staged.sourceView()});
        }
        return result;
    };
    const auto splitQuality=[&](const MixedClosureFace& f,Edge candidate) {
        const auto& v=f.triangle();
        for(unsigned k=0;k<3;++k) {
            const auto a=v[k],b=v[(k+1)%3],c=v[(k+2)%3];
            if(edge(a,b)!=candidate)continue;
            const auto p=(positions.at(a)+positions.at(b))*.5;
            return std::min(quality(positions.at(a),p,positions.at(c)),
                            quality(p,positions.at(b),positions.at(c)));
        }
        throw std::logic_error("R06-M04B tracked face does not contain chosen edge");
    };
    // All actual edge mutations belong to MixedClosureStaging::splitEdge.
    // The surrounding quality search is read-only. No caller can split
    // a tracked source/cap triangle independently from its true neighbors.
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

    // Read-only complete mirror-orbit preflight. A conforming split can
    // bisect a skinny ORIGINAL survivor as well as the new cap/zipper.
    // Never discover that damage only after spending the split budget.
    const auto uniqueMidpoint=[&](Edge chosen) {
        auto check=assessUniqueConformingMidpoint(positions,chosen);
        if(!check.unique)return check;
        // A source vertex far outside the initially staged halo can still
        // occupy this point exactly. A geometry-only match blocks the edit;
        // it NEVER grants a symbolic VertexId weld.
        for(const auto id:staged.sourceView()->source().querySphere(check.point,std::numeric_limits<double>::denorm_min())) {
            const auto p=staged.sourceView()->position(id);
            if(p.x==check.point.x&&p.y==check.point.y&&p.z==check.point.z) {
                check.unique=false;
                check.existingVertex=id;
                break;
            }
        }
        return check;
    };
    const auto safeEdge=[&](Edge chosen)->bool {
        const auto n=neighborhood(chosen);
        if(!n.conformingPair)return false;
        if(!assessSurvivorSplit(
               effectiveSides(chosen),positions,chosen,
               limits.minSurvivorQuality).safe)
            return false;
        return uniqueMidpoint(chosen).unique;
    };
    const auto survivorSafeOrbit=[&](Edge chosen)->bool {
        if(!safeEdge(chosen))return false;
        const auto midpoint=uniqueMidpoint(chosen);
        if(!limits.requireReflectedGeometry)return true;
        const auto reflected=reflectedEdge(chosen);
        if(!reflected||!safeEdge(*reflected))return false;
        if(*reflected==chosen)return true;
        const auto peer=uniqueMidpoint(*reflected);
        return peer.unique&&
            !(peer.point.x==midpoint.point.x&&
              peer.point.y==midpoint.point.y&&
              peer.point.z==midpoint.point.z);
    };
    const auto splitMirrored=[&](Edge chosen,bool optional,
                               double canonicalT=.5)->std::vector<VertexId> {
        MixedClosureSplitSettings policy;
        policy.requireReflectedGeometry=limits.requireReflectedGeometry;
        policy.minSurvivorQuality=limits.minSurvivorQuality;
        policy.minCapQuality=limits.minCapQuality;
        policy.minZipperQuality=limits.minZipperQuality;
        policy.maxInsertedVertices=limits.maxInsertedVertices;
        policy.maxRefinementSteps=limits.maxRefinementSteps;
        policy.maxTrackedFaces=std::min(
            limits.maxAuditedFaces+limits.maxNewFaces,
            stageLimits.maxTrackedFaces);
        const auto mids=staged.splitEdge(view,chosen.first,chosen.second,
            policy,out.insertedVertices.size(),out.refinementSteps,
            optional,canonicalT);
        for(const auto id:mids) {
            out.insertedVertices.push_back(id);
            owners.insert(staged.vertexOwner(id));
        }
        return mids;
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
            const auto& v=face.triangle();
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
            out.refinementSteps+=splitMirrored(worst,false).size();
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
        const auto count=staged.improveInteriorDiagonal(
            limits.requireReflectedGeometry,
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
        const auto moved=staged.fairInteriorVertex(
            out.insertedVertices,
            limits.requireReflectedGeometry,
            limits.minCapQuality,limits.minZipperQuality,
            limits.maxCapEdgeFactor,limits.maxZipperEdgeFactor);
        if(!moved)return false;
        out.interiorFairingMoves+=moved;
        return true;
    };
    // M17C: collapse newly generated cap-fan vertices is a genuine
    // local shape change. The same strict 3->1 operation is already
    // accepted in pre-refinement, but later flips/splits may expose a
    // newly redundant fan. Keep its shared finite retirement budget.
    const auto improveCapShape=[&]() -> bool {
        if(out.interiorCapCollapses>=maxPrivateCapFanRetirements)
            return false;
        const auto retired=staged.collapseInteriorCapFan(
            out.insertedVertices,limits.requireReflectedGeometry,
            limits.minCapQuality,limits.maxCapEdgeFactor);
        if(retired.empty())return false;
        for(const auto id:retired) {
            staged.retirePrivateVertex(view,id);
            std::erase(out.insertedVertices,id);
        }
        out.interiorCapCollapses+=retired.size();
        return true;
    };
    // M17C bounded policy is opt-in while the saved refusal archive is
    // being compared. The original R06 search below remains byte-identical
    // when disabled. This map measures lineage on CURRENT effective edges:
    // a freshly subdivided child cannot repeatedly chase a thin corner.
    std::map<Edge,std::size_t> scaleEdgeDepth;
    const auto planScaleEdge=[&](Edge e)->std::optional<RepairSplitTemplateDecision> {
        const auto n=neighborhood(e);
        if(n.sides()!=2||!n.conformingPair)return std::nullopt;
        const auto sides=effectiveSides(e);
        double target=std::numeric_limits<double>::infinity();
        for(const auto& f:sides)
            if(f.kind!=MixedClosureFaceKind::Survivor&&
               f.targetEdge>0&&std::isfinite(f.targetEdge))
                target=std::min(target,f.targetEdge);
        if(!std::isfinite(target))return std::nullopt;
        std::size_t orbit=1;
        if(limits.requireReflectedGeometry) {
            const auto peer=reflectedEdge(e);
            if(!peer||!staged.hasEffectiveEdge(peer->first,peer->second))
                return std::nullopt;
            orbit=*peer==e?1:2;
        }
        const auto remaining=std::min(
            limits.maxRefinementSteps-out.refinementSteps,
            limits.maxScaleAwareSplitOperations-out.scaleAwareSplitOperations);
        const auto depth=scaleEdgeDepth[e];
        if(depth>=limits.maxScaleAwareSplitDepth||remaining<orbit)
            return std::nullopt;
        RepairSplitTemplateInput input;
        input.firstId=e.first;
        input.secondId=e.second;
        input.first=positions.at(e.first);
        input.second=positions.at(e.second);
        input.policy.targetLength=target;
        input.policy.coarseScaleRatio=limits.shapeRepairScaleRatio;
        input.policy.remainingOperations=std::min<std::size_t>(2,remaining/orbit);
        input.policy.maximumDepth=std::min<std::size_t>(
            2,limits.maxScaleAwareSplitDepth-depth);
        for(const auto& f:sides) {
            const auto tri=f.triangle();
            VertexId opposite=0;
            for(const auto vertex:tri)
                if(vertex!=e.first&&vertex!=e.second)opposite=vertex;
            if(!opposite)return std::nullopt;
            input.sides.push_back({
                positions.at(opposite),accepted(f.kind,limits).threshold});
        }
        return evaluateRepairSplitTemplates(std::move(input));
    };
    const auto scaleSplit=[&](Edge e,double t)->std::vector<VertexId> {
        const auto depth=scaleEdgeDepth[e]+1;
        const auto newIds=splitMirrored(e,false,t);
        if(newIds.empty())
            throw std::logic_error("R06-M17C selected split produced no vertices");
        for(std::size_t k=0;k<newIds.size();++k) {
            const auto& event=staged.splitEvents().at(
                staged.splitEvents().size()-newIds.size()+k);
            scaleEdgeDepth[edge(event.originalA,newIds[k])]=depth;
            scaleEdgeDepth[edge(newIds[k],event.originalB)]=depth;
        }
        out.refinementSteps+=newIds.size();
        out.scaleAwareSplitOperations+=newIds.size();
        if(t!=.5)out.scaleAwareOffCenterSplits+=newIds.size();
        return newIds;
    };
    // Return false only after ALL currently supported shape operations
    // were actually called and refused their local admissibility/merit
    // tests. No-change attempts are visible as explicit, unchanged-state
    // frames in the M16 debugger; previously these calls were invisible.
    const auto tryShapeEdits=[&](std::size_t bad,double q)->bool {
        std::ostringstream ctx;
        ctx<<std::setprecision(17);
        if(bad<faces.size()) {
            const auto& f=faces.at(bad);
            const auto v=f.triangle();
            std::size_t newCorners=0;
            for(const auto id:v)
                if(std::find(out.insertedVertices.begin(),
                             out.insertedVertices.end(),id)!=
                   out.insertedVertices.end())++newCorners;
            ctx<<"face="<<v[0]<<","<<v[1]<<","<<v[2]
               <<" kind="<<(f.kind==MixedClosureFaceKind::Cap?"CAP":
                              f.kind==MixedClosureFaceKind::Zipper?"ZIPPER":"SURVIVOR")
               <<" q="<<q
               <<" new_corners="<<newCorners
               <<" target="<<f.targetEdge<<" ";
        }else ctx<<"face=NONE ";
        if(improveQuality())return true;
        staged.reportRefinementAttempt("TryDiagonalFlip",
            ctx.str()+"result=NO_ADMISSIBLE candidate_requires="
            "two_new_same_kind_faces_convex_near_coplanar_quality_gain");
        if(improveCapShape())return true;
        staged.reportRefinementAttempt("TryCapFanCollapse",
            ctx.str()+"result=NO_ADMISSIBLE candidate_requires="
            "new_cap_center_valence3_valid_outer_face");
        if(improveFairing())return true;
        staged.reportRefinementAttempt("TryInteriorFairing",
            ctx.str()+"result=NO_ADMISSIBLE candidate_requires="
            "new_unprotected_vertex_safe_positive_quality_gain");
        return false;
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
            const auto& v=f.triangle();
            const auto q=quality(positions.at(v[0]),positions.at(v[1]),
                                 positions.at(v[2]));
            // A face spanning both unmodified R03 rays at a certified
            // acute planar tip cannot meet q=.10 without changing the
            // fixed corner. Do not split it forever just to repeat the
            // same unavoidable angle. No waiver for off-ray/folded fans.
            const bool constrainedTip=
                f.kind==MixedClosureFaceKind::Cap&&
                certifyPlanarRimTipQuality(
                    f,positions,proposal.contours,score.threshold).has_value();
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
        if(limits.scaleAwareRefinement) {
            // A bad triangle at or below the local shape-work scale must
            // NOT be repeatedly divided merely because some OTHER edge
            // remains too long. Fairing/flips act on shape, not density.
            bool shapeFirst=false;
            if(lowest!=faces.size()) {
                const auto& f=faces.at(lowest);
                const auto v=f.triangle();
                const double localLongest=std::max({
                    edgeSpan(positions,edge(v[0],v[1])),
                    edgeSpan(positions,edge(v[1],v[2])),
                    edgeSpan(positions,edge(v[2],v[0]))});
                shapeFirst=shouldRepairShapeBeforeDensity(
                    smallest,accepted(f.kind,limits).threshold,
                    localLongest,f.targetEdge,limits.shapeRepairScaleRatio);
            }
            // Shape changes remain first priority, but a shape-scale
            // triangle that can be COMPLETELY quality-restored by a
            // single conforming subdivision is not irreparable. Try
            // strictly hard-floor-safe proposals only after all shape
            // operations have demonstrably declined.
            bool shapeAlreadyAttempted=false;
            if(shapeFirst) {
                ++out.scaleAwareShapeHandoffs;
                if(tryShapeEdits(lowest,smallest)) {
                    stagnant=0;
                    continue;
                }
                shapeAlreadyAttempted=true;
            }
            if(largestUnsafeRatio<=1&&lowest==faces.size())break;
            if(out.scaleAwareSplitOperations>=
               limits.maxScaleAwareSplitOperations) {
                const auto reason="R06-M17C finite scale-aware split "
                                  "budget exhausted; no subdivision cascade";
                if(limits.inspectionWarnQuality){warn(reason);break;}
                throw std::length_error(reason);
            }
            // For an actual under-quality coarse face, address ITS scale,
            // not an unrelated longest edge. Otherwise do density work.
            std::vector<Edge> candidates;
            if(lowest!=faces.size()) {
                const auto v=faces[lowest].triangle();
                for(unsigned k=0;k<3;++k)
                    candidates.push_back(edge(v[k],v[(k+1)%3]));
                std::sort(candidates.begin(),candidates.end(),
                    [&](Edge a,Edge b) {
                        const auto al=edgeSpan(positions,a);
                        const auto bl=edgeSpan(positions,b);
                        return al!=bl?al>bl:a<b;
                    });
            }else {
                for(const auto& [ratio,e]:longCandidates) {
                    (void)ratio;
                    candidates.push_back(e);
                }
            }
            std::set<Edge> attempted;
            bool progressed=false;
            for(const auto& e:candidates) {
                if(!attempted.insert(e).second)continue;
                const auto splitPlan=planScaleEdge(e);
                if(!splitPlan||!splitPlan->hasSelection())continue;
                const auto chosen=splitPlan->candidates[splitPlan->selected];
                const auto first=scaleSplit(e,chosen.firstT);
                progressed=true;
                stagnant=0;
                if(chosen.kind==RepairSplitTemplateKind::Hybrid&&
                   first.size()>0&&
                   chosen.secondT!=.5&&
                   out.scaleAwareSplitOperations<
                       limits.maxScaleAwareSplitOperations) {
                    // The follow-up is NEVER an unexamined inherited cut.
                    // Rebuild both incident source-backed neighborhoods and
                    // re-evaluate the proposed child parameter after the
                    // first complete mirrored transaction.
                    const auto child=chosen.secondT<.5?
                        edge(e.first,first.front()):
                        edge(first.front(),e.second);
                    const auto desired=positions.at(e.first)+
                        (positions.at(e.second)-positions.at(e.first))*
                        chosen.secondT;
                    const auto start=positions.at(child.first);
                    const auto span=positions.at(child.second)-start;
                    const auto span2=dot(span,span);
                    const double childT=span2>0?
                        dot(desired-start,span)/span2:0.;
                    const auto revised=planScaleEdge(child);
                    if(revised&&revised->hasSelection()) {
                        for(const auto& choice:revised->candidates) {
                            if(choice.kind!=RepairSplitTemplateKind::OffCenter||
                               !choice.admissible||
                               std::abs(choice.firstT-childT)>1e-9)
                                continue;
                            const auto next=scaleSplit(child,childT);
                            out.scaleAwareHybridCorrections+=next.size();
                            break;
                        }
                    }
                }
                break;
            }
            if(progressed)continue;
            if(!shapeAlreadyAttempted) {
                ++out.scaleAwareShapeHandoffs;
                if(tryShapeEdits(lowest,smallest))continue;
            }
            if(shapeFirst) {
                const auto& f=faces.at(lowest);
                const auto v=f.triangle();
                std::ostringstream info;
                info<<std::setprecision(17)
                    <<"face="<<v[0]<<","<<v[1]<<","<<v[2]
                    <<" q="<<smallest
                    <<" floor="<<accepted(f.kind,limits).threshold
                    <<" local_target="<<f.targetEdge
                    <<" scale_crossover="<<limits.shapeRepairScaleRatio
                    <<" result=NO_HARD_FLOOR_RESTORING_CONFORMING_SPLIT"
                    <<" operations_remaining="<<
                       limits.maxScaleAwareSplitOperations-out.scaleAwareSplitOperations;
                staged.reportRefinementAttempt(
                    "TryQualityRestoringSplit",info.str());
            }
            const auto reason=shapeFirst?
                "R06-M17C under-quality at shape scale; no admissible "
                "flip/cap-fan-collapse/interior-fairing, and no one-step "
                "two-sided split restores the hard child quality floors"
                " face="+std::to_string(faces[lowest].triangle()[0])+","+
                         std::to_string(faces[lowest].triangle()[1])+","+
                         std::to_string(faces[lowest].triangle()[2])+
                " q="+std::to_string(smallest):
                "R06-M17C no quality-safe bounded split "
                "or admissible shape edit; refuse rather than repeat "
                "a pathological subdivided needle";
            if(limits.inspectionWarnQuality){warn(reason);break;}
            throw std::logic_error(reason);
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
            const auto v=candidate.triangle();
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
                    for(const auto& [id,q]:positions) {
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
                        faces,positions,e,limits.minSurvivorQuality);
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
                                faces,positions,*mirror,
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
            out.refinementSteps+=splitMirrored(chosen,false).size();
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
                    adjacency[edge(f.triangle()[k],
                                   f.triangle()[(k+1)%3])].push_back(i);
            }
            if(newFaceCount+2>limits.maxNewFaces||
               faces.size()+2>limits.maxAuditedFaces+limits.maxNewFaces)
                break;
            Edge chosen{};
            double highestRatio=1;
            for(const auto& f:faces) {
                if(f.kind!=MixedClosureFaceKind::Cap)continue;
                const auto& v=f.triangle();
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
            const auto count=splitMirrored(chosen,true).size();
            if(!count)break;
            out.refinementSteps+=count;
            out.spatialDensitySplits+=count;
        }
    }
    if(staged.usedSpatialCapFallback)
        for(const auto& f:faces) {
            if(f.kind!=MixedClosureFaceKind::Cap)continue;
            for(unsigned k=0;k<3;++k) {
                const auto e=edge(f.triangle()[k],f.triangle()[(k+1)%3]);
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
        const auto& v=f.triangle();
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
               f,positions,proposal.contours,requirements.threshold)) {
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
    // M14E: freeze ONE complete explicit face/vertex ledger. Every
    // original survivor override (including a remote split neighbor)
    // must be represented, or reject BEFORE committing this private view.
    // Generators may have had open/nonmanifold intermediate geometry;
    // only the consolidated candidate is evaluated for manifoldness.
    phase="patch_consolidation";
    const auto patch=planPrivateMixedClosurePatch(
        staged,limits.maxAuditedFaces+limits.maxNewFaces);
    const auto applied=applyPrivateMixedClosurePatch(
        view,*cut.index,patch,limits.maxNewFaces);
    owners.insert(applied.touchedPartitions.begin(),
                  applied.touchedPartitions.end());
    for(std::size_t i=0;i<faces.size();++i) {
        const auto& f=faces[i];
        owners.insert(f.owner);
        if(f.stableId) {
            if(deficient[i])out.qualityWarningFaces.push_back(f.stableId);
            if(certifiedTip[i])out.boundaryTipQualityExemptFaces.push_back(f.stableId);
        }
    }
    for(const auto& [ordinal,id]:applied.createdFaces) {
        const auto& f=faces.at(ordinal);
        if(deficient.at(ordinal))out.qualityWarningFaces.push_back(id);
        if(certifiedTip.at(ordinal))out.boundaryTipQualityExemptFaces.push_back(id);
        if(f.kind==MixedClosureFaceKind::Cap)
            out.greenFaces.push_back(id);
        else if(f.kind==MixedClosureFaceKind::Zipper)
            out.blueFaces.push_back(id);
        // A new child of an ORIGINAL survivor edge split is legitimate
        // survivor geometry, not a green/blue closure face. It must be
        // materialized in the same private patch and count against budget.
    }
    out.newFaces=applied.createdFaces.size();
    out.touchedPartitions.assign(owners.begin(),owners.end());
    const auto revision=view.commit().revision;
    auto closed=RegionTopologyIndex::restore(
        revision,cut.index->retiredVertexIds(),cut.index->retiredFaceIds());
    phase="closed_manifold_audit";
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
    out.splitEvents=staged.splitEvents();
    out.index=std::move(closed);
    if(limits.terminalObserver) {
        limits.terminalObserver({
            MixedClosureTerminalOutcome::Completed,"completed",
            "private closed index prepared; generated_faces="+
                std::to_string(out.newFaces)+
                " inserted_vertices="+std::to_string(out.insertedVertices.size())+
                " warning_faces="+std::to_string(out.qualityWarningFaces.size()),
            limits.inspectionWarnQuality});
    }
    return out;
    }catch(const RepairStepCancelled&) {
        // User cancellation is not an algorithmic closure refusal.
        throw;
    }catch(const RepairStepObserverFailure& e) {
        // No geometry inference: the staging algorithm may have been valid,
        // but its opt-in observer failed before letting it proceed.
        if(limits.terminalObserver) {
            try {
                limits.terminalObserver({
                    MixedClosureTerminalOutcome::ObserverFailed,phase,e.what(),
                    limits.inspectionWarnQuality});
            }catch(...) {}
        }
        throw;
    }catch(const std::exception& e) {
        // Preserve the original algorithmic exception even if the observer
        // itself has failed. No private revision has been published.
        if(limits.terminalObserver) {
            try {
                limits.terminalObserver({
                    MixedClosureTerminalOutcome::Refused,phase,e.what(),
                    limits.inspectionWarnQuality});
            }catch(...) {}
        }
        throw;
    }
}

// R06-M22C: an optional projected-ear attempt is never permitted to turn
// a potentially valid AFM closure into a final refusal. The first attempt
// edits only a private candidate; on ANY geometric/budget rejection retry
// the same immutable R03 source with ears disabled. Observer cancellation
// and observer failures are not evidence of geometry failure.
MixedPrivateClosureResult applyPrivateMixedClosure(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const AutoClosurePlan& plan,
    const MixedPrivateClosureLimits& limits) {
    if(!limits.staging.tryProjectedEarClipping)
        return applyPrivateMixedClosureOnce(cut,proposal,plan,limits);
    auto first=limits;
    // An unsuccessful speculative attempt is not a final closure outcome.
    // Leave the terminal observer for the final success/failure verdict.
    first.terminalObserver={};
    std::string rejection;
    try {
        auto answer=applyPrivateMixedClosureOnce(cut,proposal,plan,first);
        if(limits.terminalObserver) {
            limits.terminalObserver({
                MixedClosureTerminalOutcome::Completed,"completed",
                "private closed index prepared after projected-ear enabled trial;"
                " generated_faces="+std::to_string(answer.newFaces)+
                " inserted_vertices="+std::to_string(answer.insertedVertices.size()),
                limits.inspectionWarnQuality});
        }
        return answer;
    }catch(const RepairStepCancelled&) {
        throw;
    }catch(const RepairStepObserverFailure&) {
        throw;
    }catch(const std::logic_error& e) {
        rejection=e.what();
    }catch(const std::length_error& e) {
        rejection=e.what();
    }
    std::cerr<<"[repair-r06-cap-metrics] method=projected-ear"
             <<" status=SOURCE_CLOSURE_REFUSED"
             <<" reason="<<rejection
             <<" next=advancing-front"
             <<" private_retry=1 source_certified=0\\n";
    auto retry=limits;
    retry.staging.tryProjectedEarClipping=false;
    return applyPrivateMixedClosureOnce(cut,proposal,plan,retry);
}
} // namespace mesher

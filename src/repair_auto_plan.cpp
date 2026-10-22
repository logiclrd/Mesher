#include "mesher/repair_auto_plan.hpp"
#include "repair_closure_internal.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>
#include <optional>
#include <numeric>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=RegionTopologyIndex::Edge;
Edge edge(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
struct DisjointSets {
    std::vector<std::size_t> parents;
    explicit DisjointSets(std::size_t count):parents(count) {
        std::iota(parents.begin(),parents.end(),std::size_t{0});
    }
    std::size_t root(std::size_t i) {
        if(parents.at(i)==i)return i;
        return parents[i]=root(parents[i]);
    }
    void join(std::size_t a,std::size_t b) {
        parents[root(a)]=root(b);
    }
};
struct Refusal : std::runtime_error {
    AutoClosurePlanStatus status;
    Refusal(AutoClosurePlanStatus kind,const std::string& why)
        :std::runtime_error(why),status(kind) {}
};
void require(bool yes,AutoClosurePlanStatus status,const char* why) {
    if(!yes)throw Refusal(status,why);
}
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
double projectedArea(const RegionTopologyIndex& index,
                     const RepairContour& contour) {
    const auto& ids=contour.vertexIds;
    require(ids.size()>=3,AutoClosurePlanStatus::InvalidInput,
            "R06-M03 contour has fewer than three rim vertices");
    const auto origin=index.vertex(ids.front()).position;
    Vec3 area{};
    for(std::size_t i=0;i<ids.size();++i) {
        const auto a=index.vertex(ids[i]).position-origin;
        const auto b=index.vertex(ids[(i+1)%ids.size()]).position-origin;
        require(finite(a)&&finite(b),AutoClosurePlanStatus::InvalidInput,
                "R06-M03 nonfinite rim vertex");
        area=area+cross(a,b);
    }
    const auto actual=length(area)/2;
    const auto squared=contour.perimeter*contour.perimeter;
    require(std::isfinite(actual)&&std::isfinite(squared)&&squared>0&&
            actual>squared*1e-14,
            AutoClosurePlanStatus::AmbiguousFitness,
            "R06-M03 no reliable positive cap area estimate");
    return actual;
}
struct TwoRimEvidence {
    double separation{},alignment{},maxNearestEdgeUnits{};
    double caps{},zipperFrustum{};
};
std::optional<TwoRimEvidence> inspectTwoRims(const RegionTopologyIndex& mesh,
    const RepairContour& first,const RepairContour& second) {
    // Read-only instrumentation must NEVER change M03's plan: V15 and
    // the later true-geometry validator, not this advisory 3D proxy,
    // decide whether malformed or folded contours are admissible.
    try {
    const auto describe=[&](const RepairContour& loop) {
        Vec3 center{},normal{};
        const auto& ids=loop.vertexIds;
        for(std::size_t i=0;i<ids.size();++i) {
            const auto a=mesh.vertex(ids[i]).position;
            const auto b=mesh.vertex(ids[(i+1)%ids.size()]).position;
            center=center+a;
            normal=normal+cross(a,b);
        }
        center=center/static_cast<double>(ids.size());
        const auto norm=length(normal);
        if(!(norm>0)||!std::isfinite(norm))
            throw Refusal(AutoClosurePlanStatus::AmbiguousFitness,
                          "R06-M06A malformed geometric rim normal");
        return std::pair{center,normal/norm};
    };
    const auto [centerA,normalA]=describe(first);
    const auto [centerB,normalB]=describe(second);
    const auto areaA=projectedArea(mesh,first);
    const auto areaB=projectedArea(mesh,second);
    const auto radiusA=std::sqrt(areaA/3.14159265358979323846);
    const auto radiusB=std::sqrt(areaB/3.14159265358979323846);
    const auto oneWay=[&](const RepairContour& source,
                          const RepairContour& dest) {
        double furthest=0;
        for(const auto id:source.vertexIds) {
            const auto p=mesh.vertex(id).position;
            double best=std::numeric_limits<double>::infinity();
            for(const auto other:dest.vertexIds)
                best=std::min(best,length(p-mesh.vertex(other).position));
            furthest=std::max(furthest,best);
        }
        return furthest;
    };
    const auto edgeSize=std::min(first.meanEdgeLength,second.meanEdgeLength);
    if(!(edgeSize>0)||!std::isfinite(edgeSize))
        throw Refusal(AutoClosurePlanStatus::InvalidInput,
                      "R06-M06A invalid local rim edge scale");
    const auto separation=length(centerA-centerB);
    return TwoRimEvidence{separation,dot(normalA,normalB),
            std::max(oneWay(first,second),oneWay(second,first))/edgeSize,
            areaA+areaB,
            3.14159265358979323846*(radiusA+radiusB)*
               std::hypot(separation,radiusA-radiusB)};
    }catch(const std::exception&) {
        return std::nullopt;
    }
}
void validateSettings(const AutoClosurePlanSettings& s) {
    require(s.maxExcisionFaces>0&&s.maxContours>0&&s.maxContourVertices>0&&
            s.maxContactPairs>0&&s.maxRegions>0&&s.maxLoopsPerRegion>0&&
            s.maxLoopsPerRegion<=4&&s.maxAuditedFaces>0&&s.maxAuditedEdges>0&&
            std::isfinite(s.minimumZipperSavings)&&
            std::isfinite(s.minimumSameOwnerZipperSavings)&&
            s.minimumZipperSavings>0&&s.minimumZipperSavings<1&&
            s.minimumSameOwnerZipperSavings>=s.minimumZipperSavings&&
            s.minimumSameOwnerZipperSavings<1&&
            std::isfinite(s.minimumFacingContactZipperSavings)&&
            s.minimumFacingContactZipperSavings>=s.minimumZipperSavings&&
            s.minimumFacingContactZipperSavings<=s.minimumSameOwnerZipperSavings&&
            std::isfinite(s.maximumFacingContactGapInEdges)&&
            s.maximumFacingContactGapInEdges>0&&
            std::isfinite(s.minimumFacingContactNormalOpposition)&&
            s.minimumFacingContactNormalOpposition>0&&
            s.minimumFacingContactNormalOpposition<=1,
            AutoClosurePlanStatus::InvalidInput,
            "R06-M03 invalid finite planning limits");
    require(s.fitness.minimumRelativeAreaAdvantage>=0&&
            s.fitness.minimumRelativeAreaAdvantage<
                s.minimumZipperSavings&&
            s.fitness.maxContours>0,
            AutoClosurePlanStatus::InvalidInput,
            "R06-M03 invalid V15 area-fitness margins");
}
} // namespace

AutoClosurePlan planPrivateAutomaticClosure(
    const RegionTopologyIndex& sourceCandidate,
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const std::vector<RegionTopologyIndex::CollisionPair>& trustedContacts,
    const AutoClosurePlanSettings& limits) {
    AutoClosurePlan out;
    try {
        validateSettings(limits);
        require(cut.index&&proposal.complete&&!proposal.proposedFaces.empty(),
                AutoClosurePlanStatus::InvalidInput,
                "R06-M03 requires complete real R02/R03 excision");
        require(proposal.proposedFaces.size()<=limits.maxExcisionFaces&&
                trustedContacts.size()<=limits.maxContactPairs&&
                proposal.contours.size()<=limits.maxContours,
                AutoClosurePlanStatus::BudgetExceeded,
                "R06-M03 excision/contact/contour budget exceeded");
        for(const auto& contour:proposal.contours)
            require(contour.vertexIds.size()<=limits.maxContourVertices,
                    AutoClosurePlanStatus::BudgetExceeded,
                    "R06-M03 contour vertex limit");
        try {
            out.survivors=inventoryPrivateExcision(cut,proposal,
                {limits.maxAuditedFaces,limits.maxAuditedEdges,limits.maxContours});
        }catch(const std::length_error& e) {
            throw Refusal(AutoClosurePlanStatus::BudgetExceeded,e.what());
        }catch(const std::exception& e) {
            throw Refusal(AutoClosurePlanStatus::InvalidInput,e.what());
        }

        const std::set<FaceId> retired(proposal.proposedFaces.begin(),
                                       proposal.proposedFaces.end());
        require(retired.size()==proposal.proposedFaces.size(),
                AutoClosurePlanStatus::InvalidInput,
                "R06-M03 repeated removed stable FaceId");
        for(const auto id:retired)
            require(sourceCandidate.maybeFace(id)&&!cut.index->maybeFace(id)&&
                    cut.index->retiredFace(id),
                    AutoClosurePlanStatus::InvalidInput,
                    "R06-M03 source/cut removed face identity disagreement");
        // Determine connected EXCISION patches independently of surviving
        // shell components, which can connect distant repair islands.
        std::map<FaceId,std::size_t> patchOf;
        for(const auto seed:retired) {
            if(patchOf.contains(seed))continue;
            const auto patch=out.excisionPatches++;
            std::vector<FaceId> pending{seed};
            while(!pending.empty()) {
                const auto id=pending.back();pending.pop_back();
                if(patchOf.contains(id))continue;
                patchOf[id]=patch;
                const auto tri=sourceCandidate.face(id).vertices;
                for(unsigned k=0;k<3;++k) {
                    const auto sides=sourceCandidate.maybeEdgeSides(
                        edge(tri[k],tri[(k+1)%3]));
                    require(sides&&sides->size()==2,
                        AutoClosurePlanStatus::InvalidInput,
                        "R06-M03 original excision edge not two-sided");
                    for(const auto& side:*sides)
                        if(retired.contains(side.face)&&!patchOf.contains(side.face))
                            pending.push_back(side.face);
                }
            }
        }
        require(out.excisionPatches<=limits.maxRegions,
                AutoClosurePlanStatus::BudgetExceeded,
                "R06-M03 exceeded excision region count");
        DisjointSets dsu(out.excisionPatches);
        std::set<std::pair<std::size_t,std::size_t>> directLinks;
        for(const auto& link:trustedContacts) {
            require(link.first!=link.second&&!link.ambiguous&&
                    patchOf.contains(link.first)&&patchOf.contains(link.second),
                    AutoClosurePlanStatus::InvalidInput,
                    "R06-M03 contact is ambiguous or outside excised face set");
            auto a=patchOf.at(link.first),b=patchOf.at(link.second);
            if(a==b)continue;
            if(a>b)std::swap(a,b);
            directLinks.insert({a,b});
            dsu.join(a,b);
            ++out.trustedContactLinks;
        }

        // Every surviving directed rim edge must have exactly one
        // corresponding deleted source face on the opposite side.
        std::vector<std::size_t> loopPatch(
            proposal.contours.size(),out.excisionPatches);
        std::vector<std::size_t> loopSurvivor(
            proposal.contours.size(),out.survivors.components.size());
        for(std::size_t c=0;c<out.survivors.components.size();++c)
            for(const auto loop:out.survivors.components[c].contourIndices)
                loopSurvivor.at(loop)=c;
        for(std::size_t i=0;i<proposal.contours.size();++i) {
            const auto& loop=proposal.contours[i];
            require(loopSurvivor[i]<out.survivors.components.size(),
                    AutoClosurePlanStatus::InvalidInput,
                    "R06-M03 boundary loop lacks a surviving face component");
            for(std::size_t k=0;k<loop.vertexIds.size();++k) {
                const auto a=loop.vertexIds[k];
                const auto b=loop.vertexIds[(k+1)%loop.vertexIds.size()];
                const auto sides=sourceCandidate.maybeEdgeSides(edge(a,b));
                require(sides&&sides->size()==2,
                    AutoClosurePlanStatus::InvalidInput,
                    "R06-M03 rim edge missing in source candidate");
                FaceId deleted=0;
                bool survivorSeen=false;
                for(const auto& side:*sides) {
                    if(side.face==loop.survivingFaces[k])survivorSeen=true;
                    else if(retired.contains(side.face))deleted=side.face;
                }
                require(survivorSeen&&deleted!=0,
                    AutoClosurePlanStatus::InvalidInput,
                    "R06-M03 rim has no opposed retired source face");
                const auto current=patchOf.at(deleted);
                if(loopPatch[i]==out.excisionPatches)loopPatch[i]=current;
                else require(loopPatch[i]==current,
                    AutoClosurePlanStatus::AmbiguousRegion,
                    "R06-M03 one directed rim borders multiple excision patches");
            }
        }

        std::map<std::size_t,std::size_t> regionOf;
        for(std::size_t patch=0;patch<out.excisionPatches;++patch) {
            const auto root=dsu.root(patch);
            if(!regionOf.contains(root)) {
                require(out.regions.size()<limits.maxRegions,
                    AutoClosurePlanStatus::BudgetExceeded,
                    "R06-M03 too many independent repair regions");
                regionOf[root]=out.regions.size();
                out.regions.emplace_back();
            }
            out.regions[regionOf.at(root)].excisionPatches.push_back(patch);
        }
        for(std::size_t loop=0;loop<loopPatch.size();++loop)
            out.regions.at(regionOf.at(dsu.root(loopPatch.at(loop))))
                       .contours.push_back(loop);
        for(const auto& [a,b]:directLinks)
            ++out.regions.at(regionOf.at(dsu.root(a))).contactLinks;
        out.geometryRegions=out.regions.size();

        for(auto& region:out.regions) {
            const auto n=region.contours.size();
            if(n==2) {
                const auto info=inspectTwoRims(
                    *cut.index,proposal.contours[region.contours[0]],
                    proposal.contours[region.contours[1]]);
                if(info) {
                    region.twoRimEvidenceValid=true;
                    region.rimCentroidSeparation=info->separation;
                    region.rimNormalAlignment=info->alignment;
                    region.rimNearestSeparationInEdges=info->maxNearestEdgeUnits;
                    region.estimatedCapArea=info->caps;
                    region.estimatedZipperArea=info->zipperFrustum;
                }
            }
            if(!n) {
                region.selectionReason="zero-rim entire removed component; M02 must certify material";
                // The cut may already have retired a whole closed component.
                // Only R06-M04 plus M02 material evidence may authorize it.
                region.zeroLoopComponentRetirement=true;
                continue;
            }
            require(n<=limits.maxLoopsPerRegion,
                    AutoClosurePlanStatus::AmbiguousRegion,
                    "R06-M03 ambiguous many-to-many contact region (>4 loops)");
            RepairPlan local;
            for(const auto global:region.contours)
                local.contours.push_back(proposal.contours[global]);
            RepairPlanSettings fitness=limits.fitness;
            fitness.maxContours=limits.maxLoopsPerRegion;
            // R05 real capper is more tolerant of warped cuts than V15's
            // original nearly planar circular-frustum heuristic. Estimates
            // are still provisional; M04 must validate actual triangles.
            fitness.maximumPlanarityInRadii=std::max(
                fitness.maximumPlanarityInRadii,0.75);
            fitness.maximumIsoperimetricRatio=std::max(
                fitness.maximumIsoperimetricRatio,3.0);
            fitness.maximumSingleCapPlanarityInRadii=std::max(
                fitness.maximumSingleCapPlanarityInRadii,
                fitness.maximumPlanarityInRadii);
            fitness.maximumSingleCapIsoperimetricRatio=std::max(
                fitness.maximumSingleCapIsoperimetricRatio,
                fitness.maximumIsoperimetricRatio);
            const auto choice=selectRepairClosures(*cut.index,local,fitness);
            if(!choice.ready()) {
                region.usedAmbiguousCapFallback=true;
                region.selectionReason="V15 ambiguous; two-cap fallback: "+choice.reason;
                // One loop has no possible zipper competitor. Two loops on
                // ONE survivor can safely express handle deletion via
                // separate disks, even if V15's annular fit is unsupported.
                bool oneOwner=true;
                for(const auto global:region.contours)
                    oneOwner &=loopSurvivor.at(global)==
                               loopSurvivor.at(region.contours.front());
                require((n==1 || (n==2 && oneOwner)) &&
                        choice.status==RepairPlanStatus::AmbiguousClosure,
                    AutoClosurePlanStatus::AmbiguousFitness,
                    choice.reason.c_str());
                for(const auto global:region.contours) {
                    region.caps.push_back(global);
                    region.estimatedAddedArea+=
                        projectedArea(*cut.index,proposal.contours[global]);
                }
            }else {
                region.selectionReason="V15 area-DP choice";
                if(!choice.cappedContours.empty())
                    region.selectionReason+=": independent caps="+
                        std::to_string(choice.cappedContours.size());
                for(const auto localCap:choice.cappedContours)
                    region.caps.push_back(region.contours.at(localCap));
                for(const auto& matched:choice.pairs) {
                    const auto a=matched.first,b=matched.second;
                    const auto ga=region.contours.at(a),
                               gb=region.contours.at(b);
                    auto pa=loopPatch.at(ga),pb=loopPatch.at(gb);
                    if(pa>pb)std::swap(pa,pb);
                    require(pa==pb || directLinks.contains({pa,pb}),
                        AutoClosurePlanStatus::AmbiguousRegion,
                        "R06-M03 inferred zipper without direct trusted contact");
                    const auto found=std::find_if(
                        choice.closureChoices.begin(),choice.closureChoices.end(),
                        [&](const RepairClosureChoice& x) {
                            return x.kind==ClosureKind::Zipper &&
                                   x.first==a && x.second==b;
                        });
                    require(found!=choice.closureChoices.end()&&
                            found->capArea>0&&std::isfinite(found->zipperArea),
                        AutoClosurePlanStatus::AmbiguousFitness,
                        "R06-M03 missing finite V15 zipper comparison");
                    const auto savings=1-found->zipperArea/found->capArea;
                    const bool sameSurvivor=
                        loopSurvivor.at(ga)==loopSurvivor.at(gb);
                    auto patchA=loopPatch.at(ga),patchB=loopPatch.at(gb);
                    if(patchA>patchB)std::swap(patchA,patchB);
                    // Exact R01 contact of TWO removed face patches,
                    // extremely opposing surviving contour normals,
                    // and a small local mesh-scale gap together support
                    // a shorter same-shell bridge without globally
                    // defeating the torus-handle penalty.
                    const bool facingContact=
                        region.twoRimEvidenceValid&&
                        sameSurvivor&&n==2&&patchA!=patchB&&
                        directLinks.contains({patchA,patchB})&&
                        region.rimNormalAlignment<=
                            -limits.minimumFacingContactNormalOpposition&&
                        region.rimNearestSeparationInEdges<=
                            limits.maximumFacingContactGapInEdges;
                    const double threshold=!sameSurvivor?
                        limits.minimumZipperSavings:
                        (facingContact?
                            limits.minimumFacingContactZipperSavings:
                            limits.minimumSameOwnerZipperSavings);
                    if(facingContact)
                        region.selectionReason+="; directly linked close opposing rims";
                    if(savings<threshold) {
                        region.selectionReason+="; zipper downgraded to caps: "+
                            std::to_string(savings)+" < "+
                            std::to_string(threshold)+
                            (!sameSurvivor?" separate-surface threshold":
                             (facingContact?" close-opposing same-surface threshold":
                              " ordinary same-surface threshold"));
                        // Reject a long/slender unnecessary same-shell seam:
                        // two caps are preferable to retaining a tunnel.
                        region.caps.push_back(ga);
                        region.caps.push_back(gb);
                    }else {
                        region.selectionReason+="; zipper accepted: saving="+
                            std::to_string(savings)+" threshold="+
                            std::to_string(threshold);
                        auto globalMatch=matched;
                        globalMatch.first=ga;globalMatch.second=gb;
                        region.zippers.push_back(globalMatch);
                    }
                }
                // Recompute cost after possible conservative zipper-to-cap
                // downgrades, instead of copying V15's now-stale total.
                for(const auto global:region.caps)
                    region.estimatedAddedArea+=
                        projectedArea(*cut.index,proposal.contours[global]);
                for(const auto& pair:region.zippers) {
                    const auto a=static_cast<std::size_t>(std::distance(
                        region.contours.begin(),
                        std::find(region.contours.begin(),
                                  region.contours.end(),pair.first)));
                    const auto b=static_cast<std::size_t>(std::distance(
                        region.contours.begin(),
                        std::find(region.contours.begin(),
                                  region.contours.end(),pair.second)));
                    const auto found=std::find_if(
                        choice.closureChoices.begin(),choice.closureChoices.end(),
                        [&](const RepairClosureChoice& x) {
                            return x.kind==ClosureKind::Zipper &&
                                   x.first==a&&x.second==b;
                        });
                    require(found!=choice.closureChoices.end(),
                        AutoClosurePlanStatus::InvalidInput,
                        "R06-M03 lost selected zipper fitness");
                    region.estimatedAddedArea+=found->zipperArea;
                }
            }
            std::set<std::size_t> used;
            for(const auto cap:region.caps)
                require(used.insert(cap).second,
                    AutoClosurePlanStatus::InvalidInput,
                    "R06-M03 selected contour twice as cap");
            for(const auto& seam:region.zippers)
                require(used.insert(seam.first).second&&
                        used.insert(seam.second).second,
                    AutoClosurePlanStatus::InvalidInput,
                    "R06-M03 selected contour twice as zipper");
            require(used.size()==n&&std::isfinite(region.estimatedAddedArea),
                AutoClosurePlanStatus::InvalidInput,
                "R06-M03 closure has missing loops or nonfinite cost");
            out.estimatedAddedArea+=region.estimatedAddedArea;
            out.cappedContours.insert(out.cappedContours.end(),
                                      region.caps.begin(),region.caps.end());
            out.zipperPairs.insert(out.zipperPairs.end(),
                                   region.zippers.begin(),region.zippers.end());
        }
        std::sort(out.cappedContours.begin(),out.cappedContours.end());
        require(out.cappedContours.size()+2*out.zipperPairs.size()==
                proposal.contours.size(),
                AutoClosurePlanStatus::InvalidInput,
                "R06-M03 incomplete global region closure");
        out.status=AutoClosurePlanStatus::Ready;
        out.diagnostic="R06-M03 topology decision only; unvalidated R06-M04 geometry";
    }catch(const Refusal& e) {
        out.status=e.status;
        out.diagnostic=e.what();
        out.regions.clear();
        out.cappedContours.clear();
        out.zipperPairs.clear();
        out.estimatedAddedArea=0;
    }catch(const std::length_error& e) {
        out.status=AutoClosurePlanStatus::BudgetExceeded;
        out.diagnostic=e.what();
        out.regions.clear();out.cappedContours.clear();out.zipperPairs.clear();
        out.estimatedAddedArea=0;
    }catch(const std::exception& e) {
        out.status=AutoClosurePlanStatus::InvalidInput;
        out.diagnostic=e.what();
        out.regions.clear();out.cappedContours.clear();out.zipperPairs.clear();
        out.estimatedAddedArea=0;
    }
    return out;
}
} // namespace mesher

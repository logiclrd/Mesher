#include "mesher/repair_surgery.hpp"
#include "mesher/repair_intersection.hpp"
#include "mesher/versioned_collapse.hpp"
#include "mesher/versioned_refine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
bool same(Vec3 a,Vec3 b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}

// This validation is intentionally independent of the Step under review.
// In particular, an attacker-supplied "first reference" cannot redefine
// the seven witness positions or scale to hide a cumulative deformation.
SourceSplitLineage verifyLifetime(
    const OriginalPatchLifetime& lifetime,
    std::size_t maxLivingDescendants) {
    if(!lifetime.anchors.original||!lifetime.lineage.original||
       !lifetime.lineage.terminal||
       lifetime.anchors.original!=lifetime.lineage.original||
       lifetime.anchors.faces.empty()||
       lifetime.lineage.living.size()!=lifetime.anchors.faces.size())
        throw std::invalid_argument("Missing or substituted lifetime first-original root");
    if(!std::is_sorted(lifetime.certifiedRetiredOriginals.begin(),
                       lifetime.certifiedRetiredOriginals.end())||
       std::adjacent_find(lifetime.certifiedRetiredOriginals.begin(),
                          lifetime.certifiedRetiredOriginals.end())!=
           lifetime.certifiedRetiredOriginals.end())
        throw std::logic_error("Duplicate or unordered source retirement authority");
    std::set<FaceId> originalIds,liveIds;
    std::vector<FaceId> expectedRetired;
    SourceSplitLineage active;
    active.original=lifetime.lineage.original;
    active.terminal=lifetime.lineage.terminal;
    active.verifiedFaceSplits=lifetime.lineage.verifiedFaceSplits;
    std::size_t liveCount=0;
    for(std::size_t i=0;i<lifetime.anchors.faces.size();++i) {
        const auto& anchor=lifetime.anchors.faces[i];
        const auto& [first,faces]=lifetime.lineage.living[i];
        if(!first||!originalIds.insert(first).second||
           first!=anchor.face)
            throw std::logic_error("First-original protected FaceIds no longer correspond");
        const auto recomputed=captureOriginalSourceAnchors(
            lifetime.anchors.original,{first},7).faces.front();
        if(recomputed.originalVertices!=anchor.originalVertices||
           recomputed.originalEdgeLength!=anchor.originalEdgeLength)
            throw std::logic_error("Fixed-original source incidence or edge scale was substituted");
        for(std::size_t j=0;j<7;++j)
            if(!same(recomputed.witnesses[j],anchor.witnesses[j]))
                throw std::logic_error("First-original immutable witnesses were rewritten");
        if(faces.empty()) {
            expectedRetired.push_back(first);
            if(lifetime.lineage.terminal->maybeFace(first))
                throw std::logic_error("Certified retired original FaceId was resurrected");
            continue;
        }
        if(faces.size()>maxLivingDescendants-liveCount)
            throw std::length_error("Lifetime protected descendant limit exhausted");
        liveCount+=faces.size();
        std::set<FaceId> within;
        for(const auto id:faces)
            if(!id||!within.insert(id).second||
               !liveIds.insert(id).second||
               !lifetime.lineage.terminal->maybeFace(id))
                throw std::logic_error("Missing, duplicated or foreign live descendant ID");
        active.living.emplace_back(first,faces);
    }
    if(expectedRetired!=lifetime.certifiedRetiredOriginals)
        throw std::logic_error("Retired original source lacks certified c3/M03 provenance");
    return active;
}

void ensureBudget(std::size_t consumed,std::size_t next,
                  std::size_t maximum) {
    if(consumed>maximum||next>maximum-consumed)
        throw std::length_error("Cumulative source patch evidence work cap exhausted");
}

OriginalPatchLifetime applyVerified(
    const OriginalPatchLifetime& earlier,
    const SourceSplitLineage& stage,
    const std::vector<FaceId>& newRetired,
    std::size_t additionalEvidence,
    std::size_t maxCumulativeFaceEvidence,
    std::size_t maxLivingDescendants) {
    ensureBudget(earlier.sourceFaceEvidence,additionalEvidence,
                 maxCumulativeFaceEvidence);
    if(stage.original!=earlier.anchors.original||!stage.terminal)
        throw std::logic_error("Stage changed immutable original source reference");
    OriginalPatchLifetime next=earlier;
    std::map<FaceId,std::vector<FaceId>> accepted;
    for(const auto& [first,faces]:stage.living)
        if(!accepted.emplace(first,faces).second)
            throw std::logic_error("Source stage duplicated original owner");
    std::set<FaceId> newlyRetired(newRetired.begin(),newRetired.end());
    if(newlyRetired.size()!=newRetired.size())
        throw std::logic_error("Stage duplicated certified original source retirement");
    for(auto& [first,faces]:next.lineage.living) {
        if(faces.empty()) {
            if(accepted.contains(first)||newlyRetired.contains(first))
                throw std::logic_error("Prior certified retired face reappeared");
            continue;
        }
        const auto it=accepted.find(first);
        if(it==accepted.end())
            throw std::logic_error("Stage dropped a protected original FaceId");
        faces=it->second;
        if(faces.empty()&&!newlyRetired.contains(first))
            throw std::logic_error("Missing classified Buried/Outside source retirement");
        if(!faces.empty()&&newlyRetired.contains(first))
            throw std::logic_error("Stage retired a still-living protected original");
        accepted.erase(it);
    }
    if(!accepted.empty())throw std::logic_error("Stage invented foreign source owners");
    next.lineage.terminal=stage.terminal;
    next.lineage.verifiedFaceSplits=stage.verifiedFaceSplits;
    next.sourceFaceEvidence+=additionalEvidence;
    if(next.acceptedTransitions==std::numeric_limits<std::size_t>::max())
        throw std::length_error("Lifetime accepted source transition count overflow");
    ++next.acceptedTransitions;
    next.certifiedRetiredOriginals.insert(
        next.certifiedRetiredOriginals.end(),newRetired.begin(),newRetired.end());
    std::sort(next.certifiedRetiredOriginals.begin(),
              next.certifiedRetiredOriginals.end());
    (void)verifyLifetime(next,maxLivingDescendants);
    return next;
}
std::size_t collapseFaceWork(const VersionedCollapseResult& result,
                             std::size_t maxWork) {
    std::size_t work=0;
    for(const auto& event:result.collapseSources) {
        if(!event.before)throw std::invalid_argument(
            "Incomplete source collapse root for lifetime evidence");
        std::set<FaceId> touched;
        for(const auto id:{event.survivor,event.retired})
            for(const auto f:event.before->vertex(id).incidentFaces)
                touched.insert(f);
        if(event.paired)
            for(const auto id:{event.mirrorSurvivor,event.mirrorRetired})
                for(const auto f:event.before->vertex(id).incidentFaces)
                    touched.insert(f);
        if(touched.size()>maxWork-work)
            throw std::length_error("Lifetime collapse full source halo work cap exhausted");
        work+=touched.size();
    }
    return work;
}
} // namespace

OriginalPatchLifetime beginOriginalPatchLifetime(
    const OriginalSourceSurfaceAnchors& anchors) {
    OriginalPatchLifetime state;
    state.anchors=anchors;
    state.lineage=beginSourceSplitLineage(anchors);
    (void)verifyLifetime(state,16384);
    return state;
}

OriginalPatchLifetime advanceOriginalPatchLifetime(
    const OriginalPatchLifetime& previous,const VersionedRefineResult& refined,
    std::size_t maxCumulativeFaceEvidence,
    std::size_t maxLivingDescendants) {
    auto active=verifyLifetime(previous,maxLivingDescendants);
    if(active.living.empty())
        throw std::logic_error("All original patches retired: no active refine ancestry");
    const auto cost=refined.splitSources.size()+refined.flipSources.size();
    if(cost<refined.splitSources.size())
        throw std::length_error("Refine face evidence count overflow");
    ensureBudget(previous.sourceFaceEvidence,cost,maxCumulativeFaceEvidence);
    const auto stage=advanceCertifiedRefineLineage(
        active,refined,maxCumulativeFaceEvidence-previous.sourceFaceEvidence,
        maxLivingDescendants);
    return applyVerified(previous,stage,{},cost,maxCumulativeFaceEvidence,
                         maxLivingDescendants);
}

OriginalPatchLifetime advanceOriginalPatchLifetime(
    const OriginalPatchLifetime& previous,const VersionedCollapseResult& collapsed,
    std::size_t maxCumulativeFaceEvidence,
    std::size_t maxLivingDescendants) {
    auto active=verifyLifetime(previous,maxLivingDescendants);
    if(active.living.empty())
        throw std::logic_error("All original patches retired: no active collapse ancestry");
    if(previous.sourceFaceEvidence>maxCumulativeFaceEvidence)
        throw std::length_error("Cumulative collapse source budget already exhausted");
    const auto remaining=maxCumulativeFaceEvidence-previous.sourceFaceEvidence;
    const auto cost=collapseFaceWork(collapsed,remaining);
    const auto stage=advanceCertifiedCollapseLineage(
        active,collapsed,remaining,maxLivingDescendants);
    return applyVerified(previous,stage,{},cost,maxCumulativeFaceEvidence,
                         maxLivingDescendants);
}

OriginalPatchLifetime advanceOriginalPatchLifetime(
    const OriginalPatchLifetime& previous,const ExactCutStep& step,
    const std::unordered_map<VertexId,VertexId>* originalMirrors,
    std::size_t maxCumulativeFaceEvidence,
    std::size_t maxLivingDescendants) {
    auto active=verifyLifetime(previous,maxLivingDescendants);
    if(active.living.empty())
        throw std::logic_error("All original patches retired: no active exact-cut ancestry");
    if(previous.sourceFaceEvidence>=maxCumulativeFaceEvidence)
        throw std::length_error("Cumulative exact-cut source budget exhausted");
    const auto remaining=maxCumulativeFaceEvidence-previous.sourceFaceEvidence;
    const auto certified=advanceCertifiedExactCutLineage(
        active,step,originalMirrors,remaining,maxLivingDescendants);
    return applyVerified(previous,certified.survivors,
                         certified.certifiedRetiredOrigins,
                         certified.checkedFaceEvidence,
                         maxCumulativeFaceEvidence,maxLivingDescendants);
}

OriginalPatchLifetimeAudit auditOriginalPatchLifetime(
    const OriginalPatchLifetime& lifetime,
    const std::shared_ptr<const RegionTopologyIndex>& expectedOriginal,
    std::size_t maxDistanceTests,
    std::size_t maxLivingDescendants) {
    if(!expectedOriginal||expectedOriginal!=lifetime.anchors.original)
        throw std::invalid_argument("Stale first-original lifetime fidelity reference");
    (void)verifyLifetime(lifetime,maxLivingDescendants);
    // PRE-FLIGHT entire 7 x number-of-live-descendants workload. Refuse
    // before sampling any source: no incomplete/partial "green" audit.
    std::size_t totalLiving=0,neededTests=0;
    for(const auto& [_,faces]:lifetime.lineage.living) {
        if(faces.size()>maxDistanceTests/7-totalLiving)
            throw std::length_error("First-original witness audit distance budget exhausted");
        totalLiving+=faces.size();
    }
    neededTests=7*totalLiving;
    if(neededTests>maxDistanceTests)
        throw std::length_error("First-original witness audit distance work overflow");
    OriginalPatchLifetimeAudit result;
    result.certifiedRetiredOriginals=lifetime.certifiedRetiredOriginals;
    result.originalSources=lifetime.anchors.faces.size();
    double meanSum=0,rmsEnergy=0;
    for(std::size_t i=0;i<lifetime.anchors.faces.size();++i) {
        const auto& fixed=lifetime.anchors.faces[i];
        const auto& descendants=lifetime.lineage.living[i].second;
        if(descendants.empty())continue;
        const std::vector<Vec3> witnesses(
            fixed.witnesses.begin(),fixed.witnesses.end());
        const auto sample=auditSurfaceWitnesses(
            *lifetime.lineage.terminal,witnesses,descendants,
            fixed.originalEdgeLength,7*descendants.size());
        result.perOriginal.push_back({
            fixed.face,sample,descendants.size()});
        result.total.witnessCount+=sample.witnessCount;
        result.total.candidateFaces+=sample.candidateFaces;
        result.total.distanceTests+=sample.distanceTests;
        result.total.maximumDistanceInEdges=std::max(
            result.total.maximumDistanceInEdges,sample.maximumDistanceInEdges);
        meanSum+=sample.meanDistanceInEdges*sample.witnessCount;
        rmsEnergy+=sample.rmsDistanceInEdges*
                   sample.rmsDistanceInEdges*sample.witnessCount;
        ++result.livingOriginals;
    }
    if(result.total.distanceTests!=neededTests)
        throw std::logic_error("Incomplete or duplicate lifetime witness distance work");
    if(result.total.witnessCount) {
        result.total.meanDistanceInEdges=
            meanSum/result.total.witnessCount;
        result.total.rmsDistanceInEdges=
            std::sqrt(rmsEnergy/result.total.witnessCount);
    }
    if(!std::isfinite(result.total.meanDistanceInEdges)||
       !std::isfinite(result.total.rmsDistanceInEdges))
        throw std::logic_error("Nonfinite original source lifetime distance aggregate");
    return result;
}

} // namespace mesher

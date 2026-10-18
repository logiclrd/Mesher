#include "mesher/repair_surgery.hpp"
#include "mesher/repair_intersection.hpp"
#include "mesher/versioned_collapse.hpp"
#include "mesher/versioned_inflate.hpp"
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
// Deliberately not defined in the public header: an external client may
// copy a certified lifetime but cannot manufacture fresh accepted owner,
// root, retired patch or cumulative-work evidence.
struct LifetimeSourceCertificate {
    std::shared_ptr<const RegionTopologyIndex> original,terminal;
    // Exact preceding certified lifetime. A sibling/independently
    // reanchored candidate is NOT a successor of an intent declaration.
    // Do not pin every historical COW root after undo history evicts it.
    std::weak_ptr<const LifetimeSourceCertificate> parent;
    bool reflectedStep{};
    std::vector<std::pair<FaceId,std::vector<FaceId>>> living;
    std::vector<FaceId> retired;
    std::size_t verifiedFaceSplits{},acceptedTransitions{},sourceFaceEvidence{};
};
// The caller cannot fabricate this in the public header.
struct OriginalPatchIntentCertificate {
    std::shared_ptr<const LifetimeSourceCertificate> predecessor;
    std::vector<FaceId> sources;
    bool mirrored{};
};
namespace {
void seal(OriginalPatchLifetime& lifetime,
          std::shared_ptr<const LifetimeSourceCertificate> parent={},
          bool reflectedStep=false) {
    auto proof=std::make_shared<LifetimeSourceCertificate>();
    proof->parent=std::move(parent);
    proof->reflectedStep=reflectedStep;
    proof->original=lifetime.anchors.original;
    proof->terminal=lifetime.lineage.terminal;
    proof->living=lifetime.lineage.living;
    proof->retired=lifetime.certifiedRetiredOriginals;
    proof->verifiedFaceSplits=lifetime.lineage.verifiedFaceSplits;
    proof->acceptedTransitions=lifetime.acceptedTransitions;
    proof->sourceFaceEvidence=lifetime.sourceFaceEvidence;
    lifetime.certificate=std::move(proof);
}
bool same(Vec3 a,Vec3 b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}

// This validation is intentionally independent of the Step under review.
// In particular, an attacker-supplied "first reference" cannot redefine
// the seven witness positions or scale to hide a cumulative deformation.
SourceSplitLineage verifyLifetime(
    const OriginalPatchLifetime& lifetime,
    std::size_t maxLivingDescendants) {
    const auto proof=lifetime.certificate;
    if(!proof||proof->original!=lifetime.anchors.original||
       proof->original!=lifetime.lineage.original||
       proof->terminal!=lifetime.lineage.terminal||
       proof->living!=lifetime.lineage.living||
       proof->retired!=lifetime.certifiedRetiredOriginals||
       proof->verifiedFaceSplits!=lifetime.lineage.verifiedFaceSplits||
       proof->acceptedTransitions!=lifetime.acceptedTransitions||
       proof->sourceFaceEvidence!=lifetime.sourceFaceEvidence)
        throw std::logic_error("Uncertified substituted lifetime owner, root, retirement or work counter");
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
    std::sort(expectedRetired.begin(),expectedRetired.end());
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
    std::size_t maxLivingDescendants,
    bool reflectedStep) {
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
    seal(next,earlier.certificate,reflectedStep);
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
    seal(state);
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
                         maxLivingDescendants,refined.sourceSplitsSymmetric);
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
    bool reflected=false;
    for(const auto& event:collapsed.collapseSources)
        reflected=reflected||event.symmetric;
    return applyVerified(previous,stage,{},cost,maxCumulativeFaceEvidence,
                         maxLivingDescendants,reflected);
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
    const bool reflected=step.sourceEvidence&&
        (!step.sourceEvidence->prepared.mirroredFaces.empty()||
         !step.sourceEvidence->prepared.mirroredVertices.empty()||
         !step.sourceEvidence->prepared.mirroredSeamVertices.empty());
    return applyVerified(previous,certified.survivors,
                         certified.certifiedRetiredOrigins,
                         certified.checkedFaceEvidence,
                         maxCumulativeFaceEvidence,maxLivingDescendants,
                         reflected);
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


OriginalPatchLifetime advanceCertifiedPhysicalStampLifetime(
    const OriginalPatchLifetime& before,
    const VersionedInflateStamp& inflated,
    const VersionedRefineResult& refined,
    const VersionedCollapseResult& collapsed,
    const ExactCutStep* exact,
    const std::unordered_map<VertexId,VertexId>* sourceMirrors,
    std::size_t maxCumulativeFaceEvidence,
    std::size_t maxLivingDescendants,
    std::size_t maxInflateVertices,
    std::size_t maxInflateFaces) {
    (void)verifyLifetime(before,maxLivingDescendants);
    if(!inflated.baseline.before||!inflated.index||
       inflated.baseline.before!=before.lineage.terminal||
       refined.stampOriginal!=inflated.baseline.before||
       refined.sourceIndex!=inflated.index||!refined.index||
       collapsed.stampOriginal!=inflated.baseline.before||
       collapsed.sourceIndex!=refined.index||!collapsed.index||
       (exact&&(!exact->complete||exact->expected!=collapsed.index||
                !exact->final)))
        throw std::invalid_argument("Physical source bundle contains stale or skipped private roots");
    // Certify the full Inflate geometry domain by source ID, not nearest
    // matching, and ensure no unreported new/retired/rewritten triangles.
    // Work is capped before it can be accidentally truncated.
    if(!maxInflateVertices||!maxInflateFaces)
        throw std::length_error("Finite original Inflate evidence limits required");
    std::map<VertexId,Vec3> oldVertices,newVertices;
    std::map<FaceId,std::array<VertexId,3>> oldFaces,newFaces;
    const auto load=[&](const RegionTopologyIndex& index,
                        std::map<VertexId,Vec3>& vertices,
                        std::map<FaceId,std::array<VertexId,3>>& faces) {
        index.revision().forEachPartition(
            [&](PartitionId,const MeshPartition& partition) {
                if(partition.vertices.size()>maxInflateVertices-vertices.size()||
                   partition.faces.size()>maxInflateFaces-faces.size())
                    throw std::length_error("Complete original Inflate geometry evidence exhausted");
                for(const auto& vertex:partition.vertices)
                    if(!vertices.emplace(vertex.id,vertex.position).second)
                        throw std::logic_error("Duplicate original Inflate vertex source");
                for(const auto& face:partition.faces)
                    if(!faces.emplace(face.id,face.vertices).second)
                        throw std::logic_error("Duplicate original Inflate face source");
            });
    };
    load(*before.lineage.terminal,oldVertices,oldFaces);
    load(*inflated.index,newVertices,newFaces);
    if(oldFaces!=newFaces||oldVertices.size()!=newVertices.size()||
       inflated.baseline.movedVertices.size()!=inflated.moved)
        throw std::logic_error("Original Inflate silently replaced source face/vertex incidence");
    std::set<VertexId> moved(inflated.baseline.movedVertices.begin(),
                             inflated.baseline.movedVertices.end());
    std::set<FaceId> affected(inflated.baseline.affectedFaces.begin(),
                              inflated.baseline.affectedFaces.end());
    if(moved.size()!=inflated.baseline.movedVertices.size()||
       affected.size()!=inflated.baseline.affectedFaces.size())
        throw std::logic_error("Inflate source journals duplicate moved vertices or incident faces");
    std::set<FaceId> actualAffected;
    for(const auto& [id,position]:oldVertices) {
        const auto it=newVertices.find(id);
        if(it==newVertices.end())
            throw std::logic_error("Inflate replaced a permanent original VertexId");
        if(!moved.contains(id)&&
           (position.x!=it->second.x||
            position.y!=it->second.y||
            position.z!=it->second.z))
            throw std::logic_error("Inflate displaced an unjournaled original vertex");
    }
    for(const auto id:moved) {
        if(!oldVertices.contains(id)||!newVertices.contains(id))
            throw std::logic_error("Inflate reported foreign moved source vertex");
        for(const auto face:before.lineage.terminal->vertex(id).incidentFaces)
            actualAffected.insert(face);
    }
    if(actualAffected!=affected)
        throw std::logic_error("Inflate source journal omitted or invented incident source faces");

    // The M24/M25 validator already permits authenticated Inflate XYZ
    // displacement while requiring EXACT old source incidence. It then
    // proves each actually committed split/flip by root/FaceId journals.
    auto stage=advanceOriginalPatchLifetime(
        before,refined,maxCumulativeFaceEvidence,maxLivingDescendants);
    bool reflected=stage.certificate->reflectedStep;
    // The collapser stores the *stamp's* original for its LocalBaseline;
    // M25 requires its ACTUAL source root. Both identities are verified
    // above before re-expressing that source as the prior stage terminal.
    if(collapsed.collapseSources.empty()) {
        if(collapsed.index!=refined.index||
           collapsed.statistics.accepted||
           !collapsed.removedVertices.empty()||
           !collapsed.removedFaces.empty()||
           !collapsed.rewrittenFaces.empty()||
           !collapsed.removedMirrorPairs.empty())
            throw std::logic_error("Unjournaled topology mutation in zero-collapse physical stage");
    }else {
        auto sourceCollapse=collapsed;
        sourceCollapse.stampOriginal=refined.index;
        stage=advanceOriginalPatchLifetime(
            stage,sourceCollapse,maxCumulativeFaceEvidence,
            maxLivingDescendants);
        reflected=reflected||stage.certificate->reflectedStep;
    }
    if(exact) {
        stage=advanceOriginalPatchLifetime(
            stage,*exact,sourceMirrors,maxCumulativeFaceEvidence,
            maxLivingDescendants);
        reflected=reflected||stage.certificate->reflectedStep;
    }
    if(stage.lineage.terminal!=(exact?exact->final:collapsed.index))
        throw std::logic_error("Physical final terminal root differs from complete certified private stages");
    // Consolidate all verified *private* stage certificates into ONE
    // externally usable successor. This preserves M21's exact
    // predecessor binding and cumulative original evidence counters.
    OriginalPatchLifetime complete=std::move(stage);
    if(before.acceptedTransitions==std::numeric_limits<std::size_t>::max())
        throw std::length_error("External original lifetime Step count overflow");
    complete.acceptedTransitions=before.acceptedTransitions+1;
    // Per-stage flags are captured during verification: weak history
    // predecessors may expire immediately and must not be traversed.
    seal(complete,before.certificate,reflected);
    (void)verifyLifetime(complete,maxLivingDescendants);
    return complete;
}

void verifyOriginalPatchPublicationRoot(
    const OriginalPatchLifetime& lifetime,
    const std::shared_ptr<const RegionTopologyIndex>& expected,
    std::size_t maxLivingDescendants) {
    if(!expected||lifetime.lineage.terminal!=expected)
        throw std::logic_error("Publication source certificate is not for the exact current root");
    (void)verifyLifetime(lifetime,maxLivingDescendants);
}
void verifyOriginalPatchPublicationTransition(
    const OriginalPatchLifetime& previous,
    const OriginalPatchLifetime& next,
    const std::shared_ptr<const RegionTopologyIndex>& before,
    const std::shared_ptr<const RegionTopologyIndex>& terminal,
    std::size_t maxLivingDescendants) {
    verifyOriginalPatchPublicationRoot(previous,before,maxLivingDescendants);
    verifyOriginalPatchPublicationRoot(next,terminal,maxLivingDescendants);
    if(next.certificate->parent.lock()!=previous.certificate||
       next.anchors.original!=previous.anchors.original||
       next.acceptedTransitions!=previous.acceptedTransitions+1||
       next.sourceFaceEvidence<previous.sourceFaceEvidence||
       next.lineage.original!=previous.lineage.original||
       next.lineage.terminal==previous.lineage.terminal)
        throw std::logic_error("Proposed original-source history is not the exact certified successor");
    if(next.anchors.faces.size()!=previous.anchors.faces.size())
        throw std::logic_error("Proposed original-source history changed protected source set");
}

OriginalPatchIntentDeclaration declareOriginalPatchIntent(
    const OriginalPatchLifetime& before,
    const std::vector<FaceId>& intendedSources,
    const std::unordered_map<VertexId,VertexId>* originalMirrors,
    std::size_t maxSourceFaces) {
    // The proof root and EVERY anchor are checked before accepting an
    // explicitly named first-original source. A world-space brush radius
    // or a coincident candidate triangle is never accepted as intent.
    (void)verifyLifetime(before,maxSourceFaces);
    if(!maxSourceFaces||before.anchors.faces.size()>maxSourceFaces||
       intendedSources.size()>maxSourceFaces)
        throw std::length_error("Original intent source nomination exceeds cap");
    std::set<FaceId> available,active;
    for(const auto& [first,living]:before.lineage.living) {
        available.insert(first);
        if(!living.empty())active.insert(first);
    }
    std::set<FaceId> requested;
    for(const auto id:intendedSources)
        if(!id||!active.contains(id)||!requested.insert(id).second)
            throw std::logic_error("Intent is not a unique live first-original protected FaceId");
    if(originalMirrors) {
        // Every nominated original triangle must have its complete
        // reciprocal original VertexId orbit and an actual opposite
        // winding reflected FaceId; XYZ proximity cannot prove pairing.
        const auto& index=*before.anchors.original;
        std::map<FaceId,FaceId> partners;
        for(const auto& anchor:before.anchors.faces) {
            std::array<VertexId,3> reflected{};
            for(unsigned j=0;j<3;++j) {
                const auto id=anchor.originalVertices[j];
                const auto it=originalMirrors->find(id);
                if(it==originalMirrors->end()||
                   !originalMirrors->contains(it->second)||
                   originalMirrors->at(it->second)!=id)
                    throw std::logic_error("Original reflected intent lacks reciprocal permanent VertexId");
                const auto p=index.vertex(id).position;
                const auto q=index.vertex(it->second).position;
                if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
                   ((id==it->second)!=(p.x==0)))
                    throw std::logic_error("Reflected source vertex identity disagrees with X=0 involution");
                reflected[j]=it->second;
            }
            // Reflection reverses orientation. Only cyclic rotation
            // of the reversed source triple is allowed, not arbitrary
            // face matching or an independent coincident sheet.
            const std::array<VertexId,3> reversed{
                reflected[0],reflected[2],reflected[1]};
            FaceId partner=0;
            for(const auto& other:before.anchors.faces) {
                for(unsigned shift=0;shift<3;++shift)
                    if(other.originalVertices[shift]==reversed[0]&&
                       other.originalVertices[(shift+1)%3]==reversed[1]&&
                       other.originalVertices[(shift+2)%3]==reversed[2]) {
                        if(partner&&partner!=other.face)
                            throw std::logic_error("Ambiguous mirrored original FaceId owner");
                        partner=other.face;
                    }
            }
            if(!partner||!partners.emplace(anchor.face,partner).second)
                throw std::logic_error("Missing opposite-winding first-original reflected FaceId");
        }
        for(const auto& [face,partner]:partners) {
            if(!partners.contains(partner)||partners.at(partner)!=face||
               (requested.contains(face)!=requested.contains(partner)))
                throw std::logic_error("Caller original intent is not closed under source-proven reflection");
        }
    }
    auto proof=std::make_shared<OriginalPatchIntentCertificate>();
    proof->predecessor=before.certificate;
    proof->sources.assign(requested.begin(),requested.end());
    proof->mirrored=originalMirrors!=nullptr;
    return {std::move(proof)};
}

OriginalPatchIntentDecision evaluateOriginalPatchIntent(
    const OriginalPatchLifetime& before,
    const OriginalPatchLifetime& after,
    const OriginalPatchIntentDeclaration& intent,
    double maxProtectedDeviationInEdges,
    std::size_t maxDistanceTests,
    std::size_t maxLivingDescendants) {
    if(!std::isfinite(maxProtectedDeviationInEdges)||
       maxProtectedDeviationInEdges<0)
        throw std::invalid_argument("Invalid original-edge protected fidelity threshold");
    (void)verifyLifetime(before,maxLivingDescendants);
    (void)verifyLifetime(after,maxLivingDescendants);
    if(!intent.certificate||
       intent.certificate->predecessor!=before.certificate||
       !after.certificate||
       after.certificate->parent.lock()!=before.certificate||
       (after.certificate->reflectedStep&&!intent.certificate->mirrored)||
       before.anchors.original!=after.anchors.original||
       before.anchors.faces.size()!=after.anchors.faces.size()||
       after.acceptedTransitions!=before.acceptedTransitions+1)
        throw std::logic_error("Intent is stale or candidate lacks the EXACT certified source predecessor");
    for(std::size_t i=0;i<before.anchors.faces.size();++i)
        if(before.anchors.faces[i].face!=after.anchors.faces[i].face)
            throw std::logic_error("Original source intent was rebased to unrelated FaceIds");
    std::set<FaceId> scoped(intent.certificate->sources.begin(),
                            intent.certificate->sources.end());
    if(scoped.size()!=intent.certificate->sources.size())
        throw std::logic_error("Duplicate intent source certificate");
    const auto audit=auditOriginalPatchLifetime(
        after,before.anchors.original,maxDistanceTests,maxLivingDescendants);
    OriginalPatchIntentDecision decision;
    decision.intendedOriginals=scoped.size();
    decision.certifiedRetiredOriginals=audit.certifiedRetiredOriginals.size();
    decision.totalDistanceTests=audit.total.distanceTests;
    std::set<FaceId> previouslyActive;
    for(const auto& [id,faces]:before.lineage.living)
        if(!faces.empty())previouslyActive.insert(id);
    for(const auto id:scoped)
        if(!previouslyActive.contains(id))
            throw std::logic_error("Original intent named retired or foreign source FaceId");
    for(const auto& witness:audit.perOriginal) {
        if(scoped.contains(witness.original))continue;
        ++decision.protectedOriginals;
        decision.protectedWitnesses+=witness.sampled.witnessCount;
        decision.maximumProtectedDeviationInEdges=std::max(
            decision.maximumProtectedDeviationInEdges,
            witness.sampled.maximumDistanceInEdges);
        if(witness.sampled.maximumDistanceInEdges>
           maxProtectedDeviationInEdges)
            throw std::logic_error("Unintended exposed/reversed-cavity first-original surface drift");
    }
    // A disappearing source (whether scoped or protected) is NEVER
    // exempted on intent alone: verifyLifetime and M26's source-sealed
    // c3/M03 retired-original certificate are always mandatory.
    return decision;
}

} // namespace mesher

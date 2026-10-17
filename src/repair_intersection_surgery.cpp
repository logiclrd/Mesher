#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
ExactCutSurgeryAttempt reject(const std::string& why) {
    ExactCutSurgeryAttempt result;
    result.refusal=why;
    // A caller cannot publish either an intermediate surgery root or an
    // incomplete accepted face journal from a rejected multi-stage edit.
    return result;
}
bool exactlyMirrored(Vec3 a,Vec3 b) {
    return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&
           std::isfinite(b.x)&&std::isfinite(b.y)&&std::isfinite(b.z)&&
           a.x==-b.x&&a.y==b.y&&a.z==b.z;
}
std::vector<std::pair<VertexId,VertexId>> completeMirrorVertices(
    const ConformingCutCandidate& cut,
    const std::unordered_map<VertexId,VertexId>& original) {
    std::map<VertexId,VertexId> pairs;
    const auto add=[&](VertexId first,VertexId second,bool old) {
        if(!first||!second||first==second)
            throw std::logic_error("Private V16 handoff needs distinct off-plane pairs");
        if(old) {
            if(!cut.originalIndex->maybeVertex(first)||
               !cut.originalIndex->maybeVertex(second))
                throw std::logic_error("Original mirror mapping contains retired vertex ID");
        }else if(cut.originalIndex->maybeVertex(first)||
                 cut.originalIndex->maybeVertex(second))
            throw std::logic_error("Inserted mirror pair reused original vertex ID");
        if(!cut.index->maybeVertex(first)||!cut.index->maybeVertex(second)||
           !exactlyMirrored(cut.index->vertex(first).position,
                            cut.index->vertex(second).position))
            throw std::logic_error("Staged vertex mirror coordinates differ");
        const auto a=pairs.find(first),b=pairs.find(second);
        if((a!=pairs.end()&&a->second!=second)||
           (b!=pairs.end()&&b->second!=first))
            throw std::logic_error("Conflicting V16 candidate vertex mirror pairs");
        pairs[first]=second;pairs[second]=first;
    };
    for(const auto& [a,b]:original) {
        const auto back=original.find(b);
        if(back==original.end()||back->second!=a)
            throw std::logic_error("Original mirror mapping is not involutive");
        add(a,b,true);
    }
    for(const auto& [a,b]:cut.mirroredVertices)add(a,b,false);
    std::set<VertexId> inserted(cut.insertedVertices.begin(),
                                cut.insertedVertices.end());
    if(inserted.size()!=cut.insertedVertices.size()||
       inserted.size()!=cut.mirroredVertices.size()*2)
        throw std::logic_error("V16 candidate has incomplete inserted mirror journal");
    for(const auto id:inserted)
        if(!pairs.contains(id)||original.contains(id))
            throw std::logic_error("V16 new vertex mirror partner is missing");

    std::vector<std::pair<VertexId,VertexId>> result;
    result.reserve(pairs.size()/2);
    for(const auto& [a,b]:pairs)
        if(a<b)result.emplace_back(a,b);
    if(result.size()*2!=pairs.size())
        throw std::logic_error("Non-reciprocal mirror pair in V16 handoff");
    return result;
}
double referenceEdge(const RegionTopologyIndex& index,
                     const std::vector<FaceId>& protectedFaces) {
    double maximum=0;
    for(const auto id:protectedFaces) {
        const auto tri=index.face(id).vertices;
        for(unsigned k=0;k<3;++k)
            maximum=std::max(maximum,length(
                index.vertex(tri[k]).position-
                index.vertex(tri[(k+1)%3]).position));
    }
    return maximum;
}
} // namespace

ExactCutSurgeryAttempt attemptPrivateExactCutSurgery(
    const ConformingCutCandidate& cut,
    const ExactCutSurgerySettings& settings,
    const std::unordered_map<VertexId,VertexId>* originalMirrors) {
    try {
        if(!cut.originalIndex||!cut.index||
           !cut.commit.revision||
           cut.commit.revision.get()!=&cut.index->revision())
            return reject("Private V16 handoff has no exact immutable candidate");
        if(!std::isfinite(settings.maxProtectedDeviationInEdges)||
           settings.maxProtectedDeviationInEdges<0||
           settings.maxProtectedDistanceTests==0||
           settings.maxProtectedWitnesses==0||
           settings.maxFinalCollisionFaces==0)
            return reject("Invalid protected surface or final collision work bounds");
        // Identical preliminary contour settings are compulsory for c3
        // authorization AND V16's independent stale-plan reconstruction.
        if(settings.surgery.planning.maximumGapInEdges!=
               settings.removal.contours.maximumGapInEdges||
           settings.surgery.planning.maximumPlanarityInRadii!=
               settings.removal.contours.maximumPlanarityInRadii||
           settings.surgery.planning.maximumIsoperimetricRatio!=
               settings.removal.contours.maximumIsoperimetricRatio||
           settings.surgery.planning.maximumSingleCapPlanarityInRadii!=
               settings.removal.contours.maximumSingleCapPlanarityInRadii||
           settings.surgery.planning.maximumSingleCapIsoperimetricRatio!=
               settings.removal.contours.maximumSingleCapIsoperimetricRatio||
           settings.surgery.planning.maximumPerimeterRatio!=
               settings.removal.contours.maximumPerimeterRatio||
           settings.surgery.planning.minimumNormalAlignment!=
               settings.removal.contours.minimumNormalAlignment||
           settings.surgery.planning.minimumRelativeAreaAdvantage!=
               settings.removal.contours.minimumRelativeAreaAdvantage)
            return reject("c3 and V16 contour scoring settings differ");
        if(settings.surgery.requireReverseSurfaceFidelity||
           settings.surgery.requireExposedFragmentFidelity)
            return reject("V17 exact-child fidelity uses independent protected witnesses");

        const auto policy=planExactCutRemoval(cut,settings.removal);
        if(policy.status==CutRemovalStatus::Refused||
           policy.status==CutRemovalStatus::BudgetExceeded)
            return reject("c3 exact-child nomination refused: "+policy.refusal);
        if(policy.status==CutRemovalStatus::NoChanges) {
            ExactCutSurgeryAttempt result;
            result.status=ExactCutSurgeryStatus::NoChanges;
            return result;
        }
        if(!policy.ready()||policy.candidateIndex!=cut.index||
           policy.originalIndex!=cut.originalIndex||
           policy.proposedRemoval.empty()||!policy.contourPlan.ready())
            return reject("Private surgery lacked a current Ready exact-child plan");
        const bool mirrored=!cut.mirroredFaces.empty()||
                            !cut.mirroredVertices.empty();
        if(mirrored!=(originalMirrors!=nullptr))
            return reject("Mirrored surgery requires explicit original stable-ID involution");
        if(!mirrored&&originalMirrors)
            return reject("Nonmirrored surgery cannot add a guessed mirror map");

        RepairSurgeryResult surgery;
        if(mirrored) {
            const auto pairs=completeMirrorVertices(cut,*originalMirrors);
            surgery=applyMirroredRepairSurgery(
                cut.index,policy.contourPlan,pairs,settings.surgery);
        }else
            surgery=applyRepairSurgery(cut.index,policy.contourPlan,
                                      settings.surgery);
        if(!surgery.index||!surgery.commit.revision||
           surgery.index==cut.index||
           surgery.commit.revision.get()!=&surgery.index->revision())
            return reject("V16 did not return one complete private surgery revision");
        if(surgery.retiredFaces!=policy.proposedRemoval)
            return reject("V16 retired faces differ from c3 exact-child authorization");

        // V16 validated its scar and one-ring. Now validate EVERY surviving
        // conforming source child and new V16 face against the WHOLE final
        // private revision, not only the contour's local write halo.
        std::set<FaceId> finalSeeds(surgery.insertedFaces.begin(),
                                    surgery.insertedFaces.end());
        for(const auto& [child,_]:cut.childSources)
            if(surgery.index->maybeFace(child))finalSeeds.insert(child);
        if(finalSeeds.empty()||
           finalSeeds.size()>settings.maxFinalCollisionFaces)
            throw std::length_error("Full final local collision seed budget exhausted");
        const auto contacts=surgery.index->triangleIntersections(
            {},{finalSeeds.begin(),finalSeeds.end()},nullptr,
            settings.finalCollisionLimits);
        if(!contacts.empty())
            return reject("Full original-cut halo still intersects after V16 closure");

        // Reverse geometric fidelity is restricted to c3-PROTECTED real
        // child triangles, NEVER to deliberately deleted buried/void
        // pieces. The original surface witness positions must remain
        // on the final geometry within a tiny original-edge-relative
        // error. This does not substitute for V16's forward scar audit.
        for(const auto id:policy.protectedChildren)
            if(!surgery.index->maybeFace(id))
                return reject("V16 retired a protected exposed/cavity face");
        const auto witnesses=sampleSurfaceWitnesses(
            *cut.index,policy.protectedChildren,
            settings.maxProtectedWitnesses);
        const auto scale=referenceEdge(*cut.index,policy.protectedChildren);
        if(!witnesses.empty()&&!(scale>0))
            return reject("Protected real child has no finite edge-length scale");
        const auto audited=witnesses.empty()?SurfaceWitnessAudit{}:
            auditSurfaceWitnesses(*surgery.index,witnesses,
                {finalSeeds.begin(),finalSeeds.end()},scale,
                settings.maxProtectedDistanceTests);
        if(audited.maximumDistanceInEdges>
           settings.maxProtectedDeviationInEdges)
            return reject("Protected exposed/cavity surface moved during V16 closure");

        ExactCutSurgeryAttempt success;
        success.status=ExactCutSurgeryStatus::PrivateValidated;
        success.selectedFaces=policy.proposedRemoval.size();
        success.protectedFaces=policy.protectedChildren.size();
        success.checkedFinalContacts=contacts.size();
        success.protectedWitnesses=audited.witnessCount;
        success.maximumProtectedDeviationInEdges=
            audited.maximumDistanceInEdges;
        success.surgery=std::move(surgery);
        return success;
    }catch(const std::exception& e) {
        return reject(e.what());
    }
}
} // namespace mesher

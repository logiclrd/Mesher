#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Kind=RegionTopologyIndex::FragmentKind;
using Status=CutRemovalStatus;

CutRemovalProposal refused(const std::string& reason,
                           Status status=Status::Refused) {
    CutRemovalProposal result;
    result.status=status;
    result.refusal=reason;
    // Do not leak any partially approved FaceIds or a V15 contour.
    return result;
}
bool exactlyMirrors(Vec3 a,Vec3 b) {
    return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z)&&
           std::isfinite(b.x)&&std::isfinite(b.y)&&std::isfinite(b.z)&&
           a.x==-b.x&&a.y==b.y&&a.z==b.z;
}
bool reverseReflectedTriangles(
    const RegionTopologyIndex& index,FaceId first,FaceId second) {
    const auto a=index.face(first).vertices,b=index.face(second).vertices;
    for(unsigned shift=0;shift<3;++shift) {
        bool matches=true;
        for(unsigned i=0;i<3;++i) {
            const auto p=index.vertex(a[i]).position;
            const auto q=index.vertex(b[(shift+3-i)%3]).position;
            if(!exactlyMirrors(p,q)) {
                matches=false;
                break;
            }
        }
        if(matches)return true;
    }
    return false;
}
std::map<FaceId,FaceId> verifiedMirrorFaces(
    const ConformingCutCandidate& cut,
    const std::set<FaceId>& children,
    std::size_t maxPairs) {
    if(cut.mirroredFaces.size()>maxPairs/2)
        throw std::length_error("Paired cut face budget exhausted");
    if(cut.mirroredFaces.size()*2!=children.size())
        throw std::logic_error("Mirrored cut face journal omits a child orbit");
    std::map<FaceId,FaceId> partners;
    for(const auto& [a,b]:cut.mirroredFaces) {
        if(a==b||!children.contains(a)||!children.contains(b)||
           !partners.emplace(a,b).second||
           !partners.emplace(b,a).second)
            throw std::logic_error("Invalid or duplicated mirrored child FaceId");
        if(!reverseReflectedTriangles(*cut.index,a,b))
            throw std::logic_error("Mirrored children have inconsistent exact winding");
    }
    if(partners.size()!=children.size())
        throw std::logic_error("Incomplete mirrored child pairing");
    std::set<VertexId> inserted(
        cut.insertedVertices.begin(),cut.insertedVertices.end());
    if(inserted.size()!=cut.insertedVertices.size()||
       cut.mirroredVertices.size()*2!=inserted.size())
        throw std::logic_error("Incomplete mirrored newly inserted vertex orbits");
    std::set<VertexId> paired;
    for(const auto& [a,b]:cut.mirroredVertices) {
        if(a==b||!inserted.contains(a)||!inserted.contains(b)||
           !paired.insert(a).second||!paired.insert(b).second||
           !cut.index->maybeVertex(a)||!cut.index->maybeVertex(b)||
           cut.originalIndex->maybeVertex(a)||cut.originalIndex->maybeVertex(b)||
           !exactlyMirrors(cut.index->vertex(a).position,
                           cut.index->vertex(b).position))
            throw std::logic_error("Invalid mirrored permanent cut vertex orbits");
    }
    return partners;
}
} // namespace

CutRemovalProposal planExactCutRemoval(
    const ConformingCutCandidate& cut,const CutRemovalSettings& settings) {
    if(settings.policy!=CutRemovalPolicy::PositiveWindingUnionPreserveCavities)
        return refused("Unknown or unsupported cavity/removal policy");

    // Both calls use only cut's immutable revision graph and recompute ALL
    // evidence. No user-provided winding labels or stale certificates are
    // accepted as deletion authority. Hard work caps throw length_error;
    // this policy converts them to a recoverable, non-partial refusal.
    try {
        const auto coverage=certifyConformingCutCoverage(cut,settings.coverage);
        if(!coverage.complete)
            return refused("Incomplete cut-curve coverage: "+coverage.refusal);
        const auto occupancy=assessConformingCutFragments(cut,settings.occupancy);
        if(!occupancy.complete)
            return refused("Uncertain child occupancy: "+occupancy.refusal);
        if(coverage.checkedChildren!=occupancy.fragments.size()||
           occupancy.fragments.size()!=cut.childSources.size())
            return refused("Coverage and child-winding revision journals disagree");

        std::map<FaceId,FaceId> provenance;
        std::set<FaceId> children;
        for(const auto& [child,source]:cut.childSources)
            if(!provenance.emplace(child,source).second||
               !children.insert(child).second)
                return refused("Duplicate real-child face provenance");
        if(children.size()!=occupancy.fragments.size()||
           cut.commit.revision.get()!=&cut.index->revision())
            return refused("Cut child status references a different revision");

        const bool mirrored=!cut.mirroredFaces.empty()||
                            !cut.mirroredVertices.empty();
        if(!mirrored&&(!cut.mirroredFaces.empty()||!cut.mirroredVertices.empty()))
            return refused("Orphaned partial symmetry journal");
        std::map<FaceId,FaceId> mirrors;
        if(mirrored)
            mirrors=verifiedMirrorFaces(cut,children,
                                       settings.coverage.maxChildren);

        CutRemovalProposal proposed;
        proposed.originalIndex=cut.originalIndex;
        proposed.candidateIndex=cut.index;
        proposed.checkedContacts=coverage.checkedContacts;
        proposed.windingQueries=occupancy.windingQueries;
        proposed.classifiedChildren=occupancy.fragments;
        std::map<FaceId,Kind> classes;
        for(const auto& part:proposed.classifiedChildren) {
            if(!provenance.contains(part.child)||
               provenance.at(part.child)!=part.original||
               !classes.emplace(part.child,part.kind).second||
               part.negativeWinding-part.positiveWinding!=1)
                return refused("Stale, invalid or conflicting real-child winding");
            const bool minus=part.negativeWinding>0,
                       plus=part.positiveWinding>0;
            switch(part.kind) {
            case Kind::Exposed:
                if(!minus||plus)
                    return refused("Exposed face contradicts positive-winding occupancy");
                ++proposed.exposed;
                proposed.protectedChildren.push_back(part.child);
                break;
            case Kind::ReversedBoundary:
                if(minus||!plus)
                    return refused("Reversed cavity interface has inconsistent occupancy");
                ++proposed.reversedBoundary;
                proposed.protectedChildren.push_back(part.child);
                break;
            case Kind::Buried:
                if(!minus||!plus)
                    return refused("Buried face is not positive on both sides");
                ++proposed.buried;
                proposed.proposedRemoval.push_back(part.child);
                break;
            case Kind::Outside:
                if(minus||plus)
                    return refused("Outside face still bounds occupied material");
                ++proposed.outside;
                proposed.proposedRemoval.push_back(part.child);
                break;
            case Kind::Ambiguous:
                return refused("Ambiguous real-child classification cannot select removal");
            }
        }
        if(classes.size()!=children.size())
            return refused("Not every changed child received an occupancy decision");
        std::sort(proposed.proposedRemoval.begin(),proposed.proposedRemoval.end());
        std::sort(proposed.protectedChildren.begin(),proposed.protectedChildren.end());
        if(proposed.proposedRemoval.size()>settings.maxProposedRemoval)
            throw std::length_error("Exact-child removal budget exhausted");

        if(mirrored) {
            for(const auto& [a,b]:mirrors)
                if(classes.at(a)!=classes.at(b))
                    return refused("Mirrored child classifications disagree");
            for(const auto id:proposed.proposedRemoval) {
                const auto mate=mirrors.at(id);
                if(!std::binary_search(proposed.proposedRemoval.begin(),
                                       proposed.proposedRemoval.end(),mate))
                    return refused("Removed children must form full mirror orbits");
                if(id<mate)
                    proposed.mirroredRemoval.emplace_back(id,mate);
            }
            if(proposed.mirroredRemoval.size()*2!=proposed.proposedRemoval.size())
                return refused("Incomplete mirrored deletion FaceId pairing");
        }
        if(proposed.proposedRemoval.empty()) {
            proposed.status=Status::NoChanges;
            return proposed;
        }
        if(proposed.proposedRemoval.size()>settings.contours.maxRemovedFaces)
            throw std::length_error("V15 exact-child contour removal budget exhausted");

        // This V15 entry point NEVER performs surgery. It only verifies
        // directed surviving contours and selects an acceptable private
        // cap/zipper closure; a later V16 validator must redo all checks.
        proposed.contourPlan=planRemovalContours(
            *cut.index,proposed.proposedRemoval,settings.contours);
        if(!proposed.contourPlan.ready()||
           proposed.contourPlan.removedFaces!=proposed.proposedRemoval)
            return refused("V15 exact-child contour refusal: "+
                           proposed.contourPlan.reason);
        proposed.status=Status::Ready;
        return proposed;
    }catch(const std::length_error& e) {
        return refused(e.what(),Status::BudgetExceeded);
    }catch(const std::logic_error& e) {
        return refused(e.what());
    }catch(const std::out_of_range& e) {
        return refused(e.what());
    }
}

} // namespace mesher

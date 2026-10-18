#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace mesher {
namespace {
using Kind=RegionTopologyIndex::FragmentKind;
using Status=CutRemovalStatus;
using Edge=RegionTopologyIndex::Edge;
Edge edge(VertexId a,VertexId b) {
    return {std::min(a,b),std::max(a,b)};
}
bool same(Vec3 a,Vec3 b) {
    return a.x==b.x&&a.y==b.y&&a.z==b.z;
}
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
std::array<Vec3,3> triangle(const RegionTopologyIndex& index,FaceId id) {
    const auto vertices=index.face(id).vertices;
    return {index.vertex(vertices[0]).position,
            index.vertex(vertices[1]).position,
            index.vertex(vertices[2]).position};
}
UncutFaceRetirementProposal refuse(std::string reason,
                                   Status status=Status::Refused) {
    UncutFaceRetirementProposal result;
    result.status=status;
    result.refusal=std::move(reason);
    return result; // no partial authorization, V15 contour or published root
}
} // namespace

UncutFaceRetirementProposal planUncutFaceRetirement(
    const ConformingCutCandidate& cut,
    const CutRemovalSettings& settings,
    const UncutFaceRetirementLimits& limits,
    const std::unordered_map<VertexId,VertexId>* originalMirrors) {
    if(settings.policy!=CutRemovalPolicy::PositiveWindingUnionPreserveCavities)
        return refuse("Uncut-face retirement requires positive-winding cavity-preserving union");
    if(!cut.originalIndex||!cut.index||!cut.commit.revision||
       cut.commit.revision.get()!=&cut.index->revision()||
       cut.originalIndex==cut.index)
        return refuse("Uncut-face retirement needs matching original and private roots");
    const bool mirrored=!cut.mirroredFaces.empty()||
                        !cut.mirroredVertices.empty();
    if(mirrored!=(originalMirrors!=nullptr))
        return refuse("Reflected uncut retirement needs an explicit historical mirror map");
    try {
        // Neither a caller-nominated original FaceId nor a V15 Ready plan
        // grants any retirement right. Recompute the COMPLETE V13 c2
        // certificate and c1 classifications from exactly these roots.
        const auto coverage=certifyConformingCutCoverage(cut,settings.coverage);
        if(!coverage.complete)
            return refuse("Uncut-face original V13 coverage: "+coverage.refusal);
        const auto occupancy=assessConformingCutFragments(cut,settings.occupancy);
        if(!occupancy.complete)
            return refuse("Uncut-face actual cut children: "+occupancy.refusal);
        if(occupancy.fragments.size()!=cut.childSources.size()||
           coverage.checkedChildren!=cut.childSources.size())
            return refuse("Uncut-face cut provenance and coverage disagree");

        std::map<FaceId,FaceId> sourceOf;
        std::set<FaceId> touchedOriginals;
        for(const auto& [child,source]:cut.childSources) {
            if(!sourceOf.emplace(child,source).second||
               !cut.originalIndex->maybeFace(source)||
               !cut.index->maybeFace(child))
                return refuse("Uncut-face proposal has incomplete child source identity");
            touchedOriginals.insert(source);
        }
        if(sourceOf.size()!=cut.childSources.size())
            return refuse("Uncut-face proposal has duplicated child records");

        std::set<FaceId> removal,protectedFaces;
        for(const auto& f:occupancy.fragments) {
            const auto source=sourceOf.find(f.child);
            if(source==sourceOf.end()||source->second!=f.original||
               f.negativeWinding-f.positiveWinding!=1)
                return refuse("Uncut-face proposal has invalid c1 source or oriented jump");
            const bool minus=f.negativeWinding>0,plus=f.positiveWinding>0;
            switch(f.kind) {
            case Kind::Buried:
                if(!minus||!plus)return refuse("Invalid buried child occupancy");
                removal.insert(f.child);
                break;
            case Kind::Outside:
                if(minus||plus)return refuse("Invalid outside child occupancy");
                removal.insert(f.child);
                break;
            case Kind::Exposed:
                if(!minus||plus)return refuse("Invalid exposed child occupancy");
                protectedFaces.insert(f.child);
                break;
            case Kind::ReversedBoundary:
                if(minus||!plus)return refuse("Invalid cavity child occupancy");
                protectedFaces.insert(f.child);
                break;
            case Kind::Ambiguous:
                return refuse("Uncertain original child is never authorized");
            }
        }
        if(removal.size()+protectedFaces.size()!=sourceOf.size())
            return refuse("One cut child has no exclusive c1 classification");
        UncutFaceRetirementProposal result;
        result.checkedOriginalContacts=coverage.checkedContacts;
        result.protectedChildren.assign(protectedFaces.begin(),
                                        protectedFaces.end());

        // A source-connected patch is delimited by ACTUAL cut source
        // faces on the original manifold, not by proximity between
        // unrelated coincident shells. Inspect the WHOLE reachable
        // component of uncut original faces; never classify a handpicked
        // sample while silently ignoring adjoining unknown triangles.
        std::deque<FaceId> pending;
        std::set<FaceId> queued;
        std::size_t adjacency=0;
        const auto neighbors=[&](FaceId face) {
            const auto tri=cut.originalIndex->face(face).vertices;
            for(unsigned j=0;j<3;++j) {
                if(adjacency>=limits.maxAdjacencyTests)
                    throw std::length_error("Uncut-face original adjacency budget");
                ++adjacency;
                const auto sides=cut.originalIndex->maybeEdgeSides(
                    edge(tri[j],tri[(j+1)%3]));
                if(!sides||sides->size()!=2)
                    throw std::logic_error(
                        "Uncut original component has a nonmanifold/open edge");
                for(const auto side:*sides)
                    if(!touchedOriginals.contains(side.face)&&
                       queued.insert(side.face).second)
                        pending.push_back(side.face);
            }
        };
        for(const auto face:touchedOriginals)neighbors(face);
        if(pending.empty())
            return refuse("No unchanged original faces adjacent to real cut sources",
                          Status::NoChanges);

        // Original no-contact checks are LOCAL full V13 face queries:
        // each query visits all spatially overlapping original triangles
        // under an aggregate budget, and must return no collision.
        // This excludes every intervening cut curve on an uncut face.
        const auto remaining=[&](std::size_t limit,std::size_t used) {
            if(used>limit)throw std::length_error(
                "Uncut-face aggregate collision/winding budget");
            return limit-used;
        };
        constexpr std::array<std::array<double,3>,4> bary{{
            {1.0/3,1.0/3,1.0/3},
            {.6,.2,.2},{.2,.6,.2},{.2,.2,.6}
        }};
        std::set<FaceId> processed;
        while(!pending.empty()) {
            const auto face=pending.front();
            pending.pop_front();
            if(!processed.insert(face).second)continue;
            if(processed.size()>limits.maxOriginalFaces)
                throw std::length_error("Uncut original component face budget");
            if(touchedOriginals.contains(face)||sourceOf.contains(face)||
               !cut.originalIndex->maybeFace(face)||
               !cut.index->maybeFace(face))
                return refuse("Uncut face is not a distinct live original FaceId");

            // Stable original triangle, original vertex identities and
            // EVERY original coordinate are immutable. A changed old
            // face must pass through c1/c2 child provenance instead.
            const auto original=cut.originalIndex->face(face).vertices;
            if(cut.index->face(face).vertices!=original||
               cut.index->face(face).owner!=
                   cut.originalIndex->face(face).owner)
                return refuse("Uncut face has changed original topology or ownership");
            const auto tri=triangle(*cut.originalIndex,face);
            for(unsigned j=0;j<3;++j)
                if(!cut.index->maybeVertex(original[j])||
                   !same(cut.index->vertex(original[j]).position,tri[j]))
                    return refuse("Uncut face has changed original coordinates");
            const auto normal=cross(tri[1]-tri[0],tri[2]-tri[0]);
            const auto norm=length(normal);
            const auto shortest=std::min({length(tri[1]-tri[0]),
                length(tri[2]-tri[1]),length(tri[0]-tri[2])});
            if(!(norm>0)||!(shortest>0)||!finite(normal))
                return refuse("Unreliable original uncut face normal/scale");

            CollisionQueryLimits collision;
            collision.maxSeedFaces=1;
            collision.maxBoxTests=remaining(
                limits.maxCollisionBoxTests,result.discoveryWork.boxesTested);
            collision.maxPartitionTests=remaining(
                limits.maxCollisionPartitionTests,
                result.discoveryWork.partitionsTested);
            collision.maxCandidateTriangles=remaining(
                limits.maxCollisionCandidateTriangles,
                result.discoveryWork.candidateTriangles);
            collision.maxNarrowPhaseTests=remaining(
                limits.maxCollisionNarrowPhaseTests,
                result.discoveryWork.narrowPhaseTests);
            collision.maxReportedContacts=1;
            RegionTopologyIndex::CollisionStatistics work;
            const auto contacts=cut.originalIndex->triangleIntersections(
                {},{face},&work,collision);
            result.discoveryWork.boxesTested+=work.boxesTested;
            result.discoveryWork.partitionsTested+=work.partitionsTested;
            result.discoveryWork.candidateTriangles+=work.candidateTriangles;
            result.discoveryWork.narrowPhaseTests+=work.narrowPhaseTests;
            result.discoveryWork.reportedContacts+=work.reportedContacts;
            result.discoveryWork.seedFaces+=work.seedFaces;
            if(!contacts.empty())
                return refuse("Uncut original face has an unresolved V13 contact");

            const auto n=normal/norm;
            const double delta=std::min(1e-5*shortest,1e-3*shortest);
            if(!(delta>0)||!std::isfinite(delta))
                return refuse("Uncut face has no representable offset");
            bool referenceSet=false;
            int referenceMinus=0,referencePlus=0;
            for(const auto weights:bary) {
                const auto center=tri[0]*weights[0]+tri[1]*weights[1]+
                                  tri[2]*weights[2];
                const auto coordinateScale=std::max({1.0,
                    std::abs(center.x),std::abs(center.y),std::abs(center.z)});
                if(2*delta<=256*std::numeric_limits<double>::epsilon()*
                            coordinateScale)
                    return refuse("Uncut witness offset is below coordinate resolution");
                for(const double multiplier:std::array<double,2>{1.0,2.0}) {
                    std::array<int,2> winding{};
                    for(unsigned side=0;side<2;++side) {
                        if(result.windingQueries>=limits.maxWindingQueries)
                            throw std::length_error("Uncut winding witness budget");
                        ++result.windingQueries;
                        WindingQueryLimits windingLimits;
                        windingLimits.maxBoxTests=remaining(
                            limits.maxWindingBoxTests,
                            result.windingWork.boxesTested);
                        windingLimits.maxPartitionTests=remaining(
                            limits.maxWindingPartitionTests,
                            result.windingWork.partitionsTested);
                        windingLimits.maxTriangleTests=remaining(
                            limits.maxWindingTriangleTests,
                            result.windingWork.trianglesTested);
                        windingLimits.maxRays=remaining(
                            limits.maxWindingRays,
                            result.windingWork.raysTested);
                        RegionTopologyIndex::WindingStatistics counted;
                        const auto sample=cut.index->positiveWindingAt(
                            center+n*delta*multiplier*(side?1.0:-1.0),
                            &counted,false,windingLimits);
                        result.windingWork.boxesTested+=counted.boxesTested;
                        result.windingWork.partitionsTested+=
                            counted.partitionsTested;
                        result.windingWork.trianglesTested+=
                            counted.trianglesTested;
                        result.windingWork.raysTested+=counted.raysTested;
                        result.windingWork.referenceFallbacks+=
                            counted.referenceFallbacks;
                        result.windingWork.referenceFallbackRefusals+=
                            counted.referenceFallbackRefusals;
                        if(!sample.reliable)
                            return refuse("Uncut original witness is ambiguous");
                        winding[side]=sample.winding;
                    }
                    if(winding[0]-winding[1]!=1)
                        return refuse("Uncut original has no unit oriented winding jump");
                    if(!referenceSet) {
                        referenceSet=true;
                        referenceMinus=winding[0];
                        referencePlus=winding[1];
                    }else if(referenceMinus!=winding[0]||
                             referencePlus!=winding[1])
                        return refuse("Uncut original has nonhomogeneous signed occupancy");
                }
            }
            const bool minus=referenceMinus>0,plus=referencePlus>0;
            if(minus==plus)
                result.authorizedUncut.push_back(face);
            else
                result.protectedUncut.push_back(face);
            ++result.checkedOriginalFaces;
            neighbors(face);
        }
        if(mirrored) {
            // A complete off-plane orbit is REQUIRED even for protected
            // original faces. Prove each partner by ORIGINAL reciprocal
            // vertex identities, exact reflected XYZ and opposite winding.
            // No spatial nearest-neighbor search or world-position weld.
            std::map<FaceId,FaceId> partners;
            const auto findPartner=[&](FaceId face) {
                const auto tri=cut.originalIndex->face(face).vertices;
                std::array<VertexId,3> mapped{};
                for(unsigned j=0;j<3;++j) {
                    const auto v=tri[j];
                    const auto it=originalMirrors->find(v);
                    if(it==originalMirrors->end()||
                       !originalMirrors->contains(it->second)||
                       originalMirrors->at(it->second)!=v||
                       !cut.originalIndex->maybeVertex(it->second))
                        throw std::logic_error(
                            "Untouched source FaceId has missing/noninvolutive reflected vertex");
                    const auto p=cut.originalIndex->vertex(v).position,
                               q=cut.originalIndex->vertex(it->second).position;
                    if(q.x!=-p.x||q.y!=p.y||q.z!=p.z||
                       v==it->second||p.x==0||q.x==0)
                        throw std::logic_error(
                            "Invariant/incorrect original mirror point needs M06");
                    mapped[j]=it->second;
                }
                std::array<VertexId,3> identity=mapped;
                std::sort(identity.begin(),identity.end());
                std::optional<FaceId> found;
                for(const auto partnerFace:
                        cut.originalIndex->vertex(mapped[0]).incidentFaces) {
                    const auto other=cut.originalIndex->face(partnerFace).vertices;
                    auto sorted=other;
                    std::sort(sorted.begin(),sorted.end());
                    if(sorted!=identity)continue;
                    if(found)throw std::logic_error(
                        "Multiple coincident reflected original source faces");
                    // Reflection reverses orientation for closed outward
                    // solids; the topological cyclic winding is reversed.
                    const std::array<VertexId,3> backward{
                        mapped[0],mapped[2],mapped[1]};
                    bool valid=false;
                    for(unsigned shift=0;shift<3;++shift)
                        if(other[0]==backward[shift]&&
                           other[1]==backward[(shift+1)%3]&&
                           other[2]==backward[(shift+2)%3])
                            valid=true;
                    if(!valid)throw std::logic_error(
                        "Original uncut mirror face does not reverse winding");
                    found=partnerFace;
                }
                if(!found||*found==face)
                    throw std::logic_error(
                        "Uncut original mirror face partner absent or invariant");
                return *found;
            };
            for(const auto face:processed) {
                const auto counterpart=findPartner(face);
                if(!processed.contains(counterpart))
                    throw std::logic_error(
                        "Reflected uncut original connected component incomplete");
                partners.emplace(face,counterpart);
            }
            for(const auto& [face,counterpart]:partners)
                if(!partners.contains(counterpart)||
                   partners.at(counterpart)!=face)
                    throw std::logic_error(
                        "Uncut face correspondence is not reciprocal");
            const auto pairs=[&](const std::vector<FaceId>& classified,
                                 std::vector<std::pair<FaceId,FaceId>>& pairsOut) {
                const std::set<FaceId> included(
                    classified.begin(),classified.end());
                for(const auto face:included) {
                    const auto match=partners.at(face);
                    if(!included.contains(match))
                        throw std::logic_error(
                            "Mirrored original uncut occupancy classification differs");
                    if(face<match)pairsOut.emplace_back(face,match);
                }
                if(pairsOut.size()*2!=included.size())
                    throw std::logic_error(
                        "Unpaired original unchanged-face classification");
            };
            pairs(result.authorizedUncut,result.mirroredAuthorizedUncut);
            pairs(result.protectedUncut,result.mirroredProtectedUncut);
        }
        std::sort(result.authorizedUncut.begin(),result.authorizedUncut.end());
        std::sort(result.protectedUncut.begin(),result.protectedUncut.end());
        if(result.authorizedUncut.empty()) {
            result.status=Status::NoChanges;
            return result;
        }
        removal.insert(result.authorizedUncut.begin(),
                       result.authorizedUncut.end());
        if(removal.size()>settings.maxProposedRemoval||
           removal.size()>settings.contours.maxRemovedFaces)
            throw std::length_error("Combined child/uncut retirement face budget");
        result.combinedRemoval.assign(removal.begin(),removal.end());
        // V15 contour readiness is evaluated on the COMBINED authoritatively
        // selected FaceIds, not on a manually nominated child-only contour.
        // M04 alone may later consume the result for a final d2/d3 join.
        result.contourPlan=planRemovalContours(
            *cut.index,result.combinedRemoval,settings.contours);
        if(!result.contourPlan.ready()||
           result.contourPlan.removedFaces!=result.combinedRemoval)
            return refuse("Combined uncut-face V15 contour refusal: "+
                          result.contourPlan.reason);
        result.status=Status::Ready;
        return result;
    }catch(const std::length_error& e) {
        return refuse(e.what(),Status::BudgetExceeded);
    }catch(const std::exception& e) {
        return refuse(e.what());
    }
}
} // namespace mesher

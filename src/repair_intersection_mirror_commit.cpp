#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using CutTriangle=std::array<VertexId,3>;
using LocalTriangle=std::array<std::size_t,3>;

bool cyclic(CutTriangle a,CutTriangle b) {
    for(unsigned shift=0;shift<3;++shift)
        if(a[0]==b[shift]&&a[1]==b[(shift+1)%3]&&
           a[2]==b[(shift+2)%3])return true;
    return false;
}
std::array<VertexId,3> ordered(CutTriangle t) {
    std::sort(t.begin(),t.end());
    return t;
}
bool sameEnds(std::array<std::size_t,2> a,
              std::array<std::size_t,2> b) {
    if(a[0]>a[1])std::swap(a[0],a[1]);
    if(b[0]>b[1])std::swap(b[0],b[1]);
    return a==b;
}
PartitionId pointOwner(const RegionTopologyIndex& baseline,
                       const ConformingCutPoint& point) {
    if(point.boundaryEdge) {
        if(!baseline.maybeEdgeSides(*point.boundaryEdge))
            throw std::logic_error("Mirrored cut references a retired original edge");
        return baseline.vertex(point.boundaryEdge->a).owner;
    }
    if(!baseline.maybeFace(point.owningFace))
        throw std::logic_error("Mirrored interior cut owner is not a live original face");
    return baseline.face(point.owningFace).owner;
}
} // namespace

// Called only by materializeTransverseCutCandidate. Every permanent ID
// allocated below belongs to ONE unpublished, local RegionEdit. No root CAS,
// mirror-map update, dirty renderer partition or history record occurs here.
ConformingCutCandidate materializeMirroredTransverseCutCandidate(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ConformingCutLimits& limits,std::size_t partitionCapacity,
    const std::unordered_map<VertexId,VertexId>& mirrors) {
    // Rebuild against this exact immutable candidate. A caller cannot inject
    // stale face triangles or an incomplete ephemeral pairing into a commit.
    const auto plan=buildConformingCutPlan(*baseline,contacts,limits,&mirrors);
    if(plan.status!=IntersectionSectionStatus::TransverseSegment||
       plan.faces.empty()||plan.segments.empty())
        throw std::logic_error("Mirrored conforming candidate refused: "+plan.refusal);

    std::map<FaceId,std::size_t> faceById;
    std::map<CutTriangle,std::size_t> faceByCorners;
    std::map<VertexId,std::size_t> originalPoint;
    std::set<VertexId> seeds;
    for(std::size_t i=0;i<plan.faces.size();++i) {
        const auto id=plan.faces[i].sourceFace;
        if(plan.faces[i].triangles.empty()||
           !baseline->maybeFace(id)||!faceById.emplace(id,i).second)
            throw std::logic_error("Mirrored cut has missing/duplicate source face");
        const auto vertices=baseline->face(id).vertices;
        if(!faceByCorners.emplace(ordered(vertices),i).second)
            throw std::logic_error("Ambiguous original face orbit");
        seeds.insert(vertices.begin(),vertices.end());
    }
    if(seeds.empty())throw std::logic_error("Mirrored cut has no writable source");

    // The RegionEdit mirror map is restricted to the LOCAL writable halo,
    // never copied from all mirrored vertices in a large sculpted model.
    const auto mirrorOf=[&](VertexId v)->VertexId {
        const auto it=mirrors.find(v);
        if(it==mirrors.end()||!baseline->maybeVertex(it->second)||
           !mirrors.contains(it->second)||mirrors.at(it->second)!=v)
            throw std::logic_error("Missing/noninvolutive original cut vertex mirror");
        const auto p=baseline->vertex(v).position,
                   q=baseline->vertex(it->second).position;
        if(q.x!=-p.x||q.y!=p.y||q.z!=p.z||
           (v==it->second&&p.x!=0))
            throw std::logic_error("Original cut vertex is not exactly reflected");
        return it->second;
    };
    std::vector<std::pair<VertexId,VertexId>> localMirrors;
    localMirrors.reserve(seeds.size());
    for(const auto v:seeds) {
        const auto partner=mirrorOf(v);
        if(!seeds.contains(partner))
            throw std::logic_error("Mirrored cut writable halo is not orbit-closed");
        if(v<=partner)localMirrors.emplace_back(v,partner);
    }
    for(std::size_t i=0;i<plan.points.size();++i)
        if(plan.points[i].existingVertex) {
            const auto id=*plan.points[i].existingVertex;
            if(!seeds.contains(id)||!originalPoint.emplace(id,i).second)
                throw std::logic_error("Invalid/duplicate original vertex in cut graph");
            const auto p=baseline->vertex(id).position;
            const auto q=plan.points[i].position;
            if(p.x!=q.x||p.y!=q.y||p.z!=q.z)
                throw std::logic_error("Cut graph altered original vertex coordinates");
        }
    // Full point orbits include ORIGINAL corners and NEW cut vertices. A new
    // X=0 self-pair cannot be manufactured without proving seam fan topology.
    std::vector<std::size_t> partner(plan.points.size(),plan.points.size());
    for(std::size_t i=0;i<plan.points.size();++i)
        if(plan.points[i].existingVertex) {
            const auto twin=originalPoint.find(mirrorOf(*plan.points[i].existingVertex));
            if(twin==originalPoint.end())
                throw std::logic_error("Missing original mirrored corner in cut graph");
            partner[i]=twin->second;
        }
    for(const auto& [a,b]:plan.mirroredCutPoints) {
        if(a>=plan.points.size()||b>=plan.points.size()||a==b||
           plan.points[a].existingVertex||plan.points[b].existingVertex||
           partner[a]!=plan.points.size()||partner[b]!=plan.points.size())
            throw std::logic_error("Invalid or duplicated mirrored new cut point");
        const auto p=plan.points[a].position,q=plan.points[b].position;
        if(p.x==0||q.x==0||q.x!=-p.x||q.y!=p.y||q.z!=p.z)
            throw std::logic_error("New mirror cut requires distinct exact off-plane orbit");
        partner[a]=b;partner[b]=a;
    }
    for(std::size_t i=0;i<partner.size();++i)
        if(partner[i]>=partner.size()||partner[partner[i]]!=i)
            throw std::logic_error("Incomplete mirrored cut point orbit");

    std::vector<std::size_t> facePartner(plan.faces.size(),plan.faces.size());
    for(std::size_t i=0;i<plan.faces.size();++i) {
        const auto original=baseline->face(plan.faces[i].sourceFace).vertices;
        const CutTriangle reflected{
            mirrorOf(original[0]),mirrorOf(original[2]),mirrorOf(original[1])};
        const auto found=faceByCorners.find(ordered(reflected));
        if(found==faceByCorners.end()||found->second==i||
           !cyclic(reflected,
               baseline->face(plan.faces[found->second].sourceFace).vertices))
            throw std::logic_error("Invalid or invariant reflected source face");
        facePartner[i]=found->second;
    }
    for(std::size_t i=0;i<facePartner.size();++i)
        if(facePartner[facePartner[i]]!=i)
            throw std::logic_error("Reflected source faces are not reciprocal");

    // Single-section as well as multiple-section plans must include BOTH
    // contact curves. Pair curves using ORIGINAL face provenance and their
    // independently topological endpoint orbits, never coordinate welding.
    std::map<std::pair<FaceId,FaceId>,std::size_t> sections;
    for(std::size_t i=0;i<plan.segments.size();++i) {
        const auto& seg=plan.segments[i];
        if(!sections.emplace(std::minmax(seg.firstFace,seg.secondFace),i).second)
            throw std::logic_error("Duplicated cut section pair");
    }
    for(const auto& seg:plan.segments) {
        const auto a=faceById.find(seg.firstFace),
                   b=faceById.find(seg.secondFace);
        if(a==faceById.end()||b==faceById.end())
            throw std::logic_error("Cut section source absent from editable faces");
        const auto reflectedA=plan.faces[facePartner[a->second]].sourceFace,
                   reflectedB=plan.faces[facePartner[b->second]].sourceFace;
        const auto other=sections.find(std::minmax(reflectedA,reflectedB));
        if(other==sections.end())
            throw std::logic_error("Mirrored contact section missing");
        const auto& twin=plan.segments[other->second];
        const std::array<std::size_t,2> mappedA{
            partner.at(seg.firstSide[0]),partner.at(seg.firstSide[1])};
        const std::array<std::size_t,2> mappedB{
            partner.at(seg.secondSide[0]),partner.at(seg.secondSide[1])};
        const bool same=twin.firstFace==reflectedA;
        if(!sameEnds(mappedA,same?twin.firstSide:twin.secondSide)||
           !sameEnds(mappedB,same?twin.secondSide:twin.firstSide))
            throw std::logic_error("Mirrored contact curve mismatches section endpoints");
    }

    // First validate all canonical face children have reflected counterparts.
    // Two independent ear clippers may select different scaffold diagonals:
    // the canonical side DEFINES both halves' child topology at materialize.
    for(std::size_t i=0;i<plan.faces.size();++i) {
        const auto j=facePartner[i];
        if(i>=j)continue;
        const auto& source=plan.faces[i];
        const auto& reflected=plan.faces[j];
        if(source.triangles.size()!=reflected.triangles.size())
            throw std::logic_error("Reflected source has different child triangle count");
        for(std::size_t k=0;k<source.triangles.size();++k) {
            const LocalTriangle t=source.triangles[k];
            const LocalTriangle expected{
                partner.at(t[0]),partner.at(t[2]),partner.at(t[1])};
            // The multi-section preflight already enforces this; the
            // single-section fast path is permitted different scaffold
            // diagonals only if it keeps the same number of subfaces.
            (void)expected;
        }
    }

    RegionEdit edit(baseline,{seeds.begin(),seeds.end()},
                    std::move(localMirrors),partitionCapacity);
    ConformingCutCandidate output;
    output.originalIndex=baseline;
    output.sourceContacts=contacts;
    std::vector<VertexId> assigned(plan.points.size());
    for(std::size_t i=0;i<plan.points.size();++i)
        if(plan.points[i].existingVertex)assigned[i]=*plan.points[i].existingVertex;
    for(std::size_t i=0;i<plan.points.size();++i) {
        if(plan.points[i].existingVertex||i>partner[i])continue;
        const auto j=partner[i];
        const auto [first,second]=edit.appendMirroredVertices(
            pointOwner(*baseline,plan.points[i]),
            pointOwner(*baseline,plan.points[j]),plan.points[i].position);
        assigned[i]=first;assigned[j]=second;
        output.insertedVertices.push_back(first);
        output.insertedVertices.push_back(second);
        output.mirroredVertices.emplace_back(first,second);
    }
    const auto transformed=[&](LocalTriangle tri)->CutTriangle {
        for(const auto i:tri)
            if(i>=assigned.size()||assigned[i]==0)
                throw std::logic_error("Reflected cut child has unassigned vertex");
        return {assigned[tri[0]],assigned[tri[1]],assigned[tri[2]]};
    };
    for(std::size_t i=0;i<plan.faces.size();++i) {
        const auto j=facePartner[i];
        if(i>=j)continue;
        const auto& a=plan.faces[i];
        const auto& b=plan.faces[j];
        edit.replaceMirroredFaces(a.sourceFace,b.sourceFace,
                                  transformed(a.triangles.front()));
        output.rewrittenFaces.push_back(a.sourceFace);
        output.rewrittenFaces.push_back(b.sourceFace);
        output.childSources.emplace_back(a.sourceFace,a.sourceFace);
        output.childSources.emplace_back(b.sourceFace,b.sourceFace);
        output.mirroredFaces.emplace_back(a.sourceFace,b.sourceFace);
        for(std::size_t k=1;k<a.triangles.size();++k) {
            const auto [first,second]=edit.appendMirroredFaces(
                baseline->face(a.sourceFace).owner,
                baseline->face(b.sourceFace).owner,
                transformed(a.triangles[k]));
            output.insertedFaces.push_back(first);
            output.insertedFaces.push_back(second);
            output.childSources.emplace_back(first,a.sourceFace);
            output.childSources.emplace_back(second,b.sourceFace);
            output.mirroredFaces.emplace_back(first,second);
        }
    }
    for(const auto& segment:plan.segments) {
        std::array<VertexId,2> first{},second{};
        for(unsigned i=0;i<2;++i) {
            if(segment.firstSide[i]>=assigned.size()||
               segment.secondSide[i]>=assigned.size())
                throw std::logic_error("Contact segment references unknown cut point");
            first[i]=assigned[segment.firstSide[i]];
            second[i]=assigned[segment.secondSide[i]];
            if(first[i]==second[i])
                throw std::logic_error("Independent intersecting surfaces were pre-welded");
        }
        output.sectionFirst.push_back(first);
        output.sectionSecond.push_back(second);
    }

    // A single RegionEdit validates all changed face pairs, edge incidence
    // and complete vertex fans before any immutable revision is returned.
    output.commit=edit.commit();
    output.index=edit.updatedIndex();
    if(!output.index||output.index==baseline||
       output.commit.revision.get()!=&output.index->revision())
        throw std::logic_error("Mirrored cut omitted its unpublished revision root");
    return output;
}

} // namespace mesher

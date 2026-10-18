#include "mesher/repair_surgery.hpp"
#include "mesher/versioned_refine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace mesher {
namespace {
bool finitePoint(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
double closestSquared(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    const auto ab=b-a,ac=c-a,ap=p-a;
    const double d1=dot(ab,ap),d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return dot(ap,ap);
    const auto bp=p-b;
    const double d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0&&d4<=d3)return dot(bp,bp);
    const double vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0) {
        const auto nearest=a+ab*(d1/(d1-d3));
        return dot(p-nearest,p-nearest);
    }
    const auto cp=p-c;
    const double d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0&&d5<=d6)return dot(cp,cp);
    const double vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0) {
        const auto nearest=a+ac*(d2/(d2-d6));
        return dot(p-nearest,p-nearest);
    }
    const double va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0) {
        const auto bc=c-b;
        const auto nearest=b+bc*((d4-d3)/((d4-d3)+(d5-d6)));
        return dot(p-nearest,p-nearest);
    }
    const double denominator=va+vb+vc;
    if(!(denominator>0)||!std::isfinite(denominator))
        throw std::logic_error("Invalid candidate triangle for surface witness audit");
    const auto nearest=a+ab*(vb/denominator)+ac*(vc/denominator);
    return dot(p-nearest,p-nearest);
}
}

std::vector<Vec3> sampleSurfaceWitnesses(
    const RegionTopologyIndex& original,
    const std::vector<FaceId>& faces,
    std::size_t maxWitnesses) {
    if(maxWitnesses==0||faces.empty()||
       faces.size()>maxWitnesses/7)
        throw std::length_error("Surface witness sampling exceeds face or sample budget");
    std::vector<Vec3> points;
    points.reserve(faces.size()*7);
    for(const auto id:faces) {
        const auto ids=original.face(id).vertices;
        const auto a=original.vertex(ids[0]).position;
        const auto b=original.vertex(ids[1]).position;
        const auto c=original.vertex(ids[2]).position;
        if(!finitePoint(a)||!finitePoint(b)||!finitePoint(c))
            throw std::logic_error("Nonfinite original surface witness");
        points.insert(points.end(),{
            a,b,c,(a+b)/2,(b+c)/2,(c+a)/2,(a+b+c)/3});
    }
    return points;
}

SurfaceWitnessAudit auditSurfaceWitnesses(
    const RegionTopologyIndex& current,
    const std::vector<Vec3>& witnesses,
    const std::vector<FaceId>& candidateFaces,
    double originalEdgeLength,
    std::size_t maxDistanceTests) {
    if(witnesses.empty()||candidateFaces.empty()||
       !std::isfinite(originalEdgeLength)||originalEdgeLength<=0||
       maxDistanceTests==0)
        throw std::invalid_argument("Invalid surface witness audit arguments");
    std::set<FaceId> uniqueFaces(candidateFaces.begin(),candidateFaces.end());
    if(witnesses.size()>maxDistanceTests/uniqueFaces.size())
        throw std::length_error("Surface witness audit exceeds distance work budget");
    std::vector<std::array<Vec3,3>> triangles;
    triangles.reserve(uniqueFaces.size());
    for(const auto id:uniqueFaces) {
        const auto ids=current.face(id).vertices; // retired IDs fail closed
        const auto a=current.vertex(ids[0]).position;
        const auto b=current.vertex(ids[1]).position;
        const auto c=current.vertex(ids[2]).position;
        if(!finitePoint(a)||!finitePoint(b)||!finitePoint(c)||
           !(dot(cross(b-a,c-a),cross(b-a,c-a))>0))
            throw std::logic_error("Invalid audited candidate triangle");
        triangles.push_back({a,b,c});
    }
    SurfaceWitnessAudit audit;
    audit.witnessCount=witnesses.size();
    audit.candidateFaces=uniqueFaces.size();
    double sum=0,energy=0;
    for(const auto witness:witnesses) {
        if(!finitePoint(witness))
            throw std::invalid_argument("Nonfinite original witness point");
        double closest=std::numeric_limits<double>::infinity();
        for(const auto& triangle:triangles) {
            ++audit.distanceTests;
            closest=std::min(closest,closestSquared(
                witness,triangle[0],triangle[1],triangle[2]));
        }
        if(!std::isfinite(closest))
            throw std::logic_error("Surface witness distance is not finite");
        const double distance=std::sqrt(closest)/originalEdgeLength;
        if(!std::isfinite(distance))
            throw std::logic_error("Surface witness normalization overflow");
        audit.maximumDistanceInEdges=std::max(
            audit.maximumDistanceInEdges,distance);
        sum+=distance;
        energy+=distance*distance;
    }
    audit.meanDistanceInEdges=sum/audit.witnessCount;
    audit.rmsDistanceInEdges=std::sqrt(energy/audit.witnessCount);
    if(!std::isfinite(audit.meanDistanceInEdges)||
       !std::isfinite(audit.rmsDistanceInEdges))
        throw std::logic_error("Surface witness aggregate overflow");
    return audit;
}
OriginalSourceSurfaceAnchors captureOriginalSourceAnchors(
    std::shared_ptr<const RegionTopologyIndex> original,
    const std::vector<FaceId>& protectedFaces,
    std::size_t maxWitnesses) {
    if(!original||protectedFaces.empty())
        throw std::invalid_argument("Original source anchors require a root and protected faces");
    if(maxWitnesses==0||protectedFaces.size()>maxWitnesses/7)
        throw std::length_error("Original source anchor capture exceeds witness budget");
    OriginalSourceSurfaceAnchors result;
    result.original=std::move(original);
    result.faces.reserve(protectedFaces.size());
    std::set<FaceId> unique;
    for(const FaceId id:protectedFaces) {
        if(!unique.insert(id).second||!result.original->maybeFace(id))
            throw std::invalid_argument("Duplicate or missing protected original FaceId");
        const auto vertices=result.original->face(id).vertices;
        const Vec3 a=result.original->vertex(vertices[0]).position;
        const Vec3 b=result.original->vertex(vertices[1]).position;
        const Vec3 c=result.original->vertex(vertices[2]).position;
        if(!finitePoint(a)||!finitePoint(b)||!finitePoint(c))
            throw std::invalid_argument("Nonfinite protected original geometry");
        const double ab=dot(b-a,b-a),bc=dot(c-b,c-b),
                     ca=dot(a-c,a-c);
        const double smallest=std::min({ab,bc,ca});
        if(!(smallest>0)||!std::isfinite(smallest))
            throw std::invalid_argument("Degenerate protected original edge scale");
        OriginalSourceFaceAnchor anchor;
        anchor.face=id;
        anchor.originalVertices=vertices;
        anchor.originalEdgeLength=std::sqrt(smallest);
        const auto points=sampleSurfaceWitnesses(*result.original,{id},7);
        std::copy(points.begin(),points.end(),anchor.witnesses.begin());
        result.faces.push_back(anchor);
    }
    return result;
}

SurfaceWitnessAudit auditOriginalSourceAnchors(
    const OriginalSourceSurfaceAnchors& anchors,
    const std::shared_ptr<const RegionTopologyIndex>& expectedReference,
    const RegionTopologyIndex& candidate,
    std::size_t maxDistanceTests) {
    if(!anchors.original||!expectedReference||
       anchors.original!=expectedReference||anchors.faces.empty())
        throw std::invalid_argument("Missing or stale original surface anchor reference");
    if(anchors.faces.size()>maxDistanceTests/7)
        throw std::length_error("Original source anchor audit exceeds distance work budget");
    SurfaceWitnessAudit total;
    double meanSum=0,energySum=0;
    std::set<FaceId> unique;
    for(const auto& anchor:anchors.faces) {
        // Source FaceId and ordered vertex incidence, not XYZ, establish
        // this limited M19 correspondence. M20 supplies certified remesh
        // descendants; this conservative gate refuses them until then.
        if(!unique.insert(anchor.face).second)
            throw std::invalid_argument("Duplicate source patch anchor");
        const auto source=anchors.original->maybeFace(anchor.face);
        const auto living=candidate.maybeFace(anchor.face);
        if(!source||source->vertices!=anchor.originalVertices||
           !living||living->vertices!=anchor.originalVertices)
            throw std::logic_error("Protected original patch ancestry is missing or rewritten");
        if(!(anchor.originalEdgeLength>0)||
           !std::isfinite(anchor.originalEdgeLength))
            throw std::invalid_argument("Invalid original source edge scale");
        const std::vector<Vec3> points(
            anchor.witnesses.begin(),anchor.witnesses.end());
        const auto sample=auditSurfaceWitnesses(
            candidate,points,{anchor.face},anchor.originalEdgeLength,7);
        total.witnessCount+=sample.witnessCount;
        total.candidateFaces+=sample.candidateFaces;
        total.distanceTests+=sample.distanceTests;
        total.maximumDistanceInEdges=std::max(
            total.maximumDistanceInEdges,sample.maximumDistanceInEdges);
        meanSum+=sample.meanDistanceInEdges*sample.witnessCount;
        energySum+=sample.rmsDistanceInEdges*
                   sample.rmsDistanceInEdges*sample.witnessCount;
    }
    total.meanDistanceInEdges=meanSum/total.witnessCount;
    total.rmsDistanceInEdges=std::sqrt(energySum/total.witnessCount);
    if(!std::isfinite(total.meanDistanceInEdges)||
       !std::isfinite(total.rmsDistanceInEdges))
        throw std::logic_error("Original source anchor distance aggregate overflow");
    return total;
}


SourceSplitLineage beginSourceSplitLineage(
    const OriginalSourceSurfaceAnchors& anchors) {
    if(!anchors.original||anchors.faces.empty())
        throw std::invalid_argument("Split lineage needs immutable original protected sources");
    SourceSplitLineage lineage;
    lineage.original=anchors.original;
    lineage.terminal=anchors.original;
    std::set<FaceId> distinct;
    for(const auto& anchor:anchors.faces) {
        if(!distinct.insert(anchor.face).second)
            throw std::invalid_argument("Duplicate fixed-original lineage source");
        const auto source=anchors.original->maybeFace(anchor.face);
        if(!source||source->vertices!=anchor.originalVertices)
            throw std::logic_error("Original lineage source incidence was replaced");
        lineage.living.emplace_back(anchor.face,std::vector<FaceId>{anchor.face});
    }
    return lineage;
}

SourceSplitLineage advanceCertifiedSplitLineage(
    const SourceSplitLineage& prior,
    const VersionedRefineResult& refined,
    std::size_t maxFaceEvidence,std::size_t maxLivingDescendants) {
    if(!prior.original||!prior.terminal||prior.living.empty()||
       !refined.stampOriginal||!refined.sourceIndex||!refined.index||
       prior.terminal!=refined.stampOriginal)
        throw std::invalid_argument("Stale or missing original-root split lineage chain");
    if(!maxFaceEvidence||!maxLivingDescendants)
        throw std::invalid_argument("Split provenance limits must be positive");
    if(refined.splitSources.size()>maxFaceEvidence)
        throw std::length_error("Split provenance source-face work cap exceeded");
    if(refined.statistics.flipPairs)
        throw std::logic_error("Split-only source lineage cannot approve a face flip");
    SourceSplitLineage updated=prior;
    // Normal-driven Inflate may have moved vertices, but cannot silently
    // replace any original patch's ordered permanent vertex incidence.
    std::set<FaceId> tracked;
    for(const auto& [source,faces]:updated.living) {
        if(!prior.original->maybeFace(source)||faces.empty())
            throw std::logic_error("Source lineage has missing original identity");
        for(const auto face:faces) {
            if(!tracked.insert(face).second)
                throw std::logic_error("Independent source patches share a live FaceId");
            const auto before=prior.terminal->maybeFace(face);
            const auto now=refined.sourceIndex->maybeFace(face);
            if(!before||!now||before->vertices!=now->vertices)
                throw std::logic_error("Unproven source face changed before the first split");
        }
    }
    if(tracked.size()>maxLivingDescendants)
        throw std::length_error("Split lineage living patch budget exhausted");
    if(refined.splitSources.empty()) {
        if(refined.statistics.splitPairs||!refined.insertedVertices.empty()||
           !refined.changedFaces.empty()||refined.index!=refined.sourceIndex)
            throw std::logic_error("Unjournaled source split or candidate topology change");
        updated.terminal=refined.index;
        return updated;
    }
    std::set<FaceId> changed;
    std::set<VertexId> seenMidpoints;
    auto current=refined.sourceIndex;
    std::size_t groups=0;
    for(std::size_t i=0;i<refined.splitSources.size();) {
        const auto& first=refined.splitSources[i];
        if(first.before!=current||!first.after||first.after==current)
            throw std::logic_error("Split source proof roots are stale or out of order");
        auto next=first.after;
        const std::size_t start=i;
        while(i<refined.splitSources.size()&&
              refined.splitSources[i].before==current&&
              refined.splitSources[i].after==next)
            ++i;
        const auto count=i-start;
        if(count!=2&&count!=4)
            throw std::logic_error("Committed split is missing an incident source face");
        std::set<FaceId> groupSources,groupChildren;
        std::map<VertexId,std::size_t> midpointUse;
        for(std::size_t j=start;j<i;++j) {
            const auto& record=refined.splitSources[j];
            if(!record.sourceFace||!record.appendedFace||
               !record.midpoint||!record.edgeA||!record.edgeB||
               record.edgeA==record.edgeB||
               record.sourceFace==record.appendedFace||
               !groupSources.insert(record.sourceFace).second||
               !groupChildren.insert(record.appendedFace).second||
               current->maybeFace(record.appendedFace)||
               current->maybeVertex(record.midpoint))
                throw std::logic_error("Forged split source ID or repeated child ancestry");
            const auto parent=current->maybeFace(record.sourceFace);
            const auto kept=next->maybeFace(record.sourceFace);
            const auto appended=next->maybeFace(record.appendedFace);
            const auto midpoint=next->maybeVertex(record.midpoint);
            const auto a=current->maybeVertex(record.edgeA);
            const auto b=current->maybeVertex(record.edgeB);
            if(!parent||!kept||!appended||!midpoint||!a||!b)
                throw std::logic_error("Split source triangle or midpoint was not committed");
            const auto mid=(a->position+b->position)*.5;
            if(midpoint->position.y!=mid.y||midpoint->position.z!=mid.z||
               (midpoint->position.x!=mid.x&&
                !(midpoint->position.x==0&&std::abs(mid.x)<=1e-9)))
                throw std::logic_error("Child vertex is not its proven original edge midpoint");
            bool matches=false;
            const auto old=parent->vertices;
            for(unsigned k=0;k<3;++k) {
                const auto u=old[k],v=old[(k+1)%3],c=old[(k+2)%3];
                if(!((u==record.edgeA&&v==record.edgeB)||
                     (u==record.edgeB&&v==record.edgeA)))continue;
                if(kept->vertices==std::array<VertexId,3>{u,record.midpoint,c}&&
                   appended->vertices==
                       std::array<VertexId,3>{record.midpoint,v,c})
                    matches=true;
            }
            if(!matches)
                throw std::logic_error("Child triangles are not oriented exact source subdivisions");
            ++midpointUse[record.midpoint];
            changed.insert(record.sourceFace);
            if(!changed.insert(record.appendedFace).second)
                throw std::logic_error("A new child FaceId was assigned twice");
            // Update protected roots exclusively through this source-owned
            // directed parent record, retaining the parent rewritten FaceId.
            for(auto& [_,living]:updated.living) {
                if(std::find(living.begin(),living.end(),record.sourceFace)!=
                   living.end()) {
                    if(std::find(living.begin(),living.end(),
                                 record.appendedFace)!=living.end())
                        throw std::logic_error("Duplicate protected descendant FaceId");
                    living.push_back(record.appendedFace);
                }
            }
        }
        if(midpointUse.size()!=count/2)
            throw std::logic_error("Split group has inconsistent new midpoint count");
        for(const auto& [id,uses]:midpointUse) {
            if(uses!=2||!seenMidpoints.insert(id).second)
                throw std::logic_error("Split midpoint lacks exactly two original incident faces");
        }
        ++groups;
        current=next;
    }
    if(current!=refined.index||groups!=refined.statistics.splitPairs)
        throw std::logic_error("Split evidence does not span the complete final remesh");
    std::set<VertexId> inserted(
        refined.insertedVertices.begin(),refined.insertedVertices.end());
    std::set<FaceId> reported(
        refined.changedFaces.begin(),refined.changedFaces.end());
    if(inserted.size()!=refined.insertedVertices.size()||
       inserted!=seenMidpoints||reported!=changed||
       reported.size()!=refined.changedFaces.size())
        throw std::logic_error("Split result ID journals are incomplete or forged");
    std::set<VertexId> mirrorRecorded;
    for(const auto& [a,b]:refined.insertedMirrorPairs) {
        if(!seenMidpoints.contains(a)||!seenMidpoints.contains(b)||
           !mirrorRecorded.insert(a).second||
           (a!=b&&!mirrorRecorded.insert(b).second))
            throw std::logic_error("New mirror split provenance is incomplete");
        const auto p=refined.index->vertex(a).position;
        const auto q=refined.index->vertex(b).position;
        if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
           ((a==b)!=(p.x==0)))
            throw std::logic_error("Split midpoint mirror orbit is not exact");
    }
    if(!refined.insertedMirrorPairs.empty()&&
       mirrorRecorded!=seenMidpoints)
        throw std::logic_error("Reflected split provenance omits a midpoint orbit");
    tracked.clear();
    std::size_t livingCount=0;
    for(auto& [source,faces]:updated.living) {
        if(!prior.original->maybeFace(source))
            throw std::logic_error("Original lineage has disappeared");
        for(const auto id:faces) {
            if(!refined.index->maybeFace(id)||!tracked.insert(id).second)
                throw std::logic_error("Protected split descendant missing or double-owned");
        }
        if(faces.size()>maxLivingDescendants-livingCount)
            throw std::length_error("Protected split descendants exceed finite patch budget");
        livingCount+=faces.size();
        std::sort(faces.begin(),faces.end());
    }
    updated.terminal=refined.index;
    updated.verifiedFaceSplits+=refined.splitSources.size();
    return updated;
}

} // namespace mesher

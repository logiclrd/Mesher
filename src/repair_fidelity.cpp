#include "mesher/repair_surgery.hpp"
#include "mesher/versioned_refine.hpp"
#include "mesher/versioned_collapse.hpp"
#include "mesher/repair_intersection.hpp"

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
        if((count!=2&&count!=4)||
           (!refined.sourceSplitsSymmetric&&count!=2))
            throw std::logic_error("Committed split has missing or extra incident source faces");
        std::set<FaceId> groupSources,groupChildren;
        std::map<VertexId,std::size_t> midpointUse;
        // Each midpoint must subdivide ONE undirected permanent-ID edge,
        // seen once in each opposite orientation. Coincident disjoint edges
        // have different IDs and must never be falsely source-paired.
        std::map<VertexId,std::pair<VertexId,VertexId>> midpointEdges;
        std::map<VertexId,unsigned> midpointOrientations;
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
                !(refined.sourceSplitsSymmetric&&midpoint->position.x==0&&
                  std::abs(mid.x)<=1e-9&&
                  a->position.x==-b->position.x&&
                  a->position.y==b->position.y&&
                  a->position.z==b->position.z)))
                throw std::logic_error("Child vertex is not its proven original edge midpoint");
            bool matches=false;
            const auto old=parent->vertices;
            unsigned directedMask=0;
            for(unsigned k=0;k<3;++k) {
                const auto u=old[k],v=old[(k+1)%3],c=old[(k+2)%3];
                if(!((u==record.edgeA&&v==record.edgeB)||
                     (u==record.edgeB&&v==record.edgeA)))continue;
                if(kept->vertices==std::array<VertexId,3>{u,record.midpoint,c}&&
                   appended->vertices==
                       std::array<VertexId,3>{record.midpoint,v,c}) {
                    matches=true;
                    directedMask=u==std::min(record.edgeA,record.edgeB)?1u:2u;
                }
            }
            if(!matches)
                throw std::logic_error("Child triangles are not oriented exact source subdivisions");
            const auto physicalEdge=std::minmax(record.edgeA,record.edgeB);
            const std::pair<VertexId,VertexId> edgeIds{
                physicalEdge.first,physicalEdge.second};
            const auto [edgeIt,newEdge]=midpointEdges.emplace(
                record.midpoint,edgeIds);
            if(!newEdge&&edgeIt->second!=edgeIds)
                throw std::logic_error("Two coincident source edges falsely share a midpoint ID");
            midpointOrientations[record.midpoint]|=directedMask;
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
            if(uses!=2||midpointOrientations[id]!=3u||
               !seenMidpoints.insert(id).second)
                throw std::logic_error("Split midpoint lacks both opposite source edge incidences");
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
    if((refined.sourceSplitsSymmetric&&mirrorRecorded!=seenMidpoints)||
       (!refined.sourceSplitsSymmetric&&!refined.insertedMirrorPairs.empty()))
        throw std::logic_error("Split provenance missing or invented declared mirror orbits");
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


namespace {
// A pair of oriented triangles must have precisely the same directed
// exterior boundary after replacing its internal diagonal. This is an ID
// proof, not a proximity-based lookup or a tolerance-laden inferred weld.
using SignedEdges=std::map<std::pair<VertexId,VertexId>,int>;
SignedEdges orientedBoundary(
    const std::array<std::array<VertexId,3>,2>& triangles) {
    SignedEdges boundary;
    for(const auto& t:triangles)
        for(unsigned k=0;k<3;++k) {
            const auto a=t[k],b=t[(k+1)%3];
            if(!a||!b||a==b)
                throw std::logic_error("Invalid oriented flip triangle");
            const auto pair=std::minmax(a,b);
            const std::pair<VertexId,VertexId> key{pair.first,pair.second};
            boundary[key]+=a<b?1:-1;
        }
    for(auto it=boundary.begin();it!=boundary.end();) {
        if(it->second==0)it=boundary.erase(it);
        else {
            if(std::abs(it->second)!=1)
                throw std::logic_error("Invalid oriented flip boundary");
            ++it;
        }
    }
    if(boundary.size()!=4)
        throw std::logic_error("A source flip needs a four-edge quad boundary");
    return boundary;
}
std::set<VertexId> quadVertices(
    const std::array<std::array<VertexId,3>,2>& triangles) {
    std::set<VertexId> ids;
    for(const auto& tri:triangles)
        ids.insert(tri.begin(),tri.end());
    if(ids.size()!=4)
        throw std::logic_error("Flip loses its four unique permanent vertices");
    return ids;
}
double triangleArea(const RegionTopologyIndex& index,
                    const std::array<VertexId,3>& t) {
    const auto a=index.vertex(t[0]).position;
    const auto b=index.vertex(t[1]).position;
    const auto c=index.vertex(t[2]).position;
    return length(cross(b-a,c-a))*.5;
}
std::set<FaceId> protectedIds(const SourceSplitLineage& lineage) {
    std::set<FaceId> protectedFaces;
    for(const auto& [_,faces]:lineage.living)
        for(const auto face:faces)
            if(!protectedFaces.insert(face).second)
                throw std::logic_error("Protected FaceId belongs to two sources");
    return protectedFaces;
}
void exactProtectedFaces(const SourceSplitLineage& previous,
                         const RegionTopologyIndex& candidate) {
    for(const auto& [_,faces]:previous.living)
        for(const auto id:faces) {
            const auto old=previous.terminal->maybeFace(id);
            const auto next=candidate.maybeFace(id);
            if(!old||!next||old->vertices!=next->vertices)
                throw std::logic_error("Protected source incidence changed without intent");
            for(const auto vertex:old->vertices) {
                const auto a=previous.terminal->vertex(vertex).position;
                const auto b=candidate.vertex(vertex).position;
                if(a.x!=b.x||a.y!=b.y||a.z!=b.z)
                    throw std::logic_error("Protected original surface moved without intended displacement");
            }
        }
}
}

SourceSplitLineage advanceCertifiedRefineLineage(
    const SourceSplitLineage& prior,const VersionedRefineResult& refined,
    std::size_t maxFaceEvidence,std::size_t maxLivingDescendants) {
    if(refined.flipSources.empty())
        return advanceCertifiedSplitLineage(
            prior,refined,maxFaceEvidence,maxLivingDescendants);
    if(!refined.statistics.flipPairs||!refined.index||
       !refined.sourceIndex||!refined.stampOriginal||
       refined.flipSources.size()>maxFaceEvidence||
       refined.splitSources.size()>maxFaceEvidence-refined.flipSources.size())
        throw std::length_error("Invalid or exhausted source-flip proof budget");
    // Replay M24 split evidence only through its real last split root.
    auto split=refined;
    split.flipSources.clear();
    split.statistics.flipPairs=0;
    split.index=refined.splitSources.empty()?
        refined.sourceIndex:refined.splitSources.back().after;
    split.changedFaces.clear();
    for(const auto& record:split.splitSources) {
        split.changedFaces.push_back(record.sourceFace);
        split.changedFaces.push_back(record.appendedFace);
    }
    std::sort(split.changedFaces.begin(),split.changedFaces.end());
    split.changedFaces.erase(
        std::unique(split.changedFaces.begin(),split.changedFaces.end()),
        split.changedFaces.end());
    auto advanced=advanceCertifiedSplitLineage(
        prior,split,maxFaceEvidence,maxLivingDescendants);
    auto current=advanced.terminal;
    auto owns=std::map<FaceId,FaceId>{};
    for(const auto& [origin,faces]:advanced.living)
        for(const auto id:faces)
            if(!owns.emplace(id,origin).second)
                throw std::logic_error("Ambiguous protected source face owner");
    std::set<FaceId> changed(split.changedFaces.begin(),
                             split.changedFaces.end());
    std::size_t transactions=0;
    for(std::size_t i=0;i<refined.flipSources.size();) {
        const auto& first=refined.flipSources[i];
        if(first.before!=current||!first.after||first.after==current)
            throw std::logic_error("Source flip has stale immutable roots");
        const auto after=first.after;
        const std::size_t start=i;
        while(i<refined.flipSources.size()&&
              refined.flipSources[i].before==current&&
              refined.flipSources[i].after==after)
            ++i;
        if(i-start>2||(!refined.sourceSplitsSymmetric&&i-start!=1))
            throw std::logic_error("Incomplete reflected flip transaction");
        std::set<FaceId> pairedIds;
        std::set<VertexId> pairedVertices;
        for(std::size_t j=start;j<i;++j) {
            const auto& proof=refined.flipSources[j];
            if(proof.faces[0]==proof.faces[1]||
               !pairedIds.insert(proof.faces[0]).second||
               !pairedIds.insert(proof.faces[1]).second)
                throw std::logic_error("Duplicate or overlapping source flip faces");
            const auto priorA=current->maybeFace(proof.faces[0]);
            const auto priorB=current->maybeFace(proof.faces[1]);
            const auto nextA=after->maybeFace(proof.faces[0]);
            const auto nextB=after->maybeFace(proof.faces[1]);
            if(!priorA||!priorB||!nextA||!nextB||
               priorA->vertices!=proof.oldTriangles[0]||
               priorB->vertices!=proof.oldTriangles[1]||
               nextA->vertices!=proof.newTriangles[0]||
               nextB->vertices!=proof.newTriangles[1])
                throw std::logic_error("Flip source before/after journal was forged");
            const auto oldVertices=quadVertices(proof.oldTriangles);
            if(oldVertices!=quadVertices(proof.newTriangles)||
               orientedBoundary(proof.oldTriangles)!=
                   orientedBoundary(proof.newTriangles)||
               proof.oldTriangles==proof.newTriangles)
                throw std::logic_error("Flip changes source quad boundary or no diagonal");
            for(const auto id:oldVertices) {
                pairedVertices.insert(id);
                const auto beforePos=current->vertex(id).position;
                const auto afterPos=after->vertex(id).position;
                if(beforePos.x!=afterPos.x||
                   beforePos.y!=afterPos.y||
                   beforePos.z!=afterPos.z)
                    throw std::logic_error("Flip moved a permanent source vertex");
            }
            const auto a=owns.find(proof.faces[0]);
            const auto b=owns.find(proof.faces[1]);
            if((a==owns.end())!=(b==owns.end())||
               (a!=owns.end()&&a->second!=b->second))
                throw std::logic_error("Ambiguous protected source area transfer across flip");
            if(a!=owns.end()) {
                const double oldArea=triangleArea(*current,proof.oldTriangles[0])+
                                     triangleArea(*current,proof.oldTriangles[1]);
                const double newArea=triangleArea(*after,proof.newTriangles[0])+
                                     triangleArea(*after,proof.newTriangles[1]);
                if(!(oldArea>0)||!std::isfinite(oldArea)||
                   !std::isfinite(newArea)||
                   std::abs(newArea-oldArea)>1e-8*oldArea)
                    throw std::logic_error("Protected original patch area changed in flip");
            }
            changed.insert(proof.faces.begin(),proof.faces.end());
            if(refined.sourceSplitsSymmetric) {
                if(proof.mirrors.empty())
                    throw std::logic_error("Protected flip lacks explicit mirror source");
                std::map<VertexId,VertexId> mirror;
                for(const auto& [u,v]:proof.mirrors)
                    if(!mirror.emplace(u,v).second)
                        throw std::logic_error("Duplicate mirror source vertex");
                for(const auto& [u,v]:mirror) {
                    if(!mirror.contains(v)||mirror.at(v)!=u||
                       !current->maybeVertex(u)||!current->maybeVertex(v))
                        throw std::logic_error("Flip mirror source is not reciprocal");
                    const auto p=current->vertex(u).position;
                    const auto q=current->vertex(v).position;
                    if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
                       ((u==v)!=(p.x==0)))
                        throw std::logic_error("Flip mirror XYZ disagrees with stable IDs");
                }
                for(const auto u:oldVertices)
                    if(!mirror.contains(u))
                        throw std::logic_error("Flip source mirror map omitted corner");
            }else if(!proof.mirrors.empty())
                throw std::logic_error("Asymmetric flip invented mirror correspondence");
        }
        if(refined.sourceSplitsSymmetric) {
            std::map<VertexId,VertexId> mirror;
            for(const auto& [a,b]:refined.flipSources[start].mirrors)
                mirror.emplace(a,b);
            std::vector<std::array<VertexId,3>> oldFaces,newFaces;
            for(std::size_t j=start;j<i;++j) {
                const auto& proof=refined.flipSources[j];
                if(proof.mirrors!=refined.flipSources[start].mirrors)
                    throw std::logic_error("Reflected flip halves disagree on source mirror map");
                oldFaces.insert(oldFaces.end(),proof.oldTriangles.begin(),
                                proof.oldTriangles.end());
                newFaces.insert(newFaces.end(),proof.newTriangles.begin(),
                                proof.newTriangles.end());
            }
            const auto reflectedFan=[&](
                const std::vector<std::array<VertexId,3>>& triangles) {
                for(const auto& tri:triangles) {
                    const std::array<VertexId,3> partner{
                        mirror.at(tri[0]),mirror.at(tri[2]),mirror.at(tri[1])};
                    bool found=false;
                    for(const auto& candidate:triangles)
                        for(unsigned shift=0;shift<3;++shift)
                            if(candidate[shift]==partner[0]&&
                               candidate[(shift+1)%3]==partner[1]&&
                               candidate[(shift+2)%3]==partner[2])
                                found=true;
                    if(!found)return false;
                }
                return true;
            };
            if(!reflectedFan(oldFaces)||!reflectedFan(newFaces))
                throw std::logic_error("Reflected source flip lacks its exact opposite-winding face fan");
        }
        ++transactions;
        current=after;
    }
    if(current!=refined.index||
       transactions!=refined.statistics.flipPairs)
        throw std::logic_error("Incomplete final source flip root chain");
    std::set<FaceId> reported(
        refined.changedFaces.begin(),refined.changedFaces.end());
    if(reported!=changed||
       reported.size()!=refined.changedFaces.size())
        throw std::logic_error("Source flip face journal does not cover all changes");
    // No foreign patch may have stolen the same permanent FaceId.
    if(protectedIds(advanced).size()>maxLivingDescendants)
        throw std::length_error("Flip protected face budget exhausted");
    // Every protected living FaceId must have the exact post-split
    // incidence unless a separately audited flip explicitly names it.
    // A private extra rewrite cannot hide behind another valid flip.
    std::set<FaceId> provedFlipFaces;
    for(const auto& proof:refined.flipSources)
        provedFlipFaces.insert(proof.faces.begin(),proof.faces.end());
    for(const auto& [_,faces]:advanced.living)
        for(const auto id:faces) {
            const auto beforeFace=advanced.terminal->maybeFace(id);
            const auto afterFace=current->maybeFace(id);
            if(!beforeFace||!afterFace||
               (beforeFace->vertices!=afterFace->vertices&&
                !provedFlipFaces.contains(id)))
                throw std::logic_error("Unjournaled protected descendant changed across flips");
        }
    advanced.terminal=current;
    return advanced;
}

SourceSplitLineage advanceCertifiedCollapseLineage(
    const SourceSplitLineage& prior,const VersionedCollapseResult& collapsed,
    std::size_t maxFaceEvidence,std::size_t maxLivingDescendants) {
    if(!prior.terminal||!prior.original||!collapsed.stampOriginal||
       !collapsed.sourceIndex||!collapsed.index||
       prior.terminal!=collapsed.stampOriginal)
        throw std::invalid_argument("Stale or missing source-collapse original root");
    if(!maxFaceEvidence||!maxLivingDescendants)
        throw std::invalid_argument("Collapse proof budgets must be positive");
    if(protectedIds(prior).size()>maxLivingDescendants)
        throw std::length_error("Protected face count exceeds collapse audit cap");
    exactProtectedFaces(prior,*collapsed.sourceIndex);
    if(collapsed.collapseSources.empty()) {
        if(collapsed.statistics.accepted||
           !collapsed.removedVertices.empty()||
           !collapsed.removedFaces.empty()||
           !collapsed.rewrittenFaces.empty()||
           !collapsed.removedMirrorPairs.empty()||
           collapsed.sourceIndex!=collapsed.index)
            throw std::logic_error("Unjournaled collapse or topology rewrite");
        auto unchanged=prior;
        unchanged.terminal=collapsed.index;
        return unchanged;
    }
    if(collapsed.collapseSources.size()>maxFaceEvidence)
        throw std::length_error("Collapse transaction work cap exceeded");
    std::size_t sourceWork=0;
    std::set<VertexId> retiredVertices;
    std::set<FaceId> retiredFaces,rewrittenFaces;
    std::set<std::pair<VertexId,VertexId>> lostMirrors;
    auto current=collapsed.sourceIndex;
    const auto protectedSet=protectedIds(prior);
    for(const auto& event:collapsed.collapseSources) {
        if(event.before!=current||!event.after||event.after==current||
           !event.survivor||!event.retired||
           event.survivor==event.retired||
           !current->maybeVertex(event.survivor)||
           !current->maybeVertex(event.retired)||
           event.after->maybeVertex(event.retired))
            throw std::logic_error("Forged or stale committed collapse root/edge");
        const auto sourceEdge=std::minmax(event.survivor,event.retired);
        const auto sides=current->maybeEdgeSides(
            {sourceEdge.first,sourceEdge.second});
        if(!sides||sides->size()!=2)
            throw std::logic_error("Retired collapse source edge is not a closed two-face link");
        const auto beforeSurvivor=current->vertex(event.survivor).position;
        const auto beforeRetired=current->vertex(event.retired).position;
        const auto midpoint=(beforeSurvivor+beforeRetired)*.5;
        const bool retainedSeam=event.symmetric&&event.paired&&
            event.mirrorSurvivor==event.survivor&&
            event.mirrorRetired!=event.retired;
        const auto expected=retainedSeam?beforeSurvivor:midpoint;
        const auto actual=event.after->vertex(event.survivor).position;
        if(actual.x!=expected.x||actual.y!=expected.y||
           actual.z!=expected.z)
            throw std::logic_error("Collapsed survivor moved to an unproven source position");
        if(event.paired&&event.mirrorSurvivor!=event.survivor) {
            const auto m=current->vertex(event.mirrorSurvivor).position;
            const auto n=current->vertex(event.mirrorRetired).position;
            const auto pairPosition=(m+n)*.5;
            const auto found=event.after->vertex(event.mirrorSurvivor).position;
            if(found.x!=pairPosition.x||found.y!=pairPosition.y||
               found.z!=pairPosition.z)
                throw std::logic_error("Mirrored collapse survivor is not the certified midpoint");
        }
        std::set<FaceId> touched;
        for(const auto id:{event.survivor,event.retired})
            for(const auto face:current->vertex(id).incidentFaces)
                touched.insert(face);
        if(event.paired) {
            if(!event.symmetric||
               event.mirrorSurvivor==event.survivor||
               event.mirrorRetired==event.retired||
               !current->maybeVertex(event.mirrorSurvivor)||
               !current->maybeVertex(event.mirrorRetired)||
               event.after->maybeVertex(event.mirrorRetired))
                throw std::logic_error("Broken paired reflected collapse IDs");
            for(const auto id:{event.mirrorSurvivor,event.mirrorRetired})
                for(const auto face:current->vertex(id).incidentFaces)
                    touched.insert(face);
        }else if(event.symmetric&&
                 (event.mirrorSurvivor!=event.survivor||
                  event.mirrorRetired!=event.retired))
            throw std::logic_error("Unproven self-reflected collapse pair");
        const std::size_t faceWork=touched.size();
        if(faceWork>maxFaceEvidence-sourceWork)
            throw std::length_error("Collapse source-face work budget exhausted");
        sourceWork+=faceWork;
        std::set<FaceId> expectedRetired,expectedRewritten;
        for(const auto face:touched) {
            if(protectedSet.contains(face))
                throw std::logic_error("Collapse touches protected exposed/cavity surface without intent");
            const auto parent=current->face(face).vertices;
            auto next=parent;
            for(auto& v:next) {
                if(v==event.retired)v=event.survivor;
                else if(event.paired&&v==event.mirrorRetired)
                    v=event.mirrorSurvivor;
            }
            const bool deleted=next[0]==next[1]||
                next[1]==next[2]||next[0]==next[2];
            const auto live=event.after->maybeFace(face);
            if(deleted) {
                if(live||!event.after->retiredFace(face))
                    throw std::logic_error("Collapse missing certified retired source face");
                expectedRetired.insert(face);
            }else if(!live||live->vertices!=next)
                throw std::logic_error("Collapse changed source incidence without proof");
            else if(next!=parent)
                expectedRewritten.insert(face);
        }
        const std::set<FaceId> documentedRetired(
            event.retiredFaces.begin(),event.retiredFaces.end());
        const std::set<FaceId> documentedRewritten(
            event.rewrittenFaces.begin(),event.rewrittenFaces.end());
        if(documentedRetired!=expectedRetired||
           documentedRewritten!=expectedRewritten||
           documentedRetired.size()!=event.retiredFaces.size()||
           documentedRewritten.size()!=event.rewrittenFaces.size())
            throw std::logic_error("Collapse lost a source FaceId rewrite/retirement");
        retiredFaces.insert(expectedRetired.begin(),expectedRetired.end());
        rewrittenFaces.insert(expectedRewritten.begin(),expectedRewritten.end());
        if(!retiredVertices.insert(event.retired).second||
           !event.after->retiredVertex(event.retired))
            throw std::logic_error("Missing or duplicate collapsed source vertex tombstone");
        if(event.paired) {
            if(!retiredVertices.insert(event.mirrorRetired).second||
               !event.after->retiredVertex(event.mirrorRetired))
                throw std::logic_error("Missing reflected collapsed vertex tombstone");
        }
        if(event.symmetric) {
            const auto checkReflection=[&](const RegionTopologyIndex& index,
                                           VertexId a,VertexId b) {
                const auto p=index.vertex(a).position;
                const auto q=index.vertex(b).position;
                return p.x==-q.x&&p.y==q.y&&p.z==q.z&&
                       ((a==b)==(p.x==0));
            };
            if(!checkReflection(*current,event.survivor,event.mirrorSurvivor)||
               !checkReflection(*current,event.retired,event.mirrorRetired)||
               !checkReflection(*event.after,event.survivor,event.mirrorSurvivor))
                throw std::logic_error("Collapse mirror source lost explicit reflection");
            lostMirrors.emplace(
                std::min(event.retired,event.mirrorRetired),
                std::max(event.retired,event.mirrorRetired));
        }
        current=event.after;
    }
    const std::set<VertexId> removed(
        collapsed.removedVertices.begin(),collapsed.removedVertices.end());
    const std::set<FaceId> removedFaces(
        collapsed.removedFaces.begin(),collapsed.removedFaces.end());
    const std::set<FaceId> changedFaces(
        collapsed.rewrittenFaces.begin(),collapsed.rewrittenFaces.end());
    std::set<std::pair<VertexId,VertexId>> mirrored;
    for(const auto& [a,b]:collapsed.removedMirrorPairs)
        mirrored.emplace(std::min(a,b),std::max(a,b));
    if(current!=collapsed.index||
       collapsed.statistics.accepted!=collapsed.collapseSources.size()||
       retiredVertices!=removed||retiredFaces!=removedFaces||
       rewrittenFaces!=changedFaces||lostMirrors!=mirrored||
       retiredVertices.size()!=collapsed.removedVertices.size()||
       retiredFaces.size()!=collapsed.removedFaces.size()||
       rewrittenFaces.size()!=collapsed.rewrittenFaces.size()||
       mirrored.size()!=collapsed.removedMirrorPairs.size())
        throw std::logic_error("Collapse final source journals are incomplete or forged");
    exactProtectedFaces(prior,*current);
    auto result=prior;
    result.terminal=current;
    return result;
}


SourceExactCutLineage advanceCertifiedExactCutLineage(
    const SourceSplitLineage& prior,const ExactCutStep& step,
    const std::unordered_map<VertexId,VertexId>* originalMirrors,
    std::size_t maxFaceEvidence,std::size_t maxLivingDescendants) {
    if(!prior.original||!prior.terminal||prior.living.empty()||
       !step.complete||!step.expected||!step.final||
       !step.sourceEvidence||step.expected!=prior.terminal||
       !step.commit.revision||
       step.commit.revision.get()!=&step.final->revision())
        throw std::invalid_argument("Missing, stale or incomplete original-root exact-source Step");
    const auto& evidence=*step.sourceEvidence;
    const auto& cut=evidence.prepared;
    if(cut.originalIndex!=step.expected||!cut.index||
       !cut.commit.revision||cut.commit.revision.get()!=&cut.index->revision()||
       cut.childSources.empty()||cut.sourceContacts.empty())
        throw std::logic_error("Private cut source does not match certified original Step");
    const bool mirrored=!cut.mirroredFaces.empty()||
                        !cut.mirroredVertices.empty()||
                        !cut.mirroredSeamVertices.empty();
    if(mirrored&&!originalMirrors)
        throw std::invalid_argument("Mirrored exact-source audit requires explicit original involution");

    // Existing c1/c2/c3 and M03 work is governed by the ORIGINAL finite
    // limits recorded by the complete Step. M26 also has a hard cap on
    // the FaceId evidence/lookups it itself consumes; never truncate.
    std::size_t livingCount=0;
    std::map<FaceId,FaceId> originalOwnership;
    for(const auto& [first,faces]:prior.living) {
        if(!prior.original->maybeFace(first)||faces.empty())
            throw std::logic_error("Missing first-original protected face ownership");
        for(const auto id:faces) {
            if(!prior.terminal->maybeFace(id)||
               !originalOwnership.emplace(id,first).second)
                throw std::logic_error("Duplicate or missing protected living source FaceId");
        }
        if(faces.size()>maxLivingDescendants-livingCount)
            throw std::length_error("Pre-cut living source descendants exceed cap");
        livingCount+=faces.size();
    }
    if(cut.childSources.size()>maxFaceEvidence)
        throw std::length_error("Exact cut source-child evidence work exhausted");
    const auto c3=planExactCutRemoval(cut,evidence.removalSettings);
    if(!c3.ready()||c3.originalIndex!=step.expected||
       c3.candidateIndex!=cut.index)
        throw std::logic_error("Independent c1/c2/c3 source classification refused");
    const auto m03=planUncutFaceRetirement(
        cut,evidence.removalSettings,evidence.uncutLimits,originalMirrors);
    if(!m03.ready())
        throw std::logic_error("Independent M03 source retirement classification refused");
    const auto sameIds=[](const std::vector<FaceId>& a,
                          const std::vector<FaceId>& b) {
        return a==b&&std::adjacent_find(a.begin(),a.end())==a.end();
    };
    if(!sameIds(evidence.removedChildren,c3.proposedRemoval)||
       !sameIds(evidence.protectedChildren,c3.protectedChildren)||
       !sameIds(evidence.removedUncut,m03.authorizedUncut)||
       !sameIds(evidence.protectedUncut,m03.protectedUncut)||
       evidence.classifiedChildren.size()!=c3.classifiedChildren.size()||
       step.checkedProtectedFaces!=
           c3.protectedChildren.size()+m03.protectedUncut.size())
        throw std::logic_error("Exact-source evidence disagrees with independent c3/M03 decision");
    std::map<FaceId,RegionTopologyIndex::FragmentKind> kind;
    for(std::size_t i=0;i<c3.classifiedChildren.size();++i) {
        const auto& actual=c3.classifiedChildren[i];
        const auto& recorded=evidence.classifiedChildren[i];
        if(actual.child!=recorded.child||
           actual.original!=recorded.original||
           actual.kind!=recorded.kind||
           actual.negativeWinding!=recorded.negativeWinding||
           actual.positiveWinding!=recorded.positiveWinding||
           !kind.emplace(actual.child,actual.kind).second)
            throw std::logic_error("Forged or incomplete certified child occupancy");
    }
    const std::size_t remaining=maxFaceEvidence-cut.childSources.size();
    if(m03.authorizedUncut.size()>remaining||
       m03.protectedUncut.size()>
           remaining-m03.authorizedUncut.size()||
       originalOwnership.size()>
           remaining-m03.authorizedUncut.size()-m03.protectedUncut.size())
        throw std::length_error("Exact-source retained/uncut evidence cap exhausted");
    SourceExactCutLineage result;
    result.survivors=prior;
    result.survivors.terminal=step.final;
    result.classifiedChildren=kind.size();
    result.protectedChildren=c3.protectedChildren.size();
    result.retiredChildren=c3.proposedRemoval.size();
    result.certifiedUncutRetirements=m03.authorizedUncut.size();
    result.checkedFaceEvidence=
        cut.childSources.size()+m03.authorizedUncut.size()+
        m03.protectedUncut.size()+originalOwnership.size();
    const std::set<FaceId> removed(c3.proposedRemoval.begin(),
                                   c3.proposedRemoval.end());
    const std::set<FaceId> protectedCut(c3.protectedChildren.begin(),
                                        c3.protectedChildren.end());
    std::map<FaceId,FaceId> source;
    for(const auto& [child,parent]:cut.childSources) {
        if(!child||!parent||!cut.index->maybeFace(child)||
           !step.expected->maybeFace(parent)||
           !source.emplace(child,parent).second||
           !kind.contains(child))
            throw std::logic_error("Foreign, missing or duplicate exact source child");
        const bool live=step.final->maybeFace(child)!=nullptr;
        if(removed.contains(child)) {
            const auto k=kind.at(child);
            if(live||!step.final->retiredFace(child)||
               (k!=RegionTopologyIndex::FragmentKind::Buried&&
                k!=RegionTopologyIndex::FragmentKind::Outside))
                throw std::logic_error("Uncertified source child retirement");
        }else if(protectedCut.contains(child)) {
            const auto k=kind.at(child);
            if(!live||
               (k!=RegionTopologyIndex::FragmentKind::Exposed&&
                k!=RegionTopologyIndex::FragmentKind::ReversedBoundary))
                throw std::logic_error("Protected exposed/cavity source child disappeared");
            const auto before=cut.index->face(child).vertices;
            const auto after=step.final->face(child).vertices;
            // D2 may replace a proven seam vertex ID with its certified
            // counterpart, but must not displace the protected patch.
            for(unsigned corner=0;corner<3;++corner) {
                const auto a=cut.index->vertex(before[corner]).position;
                const auto b=step.final->vertex(after[corner]).position;
                if(a.x!=b.x||a.y!=b.y||a.z!=b.z)
                    throw std::logic_error("Exact d2 join displaced protected source face");
            }
        }else
            throw std::logic_error("Unclassified original source cut child");
    }
    if(kind.size()!=source.size())
        throw std::logic_error("Incomplete exact original child source graph");
    const std::set<FaceId> goneUncut(m03.authorizedUncut.begin(),
                                     m03.authorizedUncut.end());
    const std::set<FaceId> safeUncut(m03.protectedUncut.begin(),
                                     m03.protectedUncut.end());
    for(const auto id:goneUncut) {
        if(source.contains(id)||!step.expected->maybeFace(id)||
           !cut.index->maybeFace(id)||
           !step.final->retiredFace(id)||step.final->maybeFace(id))
            throw std::logic_error("M03 source retirement was not an uncut original");
    }
    for(const auto id:safeUncut) {
        if(source.contains(id)||!step.expected->maybeFace(id)||
           !step.final->maybeFace(id)||
           step.expected->face(id).vertices!=step.final->face(id).vertices)
            throw std::logic_error("Protected original outer/cavity uncut source was rewritten");
        for(const auto vertex:step.expected->face(id).vertices) {
            const auto a=step.expected->vertex(vertex).position;
            const auto b=step.final->vertex(vertex).position;
            if(a.x!=b.x||a.y!=b.y||a.z!=b.z)
                throw std::logic_error("Protected original uncut source XYZ moved");
        }
    }
    std::set<FaceId> allRetirements=removed;
    allRetirements.insert(goneUncut.begin(),goneUncut.end());
    if(std::vector<FaceId>(allRetirements.begin(),allRetirements.end())!=
       step.retiredFaces)
        throw std::logic_error("Terminal net retired source faces disagree with c3/M03");
    for(const auto child:step.addedFaces)
        if(!source.contains(child)||step.expected->maybeFace(child)||
           !step.final->maybeFace(child))
            throw std::logic_error("Final added FaceId lacks certified cut source");

    // Every first-original protected FaceId routes ONLY via the exact
    // previous living source ID. Source-not-in-cut must remain unchanged,
    // unless M03 independently authorized its complete retirement.
    std::set<FaceId> uniqueFinal;
    std::size_t nextLiving=0;
    for(auto& [original,living]:result.survivors.living) {
        std::vector<FaceId> next;
        for(const auto previous:living) {
            bool foundCut=false;
            for(const auto& [child,parent]:source) {
                if(parent!=previous)continue;
                foundCut=true;
                if(step.final->maybeFace(child))next.push_back(child);
            }
            if(foundCut)continue;
            if(goneUncut.contains(previous))continue;
            const auto previousFace=step.expected->maybeFace(previous);
            const auto finalFace=step.final->maybeFace(previous);
            if(!previousFace||!finalFace||
               previousFace->vertices!=finalFace->vertices)
                throw std::logic_error("Uncut protected first-original source disappeared");
            for(const auto vertex:previousFace->vertices) {
                const auto a=step.expected->vertex(vertex).position;
                const auto b=step.final->vertex(vertex).position;
                if(a.x!=b.x||a.y!=b.y||a.z!=b.z)
                    throw std::logic_error("Uncut protected original source was displaced");
            }
            next.push_back(previous);
        }
        if(next.size()>maxLivingDescendants-nextLiving)
            throw std::length_error("Exact source terminal descendant budget exhausted");
        nextLiving+=next.size();
        std::sort(next.begin(),next.end());
        if(std::adjacent_find(next.begin(),next.end())!=next.end())
            throw std::logic_error("Duplicated terminal source descendant");
        for(const auto id:next)
            if(!step.final->maybeFace(id)||!uniqueFinal.insert(id).second)
                throw std::logic_error("Coincident independent source FaceIds were aliased");
        if(next.empty())result.certifiedRetiredOrigins.push_back(original);
        living=std::move(next);
    }
    // Exact mirrored cuts are certified by c3 and d2 using SOURCE face
    // and vertex orbits. Independently assert every survivor is a
    // complete exact reflected pair or genuine fixed singleton.
    if(mirrored) {
        std::set<std::pair<FaceId,FaceId>> valid;
        for(const auto& [a,b]:cut.mirroredFaces) {
            const bool aLive=step.final->maybeFace(a)!=nullptr;
            const bool bLive=step.final->maybeFace(b)!=nullptr;
            if(aLive!=bLive)
                throw std::logic_error("Only half of source reflected face survives d2");
            if(!aLive)continue;
            valid.emplace(std::min(a,b),std::max(a,b));
            const auto ta=step.final->face(a).vertices;
            const auto tb=step.final->face(b).vertices;
            bool match=false;
            for(unsigned shift=0;shift<3;++shift) {
                bool reflected=true;
                for(unsigned j=0;j<3;++j) {
                    const auto p=step.final->vertex(ta[j]).position;
                    const auto q=step.final->vertex(tb[(shift+3-j)%3]).position;
                    if(p.x!=-q.x||p.y!=q.y||p.z!=q.z)
                        reflected=false;
                }
                if(reflected)match=true;
            }
            if(!match)throw std::logic_error("Final mirrored child fan lacks source-exact reflection");
        }
        const std::set<std::pair<FaceId,FaceId>> published(
            step.survivingMirroredFaces.begin(),
            step.survivingMirroredFaces.end());
        if(valid!=published||published.size()!=step.survivingMirroredFaces.size())
            throw std::logic_error("Final d2 mirrored source-child journals disagree");
        for(const auto& [a,b]:step.addedMirrors) {
            const auto p=step.final->vertex(a).position;
            const auto q=step.final->vertex(b).position;
            if(p.x!=-q.x||p.y!=q.y||p.z!=q.z||
               (a==b&&p.x!=0))
                throw std::logic_error("Final d2 new mirror-point source orbit invalid");
        }
    }else if(!step.survivingMirroredFaces.empty()||
             !step.addedMirrors.empty()||!step.removedMirrors.empty())
        throw std::logic_error("Nonmirrored source Step invented mirror orbits");
    for(const auto& [retired,retained]:evidence.finalWeldedPairs) {
        if(!cut.index->maybeVertex(retired)||
           !cut.index->maybeVertex(retained)||
           step.final->maybeVertex(retired)||
           !step.final->maybeVertex(retained))
            throw std::logic_error("D2 exact seam weld lacks source-proven vertex retirement");
        const auto a=cut.index->vertex(retired).position;
        const auto b=cut.index->vertex(retained).position;
        if(a.x!=b.x||a.y!=b.y||a.z!=b.z)
            throw std::logic_error("D2 weld attempted proximity-only coincidence");
    }
    return result;
}

} // namespace mesher

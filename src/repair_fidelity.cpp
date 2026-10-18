#include "mesher/repair_surgery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
        const auto* source=anchors.original->maybeFace(anchor.face);
        const auto* living=candidate.maybeFace(anchor.face);
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

} // namespace mesher

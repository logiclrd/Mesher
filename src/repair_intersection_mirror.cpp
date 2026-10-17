#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=RegionTopologyIndex::Edge;
using Key=std::tuple<bool,VertexId,VertexId>;

Edge canonical(VertexId a,VertexId b) {
    if(b<a)std::swap(a,b);
    return {a,b};
}
Vec3 reflected(Vec3 p) {return {-p.x,p.y,p.z};}
double distanceSquared(Vec3 a,Vec3 b) {
    const auto d=a-b;
    return dot(d,d);
}
std::array<VertexId,3> sorted(std::array<VertexId,3> face) {
    std::sort(face.begin(),face.end());
    return face;
}
bool reversedCycle(const std::array<VertexId,3>& original,
                   const std::array<VertexId,3>& mirror) {
    const std::array<VertexId,3> reversed{original[0],original[2],original[1]};
    for(unsigned shift=0;shift<3;++shift)
        if(mirror[0]==reversed[shift]&&
           mirror[1]==reversed[(shift+1)%3]&&
           mirror[2]==reversed[(shift+2)%3])return true;
    return false;
}
bool areaPreserved(const RegionTopologyIndex& index,
                   const ConformingCutFace& face,
                   const std::vector<ConformingCutPoint>& points) {
    const auto corners=index.face(face.sourceFace).vertices;
    const auto a=index.vertex(corners[0]).position,
               b=index.vertex(corners[1]).position,
               c=index.vertex(corners[2]).position;
    const auto normal=cross(b-a,c-a);
    const double area=length(normal);
    if(!(area>0)||!std::isfinite(area)||face.triangles.empty())return false;
    double total=0;
    for(const auto& tri:face.triangles) {
        if(tri[0]>=points.size()||tri[1]>=points.size()||
           tri[2]>=points.size()||
           tri[0]==tri[1]||tri[1]==tri[2]||tri[0]==tri[2])return false;
        const auto p=points[tri[0]].position,
                   q=points[tri[1]].position,
                   r=points[tri[2]].position;
        const double signedArea=dot(cross(q-p,r-p),normal)/area;
        if(!(signedArea>1e-12*area)||!std::isfinite(signedArea))return false;
        total+=signedArea;
    }
    return std::abs(total-area)<=1e-8*area;
}
} // namespace

// V17e2b2b3a: a PRIVATE read-only pairing pass over a completed planar
// arrangement. The caller discards the entire plan on any nonempty refusal.
// This intentionally refuses newly created X=0 self-orbits: they require
// explicit shared-seam fan normalization in the permanent staging milestone.
std::string pairMirroredMultiSectionPlan(
    const RegionTopologyIndex& index,
    ConformingCutPlan& plan,
    const std::unordered_map<VertexId,VertexId>& mirrors,
    const ConformingCutLimits& limits) {
    if(plan.status!=IntersectionSectionStatus::TransverseSegment)
        return "Cannot mirror an incomplete transverse arrangement";
    if(plan.faces.empty()||plan.segments.empty())
        return "Mirrored arrangement has no complete faces and sections";
    const auto mirrorOf=[&](VertexId id)->std::optional<VertexId> {
        const auto it=mirrors.find(id);
        if(it==mirrors.end())return std::nullopt;
        const auto back=mirrors.find(it->second);
        if(back==mirrors.end()||back->second!=id||
           !index.maybeVertex(it->second))return std::nullopt;
        const auto p=index.vertex(id).position;
        const auto q=index.vertex(it->second).position;
        if(q.x!=-p.x||q.y!=p.y||q.z!=p.z)return std::nullopt;
        if(id==it->second&&p.x!=0)return std::nullopt;
        return it->second;
    };
    std::map<std::array<VertexId,3>,std::size_t> faceByVertices;
    std::map<FaceId,std::size_t> faceById;
    for(std::size_t i=0;i<plan.faces.size();++i) {
        const auto id=plan.faces[i].sourceFace;
        if(!index.maybeFace(id)||!faceById.emplace(id,i).second)
            return "Missing or duplicate changed original face";
        if(!faceByVertices.emplace(sorted(index.face(id).vertices),i).second)
            return "Ambiguous reflected source face provenance";
    }
    std::vector<std::size_t> faceMirror(plan.faces.size(),plan.faces.size());
    for(std::size_t i=0;i<plan.faces.size();++i) {
        auto corners=index.face(plan.faces[i].sourceFace).vertices;
        for(auto& v:corners) {
            const auto other=mirrorOf(v);
            if(!other)return "Missing or noninvolutive source vertex reflection";
            v=*other;
        }
        const auto found=faceByVertices.find(sorted(corners));
        if(found==faceByVertices.end())
            return "Mirrored affected source face or edge neighbor absent";
        const auto j=found->second;
        if(i==j)return "Invariant source face needs separate X=0 seam handling";
        if(!reversedCycle(corners,index.face(plan.faces[j].sourceFace).vertices))
            return "Source face winding is not oppositely mirrored";
        faceMirror[i]=j;
    }
    for(std::size_t i=0;i<faceMirror.size();++i)
        if(faceMirror[faceMirror[i]]!=i)
            return "Face reflection does not form reciprocal orbits";

    std::map<VertexId,std::size_t> existing;
    std::map<Key,std::vector<std::size_t>> newByProvenance;
    const auto provenance=[&](const ConformingCutPoint& p)->Key {
        if(p.boundaryEdge)
            return {true,p.boundaryEdge->a,p.boundaryEdge->b};
        return {false,p.owningFace,0};
    };
    const auto localScale=[&](const ConformingCutPoint& p)->double {
        if(p.boundaryEdge)
            return length(index.vertex(p.boundaryEdge->a).position-
                          index.vertex(p.boundaryEdge->b).position);
        const auto ids=index.face(p.owningFace).vertices;
        const auto a=index.vertex(ids[0]).position,
                   b=index.vertex(ids[1]).position,
                   c=index.vertex(ids[2]).position;
        return std::max({length(a-b),length(b-c),length(c-a)});
    };
    for(std::size_t i=0;i<plan.points.size();++i) {
        const auto& p=plan.points[i];
        if(p.existingVertex) {
            if(!index.maybeVertex(*p.existingVertex)||
               !existing.emplace(*p.existingVertex,i).second||
               distanceSquared(index.vertex(*p.existingVertex).position,p.position)!=0)
                return "Invalid immutable original point in mirrored arrangement";
        }else {
            if(bool(p.boundaryEdge)==bool(p.owningFace))
                return "Cut point needs exactly one edge or face provenance";
            newByProvenance[provenance(p)].push_back(i);
        }
    }
    std::vector<std::size_t> partner(plan.points.size(),plan.points.size());
    std::size_t comparisons=0;
    const auto countTest=[&]() {
        if(comparisons>=limits.maxMirroredPairTests)
            throw std::length_error("Mirrored arrangement pairing budget exhausted");
        ++comparisons;
    };
    for(std::size_t i=0;i<plan.points.size();++i) {
        const auto& p=plan.points[i];
        if(p.existingVertex) {
            const auto originalTwin=mirrorOf(*p.existingVertex);
            if(!originalTwin||!existing.contains(*originalTwin))
                return "Original cut graph vertex has no reflected counterpart";
            partner[i]=existing.at(*originalTwin);
            continue;
        }
        Key counterpart{};
        if(p.boundaryEdge) {
            const auto a=mirrorOf(p.boundaryEdge->a);
            const auto b=mirrorOf(p.boundaryEdge->b);
            if(!a||!b)return "Reflected cut edge has incomplete original orbit";
            const auto e=canonical(*a,*b);
            counterpart={true,e.a,e.b};
        }else {
            const auto own=faceById.find(p.owningFace);
            if(own==faceById.end())
                return "Interior cut source face not in affected mirror graph";
            counterpart={false,plan.faces[faceMirror[own->second]].sourceFace,0};
        }
        const auto found=newByProvenance.find(counterpart);
        if(found==newByProvenance.end())
            return "Reflected arrangement junction or endpoint is missing";
        std::optional<std::size_t> match;
        for(const auto j:found->second) {
            countTest();
            const auto& q=plan.points[j];
            const double scaled=1e-9*std::max(localScale(p),localScale(q));
            const double coordinate=std::max({
                std::abs(p.position.x),std::abs(p.position.y),
                std::abs(p.position.z),std::abs(q.position.x),
                std::abs(q.position.y),std::abs(q.position.z)});
            const double tolerance=std::max(scaled,
                128*std::numeric_limits<double>::epsilon()*coordinate);
            if(distanceSquared(reflected(p.position),q.position)>
               tolerance*tolerance)continue;
            if(match)return "Ambiguous duplicate reflected arrangement junction";
            match=j;
        }
        if(!match)return "Unpaired reflected arrangement junction or endpoint";
        if(*match==i)return "New X=0 self-orbit requires seam-fan staging";
        partner[i]=*match;
    }
    for(std::size_t i=0;i<partner.size();++i)
        if(partner[i]>=partner.size()||partner[partner[i]]!=i)
            return "Arrangement point reflection is not one-to-one";

    // Canonicalize in ONE direction. Explicitly do not weld independent
    // sheets even when their reflected world coordinates coincide.
    for(std::size_t i=0;i<partner.size();++i)
        if(i<partner[i]&&!plan.points[i].existingVertex) {
            plan.points[partner[i]].position=reflected(plan.points[i].position);
            plan.mirroredCutPoints.emplace_back(i,partner[i]);
        }
    for(const auto& point:plan.points) {
        if(point.existingVertex)continue;
        const auto p=point.position;
        if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
            return "Nonfinite reflected arrangement point";
        const auto tolerance=1e-9*localScale(point);
        if(point.boundaryEdge) {
            const auto a=index.vertex(point.boundaryEdge->a).position,
                       b=index.vertex(point.boundaryEdge->b).position;
            const auto delta=b-a;
            const auto d2=dot(delta,delta);
            if(!(d2>0))return "Degenerate mirrored original edge";
            const auto t=dot(p-a,delta)/d2;
            if(!(t>0&&t<1)||length(a+delta*t-p)>tolerance)
                return "Reflected point moved off original cut edge";
        }else {
            const auto corners=index.face(point.owningFace).vertices;
            const auto a=index.vertex(corners[0]).position,
                       b=index.vertex(corners[1]).position,
                       c=index.vertex(corners[2]).position;
            const auto n=cross(b-a,c-a);
            if(!(length(n)>0)||std::abs(dot(p-a,n))/length(n)>tolerance)
                return "Reflected junction moved off original triangle";
        }
    }
    const auto samePair=[](std::array<std::size_t,2> a,
                           std::array<std::size_t,2> b) {
        if(a[0]>a[1])std::swap(a[0],a[1]);
        if(b[0]>b[1])std::swap(b[0],b[1]);
        return a==b;
    };
    std::set<std::size_t> matchedSegments;
    for(std::size_t i=0;i<plan.segments.size();++i) {
        const auto& a=plan.segments[i];
        const auto aFirst=faceById.find(a.firstFace),
                   aSecond=faceById.find(a.secondFace);
        if(aFirst==faceById.end()||aSecond==faceById.end())
            return "Section source face absent from reflected arrangement";
        const auto first=plan.faces[faceMirror[aFirst->second]].sourceFace,
                   second=plan.faces[faceMirror[aSecond->second]].sourceFace;
        std::optional<std::size_t> match;
        for(std::size_t j=0;j<plan.segments.size();++j) {
            countTest();
            const auto& b=plan.segments[j];
            const bool direct=b.firstFace==first&&b.secondFace==second;
            const bool reverse=b.firstFace==second&&b.secondFace==first;
            if(!direct&&!reverse)continue;
            const std::array<std::size_t,2> mappedFirst{
                partner[a.firstSide[0]],partner[a.firstSide[1]]};
            const std::array<std::size_t,2> mappedSecond{
                partner[a.secondSide[0]],partner[a.secondSide[1]]};
            if(!samePair(mappedFirst,direct?b.firstSide:b.secondSide)||
               !samePair(mappedSecond,direct?b.secondSide:b.firstSide))
                return "Reflected contact curve uses different sheet endpoints";
            if(match)return "Ambiguous reflected contact-curve orbit";
            match=j;
        }
        if(!match||!matchedSegments.insert(*match).second)
            return "Missing or duplicate reflected contact-curve orbit";
    }

    // The independent ear/scaffold walks are not required to choose the
    // same diagonal. Use ONE canonical face tessellation per orbit, and
    // reflect each child triangle with opposite winding, ensuring that
    // permanent FaceIds can later be paired deterministically.
    for(std::size_t i=0;i<plan.faces.size();++i) {
        const auto j=faceMirror[i];
        if(i>=j)continue;
        std::vector<std::array<std::size_t,3>> mirrorTriangles;
        mirrorTriangles.reserve(plan.faces[i].triangles.size());
        for(const auto& t:plan.faces[i].triangles)
            mirrorTriangles.push_back({
                partner[t[0]],partner[t[2]],partner[t[1]]});
        plan.faces[j].triangles=std::move(mirrorTriangles);
    }
    for(const auto& face:plan.faces)
        if(!areaPreserved(index,face,plan.points))
            return "Reflected arrangement changes source triangle winding/area";
    return {};
}
} // namespace mesher

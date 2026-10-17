#pragma once
#include "mesher/repair_surgery.hpp"

#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>

namespace mesher::detail {

// This describes the EXACT symmetry plane X=0, whose self-mirrored
// vertices must have a single stable ID. A contour segment crossing
// the plane without such a vertex cannot be joined safely by simply
// mirroring its triangles; it requires a separate conforming cut.
struct PlaneContourReport {
    std::size_t seamVertices{};
    std::size_t crossingEdges{};
    std::size_t onPlaneEdges{};
    std::size_t nearlyOnPlaneVertices{};
};
inline PlaneContourReport inspectPlaneContour(
    const std::vector<Vec3>& ring,double localEdgeLength) {
    if(ring.size()<3||!std::isfinite(localEdgeLength)||
       !(localEdgeLength>0))
        throw std::invalid_argument("Invalid plane contour or local resolution");
    PlaneContourReport result;
    const double near=localEdgeLength*1e-10;
    for(std::size_t i=0;i<ring.size();++i) {
        const auto a=ring[i],b=ring[(i+1)%ring.size()];
        if(!std::isfinite(a.x)||!std::isfinite(a.y)||
           !std::isfinite(a.z))
            throw std::logic_error("Nonfinite vertex on symmetry boundary");
        if(a.x==0)++result.seamVertices;
        else if(std::abs(a.x)<=near)++result.nearlyOnPlaneVertices;
        if(a.x==0&&b.x==0)++result.onPlaneEdges;
        else if((a.x<0&&b.x>0)||(a.x>0&&b.x<0))
            ++result.crossingEdges;
    }
    return result;
}
// Distinguish a transverse intersection of a contour with the mirror
// plane from a tangent seam vertex. Invariant simple disk caps have a
// single nonbranching fixed arc, hence exactly TWO transverse boundary
// contacts; extra contacts or tangencies require topology decomposition.
struct PlaneContactTopology {
    std::size_t transverseVertices{};
    std::size_t tangentVertices{};
    std::size_t crossingEdges{};
    std::size_t onPlaneEdges{};
    std::size_t nearlyOnPlaneVertices{};
    std::size_t totalTransverseContacts() const noexcept {
        return transverseVertices+crossingEdges;
    }
};
inline PlaneContactTopology inspectPlaneContactTopology(
    const std::vector<Vec3>& ring,double localEdgeLength) {
    const auto basic=inspectPlaneContour(ring,localEdgeLength);
    PlaneContactTopology result;
    result.crossingEdges=basic.crossingEdges;
    result.onPlaneEdges=basic.onPlaneEdges;
    result.nearlyOnPlaneVertices=basic.nearlyOnPlaneVertices;
    for(std::size_t i=0;i<ring.size();++i) {
        if(ring[i].x!=0)continue;
        const auto before=ring[(i+ring.size()-1)%ring.size()].x;
        const auto after=ring[(i+1)%ring.size()].x;
        if(before==0||after==0)continue; // on-plane edge already flagged
        if((before<0&&after>0)||(before>0&&after<0))
            ++result.transverseVertices;
        else
            ++result.tangentVertices;
    }
    return result;
}
enum class PlaneEdgeOrbit {SelfReflected,DistinctReflected,Unmapped};
// An off-plane edge crossing X=0 reflects to another edge crossing at
// THE SAME WORLD-SPACE POINT. When it is a distinct edge (rather than
// the same endpoint pair), those edges already geometrically intersect.
// They cannot be independently split/welded as an embedded manifold.
inline PlaneEdgeOrbit classifyPlaneCrossingOrbit(
    VertexId a,VertexId b,const std::map<VertexId,VertexId>& mirrors) {
    const auto u=mirrors.find(a),v=mirrors.find(b);
    if(u==mirrors.end()||v==mirrors.end()||
       mirrors.find(u->second)==mirrors.end()||
       mirrors.find(v->second)==mirrors.end()||
       mirrors.at(u->second)!=a||mirrors.at(v->second)!=b)
        return PlaneEdgeOrbit::Unmapped;
    return u->second==b&&v->second==a?
        PlaneEdgeOrbit::SelfReflected:PlaneEdgeOrbit::DistinctReflected;
}
inline Vec3 crossingPointOnPlane(Vec3 a,Vec3 b) {
    if(!std::isfinite(a.x)||!std::isfinite(a.y)||!std::isfinite(a.z)||
       !std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z)||
       !((a.x<0&&b.x>0)||(a.x>0&&b.x<0)))
        throw std::invalid_argument("Edge must strictly cross the plane");
    const auto t=a.x/(a.x-b.x);
    return {0,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};
}
} // namespace mesher::detail

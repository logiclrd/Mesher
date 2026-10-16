#pragma once
#include "mesher/repair_surgery.hpp"

#include <cmath>
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
} // namespace mesher::detail

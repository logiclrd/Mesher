#include "mesher/repair_rim_quality.hpp"

#include <algorithm>
#include <cmath>

namespace mesher {
namespace {
bool liesOnFixedEdge(Vec3 tip,Vec3 end,Vec3 candidate) {
    const auto direction=end-tip;
    const auto delta=candidate-tip;
    const auto squared=dot(direction,direction);
    if(!(squared>1e-16))return false;
    const auto fraction=dot(delta,direction)/squared;
    if(fraction<=1e-8||fraction>1+1e-8)return false;
    // Exact boundary segment up to 1e-9 of the fixed edge's length.
    return length(delta-direction*fraction)<=1e-9*std::sqrt(squared);
}
double q(Vec3 a,Vec3 b,Vec3 c) {
    const auto lengthSquare=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return lengthSquare>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/
        lengthSquare:0;
}
} // namespace
template<class Pos>
std::optional<ConstrainedRimTipQuality> rimQualityImpl(
    const MixedClosureFace& face,
    const Pos& positions,
    const std::vector<RepairContour>& contours,
    double minimumQuality) {
    if(face.stableId||face.kind!=MixedClosureFaceKind::Cap||
       !(minimumQuality>0))return std::nullopt;
    const auto& v=face.triangle();
    for(const auto& ring:contours) {
        const auto n=ring.vertexIds.size();
        if(n<3)continue;
        for(std::size_t i=0;i<n;++i) {
            const auto tip=ring.vertexIds[i];
            const auto it=std::find(v.begin(),v.end(),tip);
            if(it==v.end())continue;
            const auto left=ring.vertexIds[(i+n-1)%n];
            const auto right=ring.vertexIds[(i+1)%n];
            if(!positions.contains(left)||!positions.contains(tip)||
               !positions.contains(right))continue;
            const auto p=positions.at(tip);
            const auto u=positions.at(left)-p,w=positions.at(right)-p;
            const auto product=length(u)*length(w);
            if(!(product>1e-16))continue;
            const auto dotAngle=std::clamp(dot(u,w)/product,-1.0,1.0);
            const auto angle=std::atan2(length(cross(u,w)),dot(u,w));
            if(!(angle>1e-5&&angle<1.0471975511965976))continue;
            // Max q of any triangle inside a planar wedge with included
            // angle <= theta is attained at equal adjacent side lengths:
            // q_max = sqrt(3) sin(theta)/(2-cos(theta)).
            const auto theoretical=std::sqrt(3.0)*std::sin(angle)/
                (2.0-dotAngle);
            if(!(theoretical+1e-10<minimumQuality))continue;
            const auto other1=v[(static_cast<std::size_t>(it-v.begin())+1)%3];
            const auto other2=v[(static_cast<std::size_t>(it-v.begin())+2)%3];
            if(!positions.contains(other1)||!positions.contains(other2))
                continue;
            const auto a=positions.at(other1),b=positions.at(other2);
            const bool onRays=
                (liesOnFixedEdge(p,positions.at(left),a)&&
                 liesOnFixedEdge(p,positions.at(right),b))||
                (liesOnFixedEdge(p,positions.at(left),b)&&
                 liesOnFixedEdge(p,positions.at(right),a));
            if(!onRays)continue; // folded/off-segment fronts NEVER qualify
            const auto actual=q(p,a,b);
            // Exclude degeneracy and bad edge-length balance that could
            // still be improved without disturbing the original sharp tip.
            if(!std::isfinite(actual)||actual>=minimumQuality||
               actual<1e-4||actual+1e-12<0.65*theoretical)continue;
            return ConstrainedRimTipQuality{tip,theoretical,actual};
        }
    }
    return std::nullopt;
}
std::optional<ConstrainedRimTipQuality> certifyPlanarRimTipQuality(
    const MixedClosureFace& face,
    const std::map<VertexId,Vec3>& positions,
    const std::vector<RepairContour>& contours,double quality) {
    return rimQualityImpl(face,positions,contours,quality);
}
std::optional<ConstrainedRimTipQuality> certifyPlanarRimTipQuality(
    const MixedClosureFace& face,
    const MixedClosurePositions& positions,
    const std::vector<RepairContour>& contours,double quality) {
    return rimQualityImpl(face,positions,contours,quality);
}
} // namespace mesher

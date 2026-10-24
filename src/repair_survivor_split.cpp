#include "mesher/repair_survivor_split.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mesher {
namespace {
double quality(Vec3 a,Vec3 b,Vec3 c) {
    const double sides=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sides>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sides:0;
}
} // namespace
template<class Pos>
MidpointCollision midpointImpl(
    const Pos& positions,
    std::pair<VertexId,VertexId> e) {
    const auto pa=positions.at(e.first),pb=positions.at(e.second);
    MidpointCollision assessment;
    assessment.point=(pa+pb)*0.5;
    const auto p=assessment.point;
    for(const auto& [id,other]:positions)
        if(other.x==p.x&&other.y==p.y&&other.z==p.z) {
            assessment.unique=false;
            assessment.existingVertex=id;
            break;
        }
    return assessment;
}
template<class Pos>
SurvivorSplitQuality survivorImpl(
    const std::vector<MixedClosureFace>& faces,
    const Pos& positions,
    std::pair<VertexId,VertexId> e,
    double minSurvivorQuality,Vec3 middle) {
    if(!std::isfinite(minSurvivorQuality)||minSurvivorQuality<=0)
        throw std::invalid_argument("R06-M11A invalid survivor quality floor");
    if(e.second<e.first)std::swap(e.first,e.second);
    SurvivorSplitQuality result;
    for(const auto& face:faces) {
        if(face.kind!=MixedClosureFaceKind::Survivor)continue;
        for(unsigned k=0;k<3;++k) {
            const auto a=face.triangle()[k],b=face.triangle()[(k+1)%3];
            if(std::min(a,b)!=e.first||std::max(a,b)!=e.second)continue;
            const auto c=face.triangle()[(k+2)%3];
            const auto qa=quality(positions.at(a),middle,positions.at(c));
            const auto qb=quality(middle,positions.at(b),positions.at(c));
            const auto minimum=std::min(qa,qb);
            result.touchesSurvivor=true;
            if(minimum<result.worstChildQuality) {
                result.worstChildQuality=minimum;
                result.originalFace=face.stableId;
            }
            if(!std::isfinite(minimum)||minimum+1e-12<minSurvivorQuality)
                result.safe=false;
            break;
        }
    }
    return result;
}
MidpointCollision assessUniqueConformingMidpoint(
    const std::map<VertexId,Vec3>& positions,std::pair<VertexId,VertexId> e) {
    return midpointImpl(positions,e);
}
MidpointCollision assessUniqueConformingMidpoint(
    const MixedClosurePositions& positions,std::pair<VertexId,VertexId> e) {
    return midpointImpl(positions,e);
}
SurvivorSplitQuality assessSurvivorSplit(
    const std::vector<MixedClosureFace>& faces,
    const std::map<VertexId,Vec3>& positions,
    std::pair<VertexId,VertexId> e,double quality) {
    return survivorImpl(faces,positions,e,quality,
        (positions.at(e.first)+positions.at(e.second))*0.5);
}
SurvivorSplitQuality assessSurvivorSplit(
    const std::vector<MixedClosureFace>& faces,
    const MixedClosurePositions& positions,
    std::pair<VertexId,VertexId> e,double quality) {
    return survivorImpl(faces,positions,e,quality,
        (positions.at(e.first)+positions.at(e.second))*0.5);
}
MidpointCollision assessUniqueConformingSplitPoint(
    const MixedClosurePositions& positions,
    std::pair<VertexId,VertexId> e,Vec3 point) {
    const auto a=positions.at(e.first),b=positions.at(e.second);
    if(!std::isfinite(point.x)||!std::isfinite(point.y)||
       !std::isfinite(point.z)||
       (point.x==a.x&&point.y==a.y&&point.z==a.z)||
       (point.x==b.x&&point.y==b.y&&point.z==b.z))
        throw std::invalid_argument("R06-M17B proposed split point is invalid or an endpoint");
    MidpointCollision result;
    result.point=point;
    for(const auto& [id,other]:positions) {
        if(other.x==point.x&&other.y==point.y&&other.z==point.z) {
            result.unique=false;
            result.existingVertex=id;
            break;
        }
    }
    return result;
}
SurvivorSplitQuality assessSurvivorSplitAt(
    const std::vector<MixedClosureFace>& faces,
    const MixedClosurePositions& positions,
    std::pair<VertexId,VertexId> edge,Vec3 point,
    double minSurvivorQuality) {
    return survivorImpl(faces,positions,edge,minSurvivorQuality,point);
}
} // namespace mesher

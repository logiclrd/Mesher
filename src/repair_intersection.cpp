#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Triangle=std::array<Vec3,3>;
using Bary=std::array<double,3>;
using Face=std::array<unsigned,3>;
constexpr double relativeTolerance=1e-9;
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
double edgeScale(const Triangle& a) {
    return std::max({length(a[1]-a[0]),length(a[2]-a[1]),
                     length(a[0]-a[2])});
}
std::optional<std::pair<Vec3,Vec3>> planeSection(
    const Triangle& triangle,Vec3 origin,Vec3 normal,
    Vec3 direction,double tolerance) {
    std::vector<Vec3> points;
    for(unsigned i=0;i<3;++i) {
        const auto p=triangle[i],q=triangle[(i+1)%3];
        const double from=dot(p-origin,normal);
        const double to=dot(q-origin,normal);
        const bool atFrom=std::abs(from)<=tolerance;
        const bool atTo=std::abs(to)<=tolerance;
        if(atFrom&&atTo)return std::nullopt; // coplanar side / grazing
        if(atFrom)points.push_back(p);
        if((from>tolerance&&to< -tolerance)||
           (from< -tolerance&&to>tolerance)) {
            const auto t=from/(from-to);
            points.push_back(p+(q-p)*t);
        }
    }
    if(points.size()<2)return std::nullopt;
    const auto originPoint=points.front();
    const auto projection=[&](Vec3 p){return dot(p-originPoint,direction);};
    auto lo=points.front(),hi=points.front();
    for(const auto p:points) {
        if(projection(p)<projection(lo))lo=p;
        if(projection(p)>projection(hi))hi=p;
    }
    if(length(hi-lo)<=tolerance)return std::nullopt;
    return std::make_pair(lo,hi);
}
std::optional<Bary> barycentric(Vec3 p,const Triangle& a,
                                double geometricTolerance) {
    const auto e0=a[1]-a[0],e1=a[2]-a[0],v=p-a[0];
    const double d00=dot(e0,e0),d01=dot(e0,e1),
                 d11=dot(e1,e1),d20=dot(v,e0),d21=dot(v,e1);
    const auto denom=d00*d11-d01*d01;
    if(denom<=0||!std::isfinite(denom))return std::nullopt;
    const auto u=(d11*d20-d01*d21)/denom;
    const auto w=(d00*d21-d01*d20)/denom;
    Bary result{1-u-w,u,w};
    for(auto& x:result) {
        if(!std::isfinite(x)||x< -relativeTolerance||
           x>1+relativeTolerance)return std::nullopt;
        if(std::abs(x)<relativeTolerance)x=0;
        if(std::abs(x-1)<relativeTolerance)x=1;
    }
    const auto projected=a[0]*result[0]+a[1]*result[1]+a[2]*result[2];
    if(length(projected-p)>geometricTolerance)return std::nullopt;
    return result;
}
bool inside(Face triangle,Vec3 p,const std::array<Vec3,5>& points,
            Vec3 n,double minimumArea) {
    const auto x=points[triangle[0]],y=points[triangle[1]],
               z=points[triangle[2]];
    const double area=dot(cross(y-x,z-x),n);
    if(area<=minimumArea)return false;
    const double u=dot(cross(y-x,p-x),n)/area;
    const double v=dot(cross(z-y,p-y),n)/area;
    const double w=dot(cross(x-z,p-z),n)/area;
    return u>relativeTolerance&&v>relativeTolerance&&
           w>relativeTolerance;
}
bool buildPatch(IntersectionPatch& patch,
                const Triangle& tri,std::array<Vec3,2> endpoints,
                double tolerance) {
    const auto n=cross(tri[1]-tri[0],tri[2]-tri[0]);
    const auto area=length(n);
    if(area<=tolerance*tolerance)return false;
    for(unsigned i=0;i<3;++i)patch.positions[i]=tri[i];
    patch.positions[3]=endpoints[0];
    patch.positions[4]=endpoints[1];
    std::array<int,2> side{{-1,-1}};
    for(unsigned k=0;k<2;++k) {
        const auto bary=barycentric(endpoints[k],tri,tolerance);
        if(!bary)return false;
        patch.endpointBarycentrics[k]=*bary;
        unsigned zero=0;
        for(unsigned j=0;j<3;++j)if((*bary)[j]==0) {
            ++zero;
            side[k]=static_cast<int>((j+1)%3);
            // An endpoint opposite vertex j sits on the directed edge
            // (j+1) -> (j+2). A corner (two zeroes) is unsupported.
        }
        if(zero>1)return false;
        patch.interior[k]=(zero==0);
    }
    if(side[0]>=0&&side[0]==side[1])
        return false; // both on one original boundary edge: tangency

    // Construct the CCW triangle perimeter, inserting each endpoint at
    // its actual edge arclength location rather than index parity.
    std::vector<unsigned> rim;
    for(unsigned i=0;i<3;++i) {
        rim.push_back(i);
        std::vector<std::pair<double,unsigned>> cuts;
        for(unsigned k=0;k<2;++k)if(side[k]==static_cast<int>(i)) {
            const auto at=tri[i],next=tri[(i+1)%3];
            const auto t=dot(endpoints[k]-at,next-at)/dot(next-at,next-at);
            if(t<=relativeTolerance||t>=1-relativeTolerance)
                return false;
            cuts.emplace_back(t,3+k);
        }
        std::sort(cuts.begin(),cuts.end());
        for(const auto& [t,id]:cuts) {
            (void)t;
            rim.push_back(id);
        }
    }
    const auto fan=[&](unsigned pivot,const std::vector<unsigned>& perimeter) {
        for(std::size_t i=0;i<perimeter.size();++i)
            patch.triangles.push_back({
                pivot,perimeter[i],perimeter[(i+1)%perimeter.size()]});
    };
    if(patch.interior[0]&&patch.interior[1]) {
        fan(3,rim);
        std::optional<std::size_t> holder;
        for(std::size_t i=0;i<patch.triangles.size();++i)
            if(inside(patch.triangles[i],endpoints[1],patch.positions,
                      n,tolerance*tolerance)) {
                if(holder)return false;
                holder=i;
            }
        if(!holder)return false; // crossing radial fan edge: unsupported
        const auto base=patch.triangles[*holder];
        patch.triangles[*holder]={base[0],base[1],4};
        patch.triangles.push_back({base[1],base[2],4});
        patch.triangles.push_back({base[2],base[0],4});
    }else if(patch.interior[0]||patch.interior[1]) {
        fan(patch.interior[0]?3:4,rim);
    }else {
        const auto first=std::find(rim.begin(),rim.end(),3);
        const auto second=std::find(rim.begin(),rim.end(),4);
        if(first==rim.end()||second==rim.end())return false;
        const auto nRim=rim.size();
        const auto polygon=[&](unsigned start,unsigned finish) {
            std::vector<unsigned> path;
            for(unsigned at=start;;at=(at+1)%nRim) {
                path.push_back(rim[at]);
                if(at==finish)break;
            }
            for(std::size_t i=1;i+1<path.size();++i)
                patch.triangles.push_back({path[0],path[i],path[i+1]});
        };
        polygon(static_cast<unsigned>(first-rim.begin()),
                static_cast<unsigned>(second-rim.begin()));
        polygon(static_cast<unsigned>(second-rim.begin()),
                static_cast<unsigned>(first-rim.begin()));
    }
    // Area conservation and positive winding are checked here, while all
    // local coordinates remain private. Individual skinny triangles refuse.
    double aggregate=0;
    bool segmentEdge=false;
    for(const auto& ids:patch.triangles) {
        const auto a=patch.positions[ids[0]],b=patch.positions[ids[1]],
                   c=patch.positions[ids[2]];
        const auto signedArea=dot(cross(b-a,c-a),n)/area;
        if(!(signedArea>tolerance*tolerance)||!std::isfinite(signedArea))
            return false;
        aggregate+=signedArea;
        for(unsigned j=0;j<3;++j) {
            const auto from=ids[j],to=ids[(j+1)%3];
            if((from==3&&to==4)||(from==4&&to==3))segmentEdge=true;
        }
    }
    return segmentEdge&&std::abs(aggregate-area)<=relativeTolerance*area;
}
} // namespace

IntersectionSection splitTransverseTriangleContact(
    const Triangle& a,const std::array<VertexId,3>& aIds,
    const Triangle& b,const std::array<VertexId,3>& bIds) {
    IntersectionSection answer;
    const auto contact=triangleContact(a,aIds,b,bIds);
    if(contact==TriangleContact::Disjoint||
       contact==TriangleContact::ExpectedAdjacency)return answer;
    answer.status=IntersectionSectionStatus::UnsupportedContact;
    if(contact==TriangleContact::Ambiguous)return answer;
    for(const auto p:a)if(!finite(p))return answer;
    for(const auto p:b)if(!finite(p))return answer;
    const auto normalA=cross(a[1]-a[0],a[2]-a[0]);
    const auto normalB=cross(b[1]-b[0],b[2]-b[0]);
    const auto normA=length(normalA),normB=length(normalB);
    const auto direction=cross(normalA,normalB);
    const auto directionLength=length(direction);
    if(!std::isfinite(directionLength)||!std::isfinite(normA)||
       !std::isfinite(normB)||normA==0||normB==0||
       directionLength<=1e-7*normA*normB)return answer;
    const auto line=direction/directionLength;
    const auto scale=std::max(edgeScale(a),edgeScale(b));
    const auto epsilon=std::max(1e-9*scale,
        128*std::numeric_limits<double>::epsilon()*
        std::max({1.0,std::abs(a[0].x),std::abs(a[0].y),
                  std::abs(a[0].z),std::abs(b[0].x),
                  std::abs(b[0].y),std::abs(b[0].z)}));
    const auto crossA=planeSection(a,b[0],normalB/normB,line,epsilon);
    const auto crossB=planeSection(b,a[0],normalA/normA,line,epsilon);
    if(!crossA||!crossB)return answer;
    const auto projection=[&](Vec3 p){return dot(p-a[0],line);};
    const auto order=[&](auto section) {
        if(projection(section.first)>projection(section.second))
            std::swap(section.first,section.second);
        return section;
    };
    const auto u=order(*crossA),v=order(*crossB);
    const auto lo=std::max(projection(u.first),projection(v.first));
    const auto hi=std::min(projection(u.second),projection(v.second));
    if(hi-lo<=epsilon)return answer;
    const auto on=[&](const std::pair<Vec3,Vec3>& segment,double t) {
        const auto begin=projection(segment.first);
        const auto end=projection(segment.second);
        return segment.first+(segment.second-segment.first)*
              ((t-begin)/(end-begin));
    };
    answer.endpoints[0]=(on(u,lo)+on(v,lo))*.5;
    answer.endpoints[1]=(on(u,hi)+on(v,hi))*.5;
    if(length(on(u,lo)-on(v,lo))>epsilon||
       length(on(u,hi)-on(v,hi))>epsilon||
       !buildPatch(answer.patches[0],a,answer.endpoints,epsilon)||
       !buildPatch(answer.patches[1],b,answer.endpoints,epsilon))
        return answer;
    answer.status=IntersectionSectionStatus::TransverseSegment;
    return answer;
}

IntersectionSectionBatch splitTransverseContactBatch(
    const RegionTopologyIndex& index,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    std::size_t maxPairs) {
    if(contacts.size()>maxPairs)
        throw std::length_error("Intersection section batch exceeds pair budget");
    IntersectionSectionBatch batch;
    batch.sections.reserve(contacts.size());
    for(const auto pair:contacts) {
        if(pair.ambiguous) {
            batch.status=IntersectionSectionStatus::UnsupportedContact;
            batch.sections.clear();
            return batch;
        }
        const auto a=index.face(pair.first).vertices;
        const auto b=index.face(pair.second).vertices;
        const auto positions=[&](const std::array<VertexId,3>& ids) {
            return Triangle{{index.vertex(ids[0]).position,
                             index.vertex(ids[1]).position,
                             index.vertex(ids[2]).position}};
        };
        auto section=splitTransverseTriangleContact(
            positions(a),a,positions(b),b);
        if(section.status!=IntersectionSectionStatus::TransverseSegment) {
            batch.status=IntersectionSectionStatus::UnsupportedContact;
            batch.sections.clear();
            return batch;
        }
        batch.sections.push_back(std::move(section));
    }
    batch.status=batch.sections.empty()?
        IntersectionSectionStatus::Disjoint:
        IntersectionSectionStatus::TransverseSegment;
    return batch;
}
} // namespace mesher

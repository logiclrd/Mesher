#include "mesher/triangle_collision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace mesher {
namespace {
struct Point2 {double x{},y{};};
Vec3 difference(Vec3 a,Vec3 b) {return a-b;}
unsigned projectionAxis(Vec3 normal) {
    const Vec3 n{std::abs(normal.x),std::abs(normal.y),std::abs(normal.z)};
    return n.x>=n.y&&n.x>=n.z?0:(n.y>=n.z?1:2);
}
Point2 project(Vec3 p,unsigned axis) {
    if(axis==0)return {p.y,p.z};
    if(axis==1)return {p.x,p.z};
    return {p.x,p.y};
}
double cross2(Point2 a,Point2 b) {return a.x*b.y-a.y*b.x;}
Point2 subtract(Point2 a,Point2 b) {return {a.x-b.x,a.y-b.y};}
double area2(const std::vector<Point2>& polygon) {
    double area=0;
    for(std::size_t i=0;i<polygon.size();++i)
        area+=cross2(polygon[i],polygon[(i+1)%polygon.size()]);
    return area;
}
bool projectedInteriorOverlap(const std::array<Vec3,3>& a,
                              const std::array<Vec3,3>& b,
                              Vec3 normal,double tolerance) {
    const auto axis=projectionAxis(normal);
    std::vector<Point2> polygon;
    std::array<Point2,3> clip;
    for(unsigned i=0;i<3;++i) {
        polygon.push_back(project(a[i],axis));
        clip[i]=project(b[i],axis);
    }
    const std::vector<Point2> winding(clip.begin(),clip.end());
    const double sign=area2(winding)>=0?1.0:-1.0;
    for(unsigned i=0;i<3;++i) {
        if(polygon.empty())break;
        const auto p=clip[i],q=clip[(i+1)%3];
        const auto signedSide=[&](Point2 r) {
            return sign*cross2(subtract(q,p),subtract(r,p));
        };
        std::vector<Point2> next;
        for(std::size_t j=0;j<polygon.size();++j) {
            const auto start=polygon[j],end=polygon[(j+1)%polygon.size()];
            const double ds=signedSide(start),de=signedSide(end);
            const bool inStart=ds>=-tolerance,inEnd=de>=-tolerance;
            if(inStart!=inEnd) {
                const double fraction=ds/(ds-de);
                next.push_back({start.x+(end.x-start.x)*fraction,
                                start.y+(end.y-start.y)*fraction});
            }
            if(inEnd)next.push_back(end);
        }
        polygon=std::move(next);
    }
    // Shared-point/edge-only intersections have zero enclosed area.
    return polygon.size()>=3&&std::abs(area2(polygon))>
           2*tolerance*tolerance;
}
bool separated(const std::array<Vec3,3>& a,
               const std::array<Vec3,3>& b,Vec3 axis,double epsilon) {
    const double magnitude=length(axis);
    if(magnitude<=epsilon*epsilon)return false;
    axis=axis/magnitude;
    double amin=dot(a[0],axis),amax=amin;
    double bmin=dot(b[0],axis),bmax=bmin;
    for(unsigned i=1;i<3;++i) {
        const double av=dot(a[i],axis),bv=dot(b[i],axis);
        amin=std::min(amin,av);amax=std::max(amax,av);
        bmin=std::min(bmin,bv);bmax=std::max(bmax,bv);
    }
    return amax<bmin-epsilon||bmax<amin-epsilon;
}
bool possibleOverlap(const std::array<Vec3,3>& a,
                     const std::array<Vec3,3>& b,
                     Vec3 na,Vec3 nb,double epsilon) {
    if(separated(a,b,na,epsilon)||separated(a,b,nb,epsilon))
        return false;
    for(unsigned i=0;i<3;++i) {
        const auto ea=difference(a[(i+1)%3],a[i]);
        const auto eb=difference(b[(i+1)%3],b[i]);
        if(separated(a,b,cross(na,ea),epsilon)||
           separated(a,b,cross(nb,eb),epsilon))return false;
        for(unsigned j=0;j<3;++j)
            if(separated(a,b,cross(ea,b[(j+1)%3]-b[j]),epsilon))
                return false;
    }
    return true;
}
bool pointInsideTriangle(Vec3 p,const std::array<Vec3,3>& tri,
                         Vec3 normal,double epsilon) {
    for(unsigned i=0;i<3;++i) {
        const auto edge=tri[(i+1)%3]-tri[i];
        if(dot(cross(edge,p-tri[i]),normal) <
           -epsilon*length(edge)*length(normal))return false;
    }
    return true;
}
// A shared vertex by itself is legitimate. For noncoplanar triangles,
// another edge-plane intersection away from it proves extra contact.
bool penetratesBeyondSharedVertex(
    const std::array<Vec3,3>& a,const std::array<Vec3,3>& b,
    Vec3 na,Vec3 nb,Vec3 shared,double epsilon) {
    const auto testEdges=[&](const std::array<Vec3,3>& from,
                             const std::array<Vec3,3>& to,Vec3 normal) {
        const double planeTolerance=epsilon*length(normal);
        for(unsigned i=0;i<3;++i) {
            const auto p=from[i],q=from[(i+1)%3];
            const double dp=dot(p-to[0],normal);
            const double dq=dot(q-to[0],normal);
            if(std::abs(dp)<=planeTolerance&&std::abs(dq)<=planeTolerance) {
                // An edge contained in the opposite plane can overlap the
                // opposite triangle beyond the common vertex.
                for(const double t:{0.25,0.5,0.75}) {
                    const auto at=p+(q-p)*t;
                    if(length(at-shared)>epsilon&&
                       pointInsideTriangle(at,to,normal,epsilon))return true;
                }
                continue;
            }
            if((dp>planeTolerance&&dq>planeTolerance)||
               (dp< -planeTolerance&&dq< -planeTolerance))continue;
            const double divisor=dp-dq;
            if(std::abs(divisor)<=planeTolerance)continue;
            const double t=dp/divisor;
            if(t<0||t>1)continue;
            const auto at=p+(q-p)*t;
            if(length(at-shared)>epsilon&&
               pointInsideTriangle(at,to,normal,epsilon))return true;
        }
        return false;
    };
    return testEdges(a,b,nb)||testEdges(b,a,na);
}
} // namespace

TriangleContact triangleContact(
    const std::array<Vec3,3>& a,const std::array<VertexId,3>& aIds,
    const std::array<Vec3,3>& b,const std::array<VertexId,3>& bIds) {
    double scale=0,coordinates=0;
    for(const auto& tri:{a,b})for(unsigned i=0;i<3;++i) {
        scale=std::max(scale,length(tri[(i+1)%3]-tri[i]));
        coordinates=std::max({coordinates,std::abs(tri[i].x),
                              std::abs(tri[i].y),std::abs(tri[i].z)});
    }
    const double epsilon=std::max(1e-12*scale,
        64*std::numeric_limits<double>::epsilon()*coordinates);
    const auto na=cross(a[1]-a[0],a[2]-a[0]);
    const auto nb=cross(b[1]-b[0],b[2]-b[0]);
    if(scale==0||length(na)<=epsilon*scale||
       length(nb)<=epsilon*scale)return TriangleContact::Ambiguous;
    if(!possibleOverlap(a,b,na,nb,epsilon))
        return TriangleContact::Disjoint;

    std::array<std::pair<unsigned,unsigned>,3> shared{};
    unsigned count=0;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
        if(aIds[i]==bIds[j])shared[count++]={i,j};
    if(count==0)return TriangleContact::Intersecting;
    if(count==3)return TriangleContact::Intersecting;

    const double normals=length(cross(na,nb));
    const bool parallel=normals<=1e-10*length(na)*length(nb);
    const bool coplanar=parallel&&
        std::abs(dot(b[0]-a[0],na))<=epsilon*length(na);
    if(count==2) {
        if(!coplanar)return TriangleContact::ExpectedAdjacency;
        unsigned ai=0,bi=0;
        while(ai<3&&(ai==shared[0].first||ai==shared[1].first))++ai;
        while(bi<3&&(bi==shared[0].second||bi==shared[1].second))++bi;
        const auto start=a[shared[0].first],end=a[shared[1].first];
        const auto ac=cross(end-start,a[ai]-start);
        const auto bc=cross(end-start,b[bi]-start);
        const double side=dot(ac,bc);
        const double magnitude=length(ac)*length(bc);
        if(magnitude<=epsilon*epsilon*scale*scale)
            return TriangleContact::Ambiguous;
        return side>0 ? TriangleContact::Intersecting
                      : TriangleContact::ExpectedAdjacency;
    }
    const Vec3 sharedPoint=a[shared[0].first];
    if(coplanar)
        return projectedInteriorOverlap(a,b,na,epsilon)
            ? TriangleContact::Intersecting
            : TriangleContact::ExpectedAdjacency;
    if(parallel)return TriangleContact::Ambiguous;
    return penetratesBeyondSharedVertex(a,b,na,nb,sharedPoint,epsilon)
        ? TriangleContact::Intersecting
        : TriangleContact::ExpectedAdjacency;
}
} // namespace mesher

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
    if(polygon.size()<3)return 0;
    // Compute area from local differences, not the global-coordinate
    // shoelace sum. Coplanar clipping can produce a triangle whose
    // extent is near machine precision at a shared vertex. The global
    // sum suffers cancellation O(eps * |coordinates|^2) and mistakes
    // that single-point contact for a substantial overlap.
    const auto origin=polygon.front();
    double area=0;
    for(std::size_t i=1;i+1<polygon.size();++i)
        area+=cross2(subtract(polygon[i],origin),
                     subtract(polygon[i+1],origin));
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
            // Polygon clipping must use the exact halfplane. A
            // tolerance-expanded halfplane creates artificial positive-
            // area overlap for two coplanar triangles that touch only
            // at a shared corner (especially after boundary splitting).
            // The area test below retains a scale-aware tolerance.
            const bool inStart=ds>=0,inEnd=de>=0;
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
                // Clip the WHOLE coplanar segment against all three
                // half-planes, rather than sampling three points and
                // potentially missing a very short overlap.
                double low=0,high=1;
                for(unsigned edgeIndex=0;edgeIndex<3;++edgeIndex) {
                    const auto origin=to[edgeIndex];
                    const auto edge=to[(edgeIndex+1)%3]-origin;
                    // Clip coplanar edges to the exact supporting
                    // halfplane. Expanding all three triangle edges by
                    // epsilon fabricates a positive-length intersection
                    // near a lawful shared vertex (even when the only
                    // mathematical contact is that vertex).
                    const double limit=0;
                    const auto start=dot(cross(edge,p-origin),normal);
                    const auto finish=dot(cross(edge,q-origin),normal);
                    if(start<limit&&finish<limit) {high=-1;break;}
                    if(start<limit||finish<limit) {
                        const double t=(limit-start)/(finish-start);
                        if(start<limit)low=std::max(low,t);
                        else high=std::min(high,t);
                    }
                    if(low>high)break;
                }
                if(high>low&&length(q-p)*(high-low)>8*epsilon&&
                   length(p+(q-p)*((low+high)/2)-shared)>8*epsilon)
                    return true;
                continue;
            }
            if((dp>planeTolerance&&dq>planeTolerance)||
               (dp< -planeTolerance&&dq< -planeTolerance))continue;
            // An endpoint already on the plane (within the supported
            // plane-distance uncertainty) MUST be used as that exact
            // endpoint. Solving dp/(dp-dq) instead can land a tiny distance
            // along an otherwise disjoint edge. Near-tangential planes
            // amplify that error into >epsilon 3D motion, turning a valid
            // shared-vertex fan on a convex sphere into false penetration.
            // The endpoint owns an exact shared VertexId, so its position
            // is more reliable than an extrapolated plane intersection.
            Vec3 at{};
            if(std::abs(dp)<=planeTolerance) {
                at=p;
            }else if(std::abs(dq)<=planeTolerance) {
                at=q;
            }else {
                const double divisor=dp-dq;
                if(std::abs(divisor)<=planeTolerance)continue;
                const double t=dp/divisor;
                if(t<0||t>1)continue;
                at=p+(q-p)*t;
            }
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
    double scaleA=0,scaleB=0,coordinates=0;
    for(unsigned i=0;i<3;++i) {
        scaleA=std::max(scaleA,length(a[(i+1)%3]-a[i]));
        scaleB=std::max(scaleB,length(b[(i+1)%3]-b[i]));
        for(const auto p:{a[i],b[i]})
            coordinates=std::max({coordinates,std::abs(p.x),
                                  std::abs(p.y),std::abs(p.z)});
    }
    const double scale=std::max(scaleA,scaleB);
    const double coordinateError=
        64*std::numeric_limits<double>::epsilon()*coordinates;
    const double epsilon=std::max(1e-12*scale,coordinateError);
    const auto na=cross(a[1]-a[0],a[2]-a[0]);
    const auto nb=cross(b[1]-b[0],b[2]-b[0]);
    // Degeneracy is intrinsic to EACH triangle. A well-shaped small face
    // must not become 'degenerate' merely by meeting a very large face.
    const double limitA=std::max(1e-12*scaleA*scaleA,
                                 coordinateError*scaleA);
    const double limitB=std::max(1e-12*scaleB*scaleB,
                                 coordinateError*scaleB);
    if(scale==0||length(na)<=limitA||length(nb)<=limitB)
        return TriangleContact::Ambiguous;
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

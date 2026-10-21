#include "mesher/offmode_clearance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using FacePoints=std::array<Vec3,3>;
double square(Vec3 p) {return dot(p,p);}
double clamp01(double x) {return std::clamp(x,0.0,1.0);}
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
Triangle triangleOf(const RegionTopologyIndex& root,FaceId id) {
    const auto face=root.maybeFace(id);
    if(!face)throw std::logic_error("V18 missing stable clearance face");
    FacePoints result{};
    for(unsigned k=0;k<3;++k)
        result[k]=root.vertex(face->vertices[k]).position;
    return result;
}
double shortestEdge(const FacePoints& t) {
    double shortest=std::numeric_limits<double>::infinity();
    for(unsigned i=0;i<3;++i)
        shortest=std::min(shortest,length(t[(i+1)%3]-t[i]));
    return shortest;
}
double numericTolerance(const FacePoints& a,const FacePoints& b,
                        double localScale) {
    double coordinate=0;
    for(const auto& t:{a,b})for(const auto p:t)
        coordinate=std::max({coordinate,
            std::abs(p.x),std::abs(p.y),std::abs(p.z)});
    return std::max(1e-12*localScale,
        64*std::numeric_limits<double>::epsilon()*coordinate);
}
bool wellFormed(const FacePoints& t) {
    for(const auto p:t)if(!finite(p))return false;
    const double s=shortestEdge(t);
    if(!std::isfinite(s)||s<=0)return false;
    const double area=length(cross(t[1]-t[0],t[2]-t[0]));
    const double maximum=std::max({
        length(t[1]-t[0]),length(t[2]-t[1]),length(t[0]-t[2])});
    return std::isfinite(area)&&std::isfinite(maximum)&&
           area>1e-12*s*maximum;
}
// Ericson's Voronoi-region point/triangle closest point, with no
// barycentric tolerance that could fabricate an accepted clearance.
double pointTriangleSquared(Vec3 p,const FacePoints& t) {
    const auto a=t[0],b=t[1],c=t[2],ab=b-a,ac=c-a,ap=p-a;
    const auto d1=dot(ab,ap),d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return square(ap);
    const Vec3 bp=p-b;
    const double d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0&&d4<=d3)return square(bp);
    const double vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0)
        return square(p-(a+ab*(d1/(d1-d3))));
    const Vec3 cp=p-c;
    const double d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0&&d5<=d6)return square(cp);
    const double vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0)
        return square(p-(a+ac*(d2/(d2-d6))));
    const double va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0) {
        const auto edge=c-b;
        const double parameter=(d4-d3)/((d4-d3)+(d5-d6));
        return square(p-(b+edge*parameter));
    }
    const double denom=va+vb+vc;
    if(!(denom>0))throw std::logic_error("V18 ambiguous projected triangle");
    const double v=vb/denom,w=vc/denom;
    return square(p-(a+ab*v+ac*w));
}
// Closest points on two finite edges. Parallel/degenerate cases are
// handled without dividing by a near-zero determinant.
double segmentSegmentSquared(Vec3 p1,Vec3 q1,Vec3 p2,Vec3 q2) {
    const Vec3 u=q1-p1,v=q2-p2,w=p1-p2;
    const double a=dot(u,u),e=dot(v,v),b=dot(u,v);
    if(!(a>0)||!(e>0))throw std::logic_error("V18 zero-length clearance edge");
    const double c=dot(u,w),f=dot(v,w);
    const double determinant=a*e-b*b;
    double s=0;
    if(determinant>32*std::numeric_limits<double>::epsilon()*a*e)
        s=clamp01((b*f-c*e)/determinant);
    double t=clamp01((b*s+f)/e);
    if(t==0)s=clamp01(-c/a);
    else if(t==1)s=clamp01((b-c)/a);
    return square((p1+u*s)-(p2+v*t));
}
double triangleDistance(const FacePoints& a,const FacePoints& b) {
    if(!wellFormed(a)||!wellFormed(b))
        throw std::logic_error("V18 degenerate clearance triangle");
    double distance=std::numeric_limits<double>::infinity();
    for(unsigned i=0;i<3;++i) {
        distance=std::min(distance,pointTriangleSquared(a[i],b));
        distance=std::min(distance,pointTriangleSquared(b[i],a));
        for(unsigned j=0;j<3;++j)
            distance=std::min(distance,segmentSegmentSquared(
                a[i],a[(i+1)%3],b[j],b[(j+1)%3]));
    }
    if(!std::isfinite(distance)||distance<0)
        throw std::logic_error("V18 nonfinite triangle clearance");
    return std::sqrt(distance);
}
bool sameTriangle(const FacePoints& a,const FacePoints& b) {
    for(unsigned i=0;i<3;++i)
        if(a[i].x!=b[i].x||a[i].y!=b[i].y||a[i].z!=b[i].z)
            return false;
    return true;
}
bool topologyUnchanged(const RegionTopologyIndex& a,
                       const RegionTopologyIndex& b,FaceId id) {
    const auto fa=a.maybeFace(id),fb=b.maybeFace(id);
    return fa&&fb&&fa->vertices==fb->vertices;
}
// Legal slide is rigid relative translation in BOTH unchanged face
// tangent planes, with unchanged or increasing exact separation.
// Changing topology, rotations, nonuniform normal displacement and
// insufficient numeric evidence are never labelled tangential.
bool strictRelativeTangent(
    const FacePoints& a0,const FacePoints& a1,
    const FacePoints& b0,const FacePoints& b1,double tolerance) {
    const Vec3 da=a1[0]-a0[0],db=b1[0]-b0[0];
    for(unsigned i=1;i<3;++i)
        if(length(a1[i]-a0[i]-da)>tolerance||
           length(b1[i]-b0[i]-db)>tolerance)return false;
    const Vec3 motion=da-db;
    if(length(motion)<=tolerance)return false;
    const Vec3 na=normalized(cross(a1[1]-a1[0],a1[2]-a1[0]));
    const Vec3 nb=normalized(cross(b1[1]-b1[0],b1[2]-b1[0]));
    if(std::abs(dot(na,nb))<1.0-1e-10)return false;
    return std::abs(dot(motion,na))<=tolerance&&
           std::abs(dot(motion,nb))<=tolerance;
}
}

OffModeClearanceReport assessOffModeMeshClearance(
    const std::shared_ptr<const RegionTopologyIndex>& before,
    const std::shared_ptr<const RegionTopologyIndex>& candidate,
    std::span<const VertexId> changedVertices,
    std::span<const FaceId> changedFaces,
    const OffModeClearanceLimits& limits) {
    OffModeClearanceReport report;
    const auto uncertain=[&](std::string reason) {
        report.status=OffModeClearanceStatus::Uncertain;
        report.reason=std::move(reason);
        return report;
    };
    if(!std::isfinite(limits.minimumSpacingRatio)||
       limits.minimumSpacingRatio<=0||limits.minimumSpacingRatio>1)
        return uncertain("V18 clearance ratio must be in (0,1]");
    report.contact=assessOffModePostStateContacts(
        before,candidate,changedVertices,changedFaces,limits.contacts);
    if(report.contact.status==OffModeContactStatus::NewContact) {
        report.status=OffModeClearanceStatus::Contact;
        report.reason=report.contact.reason;
        return report;
    }
    if(!report.contact.conservativelyContactFree())
        return uncertain("V18 clearance prerequisite: "+report.contact.reason);
    if(before==candidate) {
        report.status=OffModeClearanceStatus::Clear;
        return report;
    }
    try {
        const std::vector<VertexId> vertices(
            changedVertices.begin(),changedVertices.end());
        const std::vector<FaceId> faces(
            changedFaces.begin(),changedFaces.end());
        const auto pairs=candidate->nearbyTrianglePairs(
            vertices,faces,limits.minimumSpacingRatio,
            &report.neighborhoodWork,limits.neighborhood);
        bool hadSlide=false,hadOld=false;
        for(const auto& [a,b]:pairs) {
            ++report.examinedNearPairs;
            const auto currentA=triangleOf(*candidate,a);
            const auto currentB=triangleOf(*candidate,b);
            if(!wellFormed(currentA)||!wellFormed(currentB))
                throw std::logic_error("V18 degenerate near triangle");
            const double localScale=std::min(
                shortestEdge(currentA),shortestEdge(currentB));
            const double epsilon=numericTolerance(
                currentA,currentB,localScale);
            const double required=limits.minimumSpacingRatio*localScale;
            if(!(required>4*epsilon))
                throw std::logic_error(
                    "V18 local triangle is too small for reliable mesh-scale clearance");
            const auto testDistance=[&](const FacePoints& x,const FacePoints& y) {
                if(report.distanceTests>=limits.maxDistanceTests)
                    throw std::length_error("V18 clearance distance budget exhausted");
                ++report.distanceTests;
                return triangleDistance(x,y);
            };
            const double distance=testDistance(currentA,currentB);
            report.smallestDistance=std::min(report.smallestDistance,distance);
            report.largestRequiredSpacing=std::max(
                report.largestRequiredSpacing,required);
            const bool preserved=topologyUnchanged(*before,*candidate,a)&&
                                 topologyUnchanged(*before,*candidate,b);
            FacePoints oldA{},oldB{};
            double previous=std::numeric_limits<double>::infinity();
            bool isOldUnchanged=false;
            if(preserved) {
                oldA=triangleOf(*before,a);
                oldB=triangleOf(*before,b);
                isOldUnchanged=sameTriangle(oldA,currentA)&&
                               sameTriangle(oldB,currentB);
                if(!isOldUnchanged)
                    previous=testDistance(oldA,oldB);
            }
            if(isOldUnchanged) {
                ++report.unchangedHistoricalPairs;
                hadOld=true;
                continue;
            }
            const bool slide=preserved&&
                strictRelativeTangent(oldA,currentA,oldB,currentB,epsilon)&&
                distance+epsilon>=previous;
            if(distance+epsilon<required) {
                if(slide&&previous+epsilon<required&&
                   distance>epsilon) {
                    ++report.tangentialPairs;
                    hadSlide=true;
                    continue;
                }
                report.status=OffModeClearanceStatus::TooClose;
                report.reason="V18 independent triangles violate local mesh-scale spacing";
                return report;
            }
            if(distance<=required+epsilon) {
                return uncertain("V18 clearance lies inside numeric threshold uncertainty");
            }
            if(slide) {
                ++report.tangentialPairs;
                hadSlide=true;
            }
        }
        report.status=hadSlide?OffModeClearanceStatus::TangentialSlide:
            hadOld?OffModeClearanceStatus::UnchangedOld:
                   OffModeClearanceStatus::Clear;
    }catch(const std::length_error& error) {
        return uncertain(error.what());
    }catch(const std::out_of_range& error) {
        return uncertain(error.what());
    }catch(const std::logic_error& error) {
        return uncertain(error.what());
    }
    return report;
}

} // namespace mesher

#include "mesher/crease_pinch.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mesher {
namespace {
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}

struct Vec2 { double x{},y{}; };
Vec2 operator-(Vec2 a,Vec2 b) {return {a.x-b.x,a.y-b.y};}
double dot2(Vec2 a,Vec2 b) {return a.x*b.x+a.y*b.y;}

using Matrix3=std::array<std::array<double,3>,3>;
Vec3 smallestEigenvector(Matrix3 m) {
    Matrix3 v{{{1,0,0},{0,1,0},{0,0,1}}};
    for(int iteration=0;iteration<32;++iteration) {
        int p=0,q=1;
        double greatest=std::abs(m[0][1]);
        for(int i=0;i<3;++i)
            for(int j=i+1;j<3;++j)
                if(std::abs(m[i][j])>greatest) {
                    greatest=std::abs(m[i][j]);p=i;q=j;
                }
        const double scale=std::abs(m[0][0])+std::abs(m[1][1])+
                           std::abs(m[2][2])+1e-30;
        if(greatest<=scale*1e-14)break;
        const double tau=(m[q][q]-m[p][p])/(2*m[p][q]);
        const double t=std::copysign(1.,tau)/
                       (std::abs(tau)+std::sqrt(1+tau*tau));
        const double c=1/std::sqrt(1+t*t),s=t*c;
        const double app=m[p][p],aqq=m[q][q],apq=m[p][q];
        m[p][p]=app-t*apq;
        m[q][q]=aqq+t*apq;
        m[p][q]=m[q][p]=0;
        for(int k=0;k<3;++k) {
            if(k!=p&&k!=q) {
                const double akp=m[k][p],akq=m[k][q];
                m[k][p]=m[p][k]=c*akp-s*akq;
                m[k][q]=m[q][k]=s*akp+c*akq;
            }
            const double vkp=v[k][p],vkq=v[k][q];
            v[k][p]=c*vkp-s*vkq;
            v[k][q]=s*vkp+c*vkq;
        }
    }
    int min=0;
    for(int i=1;i<3;++i)if(m[i][i]<m[min][min])min=i;
    const Vec3 n{v[0][min],v[1][min],v[2][min]};
    return length(n)>1e-20?normalized(n):Vec3{};
}

struct Support {
    VertexId id{};
    Vec3 position{};
    double distance{};
    double area{};
    Vec2 xy{};
    double z{};
};

struct FitLine {
    Vec2 center{},direction{1,0};
    double linearity{},principal{},transverse{},realMass{},confidence{};
};

// A virtual contact dimple participates in directional covariance, but is
// never counted in the real evidence mass.
FitLine fitLine(const std::vector<Support>& pts,Vec3 planeCenter,
                Vec3 axisX,Vec3 axisY,double radius,double depth,
                int sign,double surfaceArea,Vec3 contact) {
    FitLine result;
    const double threshold=0.005*depth;
    const double virtualWeight=0.04*surfaceArea*depth;
    const Vec3 delta=contact-planeCenter;
    const Vec2 virtualPoint{dot(delta,axisX),dot(delta,axisY)};
    double sum=virtualWeight;
    Vec2 center{virtualPoint.x*virtualWeight,virtualPoint.y*virtualWeight};
    std::vector<double> weights;
    weights.reserve(pts.size());
    for(const auto& p:pts) {
        const double w=p.area*std::max(0.,sign*p.z-threshold);
        result.realMass+=w;
        center.x+=w*p.xy.x;
        center.y+=w*p.xy.y;
        sum+=w;
        weights.push_back(w);
    }
    if(!(sum>0)||!std::isfinite(sum))return result;
    center.x/=sum;
    center.y/=sum;
    result.center=center;
    double xx=virtualWeight*(virtualPoint.x-center.x)*(virtualPoint.x-center.x);
    double yy=virtualWeight*(virtualPoint.y-center.y)*(virtualPoint.y-center.y);
    double xy=virtualWeight*(virtualPoint.x-center.x)*(virtualPoint.y-center.y);
    for(std::size_t i=0;i<pts.size();++i) {
        const auto diff=pts[i].xy-center;
        xx+=weights[i]*diff.x*diff.x;
        yy+=weights[i]*diff.y*diff.y;
        xy+=weights[i]*diff.x*diff.y;
    }
    xx/=sum;yy/=sum;xy/=sum;
    const double trace=xx+yy;
    const double difference=std::hypot(xx-yy,2*xy);
    result.principal=std::max(0.,(trace+difference)*.5);
    result.transverse=std::max(0.,(trace-difference)*.5);
    const double minScale=.1*radius;
    const double denom=trace+minScale*minScale;
    result.linearity=denom>0?std::clamp(difference/denom,0.,1.):0.;
    const double massReference=0.02*surfaceArea*depth;
    const double evidence=result.realMass/
                          (result.realMass+massReference+1e-300);
    result.confidence=std::clamp(result.linearity*evidence,0.,1.);
    // For an isotropic covariance the eigenvector is arbitrary; k suppresses
    // all axis-dependent effects. Choose deterministic +X as a safe fallback.
    if(difference>trace*1e-12&&difference>0) {
        const double angle=.5*std::atan2(2*xy,xx-yy);
        result.direction={std::cos(angle),std::sin(angle)};
    }
    return result;
}

Vec3 reflect(Vec3 p) {return {-p.x,p.y,p.z};}

// Squared point-to-triangle distance with a finite edge fallback. Nearest
// *face* anchoring is essential when a second disconnected sheet has a vertex
// closer to C than any vertex of the actual contacted triangle.
double contactTriangleDistance2(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    const auto segment=[p](Vec3 u,Vec3 v) {
        const Vec3 edge=v-u;
        const double size2=dot(edge,edge);
        const double t=size2>0?
            std::clamp(dot(p-u,edge)/size2,0.,1.):0.;
        const Vec3 delta=p-(u+edge*t);
        return dot(delta,delta);
    };
    const Vec3 ab=b-a,ac=c-a,n=cross(ab,ac);
    const double n2=dot(n,n);
    if(n2>0) {
        const Vec3 projected=p-n*(dot(p-a,n)/n2);
        const Vec3 ap=projected-a;
        const double aa=dot(ab,ab),bb=dot(ab,ac),cc=dot(ac,ac);
        const double d=aa*cc-bb*bb;
        if(d>1e-30) {
            const double u=(dot(ap,ab)*cc-dot(ap,ac)*bb)/d;
            const double v=(dot(ap,ac)*aa-dot(ap,ab)*bb)/d;
            if(u>=-1e-12&&v>=-1e-12&&u+v<=1+1e-12) {
                const Vec3 delta=p-projected;
                return dot(delta,delta);
            }
        }
    }
    return std::min({segment(a,b),segment(b,c),segment(c,a)});
}

} // namespace

CreasePinchProposal proposeCreasePinch(
    const RegionTopologyIndex& before,Vec3 contact,double radius,
    double width,double depth,double strength,Vec3 towardCamera,
    SculptBrushMode mode,FaceId contactFace,
    std::size_t maximumVertices,std::size_t maximumEdges) {
    if(mode==SculptBrushMode::Inflate||!finite(contact)||
       !finite(towardCamera)||length(towardCamera)<=1e-12||
       !std::isfinite(radius)||radius<=0||
       !std::isfinite(width)||width<=0||
       !std::isfinite(depth)||depth<=0||
       !std::isfinite(strength)||strength<0||
       maximumVertices<3||maximumEdges<3)
        throw std::invalid_argument("Invalid Crease/Pinch stamp parameters");

    CreasePinchProposal result;
    // Geometric rejection alone cannot allow a second disconnected sheet
    // to be moved; the final radius is a bounded edge-path distance.
    const auto candidates=before.querySphere(contact,radius);
    if(candidates.size()>maximumVertices)
        throw std::length_error("Crease/Pinch support vertex budget exhausted");
    if(candidates.empty()||strength==0)return result;

    std::unordered_set<VertexId> allowed(candidates.begin(),candidates.end());
    std::map<VertexId,double> distances;
    using QueueEntry=std::pair<double,VertexId>;
    std::priority_queue<QueueEntry,std::vector<QueueEntry>,
                        std::greater<QueueEntry>> pending;
    const auto push=[&](VertexId id,double d) {
        if(d>radius||!allowed.contains(id))return;
        const auto it=distances.find(id);
        if(it!=distances.end()&&it->second<=d)return;
        distances[id]=d;
        pending.push({d,id});
    };

    if(contactFace) {
        const auto face=before.maybeFace(contactFace);
        if(!face)throw std::invalid_argument("Crease contact FaceId is not live");
        for(const auto id:face->vertices)
            push(id,length(before.vertex(id).position-contact));
    }else {
        // Queued 4D segments carry surface positions, not a new stable face
        // identity at every interpolated sample. Choose the nearest actual
        // triangle within this local candidate halo, never the nearest
        // *vertex* (which might lie on a second, disconnected sheet).
        std::set<FaceId> faces;
        for(const auto id:candidates)
            for(const auto fid:before.vertex(id).incidentFaces) {
                faces.insert(fid);
                if(faces.size()>maximumEdges)
                    throw std::length_error("Crease contact face budget exhausted");
            }
        FaceId bestFace{};
        double best=std::numeric_limits<double>::infinity();
        for(const auto fid:faces) {
            const auto t=before.face(fid).vertices;
            const double distance2=contactTriangleDistance2(contact,
                before.vertex(t[0]).position,
                before.vertex(t[1]).position,
                before.vertex(t[2]).position);
            if(distance2<best) {
                best=distance2;
                bestFace=fid;
            }
        }
        if(bestFace) {
            const auto chosen=before.face(bestFace).vertices;
            // Co-located disconnected sheets are ambiguous; never
            // arbitrarily apply the crease to one based on map ordering.
            const double tolerance=radius*radius*1e-12;
            for(const auto fid:faces) {
                if(fid==bestFace)continue;
                const auto other=before.face(fid).vertices;
                bool sharesVertex=false;
                for(const auto x:chosen)
                    for(const auto y:other)
                        sharesVertex|=x==y;
                if(sharesVertex)continue;
                const double dist2=contactTriangleDistance2(contact,
                    before.vertex(other[0]).position,
                    before.vertex(other[1]).position,
                    before.vertex(other[2]).position);
                if(std::abs(dist2-best)<=tolerance)
                    throw std::domain_error(
                        "Crease contact has ambiguous disconnected sheet");
            }
            for(const auto id:chosen)
                push(id,length(before.vertex(id).position-contact));
        }
    }
    while(!pending.empty()) {
        const auto [d,id]=pending.top();
        pending.pop();
        if(d!=distances.at(id))continue;
        const auto& vertex=before.vertex(id);
        for(const auto fid:vertex.incidentFaces) {
            const auto face=before.face(fid).vertices;
            for(const auto neighbor:face) {
                if(neighbor==id)continue;
                if(++result.diagnostics.exploredEdges>maximumEdges)
                    throw std::length_error("Crease/Pinch geodesic edge budget exhausted");
                const double edge=length(before.vertex(neighbor).position-
                                         vertex.position);
                if(std::isfinite(edge))push(neighbor,d+edge);
            }
        }
    }
    if(distances.size()<3)return result;

    std::vector<Support> pts;
    pts.reserve(distances.size());
    double areaTotal=0;
    Vec3 centroid{},sumNormal{};
    for(const auto [id,dist]:distances) {
        const auto& vertex=before.vertex(id);
        double a=0;
        for(const auto fid:vertex.incidentFaces) {
            const auto t=before.face(fid).vertices;
            const Vec3 p=before.vertex(t[0]).position;
            const Vec3 q=before.vertex(t[1]).position;
            const Vec3 r=before.vertex(t[2]).position;
            const Vec3 n=cross(q-p,r-p);
            const double twiceArea=length(n);
            a+=twiceArea/6.;
            sumNormal=sumNormal+n;
        }
        if(!std::isfinite(a)||a<0)
            throw std::invalid_argument("Crease support contains invalid triangle area");
        if(a<=0)continue;
        pts.push_back({id,vertex.position,dist,a,{},0});
        areaTotal+=a;
        centroid=centroid+vertex.position*a;
    }
    if(pts.size()<3||areaTotal<=0)return result;
    centroid=centroid/areaTotal;
    Matrix3 cov{};
    for(const auto& p:pts) {
        const Vec3 q=(p.position-centroid)/radius;
        const double w=p.area/areaTotal;
        const double v[3]{q.x,q.y,q.z};
        for(int i=0;i<3;++i)for(int j=0;j<3;++j)
            cov[i][j]+=w*v[i]*v[j];
    }
    Vec3 n=smallestEigenvector(cov);
    if(length(n)<1e-12) {
        if(length(sumNormal)<1e-20)return result;
        n=normalized(sumNormal);
    }
    const Vec3 toward=normalized(towardCamera);
    if(dot(n,toward)<0)n=n*(-1);
    // Choose a deterministic tangent basis that is not nearly parallel to n.
    const Vec3 seed=std::abs(n.x)<0.9?Vec3{1,0,0}:Vec3{0,1,0};
    const Vec3 x=normalized(seed-n*dot(seed,n));
    const Vec3 y=cross(n,x);
    for(auto& p:pts) {
        const Vec3 q=p.position-centroid;
        p.xy={dot(q,x),dot(q,y)};
        p.z=dot(q,n);
    }
    const int sign=mode==SculptBrushMode::Crease?-1:1;
    const auto line=fitLine(pts,centroid,x,y,radius,depth,
                            sign,areaTotal,contact);
    result.diagnostics.confidence=line.confidence;
    result.diagnostics.linearity=line.linearity;
    result.diagnostics.realEvidence=line.realMass;
    result.diagnostics.principalVariance=line.principal;
    result.diagnostics.transverseVariance=line.transverse;
    result.diagnostics.supportVertices=pts.size();

    const double epsilon=.01*radius;
    const double lateralStrength=std::min(1.,strength/depth);
    std::map<VertexId,Vec3> movedPositions;
    for(const auto& p:pts) {
        const Vec2 diff=p.xy-line.center;
        const double along=dot2(diff,line.direction);
        const Vec2 closest{line.center.x+along*line.direction.x,
                           line.center.y+along*line.direction.y};
        const Vec2 towardLine=closest-p.xy;
        const double d=std::hypot(towardLine.x,towardLine.y);
        const double q=std::max(0.,d-width*.5);
        const double attenuation=std::max(0.,1-p.distance/radius);
        Vec3 change{};
        if(d>0&&q>0&&line.confidence>0) {
            const double t=lateralStrength*line.confidence*q*
                           attenuation/d;
            change=x*(towardLine.x*t)+y*(towardLine.y*t);
        }
        const double inverseLine=epsilon/(q+epsilon);
        const double inverseRadial=1/(1+p.distance/radius);
        double newZ=p.z+sign*strength*
                    (line.confidence*inverseLine+inverseRadial)*
                    attenuation;
        // Conditional depth cap: never raise previously deeper material
        // toward the new evolving fitted plane.
        if(sign*p.z>=0&&sign*p.z<=depth&&sign*newZ>depth)
            newZ=sign*depth;
        const Vec3 position=p.position+change+n*(newZ-p.z);
        if(!finite(position))
            throw std::overflow_error("Non-finite Crease/Pinch candidate displacement");
        if(length(position-p.position)>1e-15*radius)
            movedPositions.emplace(p.id,position);
    }

    // Reject any fold or vanished face *before* the private RegionEdit.
    // Dense SHORT triangles are allowed. Orientation changes are not.
    std::set<FaceId> touched;
    for(const auto& [id,_]:movedPositions)
        for(const auto fid:before.vertex(id).incidentFaces)touched.insert(fid);
    for(const auto fid:touched) {
        const auto tri=before.face(fid).vertices;
        std::array<Vec3,3> oldPos{},newPos{};
        for(int j=0;j<3;++j) {
            oldPos[j]=before.vertex(tri[j]).position;
            const auto it=movedPositions.find(tri[j]);
            newPos[j]=it==movedPositions.end()?oldPos[j]:it->second;
        }
        const Vec3 oldN=cross(oldPos[1]-oldPos[0],oldPos[2]-oldPos[0]);
        const Vec3 newN=cross(newPos[1]-newPos[0],newPos[2]-newPos[0]);
        const double areaScale=length(oldN);
        if(areaScale>1e-20*radius*radius&&
           (length(newN)<=1e-12*areaScale||dot(oldN,newN)<=0))
            throw std::domain_error("Crease/Pinch would invert or collapse a face");
    }
    result.movements.reserve(movedPositions.size());
    for(const auto& [id,p]:movedPositions)
        result.movements.push_back({id,p,distances.at(id)});
    result.diagnostics.displacedVertices=result.movements.size();
    return result;
}

VersionedInflateStamp versionedCreasePinchStamp(
    std::shared_ptr<const RegionTopologyIndex> before,
    std::span<const BrushArea> areas,const SculptDeformation& settings,
    double strength,const std::unordered_map<VertexId,VertexId>& mirrors,
    std::size_t partitionCapacity) {
    if(!before||!partitionCapacity||settings.mode==SculptBrushMode::Inflate||
       !std::isfinite(settings.widthFraction)||settings.widthFraction<=0||
       !std::isfinite(settings.depthFraction)||settings.depthFraction<=0)
        throw std::invalid_argument("Invalid versioned Crease/Pinch settings");
    VersionedInflateStamp result;
    result.index=before;
    result.baseline.before=before;
    if(areas.empty())return result;

    std::map<VertexId,Vec3> positions;
    // The mirrored half is not independently fitted (which would introduce
    // floating-point PCA differences). One side is authoritative.
    const std::size_t count=mirrors.empty()?areas.size():1;
    for(std::size_t i=0;i<count;++i) {
        const auto& area=areas[i];
        const auto prop=proposeCreasePinch(*before,area.center,area.radius,
            area.radius*settings.widthFraction,
            area.radius*settings.depthFraction,std::abs(strength),
            settings.towardCamera,settings.mode);
        result.candidates+=prop.diagnostics.supportVertices;
        for(const auto& move:prop.movements) {
            const auto old=before->vertex(move.vertex).position;
            const auto it=positions.find(move.vertex);
            if(it==positions.end()||
               length(move.position-old)>length(it->second-old))
                positions[move.vertex]=move.position;
        }
    }
    if(positions.empty())return result;
    if(!mirrors.empty()) {
        std::map<VertexId,Vec3> exact;
        for(const auto& [id,p]:positions) {
            const auto it=mirrors.find(id);
            if(it==mirrors.end())
                throw std::logic_error("Crease/Pinch influenced a vertex without mirror");
            const VertexId partner=it->second;
            if(partner!=id&&positions.contains(partner)) {
                // Both endpoints of one X-mirror orbit can fall inside
                // the SAME Euclidean brush sphere near the seam. Choose the
                // original vertex nearest the actual contact area; never
                // choose by stable-ID ordering, which can make the opposite
                // half authoritative and invert a stroke's local direction.
                const Vec3 c=areas.front().center;
                const double first=length(before->vertex(id).position-c);
                const double second=length(before->vertex(partner).position-c);
                if(second<first||(first==second&&partner<id))continue;
            }
            if(id==partner) {
                exact[id]={0,p.y,p.z};
            }else {
                exact[id]=p;
                exact[partner]=reflect(p);
            }
        }
        positions=std::move(exact);
    }
    std::vector<VertexId> seeds;
    std::set<FaceId> faces;
    seeds.reserve(positions.size());
    for(const auto& [id,_]:positions) {
        seeds.push_back(id);
        for(const auto fid:before->vertex(id).incidentFaces)faces.insert(fid);
    }
    result.baseline.movedVertices=seeds;
    result.baseline.affectedFaces.assign(faces.begin(),faces.end());
    std::vector<std::pair<VertexId,VertexId>> activeMirrors;
    if(!mirrors.empty())for(const auto id:seeds)
        activeMirrors.push_back({id,mirrors.at(id)});
    RegionEdit edit(before,seeds,std::move(activeMirrors),partitionCapacity);
    for(const auto& [id,p]:positions) {
        if(!mirrors.empty()&&id>mirrors.at(id))continue;
        edit.moveVertex(id,p);
    }
    const auto commit=edit.commit();
    result.index=edit.updatedIndex();
    result.commitStatistics=commit.statistics;
    result.moved=seeds.size();
    return result;
}

} // namespace mesher

#include "mesher/repair_surgery.hpp"
#include "repair_surgery_detail.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
double triangleQuality(Vec3 a,Vec3 b,Vec3 c) {
    const double ab=dot(b-a,b-a),bc=dot(c-b,c-b),ca=dot(a-c,a-c);
    const double sum=ab+bc+ca;
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}
double maxEdge(Vec3 a,Vec3 b,Vec3 c) {
    return std::max({length(a-b),length(b-c),length(c-a)});
}
double medianEdge(const RegionTopologyIndex& index,
                  const RepairContour& loop) {
    std::vector<double> lengths;
    lengths.reserve(loop.vertexIds.size());
    for(std::size_t i=0;i<loop.vertexIds.size();++i)
        lengths.push_back(length(
            index.vertex(loop.vertexIds[i]).position-
            index.vertex(loop.vertexIds[(i+1)%loop.vertexIds.size()]).position));
    std::sort(lengths.begin(),lengths.end());
    return lengths[lengths.size()/2];
}
struct LocalLoop {
    std::vector<VertexId> ids;
    double target{};
};
struct BaryProjection {
    Vec3 origin{},normal{},u{},v{};
    Vec3 point(Vec3 p) const {return {dot(p-origin,u),dot(p-origin,v),0};}
};
BaryProjection projection(const std::vector<VertexId>& ids,
                          const std::unordered_map<VertexId,Vec3>& positions) {
    BaryProjection out;
    out.origin=positions.at(ids.front());
    Vec3 sum{};
    for(std::size_t i=0;i<ids.size();++i)
        sum=sum+cross(positions.at(ids[i])-out.origin,
                      positions.at(ids[(i+1)%ids.size()])-out.origin);
    if(length(sum)<=0)throw std::logic_error("Closure contour has zero area");
    out.normal=normalized(sum);
    const auto axis=std::abs(out.normal.z)<.8?Vec3{0,0,1}:Vec3{0,1,0};
    out.u=normalized(cross(axis,out.normal));
    out.v=cross(out.normal,out.u);
    return out;
}
double cross2(Vec3 a,Vec3 b,Vec3 c) {
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
bool pointInTriangle(Vec3 point,Vec3 a,Vec3 b,Vec3 c,double epsilon) {
    return cross2(a,b,point)>=-epsilon&&
           cross2(b,c,point)>=-epsilon&&
           cross2(c,a,point)>=-epsilon;
}
std::vector<std::array<VertexId,3>> capTriangles(
    const LocalLoop& loop,
    const std::unordered_map<VertexId,Vec3>& positions,
    const RepairSurgerySettings& settings) {
    // Reverse the surviving-boundary orientation. This is a bounded,
    // projection-based ear clip, never a center-vertex fan.
    std::vector<VertexId> polygon(loop.ids.rbegin(),loop.ids.rend());
    auto basis=projection(polygon,positions);
    std::unordered_map<VertexId,Vec3> points;
    for(auto id:polygon)points.emplace(id,basis.point(positions.at(id)));
    const double epsilon=1e-12*loop.target*loop.target;
    std::vector<std::array<VertexId,3>> triangles;
    while(polygon.size()>3) {
        std::optional<std::size_t> best;
        double score=-1;
        for(std::size_t i=0;i<polygon.size();++i) {
            const auto a=polygon[(i+polygon.size()-1)%polygon.size()];
            const auto b=polygon[i],c=polygon[(i+1)%polygon.size()];
            if(cross2(points.at(a),points.at(b),points.at(c))<=epsilon)
                continue;
            bool contains=false;
            for(const auto id:polygon) {
                if(id==a||id==b||id==c)continue;
                if(pointInTriangle(points.at(id),points.at(a),
                                   points.at(b),points.at(c),epsilon)) {
                    contains=true;break;
                }
            }
            if(contains)continue;
            const auto q=triangleQuality(positions.at(a),positions.at(b),
                                         positions.at(c));
            if(q>score) {score=q;best=i;}
        }
        if(!best)throw std::logic_error("No simple, positive-area cap ear");
        const auto i=*best;
        triangles.push_back({polygon[(i+polygon.size()-1)%polygon.size()],
                             polygon[i],polygon[(i+1)%polygon.size()]});
        polygon.erase(polygon.begin()+static_cast<std::ptrdiff_t>(i));
    }
    triangles.push_back({polygon[0],polygon[1],polygon[2]});
    // Cap spans may be more extensive than boundary edges, but never
    // accept degenerate ears or extremely skinny scar triangles.
    for(const auto& ids:triangles) {
        const auto a=positions.at(ids[0]),b=positions.at(ids[1]),
                   c=positions.at(ids[2]);
        if(triangleQuality(a,b,c)<settings.minimumTriangleQuality)
            throw std::logic_error("Cap ear has unacceptable aspect ratio");
    }
    return triangles;
}
struct GridCost {
    double cost{std::numeric_limits<double>::infinity()};
    unsigned char previous{}; // 1 advances A, 2 advances B
};
std::vector<std::array<VertexId,3>> zipperTriangles(
    const LocalLoop& a,const LocalLoop& b,
    const std::unordered_map<VertexId,Vec3>& pos,
    const RepairSurgerySettings& settings) {
    const std::size_t n=a.ids.size(),m=b.ids.size();
    if(n==0||m==0||n>settings.maxContourVertices||
       m>settings.maxContourVertices)
        throw std::length_error("Zipper contour exceeds bounded grid dimensions");
    // Traversal A follows survivor orientation; B traverses the *reverse*
    // survivor orientation, ensuring both boundary edges get new opposite
    // edge directions in the zipper triangles.
    std::vector<VertexId> x=a.ids;
    std::vector<VertexId> y(b.ids.rbegin(),b.ids.rend());
    std::size_t offset=0;
    double closest=std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i<m;++i) {
        const auto delta=pos.at(x.front())-pos.at(y[i]);
        const double squared=dot(delta,delta);
        if(squared<closest){closest=squared;offset=i;}
    }
    std::rotate(y.begin(),y.begin()+static_cast<std::ptrdiff_t>(offset),
                y.end());
    x.push_back(x.front());
    y.push_back(y.front());
    // The two fronts advance independently by *physical* arclength,
    // but must not drift halfway around the other ring. Such a grid
    // path builds folded/crossing scar triangles even if every edge
    // length and local triangle quality independently looks reasonable.
    const auto phases=[&](const std::vector<VertexId>& contour) {
        std::vector<double> s(contour.size(),0);
        for(std::size_t i=1;i<contour.size();++i)
            s[i]=s[i-1]+length(pos.at(contour[i])-pos.at(contour[i-1]));
        if(!(s.back()>0))throw std::logic_error("Zero perimeter scar");
        for(auto& value:s)value/=s.back();
        return s;
    };
    const auto xa=phases(x),yb=phases(y);
    constexpr double maximumPhaseLag=.30;
    const double h=std::min(a.target,b.target);
    const auto triangleCost=[&](std::array<VertexId,3> ids)->double {
        const auto p=pos.at(ids[0]),q=pos.at(ids[1]),r=pos.at(ids[2]);
        const double quality=triangleQuality(p,q,r);
        if(quality<settings.minimumTriangleQuality||
           maxEdge(p,q,r)>settings.maximumNewEdgeFactor*h)
            return std::numeric_limits<double>::infinity();
        // Short, well-proportioned triangles; unnecessary fans lose against
        // advancing the other independently sampled boundary.
        const double perimeter=length(p-q)+length(q-r)+length(r-p);
        return perimeter/h+1.0/quality;
    };
    const auto stride=m+1;
    std::vector<GridCost> dp((n+1)*(m+1));
    dp[0].cost=0;
    for(std::size_t i=0;i<=n;++i)for(std::size_t j=0;j<=m;++j) {
        if(i==0&&j==0)continue;
        auto& current=dp[i*stride+j];
        if(std::abs(xa[i]-yb[j])>maximumPhaseLag)continue;
        if(i>0) {
            const auto cost=triangleCost({x[i],x[i-1],y[j]});
            const auto candidate=dp[(i-1)*stride+j].cost+cost;
            if(candidate<current.cost){current.cost=candidate;current.previous=1;}
        }
        if(j>0) {
            const auto cost=triangleCost({x[i],y[j-1],y[j]});
            const auto candidate=dp[i*stride+j-1].cost+cost;
            if(candidate<current.cost){current.cost=candidate;current.previous=2;}
        }
    }
    if(!std::isfinite(dp.back().cost))
        throw std::logic_error("No edge-length/quality-bounded zipper strip");
    std::vector<std::array<VertexId,3>> faces;
    faces.reserve(n+m);
    std::size_t i=n,j=m;
    while(i||j) {
        const auto step=dp[i*stride+j].previous;
        if(step==1){faces.push_back({x[i],x[i-1],y[j]});--i;}
        else if(step==2){faces.push_back({x[i],y[j-1],y[j]});--j;}
        else throw std::logic_error("Unreachable zipper DP state");
    }
    std::reverse(faces.begin(),faces.end());
    std::unordered_map<VertexId,std::size_t> added;
    for(const auto& face:faces)
        for(const auto id:face)
            if(++added[id]>settings.maximumAddedFanDegree)
                throw std::logic_error("Zipper produces a high-degree vertex hub");
    return faces;
}
using detail::SideSplits;
using detail::subdivideRetainedTriangle;
double pointTriangleDistanceSquared(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    // Closest-point Voronoi-region test. No projection-axis assumptions:
    // this also measures curved/nonplanar scars in world space.
    const auto ab=b-a,ac=c-a,ap=p-a;
    const double d1=dot(ab,ap),d2=dot(ac,ap);
    if(d1<=0&&d2<=0)return dot(ap,ap);
    const auto bp=p-b;
    const double d3=dot(ab,bp),d4=dot(ac,bp);
    if(d3>=0&&d4<=d3)return dot(bp,bp);
    const double vc=d1*d4-d3*d2;
    if(vc<=0&&d1>=0&&d3<=0) {
        const auto projection=a+ab*(d1/(d1-d3));
        return dot(p-projection,p-projection);
    }
    const auto cp=p-c;
    const double d5=dot(ab,cp),d6=dot(ac,cp);
    if(d6>=0&&d5<=d6)return dot(cp,cp);
    const double vb=d5*d2-d1*d6;
    if(vb<=0&&d2>=0&&d6<=0) {
        const auto projection=a+ac*(d2/(d2-d6));
        return dot(p-projection,p-projection);
    }
    const double va=d3*d6-d5*d4;
    if(va<=0&&(d4-d3)>=0&&(d5-d6)>=0) {
        const auto bc=c-b;
        const auto projection=b+bc*((d4-d3)/((d4-d3)+(d5-d6)));
        return dot(p-projection,p-projection);
    }
    const double denominator=va+vb+vc;
    if(!(denominator>0))
        throw std::logic_error("Degenerate excised triangle in drift validator");
    const double v=vb/denominator,w=vc/denominator;
    const auto closest=a+ab*v+ac*w;
    return dot(p-closest,p-closest);
}
void validateSurgeryGeometry(
    const RegionTopologyIndex& baseline,
    const RepairPlan& plan,
    const std::set<VertexId>& selected,
    const RepairSurgerySettings& settings,
    RepairSurgeryResult& out) {
    if(!std::isfinite(settings.maximumSurfaceDeviationInEdges)||
       settings.maximumSurfaceDeviationInEdges<=0||
       (settings.requireReverseSurfaceFidelity&&
        (!std::isfinite(settings.maximumReverseDeviationInEdges)||
         settings.maximumReverseDeviationInEdges<=0))||
       settings.maxResidualFaces==0)
        throw std::invalid_argument("Invalid residual collision/deviation budgets");
    if(!out.index)
        throw std::logic_error("Repair geometry has no committed private index");
    const std::set<FaceId> removed(plan.removedFaces.begin(),
                                    plan.removedFaces.end());
    // Verify every surviving face in the local write/read one-ring, not
    // merely triangles created by the scar. BVH queries this COMPLETE
    // private post-state revision for collisions with any other face.
    std::set<FaceId> verify(out.insertedFaces.begin(),out.insertedFaces.end());
    for(const auto id:selected)
        for(const auto face:baseline.vertex(id).incidentFaces)
            if(!removed.contains(face))verify.insert(face);
    if(verify.size()>settings.maxResidualFaces)
        throw std::length_error("Residual collision neighborhood exceeds budget");
    out.verifiedLocalFaces=verify.size();
    const auto contacts=out.index->triangleIntersections({},
                           {verify.begin(),verify.end()});
    if(!contacts.empty()) {
        const auto describe=[&](FaceId id) {
            std::string result=std::to_string(id);
            for(const auto v:out.index->face(id).vertices) {
                const auto p=out.index->vertex(v).position;
                result+=" ["+std::to_string(v)+" ("+
                    std::to_string(p.x)+","+
                    std::to_string(p.y)+","+
                    std::to_string(p.z)+")]";
            }
            return result;
        };
        throw std::logic_error("Private repair has a residual local collision "+
            describe(contacts.front().first)+" / "+
            describe(contacts.front().second)+
            (contacts.front().ambiguous?" (ambiguous)":" (intersecting)"));
    }

    if(out.insertedFaces.empty()) {
        if(settings.requireReverseSurfaceFidelity)
            throw std::logic_error(
                "Reverse surface fidelity cannot retain a wholly discarded shell");
        return;
    }
    double h=std::numeric_limits<double>::infinity();
    for(const auto& contour:plan.contours)
        h=std::min(h,medianEdge(baseline,contour));
    if(!(h>0)||!std::isfinite(h))
        throw std::logic_error("No local density for scar drift validation");
    std::vector<std::array<Vec3,3>> removedTriangles;
    removedTriangles.reserve(plan.removedFaces.size());
    for(const auto id:plan.removedFaces) {
        const auto vertices=baseline.face(id).vertices;
        removedTriangles.push_back({
            baseline.vertex(vertices[0]).position,
            baseline.vertex(vertices[1]).position,
            baseline.vertex(vertices[2]).position});
    }
    for(const auto faceId:out.insertedFaces) {
        const auto tri=out.index->face(faceId).vertices;
        const auto a=out.index->vertex(tri[0]).position;
        const auto b=out.index->vertex(tri[1]).position;
        const auto c=out.index->vertex(tri[2]).position;
        for(const auto p:std::array<Vec3,4>{a,b,c,(a+b+c)/3}) {
            double closest=std::numeric_limits<double>::infinity();
            for(const auto& original:removedTriangles)
                closest=std::min(closest,pointTriangleDistanceSquared(
                    p,original[0],original[1],original[2]));
            if(!std::isfinite(closest))
                throw std::logic_error("Invalid geometric drift measurement");
            out.maximumSurfaceDeviationInEdges=std::max(
                out.maximumSurfaceDeviationInEdges,
                std::sqrt(std::max(0.0,closest))/h);
            if(out.maximumSurfaceDeviationInEdges>
               settings.maximumSurfaceDeviationInEdges)
                throw std::logic_error("Repair exceeds local surface deviation budget");
        }
    }
    if(settings.requireReverseSurfaceFidelity) {
        // Reverse sample distance measures deleted feature-bearing surface
        // to the constructed scar or retained neighborhood. It must only
        // be used when preservation of the removed surface is the stated
        // intent: intentionally buried red fragments should be discarded.
        std::vector<std::array<Vec3,3>> repairedTriangles;
        repairedTriangles.reserve(verify.size());
        for(const auto id:verify) {
            const auto face=out.index->face(id).vertices;
            repairedTriangles.push_back({
                out.index->vertex(face[0]).position,
                out.index->vertex(face[1]).position,
                out.index->vertex(face[2]).position});
        }
        for(const auto& original:removedTriangles) {
            const auto& a=original[0],&b=original[1],&d=original[2];
            for(const auto p:std::array<Vec3,4>{a,b,d,(a+b+d)/3}) {
                double closest=std::numeric_limits<double>::infinity();
                for(const auto& current:repairedTriangles)
                    closest=std::min(closest,pointTriangleDistanceSquared(
                        p,current[0],current[1],current[2]));
                if(!std::isfinite(closest))
                    throw std::logic_error("Invalid reverse surface measurement");
                out.maximumReverseDeviationInEdges=std::max(
                    out.maximumReverseDeviationInEdges,
                    std::sqrt(std::max(0.0,closest))/h);
                if(out.maximumReverseDeviationInEdges>
                   settings.maximumReverseDeviationInEdges)
                    throw std::logic_error(
                        "Repair exceeds reverse surface fidelity budget");
            }
        }
    }
}
} // namespace

RepairSurgeryResult applyRepairSurgery(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const RepairPlan& proposed,const RepairSurgerySettings& settings) {
    if(!baseline||!proposed.ready())
        throw std::invalid_argument("Surgery requires an immutable baseline and ready plan");
    if(settings.partitionCapacity==0||settings.maxNewFaces==0||
       settings.maxContourVertices<3||settings.maxNewVertices==0||
       !std::isfinite(settings.maximumBoundaryEdgeFactor)||
       settings.maximumBoundaryEdgeFactor<=1||
       !std::isfinite(settings.maximumNewEdgeFactor)||
       settings.maximumNewEdgeFactor<=1||
       !std::isfinite(settings.minimumTriangleQuality)||
       settings.minimumTriangleQuality<=0||
       settings.minimumTriangleQuality>=1)
        throw std::invalid_argument("Invalid surgery quality/budget settings");
    if(proposed.removedFaces.empty())
        throw std::invalid_argument("Empty removal is not surgery");
    // Never accept stale, forged, or incorrectly matched index-sensitive
    // contours as sufficient authorization for topological surgery.
    const auto checked=planRemovalContours(*baseline,proposed.removedFaces,
                                           settings.planning);
    if(!checked.ready()||checked.pairs.size()!=proposed.pairs.size()||
       checked.cappedContours!=proposed.cappedContours||
       checked.contours.size()!=proposed.contours.size())
        throw std::logic_error("Surgery plan is stale or incompatible with baseline");
    for(std::size_t i=0;i<checked.contours.size();++i)
        if(checked.contours[i].vertexIds!=proposed.contours[i].vertexIds)
            throw std::logic_error("Surgery contour vertices differ from baseline");
    for(std::size_t i=0;i<checked.pairs.size();++i)
        if(checked.pairs[i].first!=proposed.pairs[i].first||
           checked.pairs[i].second!=proposed.pairs[i].second)
            throw std::logic_error("Surgery zipper pairs differ from baseline");

    std::set<FaceId> deleted(proposed.removedFaces.begin(),
                             proposed.removedFaces.end());
    std::set<VertexId> selected;
    for(const auto id:deleted)
        for(const auto v:baseline->face(id).vertices)selected.insert(v);
    for(const auto& contour:checked.contours)
        for(const auto id:contour.survivingFaces)
            for(const auto v:baseline->face(id).vertices)selected.insert(v);
    if(selected.empty())throw std::logic_error("Surgery has no editable vertices");
    RegionEdit edit(baseline,{selected.begin(),selected.end()},{},
                    settings.partitionCapacity);
    RepairSurgeryResult output;
    output.retiredFaces.assign(deleted.begin(),deleted.end());
    std::unordered_map<VertexId,Vec3> positions;
    for(const auto id:selected)positions.emplace(id,baseline->vertex(id).position);
    for(const auto id:deleted)edit.removeFace(id);
    // Each boundary segment must retain its owner orientation and must be
    // subdivided in the SAME transaction as the surviving adjacent triangle.
    std::set<FaceId> affectedSurvivors;
    std::map<FaceId,SideSplits> boundarySplits;
    std::vector<LocalLoop> loops;
    loops.reserve(checked.contours.size());
    std::vector<double> medians;
    for(const auto& contour:checked.contours)
        medians.push_back(medianEdge(*baseline,contour));
    for(std::size_t k=0;k<checked.contours.size();++k) {
        const auto& contour=checked.contours[k];
        LocalLoop loop;
        loop.target=medians[k];
        for(const auto& pair:checked.pairs)
            if(pair.first==k||pair.second==k)
                loop.target=std::min(medians[pair.first],
                                     medians[pair.second]);
        if(!(loop.target>0)||!std::isfinite(loop.target))
            throw std::logic_error("No finite local boundary resolution");
        for(std::size_t i=0;i<contour.vertexIds.size();++i) {
            const auto a=contour.vertexIds[i],
                       b=contour.vertexIds[(i+1)%contour.vertexIds.size()];
            loop.ids.push_back(a);
            const auto p=positions.at(a),q=positions.at(b);
            const auto segments=static_cast<std::size_t>(std::ceil(
                length(q-p)/(settings.maximumBoundaryEdgeFactor*loop.target)));
            if(segments<=1)continue;
            if(segments>settings.maxNewVertices+1||
               output.insertedVertices.size()+segments-1>
                   settings.maxNewVertices)
                throw std::length_error("Boundary subdivision vertex budget exhausted");
            const auto survivingId=contour.survivingFaces[i];
            const auto face=baseline->face(survivingId);
            bool oriented=false;
            for(unsigned j=0;j<3;++j)
                if(face.vertices[j]==a&&face.vertices[(j+1)%3]==b)
                    oriented=true;
            if(!oriented)
                throw std::logic_error("Stale retained face orientation");
            auto& splits=boundarySplits[survivingId][{a,b}];
            if(!splits.empty())
                throw std::logic_error("Repeated boundary edge subdivision");
            affectedSurvivors.insert(survivingId);
            for(std::size_t j=1;j<segments;++j) {
                const auto point=p+(q-p)*(static_cast<double>(j)/segments);
                const auto id=edit.appendLocalVertex(
                    baseline->vertex(a).owner,point);
                positions.emplace(id,point);
                output.insertedVertices.push_back(id);
                loop.ids.push_back(id);
                splits.push_back(id);
            }
            ++output.splitBoundaryEdges;
        }
        if(loop.ids.size()>settings.maxContourVertices)
            throw std::length_error("Refined contour exceeds budget");
        loops.push_back(std::move(loop));
    }
    // A single original triangle may border TWO or THREE long contour
    // edges. Construct all their splits together, retaining its first
    // FaceId and appending only the additional oriented triangles.
    for(const auto& [id,segments]:boundarySplits) {
        const auto& face=baseline->face(id);
        const auto parts=subdivideRetainedTriangle(
            face.vertices,segments,positions,settings);
        edit.replaceFace(id,parts.front());
        for(std::size_t i=1;i<parts.size();++i) {
            if(output.insertedFaces.size()>=settings.maxNewFaces)
                throw std::length_error("Survivor subdivision face budget exhausted");
            output.insertedFaces.push_back(
                edit.appendLocalFace(face.owner,parts[i]));
        }
    }
    auto chooseOwner=[&](std::size_t loop)->PartitionId {
        const auto& c=checked.contours[loop];
        return baseline->face(c.survivingFaces.front()).owner;
    };
    const auto insert=[&](PartitionId owner,
                          const std::vector<std::array<VertexId,3>>& triangles,
                          bool isCap) {
        for(const auto& v:triangles) {
            const auto a=positions.at(v[0]),b=positions.at(v[1]),
                       c=positions.at(v[2]);
            if(!finite(a)||!finite(b)||!finite(c)||
               triangleQuality(a,b,c)<settings.minimumTriangleQuality)
                throw std::logic_error("Inserted triangle is degenerate or skinny");
            // A zipper's maximum edges are enforced by the per-pair DP;
            // caps use independent polygon quality checks, not the first
            // loop's unrelated density target.
            if(output.insertedFaces.size()>=settings.maxNewFaces)
                throw std::length_error("Closure face budget exhausted");
            const auto id=edit.appendLocalFace(owner,v);
            output.insertedFaces.push_back(id);
            if(isCap)++output.capFaces;
            else ++output.zipperFaces;
        }
    };
    for(const auto id:checked.cappedContours)
        insert(chooseOwner(id),capTriangles(loops.at(id),positions,settings),true);
    for(const auto& pair:checked.pairs)
        insert(chooseOwner(pair.first),
               zipperTriangles(loops.at(pair.first),loops.at(pair.second),
                               positions,settings),false);

    // Retire only genuinely orphaned old IDs; boundary and unrelated
    // survivor vertices keep their exact original identity and position.
    for(const auto id:selected) {
        bool allDeleted=true;
        for(const auto incident:baseline->vertex(id).incidentFaces)
            if(!deleted.contains(incident)){allDeleted=false;break;}
        if(allDeleted) {
            edit.removeVertex(id);
            output.retiredVertices.push_back(id);
        }
    }
    output.commit=edit.commit();
    output.index=edit.updatedIndex();
    if(!output.index)
        throw std::logic_error("Surgery commit omitted immutable topology index");
    validateSurgeryGeometry(*baseline,checked,selected,settings,output);
    return output;
}

RepairSurgeryResult applyMirroredRepairSurgery(
    std::shared_ptr<const RegionTopologyIndex> baseline,
    const RepairPlan& proposed,
    const std::vector<std::pair<VertexId,VertexId>>& vertexMirrors,
    const RepairSurgerySettings& settings) {
    if(!baseline||!proposed.ready()||vertexMirrors.empty())
        throw std::invalid_argument("Mirrored surgery requires ready plan and mirror pairs");
    if(settings.partitionCapacity==0||settings.maxNewFaces==0||
       settings.maxNewVertices==0||settings.maxContourVertices<3||
       !std::isfinite(settings.maximumBoundaryEdgeFactor)||
       settings.maximumBoundaryEdgeFactor<=1||
       !std::isfinite(settings.maximumNewEdgeFactor)||
       settings.maximumNewEdgeFactor<=1||
       !std::isfinite(settings.minimumTriangleQuality)||
       settings.minimumTriangleQuality<=0||
       settings.minimumTriangleQuality>=1)
        throw std::invalid_argument("Invalid mirrored surgery work/quality limits");
    const auto checked=planRemovalContours(*baseline,proposed.removedFaces,
                                           settings.planning);
    if(!checked.ready()||checked.removedFaces!=proposed.removedFaces||
       checked.cappedContours!=proposed.cappedContours||
       checked.pairs.size()!=proposed.pairs.size()||
       checked.contours.size()!=proposed.contours.size())
        throw std::logic_error("Mirrored surgery plan is stale or incompatible");
    for(std::size_t i=0;i<checked.contours.size();++i)
        if(checked.contours[i].vertexIds!=proposed.contours[i].vertexIds)
            throw std::logic_error("Mirrored surgery contours changed");
    for(std::size_t i=0;i<checked.pairs.size();++i)
        if(checked.pairs[i].first!=proposed.pairs[i].first||
           checked.pairs[i].second!=proposed.pairs[i].second)
            throw std::logic_error("Mirrored surgery pair assignments changed");
    if(checked.removedFaces.empty())
        throw std::logic_error("Empty mirrored repair cannot publish a revision");

    std::unordered_map<VertexId,VertexId> mirrored;
    for(const auto& [a,b]:vertexMirrors) {
        const auto p=baseline->vertex(a).position;
        const auto q=baseline->vertex(b).position;
        if(p.x!=-q.x||p.y!=q.y||p.z!=q.z)
            throw std::logic_error("Input vertex mapping is not an exact X reflection");
        const auto [it,inserted]=mirrored.emplace(a,b);
        if(!inserted&&it->second!=b)
            throw std::logic_error("Conflicting mirrored vertices");
        const auto [jt,added]=mirrored.emplace(b,a);
        if(!added&&jt->second!=a)
            throw std::logic_error("Mirror map is not an involution");
    }
    const auto opposite=[&](VertexId id)->VertexId {
        const auto it=mirrored.find(id);
        if(it==mirrored.end())
            throw std::logic_error("Mirrored repair is missing a vertex mapping");
        return it->second;
    };
    const auto reflectFace=[&](FaceId id)->FaceId {
        const auto v=baseline->face(id).vertices;
        const std::array<VertexId,3> reflected={
            opposite(v[0]),opposite(v[2]),opposite(v[1])};
        FaceId result=0;
        for(const auto candidate:baseline->vertex(reflected[0]).incidentFaces) {
            const auto w=baseline->face(candidate).vertices;
            for(unsigned shift=0;shift<3;++shift)
                if(w[shift]==reflected[0]&&
                   w[(shift+1)%3]==reflected[1]&&
                   w[(shift+2)%3]==reflected[2]) {
                    if(result&&result!=candidate)
                        throw std::logic_error("Multiple matching mirrored faces");
                    result=candidate;
                }
        }
        if(!result||result==id)
            throw std::logic_error("Surgery face has no distinct reflected partner");
        return result;
    };
    const std::set<FaceId> deleted(checked.removedFaces.begin(),
                                    checked.removedFaces.end());
    for(const auto id:deleted)
        if(!deleted.contains(reflectFace(id))||
           reflectFace(reflectFace(id))!=id)
            throw std::logic_error("Mirrored excision is missing a reflected face");

    std::set<VertexId> selected;
    for(const auto id:deleted)
        for(const auto v:baseline->face(id).vertices)selected.insert(v);
    for(const auto& loop:checked.contours)
        for(const auto face:loop.survivingFaces)
            for(const auto v:baseline->face(face).vertices)
                selected.insert(v);
    for(const auto id:selected)
        if(!selected.contains(opposite(id)))
            throw std::logic_error("Mirrored write halo is asymmetric");
    RegionEdit edit(baseline,{selected.begin(),selected.end()},
                    vertexMirrors,settings.partitionCapacity);

    RepairSurgeryResult out;
    out.retiredFaces.assign(deleted.begin(),deleted.end());
    for(const auto id:deleted) {
        const auto partner=reflectFace(id);
        if(id<partner)edit.removeMirroredFaces(id,partner);
    }
    std::unordered_map<VertexId,Vec3> pos;
    for(const auto id:selected)pos.emplace(id,baseline->vertex(id).position);
    // Refine the ORIGINAL contour edges in mirrored pairs, including
    // their adjacent retained faces, before triangulating either closure.
    // Directed additions are recorded for BOTH reflected ring traversals.
    // Opposite mirrored face winding reverses the edge direction.
    // Accumulate ALL boundary subdivisions before changing any retained
    // faces. A face may meet two or even three long boundary sides; its
    // reflected partner must receive the exactly mirrored triangulation.
    using DirectedEdge=std::pair<VertexId,VertexId>;
    std::map<DirectedEdge,std::vector<VertexId>> insertedOnEdge;
    std::map<FaceId,SideSplits> splitsByFace;
    std::vector<double> localTargets;
    for(const auto& contour:checked.contours) {
        const double h=medianEdge(*baseline,contour);
        if(!(h>0)||!std::isfinite(h))
            throw std::logic_error("Nonfinite mirrored boundary target density");
        localTargets.push_back(h);
    }
    for(std::size_t k=0;k<checked.contours.size();++k) {
        const auto& contour=checked.contours[k];
        double h=localTargets[k];
        for(const auto& pair:checked.pairs)
            if(pair.first==k||pair.second==k)
                h=std::min(localTargets[pair.first],
                           localTargets[pair.second]);
        for(std::size_t i=0;i<contour.vertexIds.size();++i) {
            const auto a=contour.vertexIds[i];
            const auto b=contour.vertexIds[(i+1)%contour.vertexIds.size()];
            if(insertedOnEdge.contains({a,b}))continue;
            const auto p=pos.at(a),q=pos.at(b);
            const double L=length(q-p);
            if(L<=settings.maximumBoundaryEdgeFactor*h)continue;
            const auto segments=static_cast<std::size_t>(std::ceil(
                L/(settings.maximumBoundaryEdgeFactor*h)));
            if(segments<=1)continue;
            if(segments>settings.maxNewVertices/2+1||
               out.insertedVertices.size()+2*(segments-1)>
                   settings.maxNewVertices)
                throw std::length_error("Mirrored boundary division exceeds vertex budget");
            const auto source=contour.survivingFaces[i];
            const auto twin=reflectFace(source);
            if(source==twin)
                throw std::logic_error("A self-reflected survivor face cannot be split in pairs");
            const auto face=baseline->face(source);
            bool found=false;
            for(unsigned j=0;j<3;++j)
                if(face.vertices[j]==a&&face.vertices[(j+1)%3]==b)
                    found=true;
            if(!found)
                throw std::logic_error("Mirrored boundary edge lost survivor orientation");
            const auto ownerA=face.owner;
            const auto ownerB=baseline->face(twin).owner;
            std::vector<VertexId> innerA,innerB;
            for(std::size_t j=1;j<segments;++j) {
                const auto point=p+(q-p)*(static_cast<double>(j)/segments);
                if(point.x==0)
                    throw std::logic_error("Boundary crosses X=0: needs self-seam split");
                const auto made=edit.appendMirroredVertices(
                    ownerA,ownerB,point);
                pos.emplace(made.first,point);
                pos.emplace(made.second,Vec3{-point.x,point.y,point.z});
                mirrored.emplace(made.first,made.second);
                mirrored.emplace(made.second,made.first);
                out.insertedVertices.push_back(made.first);
                out.insertedVertices.push_back(made.second);
                innerA.push_back(made.first);
                innerB.push_back(made.second);
            }
            std::reverse(innerB.begin(),innerB.end());
            insertedOnEdge.emplace(DirectedEdge{a,b},innerA);
            insertedOnEdge.emplace(DirectedEdge{opposite(b),opposite(a)},innerB);
            if(!splitsByFace[source].emplace(DirectedEdge{a,b},
                                             std::move(innerA)).second||
               !splitsByFace[twin].emplace(
                   DirectedEdge{opposite(b),opposite(a)},
                   std::move(innerB)).second)
                throw std::logic_error("Mirrored boundary was split twice");
            out.splitBoundaryEdges+=2;
        }
    }
    std::set<FaceId> completed;
    for(const auto& [id,_]:splitsByFace) {
        if(completed.contains(id))continue;
        const auto twin=reflectFace(id);
        if(!splitsByFace.contains(twin))
            throw std::logic_error("Missing mirrored retained face split");
        const auto face=baseline->face(id);
        const auto other=baseline->face(twin);
        const auto parts=subdivideRetainedTriangle(
            face.vertices,splitsByFace.at(id),pos,settings);
        edit.replaceMirroredFaces(id,twin,parts.front());
        for(std::size_t k=1;k<parts.size();++k) {
            if(out.insertedFaces.size()+2>settings.maxNewFaces)
                throw std::length_error("Mirrored subdivision face budget exhausted");
            const auto created=edit.appendMirroredFaces(
                face.owner,other.owner,parts[k]);
            out.insertedFaces.push_back(created.first);
            out.insertedFaces.push_back(created.second);
        }
        completed.insert(id);
        completed.insert(twin);
    }
    std::vector<LocalLoop> loops;
    loops.reserve(checked.contours.size());
    for(std::size_t k=0;k<checked.contours.size();++k) {
        const auto& contour=checked.contours[k];
        LocalLoop loop;
        loop.target=localTargets[k];
        for(const auto& pair:checked.pairs)
            if(pair.first==k||pair.second==k)
                loop.target=std::min(localTargets[pair.first],
                                     localTargets[pair.second]);
        for(std::size_t i=0;i<contour.vertexIds.size();++i) {
            const auto a=contour.vertexIds[i];
            const auto b=contour.vertexIds[(i+1)%contour.vertexIds.size()];
            loop.ids.push_back(a);
            const auto it=insertedOnEdge.find({a,b});
            if(it!=insertedOnEdge.end())
                loop.ids.insert(loop.ids.end(),it->second.begin(),
                                it->second.end());
        }
        if(loop.ids.size()>settings.maxContourVertices)
            throw std::length_error("Refined mirrored contour exceeds budget");
        loops.push_back(std::move(loop));
    }
    // Identify reflection of entire directed rings by their SET of vertex
    // IDs, not their original per-ring vertex indexing or phase.
    std::vector<std::size_t> ringMirror(loops.size(),loops.size());
    for(std::size_t i=0;i<loops.size();++i) {
        std::set<VertexId> image;
        for(const auto id:loops[i].ids)image.insert(opposite(id));
        for(std::size_t j=0;j<loops.size();++j) {
            if(loops[i].ids.size()!=loops[j].ids.size())continue;
            const std::set<VertexId> candidate(loops[j].ids.begin(),
                                                loops[j].ids.end());
            if(candidate==image) {
                if(ringMirror[i]!=loops.size())
                    throw std::logic_error("Duplicated reflected contour ring");
                ringMirror[i]=j;
            }
        }
        if(ringMirror[i]==loops.size())
            throw std::logic_error("No reflected counterpart for contour");
    }
    for(std::size_t i=0;i<loops.size();++i)
        if(ringMirror[ringMirror[i]]!=i)
            throw std::logic_error("Contour reflection is not an involution");

    const auto owner=[&](std::size_t loop)->PartitionId {
        return baseline->face(checked.contours.at(loop).survivingFaces.front()).owner;
    };
    const auto append=[&](PartitionId one,PartitionId other,
                          const std::array<VertexId,3>& face,bool cap) {
        const auto a=pos.at(face[0]),b=pos.at(face[1]),c=pos.at(face[2]);
        if(triangleQuality(a,b,c)<settings.minimumTriangleQuality)
            throw std::logic_error("Mirrored scar would create a skinny triangle");
        if(out.insertedFaces.size()+2>settings.maxNewFaces)
            throw std::length_error("Mirrored face budget exceeded");
        const auto created=edit.appendMirroredFaces(one,other,face);
        out.insertedFaces.push_back(created.first);
        out.insertedFaces.push_back(created.second);
        if(cap)out.capFaces+=2;
        else out.zipperFaces+=2;
    };

    const std::set<std::size_t> caps(checked.cappedContours.begin(),
                                     checked.cappedContours.end());
    std::set<std::size_t> completedCaps;
    for(const auto i:caps) {
        if(completedCaps.contains(i))continue;
        const auto j=ringMirror[i];
        if(!caps.contains(j))
            throw std::logic_error("A mirrored cap ring has no matching cap choice");
        if(j==i) {
            // A self-reflected disk must not be independently filled and
            // mirrored: that duplicates coincident faces. Partition its
            // boundary into the positive-X and negative-X arcs between
            // EXACTLY TWO existing X=0 seam vertices, and triangulate
            // only the positive arc against the common seam chord. Its
            // reflected triangles share that single chord and use no
            // duplicate seam vertices.
            const auto& ring=loops[i].ids;
            std::vector<std::size_t> seamIndices;
            for(std::size_t k=0;k<ring.size();++k)
                if(pos.at(ring[k]).x==0)seamIndices.push_back(k);
            if(seamIndices.size()!=2)
                throw std::logic_error(
                    "Self-mirrored cap requires exactly two existing seam vertices");
            std::vector<VertexId> positive;
            const auto trace=[&](std::size_t from,std::size_t to) {
                std::vector<VertexId> arc;
                for(std::size_t k=from;;k=(k+1)%ring.size()) {
                    arc.push_back(ring[k]);
                    if(k==to)break;
                }
                return arc;
            };
            for(const auto& candidate:{
                trace(seamIndices[0],seamIndices[1]),
                trace(seamIndices[1],seamIndices[0])}) {
                bool side=true;
                for(std::size_t k=1;k+1<candidate.size();++k)
                    if(pos.at(candidate[k]).x<=0)side=false;
                if(side&&candidate.size()>=3) {
                    if(!positive.empty())
                        throw std::logic_error("Self-reflected cap has ambiguous sides");
                    positive=candidate;
                }
            }
            if(positive.empty())
                throw std::logic_error(
                    "Self-reflected cap lacks a contiguous positive-X boundary arc");
            // Verify the opposite original arc is the SAME reflected
            // polygon, not a coincident-but-unrelated component.
            std::set<VertexId> expected;
            for(const auto id:positive)expected.insert(opposite(id));
            const std::set<VertexId> actual(ring.begin(),ring.end());
            for(const auto id:expected)
                if(!actual.contains(id))
                    throw std::logic_error("Self-reflected cap arc is not paired");
            const LocalLoop half{positive,loops[i].target};
            const auto triangles=capTriangles(half,pos,settings);
            for(const auto& face:triangles)
                append(owner(i),owner(i),face,true);
            completedCaps.insert(i);
            continue;
        }
        const auto triangles=capTriangles(loops[i],pos,settings);
        for(const auto& face:triangles)
            append(owner(i),owner(j),face,true);
        completedCaps.insert(i);
        completedCaps.insert(j);
    }

    std::map<std::size_t,std::size_t> zipped;
    for(const auto& pair:checked.pairs) {
        zipped.emplace(pair.first,pair.second);
        zipped.emplace(pair.second,pair.first);
    }
    std::set<std::size_t> completedZippers;
    for(const auto& pair:checked.pairs) {
        const auto a=pair.first,b=pair.second;
        if(completedZippers.contains(a))continue;
        const auto ra=ringMirror[a],rb=ringMirror[b];
        if(zipped.find(ra)==zipped.end()||zipped.at(ra)!=rb)
            throw std::logic_error("A zipper has no reflected zipper assignment");
        if(ra==b&&rb==a) {
            // A zipper whose two contours reflect into one another must
            // create a SINGLE X=0 seam ring. Otherwise independent
            // triangulation of each half would cross the symmetry plane.
            if(loops[a].ids.size()!=loops[b].ids.size())
                throw std::logic_error("Unequal reflected zipper rings need paired refinement");
            std::vector<VertexId> seam;
            seam.reserve(loops[a].ids.size());
            const std::set<VertexId> otherVertices(
                loops[b].ids.begin(),loops[b].ids.end());
            for(const auto id:loops[a].ids) {
                if(!otherVertices.contains(opposite(id)))
                    throw std::logic_error("Reflected zipper boundary vertices differ");
                const auto p=pos.at(id);
                if(p.x==0)
                    throw std::logic_error("Self-mirrored zipper boundary requires a different patch");
                if(out.insertedVertices.size()>=settings.maxNewVertices)
                    throw std::length_error("Symmetry seam vertex budget exceeded");
                const auto s=edit.appendSeamVertex(owner(a),{0,p.y,p.z});
                seam.push_back(s);
                pos.emplace(s,Vec3{0,p.y,p.z});
                out.insertedVertices.push_back(s);
            }
            for(std::size_t i=0;i<seam.size();++i) {
                const auto next=(i+1)%seam.size();
                const auto v=loops[a].ids[i],w=loops[a].ids[next];
                const auto safeStrip=[&](std::array<VertexId,3> triangle) {
                    const auto pa=pos.at(triangle[0]),pb=pos.at(triangle[1]),
                               pc=pos.at(triangle[2]);
                    if(maxEdge(pa,pb,pc)>settings.maximumNewEdgeFactor*
                       std::min(loops[a].target,loops[b].target))
                        throw std::logic_error("Mirrored seam triangle exceeds local edge density");
                    append(owner(a),owner(b),triangle,false);
                };
                safeStrip({w,v,seam[i]});
                safeStrip({w,seam[i],seam[next]});
            }
        }else {
            if(ra==a||rb==b||ra==b||rb==a)
                throw std::logic_error("Partly self-reflected zipper not supported");
            const auto faces=zipperTriangles(loops[a],loops[b],pos,settings);
            for(const auto& face:faces)
                append(owner(a),owner(ra),face,false);
        }
        completedZippers.insert(a);
        completedZippers.insert(b);
        completedZippers.insert(ra);
        completedZippers.insert(rb);
    }
    // Mirror orphan retirement includes any self-mirrored X=0 old vertex.
    std::set<VertexId> retired;
    for(const auto id:selected) {
        if(retired.contains(id))continue;
        bool orphan=true;
        for(const auto face:baseline->vertex(id).incidentFaces)
            if(!deleted.contains(face)){orphan=false;break;}
        if(!orphan)continue;
        const auto twin=opposite(id);
        bool mirrorOrphan=true;
        for(const auto face:baseline->vertex(twin).incidentFaces)
            if(!deleted.contains(face)){mirrorOrphan=false;break;}
        if(!mirrorOrphan)
            throw std::logic_error("Mirrored orphan state differs");
        edit.removeMirroredVertices(id,twin);
        retired.insert(id);
        retired.insert(twin);
    }
    out.retiredVertices.assign(retired.begin(),retired.end());
    out.commit=edit.commit();
    out.index=edit.updatedIndex();
    if(!out.index)
        throw std::logic_error("Missing immutable mirrored commit");
    validateSurgeryGeometry(*baseline,checked,selected,settings,out);
    return out;
}
} // namespace mesher

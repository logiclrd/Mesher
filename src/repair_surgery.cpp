#include "mesher/repair_surgery.hpp"

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
using Edge=RegionTopologyIndex::Edge;
Edge key(VertexId a,VertexId b) {return {std::min(a,b),std::max(a,b)};}
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
    std::set<FaceId> alreadySplit;
    std::set<FaceId> affectedSurvivors;
    std::vector<LocalLoop> loops;
    loops.reserve(checked.contours.size());
    std::vector<double> medians;
    for(const auto& c:checked.contours)
        medians.push_back(medianEdge(*baseline,c));
    for(std::size_t k=0;k<checked.contours.size();++k) {
        const auto& contour=checked.contours[k];
        LocalLoop loop;
        loop.target=medians[k];
        // For a zipper, the finer boundary sets the geometric target.
        for(const auto& pair:checked.pairs)
            if(pair.first==k||pair.second==k)
                loop.target=std::min(medians[pair.first],
                                     medians[pair.second]);
        if(!(loop.target>0)||!std::isfinite(loop.target))
            throw std::logic_error("No finite local boundary editing resolution");
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
            if(!alreadySplit.insert(survivingId).second)
                throw std::logic_error("Two subdivided contour edges share a face");
            const auto face=baseline->face(survivingId);
            affectedSurvivors.insert(survivingId);
            VertexId third=0;
            bool oriented=false;
            for(unsigned j=0;j<3;++j) {
                if(face.vertices[j]==a&&face.vertices[(j+1)%3]==b) {
                    third=face.vertices[(j+2)%3];oriented=true;break;
                }
            }
            if(!oriented)throw std::logic_error("Stale retained face orientation");
            std::vector<VertexId> sequence{a};
            for(std::size_t j=1;j<segments;++j) {
                const auto point=p+(q-p)*(static_cast<double>(j)/segments);
                const auto newVertex=edit.appendLocalVertex(
                    baseline->vertex(a).owner,point);
                positions.emplace(newVertex,point);
                output.insertedVertices.push_back(newVertex);
                loop.ids.push_back(newVertex);
                sequence.push_back(newVertex);
            }
            sequence.push_back(b);
            // Preserve original survivor FaceId for its first subtriangle.
            edit.replaceFace(survivingId,{sequence[0],sequence[1],third});
            for(std::size_t j=1;j<segments;++j) {
                const auto newFace=edit.appendLocalFace(face.owner,
                    {sequence[j],sequence[j+1],third});
                output.insertedFaces.push_back(newFace);
                if(output.insertedFaces.size()>settings.maxNewFaces)
                    throw std::length_error("Subdivision face budget exhausted");
            }
            ++output.splitBoundaryEdges;
        }
        if(loop.ids.size()>settings.maxContourVertices)
            throw std::length_error("Refined ring exceeds contour vertex limit");
        loops.push_back(std::move(loop));
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
    // V13 tests the complete PRIVATE post-state mesh. Any intersections
    // involving generated or modified survivor faces reject publication.
    std::vector<FaceId> geometry=output.insertedFaces;
    geometry.insert(geometry.end(),affectedSurvivors.begin(),
                    affectedSurvivors.end());
    if(!output.index->triangleIntersections({},geometry).empty())
        throw std::logic_error("Reconstructed scar introduces triangle intersections");
    return output;
}
} // namespace mesher

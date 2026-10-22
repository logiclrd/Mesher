#include "mesher/repair_cap.hpp"
#include "mesher/repair_mixed_primitives.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Tri=std::array<VertexId,3>;
using Edge=std::pair<VertexId,VertexId>;
Edge canonical(VertexId a,VertexId b) {
    if(b<a)std::swap(a,b);
    return {a,b};
}
double quality(Vec3 a,Vec3 b,Vec3 c) {
    const double denominator=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return denominator>0?
        2*std::sqrt(3.0)*length(cross(b-a,c-a))/denominator:0;
}
struct Face {
    FaceId original{};
    PartitionId owner{};
    Tri ids{};
    bool cap{};
    double target{};
};
double cross2(Vec3 a,Vec3 b,Vec3 c) {
    return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);
}
double medianEdge(const RegionTopologyIndex& index,const RepairContour& loop) {
    std::vector<double> lengths;
    for(std::size_t i=0;i<loop.vertexIds.size();++i)
        lengths.push_back(length(
            index.vertex(loop.vertexIds[i]).position-
            index.vertex(loop.vertexIds[(i+1)%loop.vertexIds.size()]).position));
    std::sort(lengths.begin(),lengths.end());
    return lengths[lengths.size()/2];
}
struct Projection {
    Vec3 origin{},u{},v{},normal{};
    Vec3 operator()(Vec3 p) const {
        return {dot(p-origin,u),dot(p-origin,v),0};
    }
};
Projection basis(const std::vector<VertexId>& ids,
                 const std::map<VertexId,Vec3>& positions,
                 Vec3 optionalNormal={}) {
    Projection p;
    p.origin=positions.at(ids.front());
    Vec3 area{};
    for(std::size_t i=0;i<ids.size();++i)
        area=area+cross(positions.at(ids[i])-p.origin,
              positions.at(ids[(i+1)%ids.size()])-p.origin);
    if(!std::isfinite(length(area))||length(area)<1e-14)
        throw std::logic_error("R05 contour has vanishing projected area");
    p.normal=length(optionalNormal)>0?normalized(optionalNormal):normalized(area);
    // Preserve the original surviving boundary's orientation: do not
    // silently flip a projected cap to obtain a prettier polygon.
    if(dot(p.normal,area)<=.1*length(area))
        throw std::logic_error("R05 projection normal disagrees with winding");
    const auto axis=std::abs(p.normal.z)<.8?Vec3{0,0,1}:Vec3{0,1,0};
    p.u=normalized(cross(axis,p.normal));
    p.v=cross(p.normal,p.u);
    return p;
}
std::vector<Tri> ears(const RepairContour& contour,
                      const std::map<VertexId,Vec3>& positions,
                      double target) {
    // R02's directed surviving boundary is reversed for cap winding.
    const std::vector<VertexId> basePolygon(contour.vertexIds.rbegin(),
                                          contour.vertexIds.rend());
    const auto original=basis(basePolygon,positions);
    const auto n=original.normal;
    // Exhaustive finite retry on a few rotations of the *projection*
    // plane. Reprojecting preserves the original 3D vertices and contour
    // winding, but can unwrap a genuinely simple folded space contour
    // whose Newell projection overlaps itself.
    std::vector<Vec3> directions{n};
    for(const auto axis:std::array<Vec3,3>{{{1,0,0},{0,1,0},{0,0,1}}}) {
        const auto positive=dot(axis,n)>=0?axis:axis*-1;
        directions.push_back(normalized(n+positive));
        directions.push_back(normalized(n+positive*.5));
        directions.push_back(normalized(n+positive*2));
        directions.push_back(positive);
    }
    std::string lastReason="No supported projected cap";
    for(const auto direction:directions) {
        try {
    std::vector<VertexId> polygon=basePolygon;
    const auto project=basis(polygon,positions,direction);
    std::map<VertexId,Vec3> xy;
    for(const auto id:polygon)
        xy.emplace(id,project(positions.at(id)));
    const double epsilon=1e-12*target*target;
    const double tolerance=1e-12*target;
    const auto onSegment=[&](Vec3 a,Vec3 b,Vec3 p) {
        return std::abs(cross2(a,b,p))<=epsilon&&
               p.x>=std::min(a.x,b.x)-tolerance&&
               p.x<=std::max(a.x,b.x)+tolerance&&
               p.y>=std::min(a.y,b.y)-tolerance&&
               p.y<=std::max(a.y,b.y)+tolerance;
    };
    const auto overlap=[&](Vec3 a,Vec3 b,Vec3 p,Vec3 q) {
        const double u=cross2(a,b,p),v=cross2(a,b,q);
        const double s=cross2(p,q,a),t=cross2(p,q,b);
        if(((u>epsilon&&v< -epsilon)||(u< -epsilon&&v>epsilon))&&
           ((s>epsilon&&t< -epsilon)||(s< -epsilon&&t>epsilon)))
            return true;
        return onSegment(a,b,p)||onSegment(a,b,q)||
               onSegment(p,q,a)||onSegment(p,q,b);
    };
    const auto count=polygon.size();
    for(std::size_t i=0;i<count;++i)
        for(std::size_t j=i+1;j<count;++j) {
            if(j==i+1||(i==0&&j+1==count))continue;
            if(overlap(xy.at(polygon[i]),xy.at(polygon[(i+1)%count]),
                       xy.at(polygon[j]),xy.at(polygon[(j+1)%count])))
                throw std::logic_error(
                    "R05 contour projection overlaps itself; unsuitable cap");
        }
    std::vector<Tri> output;
    while(polygon.size()>3) {
        std::size_t best=polygon.size();
        double bestQuality=-1;
        for(std::size_t i=0;i<polygon.size();++i) {
            const auto a=polygon[(i+polygon.size()-1)%polygon.size()];
            const auto b=polygon[i],c=polygon[(i+1)%polygon.size()];
            if(cross2(xy.at(a),xy.at(b),xy.at(c))<=epsilon)continue;
            bool contains=false;
            for(const auto p:polygon) {
                if(p==a||p==b||p==c)continue;
                if(cross2(xy.at(a),xy.at(b),xy.at(p))>=-epsilon&&
                   cross2(xy.at(b),xy.at(c),xy.at(p))>=-epsilon&&
                   cross2(xy.at(c),xy.at(a),xy.at(p))>=-epsilon) {
                    contains=true;break;
                }
            }
            if(contains)continue;
            const auto q=quality(positions.at(a),positions.at(b),
                                 positions.at(c));
            if(q>bestQuality){best=i;bestQuality=q;}
        }
        if(best==polygon.size())
            throw std::logic_error("R05 no admissible ear in projected cap");
        output.push_back({polygon[(best+polygon.size()-1)%polygon.size()],
                          polygon[best],polygon[(best+1)%polygon.size()]});
        polygon.erase(polygon.begin()+static_cast<std::ptrdiff_t>(best));
    }
    if(cross2(xy.at(polygon[0]),xy.at(polygon[1]),xy.at(polygon[2]))<=epsilon)
        throw std::logic_error("R05 final cap ear has degenerate orientation");
    output.push_back({polygon[0],polygon[1],polygon[2]});
    return output;

        }catch(const std::logic_error& e) {
            lastReason=e.what();
        }
    }
    throw std::logic_error(
        "R05 no simple admissible projected disk after bounded orientation search: "+
        lastReason);
}
} // namespace


std::vector<PrivateRepairTriangle> triangulatePrivateCapDisk(
    const RepairContour& contour,
    const std::map<VertexId,Vec3>& positions,
    double localEdgeLength) {
    return ears(contour,positions,localEdgeLength);
}

ForcedCapResult applyForcedPrivateCap(
    const PrivateExcisionResult& cut,
    const RepairExcisionPreview& proposal,
    const ForcedCapSettings& limits) {
    if(!cut.index||!proposal.complete||
       cut.boundaryEdges!=proposal.boundaryEdges||
       cut.boundaryLoops!=proposal.contours.size())
        throw std::logic_error("R05 requires the exact completed R03 open cut");
    if(proposal.contours.empty()||proposal.contours.size()>limits.maxLoops)
        throw std::logic_error("R05 requires at least one bounded open contour");
    if(!limits.maxInsertedVertices||!limits.maxNewFaces||
       !limits.maxRefinementSteps||!limits.maxAuditedFaces||
       !std::isfinite(limits.maxCapEdgeFactor)||
       !std::isfinite(limits.minCapQuality)||
       limits.maxCapEdgeFactor<=1||limits.minCapQuality<=0||
       limits.minCapQuality>=1)
        throw std::invalid_argument("R05 requires positive finite cap quality budgets");
    ForcedCapResult output;
    auto view=cut.index->revision().createMutableView();
    std::vector<Face> faces;
    std::set<FaceId> oldFaces;
    std::set<PartitionId> owners;
    std::map<VertexId,Vec3> positions;
    std::map<VertexId,PartitionId> vertexOwners;
    const auto loadVertex=[&](VertexId id) {
        const auto& v=cut.index->vertex(id);
        positions.emplace(id,v.position);
        vertexOwners.emplace(id,v.owner);
    };
    for(const auto& contour:proposal.contours) {
        if(contour.vertexIds.size()<3||
           contour.vertexIds.size()>limits.maxContourVertices)
            throw std::length_error("R05 bounded cap contour size exceeded");
        const double scale=medianEdge(*cut.index,contour);
        if(!(scale>0)||!std::isfinite(scale))
            throw std::logic_error("R05 cap contour has nonfinite local edge length");
        for(const auto id:contour.vertexIds)loadVertex(id);
        for(const auto id:contour.survivingFaces) {
            if(!oldFaces.insert(id).second)continue;
            const auto& f=cut.index->face(id);
            faces.push_back({id,f.owner,f.vertices,false,scale});
            for(const auto vertex:f.vertices)loadVertex(vertex);
        }
        auto tris=ears(contour,positions,scale);
        const auto owner=cut.index->vertex(contour.vertexIds.front()).owner;
        for(const auto& triangle:tris)
            faces.push_back({0,owner,triangle,true,scale});
        ++output.cappedLoops;
    }
    const auto edgeLength=[&](Edge e) {
        return length(positions.at(e.first)-positions.at(e.second));
    };
    const auto incident=[&](Edge e) {
        std::vector<std::size_t> result;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& v=faces[i].ids;
            for(unsigned k=0;k<3;++k) {
                if(canonical(v[k],v[(k+1)%3])==e) {
                    result.push_back(i);break;
                }
            }
        }
        return result;
    };
    const auto splitScore=[&](const Face& f,Edge e,Vec3 p) {
        const auto& v=f.ids;
        for(unsigned k=0;k<3;++k) {
            const auto a=v[k],b=v[(k+1)%3],c=v[(k+2)%3];
            if(canonical(a,b)!=e)continue;
            return std::min(quality(positions.at(a),p,positions.at(c)),
                            quality(p,positions.at(b),positions.at(c)));
        }
        throw std::logic_error("R05 edge has no parent triangle");
    };
    const auto insertVertex=[&](PartitionId owner,Vec3 p) {
        if(output.insertedVertices.size()>=limits.maxInsertedVertices)
            throw std::length_error("R05 cap inserted-vertex budget exceeded");
        const auto id=view.addVertex(owner,p);
        positions.emplace(id,p);
        vertexOwners.emplace(id,owner);
        owners.insert(owner);
        output.insertedVertices.push_back(id);
        return id;
    };
    for(std::size_t step=0;step<limits.maxRefinementSteps;++step) {
        Edge worst{};
        double largestRatio=1;
        std::size_t lowestIndex=faces.size();
        double lowestQuality=limits.minCapQuality;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& f=faces[i];
            if(!f.cap)continue;
            const auto q=quality(positions.at(f.ids[0]),
                                 positions.at(f.ids[1]),
                                 positions.at(f.ids[2]));
            if(q<lowestQuality){lowestQuality=q;lowestIndex=i;}
            for(unsigned k=0;k<3;++k) {
                const auto e=canonical(f.ids[k],f.ids[(k+1)%3]);
                const auto ratio=edgeLength(e)/(f.target*limits.maxCapEdgeFactor);
                if(ratio>largestRatio){largestRatio=ratio;worst=e;}
            }
        }
        if(largestRatio<=1&&lowestIndex==faces.size())break;
        if(largestRatio<=1) {
            const auto& v=faces[lowestIndex].ids;
            double bestScore=-1;
            for(unsigned k=0;k<3;++k) {
                const auto e=canonical(v[k],v[(k+1)%3]);
                const auto adjacent=incident(e);
                if(adjacent.size()!=2)continue;
                const auto p=(positions.at(e.first)+positions.at(e.second))*.5;
                double score=1;
                for(const auto index:adjacent)
                    score=std::min(score,splitScore(faces[index],e,p));
                score+=1e-7*edgeLength(e)/faces[lowestIndex].target;
                if(score>bestScore){bestScore=score;worst=e;}
            }
            if(bestScore<0)throw std::logic_error(
                "R05 cap has no two-sided edge to improve bad triangle");
        }
        const auto adjacent=incident(worst);
        if(adjacent.size()!=2)throw std::logic_error(
            "R05 cap refinement requires exactly two tracked sides (no T-junction)");
        const auto p=(positions.at(worst.first)+positions.at(worst.second))*.5;
        const auto mid=insertVertex(vertexOwners.at(worst.first),p);
        for(const auto index:adjacent) {
            const auto source=faces[index];
            bool found=false;
            for(unsigned k=0;k<3;++k) {
                const auto a=source.ids[k],b=source.ids[(k+1)%3],
                           c=source.ids[(k+2)%3];
                if(canonical(a,b)!=worst)continue;
                faces[index].ids={a,mid,c};
                faces.push_back({0,source.owner,{mid,b,c},source.cap,source.target});
                found=true;break;
            }
            if(!found)throw std::logic_error("R05 tracked cap edge split lost an incident triangle");
        }
        ++output.refinementSteps;
        if(faces.size()>limits.maxNewFaces+limits.maxAuditedFaces)
            throw std::length_error("R05 face tracking exceeded finite budget");
    }
    for(const auto& face:faces) {
        const auto& v=face.ids;
        const double q=quality(positions.at(v[0]),positions.at(v[1]),
                               positions.at(v[2]));
        if(face.cap) {
            const auto longest=std::max({edgeLength(canonical(v[0],v[1])),
                 edgeLength(canonical(v[1],v[2])),
                 edgeLength(canonical(v[2],v[0]))});
            if(q<limits.minCapQuality||
               longest>face.target*limits.maxCapEdgeFactor*(1+1e-8))
                throw std::logic_error(
                    "R05 unsuitable cap after bounded subdivision:"
                    " quality="+std::to_string(q)+
                    " required="+std::to_string(limits.minCapQuality)+
                    " max_edge_ratio="+std::to_string(longest/(face.target*limits.maxCapEdgeFactor))+
                    " subdivisions="+std::to_string(output.refinementSteps));
            output.minimumCapQuality=std::min(output.minimumCapQuality,q);
        }else if(q<limits.minSurvivorQuality)
            throw std::logic_error("R05 cap subdivision left a skinny survivor");
    }
    for(const auto& face:faces) {
        owners.insert(face.owner);
        if(face.original) {
            auto& partition=view.editPartition(face.owner);
            const auto it=std::find_if(partition.faces.begin(),partition.faces.end(),
               [&](const PartitionFace& item){return item.id==face.original;});
            if(it==partition.faces.end())
                throw std::logic_error("R05 existing stable face missing in private COW");
            it->vertices=face.ids;
        }else {
            if(output.totalNewFaces>=limits.maxNewFaces)
                throw std::length_error("R05 cap new-face budget exceeded");
            const auto id=view.addFace(face.owner,face.ids);
            if(face.cap)output.greenFaces.push_back(id);
            ++output.totalNewFaces;
        }
    }
    output.touchedPartitions.assign(owners.begin(),owners.end());
    auto revision=view.commit().revision;
    auto closed=RegionTopologyIndex::restore(revision,
        cut.index->retiredVertexIds(),cut.index->retiredFaceIds());
    validatePrivateClosedManifold(*closed,limits.maxAuditedFaces);
    for(const auto id:proposal.proposedFaces)
        if(closed->maybeFace(id)||!closed->retiredFace(id))
            throw std::logic_error("R05 resurrected an R03 deleted face");
    output.index=std::move(closed);
    return output;
}
} // namespace mesher

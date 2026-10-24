#include "mesher/repair_seam_audit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <map>
#include <set>
#include <tuple>

namespace mesher {
namespace {
using Edge=std::pair<VertexId,VertexId>;
Edge key(VertexId a,VertexId b){return {std::min(a,b),std::max(a,b)};}
struct Side {FaceId face{};bool generated=false;bool forward=false;};
struct Segment {Edge edge{};Vec3 a{},b{};};
bool overlaps(const Segment& a,const Segment& b,double epsilon) {
    for(int axis=0;axis<3;++axis) {
        const auto at=[axis](Vec3 p){
            return axis==0?p.x:axis==1?p.y:p.z;
        };
        if(std::max(at(a.a),at(a.b))+epsilon<
           std::min(at(b.a),at(b.b))||
           std::max(at(b.a),at(b.b))+epsilon<
           std::min(at(a.a),at(a.b)))return false;
    }
    return true;
}
bool nearPointOnInteriorSegment(
    Vec3 p,const Segment& segment,double tolerance,double& distance) {
    const Vec3 direction=segment.b-segment.a;
    const auto lengthSquared=dot(direction,direction);
    if(lengthSquared<=1e-30)return false;
    const double t=dot(p-segment.a,direction)/lengthSquared;
    if(t<=1e-7||t>=1.-1e-7)return false;
    distance=length(p-(segment.a+direction*t));
    return distance<=tolerance;
}
bool crossing(
    const Segment& a,const Segment& b,double tolerance,
    Vec3& midpoint,double& distance) {
    if(a.edge.first==b.edge.first||a.edge.first==b.edge.second||
       a.edge.second==b.edge.first||a.edge.second==b.edge.second)
        return false;
    if(!overlaps(a,b,tolerance))return false;
    const Vec3 u=a.b-a.a,v=b.b-b.a,w=a.a-b.a;
    const double aa=dot(u,u),bb=dot(u,v),cc=dot(v,v);
    const double dd=dot(u,w),ee=dot(v,w);
    const double determinant=aa*cc-bb*bb;
    // Parallel and coincident segments are handled by interior point
    // and exact duplicate checks; don't invent an intersection.
    if(aa<1e-30||cc<1e-30||determinant<=1e-18*aa*cc)
        return false;
    const double s=(bb*ee-cc*dd)/determinant;
    const double t=(aa*ee-bb*dd)/determinant;
    if(s<=1e-7||s>=1-1e-7||t<=1e-7||t>=1-1e-7)
        return false;
    const Vec3 p=a.a+u*s,q=b.a+v*t;
    distance=length(p-q);
    midpoint=(p+q)*.5;
    return distance<=tolerance;
}
using Point=std::tuple<double,double,double>;
Point pointKey(Vec3 p) {
    return {p.x==0?0:p.x,p.y==0?0:p.y,p.z==0?0:p.z};
}
} // namespace

RepairSeamAudit auditPrivateRepairSeam(
    const RegionTopologyIndex& mesh,const std::vector<FaceId>& generatedFaces,
    RepairSeamAuditLimits limits) {
    RepairSeamAudit result;
    if(!limits.maxFaceEdges||!limits.maxGeometricPairs||
       !limits.maxStoredIssues||!std::isfinite(limits.relativeTolerance)||
       limits.relativeTolerance<=0)
        throw std::invalid_argument("R06 seam audit needs positive finite budgets/tolerance");
    std::set<FaceId> generated(generatedFaces.begin(),generatedFaces.end());
    std::map<Edge,std::vector<Side>> edges;
    std::map<VertexId,Vec3> positions;
    std::set<VertexId> greenVertices,grayVertices;
    std::size_t faceEdges=0;
    mesh.revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& v:part.vertices)
                positions.emplace(v.id,v.position);
            for(const auto& f:part.faces) {
                const bool inserted=generated.contains(f.id);
                for(const auto id:f.vertices)
                    (inserted?greenVertices:grayVertices).insert(id);
                for(unsigned k=0;k<3;++k) {
                    if(++faceEdges>limits.maxFaceEdges) {
                        result.complete=false;
                        return;
                    }
                    const auto a=f.vertices[k],b=f.vertices[(k+1)%3];
                    const auto e=key(a,b);
                    edges[e].push_back({f.id,inserted,a==e.first});
                }
            }
        });
    if(faceEdges>limits.maxFaceEdges) {
        result.complete=false;
        return result;
    }
    const auto record=[&](RepairSeamIssue item) {
        ++result.totalIssues;
        if(result.issues.size()<limits.maxStoredIssues)
            result.issues.push_back(std::move(item));
        else result.issueListTruncated=true;
    };
    std::vector<Segment> generatedEdges,survivorEdges;
    generatedEdges.reserve(generatedFaces.size()*2);
    survivorEdges.reserve(edges.size());
    for(const auto& [edge,sides]:edges) {
        std::size_t g=0,gray=0;
        Side newSide{},oldSide{};
        for(const auto& side:sides) {
            if(side.generated){++g;newSide=side;}
            else {++gray;oldSide=side;}
        }
        if(g)generatedEdges.push_back({edge,positions.at(edge.first),
                                          positions.at(edge.second)});
        if(gray)survivorEdges.push_back({edge,positions.at(edge.first),
                                            positions.at(edge.second)});
        if(g&&!gray&&sides.size()!=2) {
            ++result.interfaceIncidenceErrors;
            record({"OPEN_OR_NONMANIFOLD_NEW_EDGE",0,0,
                    edge.first,edge.second,0,0,
                    positions.at(edge.first),0});
        }
        if(g&&gray) {
            if(g!=1||gray!=1||sides.size()!=2) {
                ++result.interfaceIncidenceErrors;
                record({"MISMATCHED_INTERFACE_INCIDENCE",0,0,
                        edge.first,edge.second,edge.first,edge.second,
                        positions.at(edge.first),0});
            }
            if(newSide.forward==oldSide.forward) {
                ++result.reversedOrientationErrors;
                record({"SAME_DIRECTION_INTERFACE",0,0,
                        edge.first,edge.second,edge.first,edge.second,
                        positions.at(edge.first),0});
            }
            result.interfaceEdges.push_back({
                edge.first,edge.second,newSide.face,oldSide.face,
                newSide.forward,oldSide.forward,
                g==1&&gray==1&&sides.size()==2});
        }
    }
    // Deterministic, explicit finite proximity tolerance relative to the
    // overall observed mesh; never infer topology from an epsilon match.
    Vec3 low{std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::infinity()};
    Vec3 high{-low.x,-low.y,-low.z};
    for(const auto& [id,p]:positions) {
        low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);
        low.z=std::min(low.z,p.z);
        high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);
        high.z=std::max(high.z,p.z);
    }
    const double tolerance=std::max(1e-12,
        length(high-low)*limits.relativeTolerance);
    // Exactly collocated, DIFFERENT stable IDs are not sewn topology.
    std::map<Point,std::vector<VertexId>> grayAt;
    for(const auto id:grayVertices)
        grayAt[pointKey(positions.at(id))].push_back(id);
    for(const auto id:greenVertices) {
        const auto it=grayAt.find(pointKey(positions.at(id)));
        if(it==grayAt.end())continue;
        for(const auto other:it->second)
            if(other!=id) {
                ++result.disconnectedCoincidentVertices;
                record({"COINCIDENT_DIFFERENT_IDS",id,other,
                        0,0,0,0,positions.at(id),0});
            }
    }
    // A compact bbox cuts the full 5K V1 scene down to local survivor
    // edges without dropping geometrically overlapping seam candidates.
    Vec3 capLow=low,capHigh=high;
    if(!greenVertices.empty()) {
        capLow={std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity()};
        capHigh={-capLow.x,-capLow.y,-capLow.z};
        for(const auto id:greenVertices) {
            const auto p=positions.at(id);
            capLow.x=std::min(capLow.x,p.x);capLow.y=std::min(capLow.y,p.y);
            capLow.z=std::min(capLow.z,p.z);
            capHigh.x=std::max(capHigh.x,p.x);capHigh.y=std::max(capHigh.y,p.y);
            capHigh.z=std::max(capHigh.z,p.z);
        }
    }
    const double margin=std::max(tolerance*10,
        .01*length(capHigh-capLow));
    const auto inZone=[&](Vec3 p){
        return p.x>=capLow.x-margin&&p.x<=capHigh.x+margin&&
               p.y>=capLow.y-margin&&p.y<=capHigh.y+margin&&
               p.z>=capLow.z-margin&&p.z<=capHigh.z+margin;
    };
    std::vector<Segment> nearbyGray;
    for(const auto& e:survivorEdges)
        if(inZone(e.a)||inZone(e.b)||
           // A long edge may pass through the cap bbox with neither
           // endpoint inside it; AABB overlap handles that case.
           !(std::max(e.a.x,e.b.x)<capLow.x-margin||
             std::min(e.a.x,e.b.x)>capHigh.x+margin||
             std::max(e.a.y,e.b.y)<capLow.y-margin||
             std::min(e.a.y,e.b.y)>capHigh.y+margin||
             std::max(e.a.z,e.b.z)<capLow.z-margin||
             std::min(e.a.z,e.b.z)>capHigh.z+margin))
            nearbyGray.push_back(e);
    const auto allowance=[&]() {
        if(++result.examinedGeometricPairs<=limits.maxGeometricPairs)
            return true;
        result.complete=false;
        return false;
    };
    // Point-on-edge tests detect T junctions missed by purely indexed
    // edge-incidence audits. Only interior points count as a T.
    for(const auto id:grayVertices) {
        const auto p=positions.at(id);
        if(!inZone(p))continue;
        for(const auto& e:generatedEdges) {
            if(!allowance())return result;
            if(id==e.edge.first||id==e.edge.second)continue;
            double distance=0;
            if(nearPointOnInteriorSegment(p,e,tolerance,distance)) {
                ++result.tJunctionCount;
                record({"SURVIVOR_VERTEX_INSIDE_NEW_EDGE",id,0,
                        e.edge.first,e.edge.second,0,0,p,distance});
            }
        }
    }
    for(const auto id:greenVertices) {
        const auto p=positions.at(id);
        for(const auto& e:nearbyGray) {
            if(!allowance())return result;
            if(id==e.edge.first||id==e.edge.second)continue;
            double distance=0;
            if(nearPointOnInteriorSegment(p,e,tolerance,distance)) {
                ++result.tJunctionCount;
                record({"NEW_VERTEX_INSIDE_SURVIVOR_EDGE",id,0,
                        0,0,e.edge.first,e.edge.second,p,distance});
            }
        }
    }
    // Edge/edge intersections are indicative, not an exhaustive proof
    // about triangle interiors. Report completeness of THIS bounded test.
    for(const auto& g:generatedEdges)
        for(const auto& s:nearbyGray) {
            if(!allowance())return result;
            ++result.candidateGeometricPairs;
            Vec3 point{};double distance=0;
            if(crossing(g,s,tolerance,point,distance)) {
                ++result.crossingEdgePairs;
                record({"UNSHARED_NEW_SURVIVOR_EDGE_CROSSING",0,0,
                        g.edge.first,g.edge.second,
                        s.edge.first,s.edge.second,point,distance});
            }
        }
    return result;
}

void printRepairSeamAudit(
    std::ostream& output,const RegionTopologyIndex& mesh,
    const RepairSeamAudit& result,const char* mode) {
    output<<"[repair-r06-seam] SUMMARY"
          <<" mode="<<mode
          <<" complete="<<result.complete
          <<" shared_interface_edges="<<result.interfaceEdges.size()
          <<" bad_incidence="<<result.interfaceIncidenceErrors
          <<" bad_orientation="<<result.reversedOrientationErrors
          <<" geometric_t_junctions="<<result.tJunctionCount
          <<" disconnected_coincident_vertices="<<result.disconnectedCoincidentVertices
          <<" new_survivor_crossing_edges="<<result.crossingEdgePairs
          <<" geometric_pairs_checked="<<result.examinedGeometricPairs
          <<" stored_issues="<<result.issues.size()
          <<" total_issues="<<result.totalIssues
          <<" issues_truncated="<<result.issueListTruncated
          <<" full_triangle_collision_certificate=0"
          <<" unpublished=1\n";
    for(const auto& e:result.interfaceEdges) {
        const auto a=mesh.vertex(e.a).position,b=mesh.vertex(e.b).position;
        output<<std::setprecision(17)
              <<"[repair-r06-seam] EDGE a="<<e.a
              <<" xyz_a="<<a.x<<","<<a.y<<","<<a.z
              <<" b="<<e.b
              <<" xyz_b="<<b.x<<","<<b.y<<","<<b.z
              <<" new_face="<<e.generatedFace
              <<" survivor_face="<<e.survivorFace
              <<" new_forward="<<e.generatedForward
              <<" survivor_forward="<<e.survivorForward
              <<" topological_match="
              <<(e.exactIncidence&&e.generatedForward!=e.survivorForward)<<"\n";
    }
    for(const auto& issue:result.issues)
        output<<std::setprecision(17)
              <<"[repair-r06-seam] ISSUE kind="<<issue.kind
              <<" vertex="<<issue.vertex
              <<" other_vertex="<<issue.otherVertex
              <<" new_edge="<<issue.generatedA<<","<<issue.generatedB
              <<" survivor_edge="<<issue.survivorA<<","<<issue.survivorB
              <<" xyz="<<issue.where.x<<","<<issue.where.y<<","<<issue.where.z
              <<" separation="<<issue.distance<<"\n";
}
} // namespace mesher

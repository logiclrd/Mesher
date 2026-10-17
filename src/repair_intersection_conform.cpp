#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=RegionTopologyIndex::Edge;
using Tri=std::array<std::size_t,3>;
Edge canonical(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
double dist2(Vec3 a,Vec3 b) {const auto d=a-b;return dot(d,d);}
std::vector<Tri> splitBoundaryFace(
    Tri original,
    const std::map<std::pair<std::size_t,std::size_t>,
                   std::vector<std::size_t>>& splitSides) {
    std::vector<Tri> triangles{original};
    for(unsigned side=0;side<3;++side) {
        const auto from=original[side],to=original[(side+1)%3];
        const auto found=splitSides.find({from,to});
        if(found==splitSides.end())continue;
        auto previous=from;
        for(const auto inserted:found->second) {
            bool matched=false;
            for(std::size_t i=0;i<triangles.size()&&!matched;++i) {
                const auto tri=triangles[i];
                for(unsigned j=0;j<3;++j)
                    if(tri[j]==previous&&tri[(j+1)%3]==to) {
                        const auto opposite=tri[(j+2)%3];
                        triangles[i]={previous,inserted,opposite};
                        triangles.push_back({inserted,to,opposite});
                        matched=true;
                        break;
                    }
            }
            if(!matched)return {}; // A non-conforming shared edge
            previous=inserted;
        }
    }
    return triangles;
}
bool areaPreserved(const RegionTopologyIndex& index,FaceId id,
                   const std::vector<Tri>& triangles,
                   const std::vector<ConformingCutPoint>& points) {
    if(triangles.empty())return false;
    const auto original=index.face(id).vertices;
    const auto a=index.vertex(original[0]).position,
               b=index.vertex(original[1]).position,
               c=index.vertex(original[2]).position;
    const auto normal=cross(b-a,c-a);
    const auto area=length(normal);
    if(!(area>0)||!std::isfinite(area))return false;
    double sum=0;
    for(const auto t:triangles) {
        if(t[0]>=points.size()||t[1]>=points.size()||
           t[2]>=points.size()||
           t[0]==t[1]||t[1]==t[2]||t[0]==t[2])return false;
        const auto p=points[t[0]].position,
                   q=points[t[1]].position,
                   r=points[t[2]].position;
        const auto twice=dot(cross(q-p,r-p),normal)/area;
        if(!(twice>1e-12*area)||!std::isfinite(twice))return false;
        sum+=twice;
    }
    return std::abs(sum-area)<=1e-8*area;
}
std::array<VertexId,3> sortedFaceIds(
    const RegionTopologyIndex& index,FaceId face) {
    auto ids=index.face(face).vertices;
    std::sort(ids.begin(),ids.end());
    return ids;
}
} // namespace

ConformingCutPlan buildConformingCutPlan(
    const RegionTopologyIndex& index,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ConformingCutLimits& limits,
    const std::unordered_map<VertexId,VertexId>* mirrors) {
    if(contacts.size()>limits.maxContacts)
        throw std::length_error("Conforming cut contact budget exhausted");
    ConformingCutPlan empty;
    if(contacts.empty())return empty;
    const auto refusal=[&](const char* why) {
        ConformingCutPlan failed;
        failed.status=IntersectionSectionStatus::UnsupportedContact;
        failed.refusal=why;
        return failed; // no partially constructed triangles or vertices
    };
    ConformingCutPlan result;
    std::map<VertexId,std::size_t> originals;
    std::map<Edge,std::vector<std::pair<double,std::size_t>>> cuts;
    std::map<FaceId,std::vector<Tri>> sectionFaces;
    std::map<FaceId,std::array<std::size_t,3>> faceCorners;
    std::set<FaceId> affected,sectionParticipants;
    const auto newPoint=[&](ConformingCutPoint p) {
        if(result.points.size()-originals.size()>=limits.maxNewPoints)
            throw std::length_error("Conforming cut vertex budget exhausted");
        result.points.push_back(std::move(p));
        return result.points.size()-1;
    };
    const auto existing=[&](VertexId id) {
        const auto found=originals.find(id);
        if(found!=originals.end())return found->second;
        const auto next=result.points.size();
        result.points.push_back({
            index.vertex(id).position,id,std::nullopt,0});
        originals.emplace(id,next);
        return next;
    };
    const auto markFace=[&](FaceId id) {
        affected.insert(id);
        if(affected.size()>limits.maxAffectedFaces)
            throw std::length_error("Conforming cut affected-face budget exhausted");
    };
    for(const auto& contact:contacts) {
        if(contact.ambiguous||contact.first==contact.second||
           !index.maybeFace(contact.first)||!index.maybeFace(contact.second))
            return refusal("Ambiguous or retired contact source");
        if(!sectionParticipants.insert(contact.first).second||
           !sectionParticipants.insert(contact.second).second)
            return refusal("Multiple contact sections in one source face unsupported");
        const std::array<FaceId,2> faceIds{contact.first,contact.second};
        const auto source=[&](FaceId id) {
            const auto ids=index.face(id).vertices;
            return std::array<Vec3,3>{
                index.vertex(ids[0]).position,index.vertex(ids[1]).position,
                index.vertex(ids[2]).position};
        };
        const auto one=index.face(faceIds[0]).vertices,
                   two=index.face(faceIds[1]).vertices;
        const auto section=splitTransverseTriangleContact(
            source(faceIds[0]),one,source(faceIds[1]),two);
        if(section.status!=IntersectionSectionStatus::TransverseSegment)
            return refusal("Contact has no certifiable transverse section");
        ConformingCutSegment match;
        match.firstFace=faceIds[0];match.secondFace=faceIds[1];
        for(unsigned half=0;half<2;++half) {
            const auto faceId=faceIds[half];
            const auto corners=index.face(faceId).vertices;
            markFace(faceId);
            auto& faceNodes=faceCorners[faceId];
            for(unsigned j=0;j<3;++j)faceNodes[j]=existing(corners[j]);
            std::array<std::size_t,5> local{
                faceNodes[0],faceNodes[1],faceNodes[2],0,0};
            for(unsigned k=0;k<2;++k) {
                const auto p=section.endpoints[k];
                const auto bary=section.patches[half].endpointBarycentrics[k];
                int opposite=-1;
                for(unsigned j=0;j<3;++j)
                    if(bary[j]==0) {
                        if(opposite>=0)return refusal("Vertex grazing cannot be a new cut");
                        opposite=static_cast<int>(j);
                    }
                if(opposite<0) {
                    local[3+k]=newPoint({p,std::nullopt,std::nullopt,faceId});
                    continue;
                }
                const auto edge=canonical(
                    corners[(opposite+1)%3],corners[(opposite+2)%3]);
                const auto low=index.vertex(edge.a).position,
                           high=index.vertex(edge.b).position;
                const auto direction=high-low;
                const auto lengthSq=dot(direction,direction);
                if(!(lengthSq>0))return refusal("Degenerate original edge");
                const auto t=dot(p-low,direction)/lengthSq;
                if(!(t>1e-9&&t<1-1e-9))
                    return refusal("Cut touches an original vertex");
                auto& edgeCuts=cuts[edge];
                std::size_t point=0;
                bool reused=false;
                for(const auto& [prior,ref]:edgeCuts) {
                    if(std::abs(prior-t)>1e-9)continue;
                    if(dist2(result.points[ref].position,p)>
                       1e-16*lengthSq)
                        return refusal("Conflicting points on one original edge");
                    point=ref;
                    reused=true;
                    break;
                }
                if(!reused) {
                    point=newPoint({p,std::nullopt,edge,0});
                    edgeCuts.emplace_back(t,point);
                }
                local[3+k]=point;
            }
            if(half==0)match.firstSide={local[3],local[4]};
            else match.secondSide={local[3],local[4]};
            auto& tessellation=sectionFaces[faceId];
            for(const auto face:section.patches[half].triangles)
                tessellation.push_back({
                    local[face[0]],local[face[1]],local[face[2]]});
        }
        result.segments.push_back(match);
    }
    std::map<Edge,std::vector<FaceId>> edgeOwners;
    for(auto& [edge,points]:cuts) {
        std::sort(points.begin(),points.end());
        for(std::size_t i=1;i<points.size();++i)
            if(points[i].first-points[i-1].first<=1e-9)
                return refusal("Unresolved close cuts on shared edge");
        const auto incident=index.edgeSides(edge);
        if(incident.empty()||incident.size()>2)
            return refusal("A cut edge has invalid/nonmanifold incident faces");
        for(const auto& side:incident) {
            markFace(side.face);
            edgeOwners[edge].push_back(side.face);
        }
    }
    for(const auto id:affected) {
        const auto corners=index.face(id).vertices;
        auto& local=faceCorners[id];
        if(!sectionParticipants.contains(id))
            for(unsigned j=0;j<3;++j)local[j]=existing(corners[j]);
        std::vector<Tri> generated;
        if(sectionParticipants.contains(id)) {
            generated=sectionFaces.at(id);
            // A source face whose boundary is independently cut again is
            // currently unsupported; two sections cannot silently create
            // T-junctions in the already tessellated source patch.
            for(const auto& [edge,owners]:edgeOwners)
                if(std::find(owners.begin(),owners.end(),id)!=owners.end())
                    for(const auto& [_,point]:cuts.at(edge)) {
                        bool found=false;
                        for(const auto triangle:generated)
                            if(std::find(triangle.begin(),triangle.end(),point)!=
                               triangle.end()) {found=true;break;}
                        if(!found)return refusal("Additional source-face edge cut unsupported");
                    }
        }else {
            std::map<std::pair<std::size_t,std::size_t>,
                     std::vector<std::size_t>> sides;
            for(unsigned j=0;j<3;++j) {
                const auto from=corners[j],to=corners[(j+1)%3];
                const auto edge=canonical(from,to);
                const auto found=cuts.find(edge);
                if(found==cuts.end())continue;
                auto& order=sides[{local[j],local[(j+1)%3]}];
                for(const auto& [_,point]:found->second)order.push_back(point);
                if(from!=edge.a)std::reverse(order.begin(),order.end());
            }
            generated=splitBoundaryFace({local[0],local[1],local[2]},sides);
        }
        if(!areaPreserved(index,id,generated,result.points))
            return refusal("Non-conforming or area-losing face reconstruction");
        if(result.faces.size()>=limits.maxAffectedFaces)
            throw std::length_error("Conforming cut affected-face budget exhausted");
        result.faces.push_back({id,std::move(generated)});
    }
    std::size_t triangleCount=0;
    for(const auto& f:result.faces) {
        triangleCount+=f.triangles.size();
        if(triangleCount>limits.maxTriangles)
            throw std::length_error("Conforming cut triangle budget exhausted");
    }
    for(const auto& [edge,owners]:edgeOwners)
        for(const auto face:owners) {
            const auto found=std::find_if(result.faces.begin(),result.faces.end(),
                [&](const auto& patch){return patch.sourceFace==face;});
            if(found==result.faces.end())
                return refusal("Missing neighboring face after conforming split");
            for(const auto& [_,point]:cuts.at(edge)) {
                bool present=false;
                for(const auto tri:found->triangles)
                    if(std::find(tri.begin(),tri.end(),point)!=tri.end()) {
                        present=true;break;
                    }
                if(!present)return refusal("Cut edge vertex absent from incident face");
            }
        }

    if(mirrors) {
        // Match by original mirrored EDGE or original mirrored FACE,
        // never by coordinates alone: the two intersecting solids have
        // distinct topological nodes at the same contact position.
        using Key=std::tuple<bool,VertexId,VertexId>;
        std::map<std::array<VertexId,3>,FaceId> reflectedFaces;
        for(const auto& face:result.faces)
            reflectedFaces.emplace(sortedFaceIds(index,face.sourceFace),
                                   face.sourceFace);
        const auto mirrorOf=[&](VertexId v)->VertexId {
            const auto it=mirrors->find(v);
            if(it==mirrors->end()||!mirrors->contains(it->second)||
               mirrors->at(it->second)!=v)
                throw std::logic_error("Incomplete mirrored original vertex map");
            return it->second;
        };
        std::map<Key,std::vector<std::size_t>> indexed;
        const auto keyFor=[&](const ConformingCutPoint& point)->Key {
            if(point.boundaryEdge)
                return {true,point.boundaryEdge->a,point.boundaryEdge->b};
            return {false,point.owningFace,0};
        };
        for(std::size_t i=0;i<result.points.size();++i)
            if(!result.points[i].existingVertex)
                indexed[keyFor(result.points[i])].push_back(i);
        const auto localScale=[&](const ConformingCutPoint& point) {
            if(point.boundaryEdge)
                return length(index.vertex(point.boundaryEdge->a).position-
                              index.vertex(point.boundaryEdge->b).position);
            const auto ids=index.face(point.owningFace).vertices;
            const auto a=index.vertex(ids[0]).position,
                       b=index.vertex(ids[1]).position,
                       c=index.vertex(ids[2]).position;
            return std::max({length(b-a),length(c-b),length(a-c)});
        };
        const auto reflectTolerance=[&](const ConformingCutPoint& a,
                                   const ConformingCutPoint& b) {
            const double geometric=1e-9*std::max(localScale(a),localScale(b));
            const auto coords=std::max({
                std::abs(a.position.x),std::abs(a.position.y),
                std::abs(a.position.z),std::abs(b.position.x),
                std::abs(b.position.y),std::abs(b.position.z)});
            return std::max(geometric,
                128*std::numeric_limits<double>::epsilon()*coords);
        };
        std::set<std::size_t> matched;
        try {
            for(std::size_t i=0;i<result.points.size();++i) {
                const auto& point=result.points[i];
                if(point.existingVertex||matched.contains(i))continue;
                Key counterpart{};
                if(point.boundaryEdge) {
                    const auto a=mirrorOf(point.boundaryEdge->a);
                    const auto b=mirrorOf(point.boundaryEdge->b);
                    const auto edge=canonical(a,b);
                    counterpart={true,edge.a,edge.b};
                }else {
                    auto ids=index.face(point.owningFace).vertices;
                    for(auto& id:ids)id=mirrorOf(id);
                    std::sort(ids.begin(),ids.end());
                    const auto found=reflectedFaces.find(ids);
                    if(found==reflectedFaces.end())
                        return refusal("Reflected cut source face is missing");
                    counterpart={false,found->second,0};
                }
                const auto found=indexed.find(counterpart);
                if(found==indexed.end())
                    return refusal("Reflected cut edge/face is missing");
                const auto reflection=Vec3{-point.position.x,
                                            point.position.y,point.position.z};
                std::optional<std::size_t> twin;
                for(const auto candidate:found->second) {
                    const auto& other=result.points[candidate];
                    if(dist2(reflection,other.position)>
                       std::pow(reflectTolerance(point,other),2))continue;
                    if(twin)return refusal("Ambiguous reflected cut point");
                    twin=candidate;
                }
                if(!twin)
                    return refusal("New cut point lacks a bounded reflected partner");
                if(matched.contains(*twin))
                    return refusal("Reflected cut correspondence is not one-to-one");
                // Make symmetry EXACT before handing the private plan to
                // eventual RegionEdit staging. Numerical adjustments are
                // allowed only within local original face/edge scale.
                if(i==*twin) {
                    if(point.position.x!=0)
                        return refusal("Self-mirrored section point is off X=0");
                }else {
                    result.points[*twin].position=reflection;
                }
                matched.insert(i);matched.insert(*twin);
                result.mirroredCutPoints.emplace_back(i,*twin);
            }
        }catch(const std::logic_error&) {
            return refusal("Original reflection mapping is inconsistent");
        }
        // The symmetry snap must not move any point away from its own
        // source plane/edge or invalidate a single reconstructed patch.
        for(const auto& point:result.points) {
            if(point.existingVertex)continue;
            const auto p=point.position;
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))
                return refusal("Nonfinite mirrored cut point");
            const auto tolerance=1e-9*localScale(point);
            if(point.boundaryEdge) {
                const auto a=index.vertex(point.boundaryEdge->a).position;
                const auto b=index.vertex(point.boundaryEdge->b).position;
                const auto d=b-a;
                const auto d2=dot(d,d);
                if(!(d2>0))return refusal("Degenerate original mirrored edge");
                const auto t=dot(p-a,d)/d2;
                if(!(t>0&&t<1)||length((a+d*t)-p)>tolerance)
                    return refusal("Mirrored point moved off original cut edge");
            }else {
                const auto ids=index.face(point.owningFace).vertices;
                const auto a=index.vertex(ids[0]).position,
                           b=index.vertex(ids[1]).position,
                           c=index.vertex(ids[2]).position;
                const auto n=cross(b-a,c-a);
                if(!(length(n)>0)||
                   std::abs(dot(p-a,n))/length(n)>tolerance)
                    return refusal("Mirrored point moved off source face plane");
            }
        }
        for(const auto& face:result.faces)
            if(!areaPreserved(index,face.sourceFace,face.triangles,result.points))
                return refusal("Mirrored reconciliation violates source face winding/area");
    }
    result.status=IntersectionSectionStatus::TransverseSegment;
    return result;
}
} // namespace mesher

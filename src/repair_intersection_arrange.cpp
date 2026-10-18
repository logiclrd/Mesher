#include "mesher/repair_intersection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=RegionTopologyIndex::Edge;
using Tri=std::array<std::size_t,3>;
struct Point2 {double x{},y{};};
struct SectionLine {std::size_t a{},b{};};
struct Frame {
    Vec3 origin{},u{},v{};
    double scale{};
};
Edge edge(VertexId a,VertexId b) {
    if(a>b)std::swap(a,b);
    return {a,b};
}
double cross2(Point2 a,Point2 b) {return a.x*b.y-a.y*b.x;}
Point2 subtract(Point2 a,Point2 b) {return {a.x-b.x,a.y-b.y};}
Point2 to2(Frame f,Vec3 p) {
    const auto d=p-f.origin;
    return {dot(d,f.u),dot(d,f.v)};
}
double dist2(Vec3 a,Vec3 b) {const auto d=a-b;return dot(d,d);}
double area2(const std::vector<std::size_t>& poly,
             const std::map<std::size_t,Point2>& positions) {
    double area=0;
    for(std::size_t j=0;j<poly.size();++j) {
        const auto a=positions.at(poly[j]);
        const auto b=positions.at(poly[(j+1)%poly.size()]);
        area+=cross2(a,b);
    }
    return area;
}
std::optional<Frame> frame(const RegionTopologyIndex& index,FaceId id) {
    const auto f=index.face(id).vertices;
    const auto a=index.vertex(f[0]).position,
               b=index.vertex(f[1]).position,
               c=index.vertex(f[2]).position;
    const auto axis=b-a;
    const auto n=cross(axis,c-a);
    if(!(length(axis)>0)||!(length(n)>0))return std::nullopt;
    const auto u=axis/length(axis);
    const auto normal=n/length(n);
    return Frame{a,u,cross(normal,u),
        std::max({length(axis),length(c-b),length(c-a)})};
}
bool positiveArea(const RegionTopologyIndex& index,FaceId source,
                  const std::vector<Tri>& triangles,
                  const std::vector<ConformingCutPoint>& points) {
    const auto original=index.face(source).vertices;
    const auto a=index.vertex(original[0]).position,
               b=index.vertex(original[1]).position,
               c=index.vertex(original[2]).position;
    const auto n=cross(b-a,c-a);
    const auto twice=length(n);
    if(triangles.empty()||!(twice>0))return false;
    double summed=0;
    for(const auto t:triangles) {
        if(t[0]>=points.size()||t[1]>=points.size()||
           t[2]>=points.size()||
           t[0]==t[1]||t[0]==t[2]||t[1]==t[2])return false;
        const auto p=points[t[0]].position,
                   q=points[t[1]].position,
                   r=points[t[2]].position;
        const auto oriented=dot(cross(q-p,r-p),n)/twice;
        if(!(oriented>1e-12*twice)||!std::isfinite(oriented))
            return false;
        summed+=oriented;
    }
    return std::abs(summed-twice)<=1e-8*twice;
}
bool earClip(const std::vector<std::size_t>& boundary,
             const std::map<std::size_t,Point2>& pos,
             double areaTolerance,std::vector<Tri>& triangles,
             std::size_t maximum,
             std::size_t& predicates,std::size_t maximumPredicates) {
    if(boundary.size()<3)return false;
    std::vector<std::size_t> polygon=boundary;
    while(polygon.size()>3) {
        bool clipped=false;
        for(std::size_t i=0;i<polygon.size();++i) {
            if(predicates>=maximumPredicates)
                throw std::length_error("Ear-clipping predicate budget");
            ++predicates;
            const auto prev=polygon[(i+polygon.size()-1)%polygon.size()],
                       mid=polygon[i],
                       next=polygon[(i+1)%polygon.size()];
            const auto a=pos.at(prev),b=pos.at(mid),c=pos.at(next);
            const auto ab=subtract(b,a),ac=subtract(c,a);
            const auto signedArea=cross2(ab,ac);
            if(signedArea<=areaTolerance)continue;
            bool contains=false;
            for(const auto id:polygon) {
                if(id==prev||id==mid||id==next)continue;
                if(predicates>=maximumPredicates)
                    throw std::length_error("Ear-clipping point-test budget");
                ++predicates;
                const auto p=pos.at(id);
                const auto ap=subtract(p,a),bp=subtract(p,b),
                           cp=subtract(p,c);
                const auto x=cross2(ab,ap);
                const auto y=cross2(subtract(c,b),bp);
                const auto z=cross2(subtract(a,c),cp);
                if(x>=-areaTolerance&&y>=-areaTolerance&&
                   z>=-areaTolerance) {
                    contains=true;break;
                }
            }
            if(contains)continue;
            if(triangles.size()>=maximum)
                throw std::length_error("Planar arrangement triangle budget exceeded");
            triangles.push_back({prev,mid,next});
            polygon.erase(polygon.begin()+static_cast<std::ptrdiff_t>(i));
            clipped=true;
            break;
        }
        if(!clipped)return false;
    }
    if(cross2(subtract(pos.at(polygon[1]),pos.at(polygon[0])),
              subtract(pos.at(polygon[2]),pos.at(polygon[0])))<=
       areaTolerance)return false;
    if(triangles.size()>=maximum)
        throw std::length_error("Planar arrangement triangle budget exceeded");
    triangles.push_back({polygon[0],polygon[1],polygon[2]});
    return true;
}
bool arrangeFace(
    const RegionTopologyIndex& index,FaceId faceId,
    const std::array<std::size_t,3>& corners,
    const std::map<Edge,std::vector<std::pair<double,std::size_t>>>& cuts,
    const std::vector<SectionLine>& sourceSections,
    std::vector<ConformingCutPoint>& points,
    std::size_t& newlyCreated,
    std::size_t& arrangementTests,
    std::size_t& arrangementEdges,
    std::size_t& arrangementPredicates,
    std::size_t& totalTriangles,
    const ConformingCutLimits& limits,
    std::vector<Tri>& result,std::string& diagnostic) {
    const auto refuse=[&](int location) {
        diagnostic="arrangement stage line "+std::to_string(location);
        return false;
    };
    const auto source=index.face(faceId).vertices;
    const auto frameData=frame(index,faceId);
    if(!frameData)return refuse(__LINE__);
    const auto f=*frameData;
    const auto tolerance=1e-10*f.scale;
    const auto areaTolerance=1e-12*f.scale*f.scale;
    std::map<std::size_t,Point2> positions;
    const auto locate=[&](std::size_t id)->Point2 {
        const auto it=positions.find(id);
        if(it!=positions.end())return it->second;
        const auto p=to2(f,points.at(id).position);
        positions.emplace(id,p);
        return p;
    };
    std::set<std::pair<std::size_t,std::size_t>> graph;
    std::set<std::size_t> boundaryPoints;
    const auto addEdge=[&](std::size_t a,std::size_t b) {
        if(a==b)return refuse(__LINE__);
        if(arrangementEdges>=limits.maxArrangementEdges)
            throw std::length_error("Planar arrangement graph-edge budget");
        if(a>b)std::swap(a,b);
        if(!graph.insert({a,b}).second)return refuse(__LINE__);
        ++arrangementEdges;
        return true;
    };
    // Build the ENTIRE oriented boundary with ALL original edge cuts,
    // including points introduced by other face sections.
    for(unsigned side=0;side<3;++side) {
        const auto a=source[side],b=source[(side+1)%3];
        const auto key=edge(a,b);
        std::vector<std::size_t> boundary{corners[side]};
        const auto it=cuts.find(key);
        if(it!=cuts.end()) {
            auto found=it->second;
            std::sort(found.begin(),found.end());
            if(a!=key.a)std::reverse(found.begin(),found.end());
            for(const auto& [_,point]:found)boundary.push_back(point);
        }
        boundary.push_back(corners[(side+1)%3]);
        boundaryPoints.insert(boundary.begin(),boundary.end());
        for(std::size_t i=1;i<boundary.size();++i)
            if(!addEdge(boundary[i-1],boundary[i]))return refuse(__LINE__);
    }
    struct Mark {double t{};std::size_t id{};};
    std::vector<std::vector<Mark>> segmentMarks;
    segmentMarks.reserve(sourceSections.size());
    for(const auto& section:sourceSections)
        segmentMarks.push_back({{0,section.a},{1,section.b}});

    const auto existingAt=[&](Point2 position) {
        for(const auto& [id,p]:positions)
            if(std::hypot(p.x-position.x,p.y-position.y)<=tolerance)
                return std::optional<std::size_t>{id};
        return std::optional<std::size_t>{};
    };
    // O(number of sections^2), explicitly limited. Every actual crossing
    // is inserted in BOTH segments, yielding a common face-local node.
    for(std::size_t i=0;i<sourceSections.size();++i)
        for(std::size_t j=i+1;j<sourceSections.size();++j) {
            if(arrangementTests>=limits.maxArrangementPairTests)
                throw std::length_error("Planar arrangement intersection-test budget");
            ++arrangementTests;
            const auto& a=sourceSections[i];
            const auto& b=sourceSections[j];
            const auto p=locate(a.a),q=locate(b.a);
            const auto r=subtract(locate(a.b),p);
            const auto s=subtract(locate(b.b),q);
            const auto denominator=cross2(r,s);
            const auto qp=subtract(q,p);
            const auto scaleProduct=std::hypot(r.x,r.y)*
                                     std::hypot(s.x,s.y);
            if(scaleProduct==0)return refuse(__LINE__);
            if(std::abs(denominator)<=1e-12*scaleProduct) {
                // Near-parallel lines can share a long collinear portion;
                // no unverified overlapping constraints may be retained.
                if(std::abs(cross2(qp,r))<=tolerance*
                   std::hypot(r.x,r.y))return refuse(__LINE__);
                continue;
            }
            const double t=cross2(qp,s)/denominator;
            const double u=cross2(qp,r)/denominator;
            if(t< -1e-10||t>1+1e-10||
               u< -1e-10||u>1+1e-10)continue;
            if(t<0||t>1||u<0||u>1)return refuse(__LINE__); // grazing uncertainty
            const auto intersection=Point2{p.x+t*r.x,p.y+t*r.y};
            std::optional<std::size_t> id;
            if(t<=1e-10)id=a.a;
            else if(t>=1-1e-10)id=a.b;
            if(u<=1e-10) {
                if(id&&*id!=b.a)return refuse(__LINE__);
                id=b.a;
            }else if(u>=1-1e-10) {
                if(id&&*id!=b.b)return refuse(__LINE__);
                id=b.b;
            }
            if(!id)id=existingAt(intersection);
            if(!id) {
                if(newlyCreated>=limits.maxNewPoints)
                    throw std::length_error("Planar arrangement new-point budget");
                const auto point=points.at(a.a).position+
                    (points.at(a.b).position-points.at(a.a).position)*t;
                id=points.size();
                points.push_back({point,std::nullopt,std::nullopt,faceId});
                ++newlyCreated;
            }
            positions[*id]=intersection;
            segmentMarks[i].push_back({t,*id});
            segmentMarks[j].push_back({u,*id});
    }
    for(std::size_t i=0;i<sourceSections.size();++i) {
        auto& marks=segmentMarks[i];
        std::sort(marks.begin(),marks.end(),
            [](Mark a,Mark b){return a.t<b.t;});
        std::vector<std::size_t> ordered;
        for(const auto mark:marks) {
            if(!ordered.empty()&&ordered.back()==mark.id)continue;
            if(!ordered.empty()&&
               std::hypot(locate(ordered.back()).x-locate(mark.id).x,
                          locate(ordered.back()).y-locate(mark.id).y)
               <=tolerance)return refuse(__LINE__);
            ordered.push_back(mark.id);
        }
        for(std::size_t j=1;j<ordered.size();++j)
            if(!addEdge(ordered[j-1],ordered[j]))return refuse(__LINE__);
    }
    if(graph.empty())return refuse(__LINE__);
    // A section may stop inside this face because the intersection
    // continues across the OTHER sheet's source edge. Those endpoints
    // are true constrained vertices but require ordinary (unconstrained)
    // scaffold edges for a complete triangulation of this face.
    // Connect degree-one interior vertices to a visible perimeter node,
    // never crossing or overlapping any existing constraint.
    std::map<std::size_t,std::size_t> degree;
    for(const auto& [a,b]:graph) {++degree[a];++degree[b];}
    const auto visible=[&](std::size_t from,std::size_t to) {
        const auto p=locate(from),q=locate(to);
        const auto r=subtract(q,p);
        for(const auto& [a,b]:graph) {
            if(a==from||b==from||a==to||b==to)continue;
            if(arrangementPredicates>=limits.maxArrangementPredicates)
                throw std::length_error("Scaffold visibility-test budget");
            ++arrangementPredicates;
            const auto x=locate(a),y=locate(b);
            const auto segment=subtract(y,x),xp=subtract(x,p);
            const auto denominator=cross2(r,segment);
            const auto scaleProduct=std::hypot(r.x,r.y)*
                                     std::hypot(segment.x,segment.y);
            if(!(scaleProduct>0))return refuse(__LINE__);
            if(std::abs(denominator)<=1e-12*scaleProduct) {
                if(std::abs(cross2(xp,r))<=tolerance*
                   std::hypot(r.x,r.y)) {
                    // Another graph node lies on this proposed scaffold,
                    // so connecting directly would introduce a T-junction.
                    return refuse(__LINE__);
                }
                continue;
            }
            const auto t=cross2(xp,segment)/denominator;
            const auto u=cross2(xp,r)/denominator;
            if(t>=-1e-10&&t<=1+1e-10&&
               u>=-1e-10&&u<=1+1e-10)return refuse(__LINE__);
        }
        return true;
    };
    for(const auto& [node,count]:degree) {
        if(count!=1||boundaryPoints.contains(node))continue;
        std::optional<std::size_t> target;
        double smallest=std::numeric_limits<double>::infinity();
        for(const auto candidate:boundaryPoints) {
            const auto p=locate(node),q=locate(candidate);
            const auto distance=std::hypot(p.x-q.x,p.y-q.y);
            const auto key=std::make_pair(std::min(node,candidate),
                                          std::max(node,candidate));
            if(distance<=tolerance||distance>=smallest||
               graph.contains(key)||!visible(node,candidate))continue;
            target=candidate;
            smallest=distance;
        }
        if(!target||!addEdge(node,*target))return refuse(__LINE__);
    }
    // A detached, simple closed section loop does not have any degree-one
    // endpoints. Without a connection to the outer triangle perimeter,
    // halfedge walking would triangulate BOTH the interior of the loop
    // and the entire outer triangle, double-covering its occupied area.
    // A single bridge creates a weakly simple face with repeated bridge
    // vertices (which our ear clipper correctly rejects). TWO distinct,
    // bounded, noncrossing ordinary scaffolds make both resulting faces
    // simple while leaving all real V13 section edges untouched.
    //
    // Only isolated degree-two cycles qualify here. Complex detached
    // arrangements, nested loops without visible chords, and ambiguous
    // connector choices still refuse the entire source-face plan.
    for(;;) {
        std::map<std::size_t,std::vector<std::size_t>> adjacent;
        for(const auto& [a,b]:graph) {
            adjacent[a].push_back(b);
            adjacent[b].push_back(a);
        }
        std::set<std::size_t> attached;
        std::vector<std::size_t> stack(
            boundaryPoints.begin(),boundaryPoints.end());
        while(!stack.empty()) {
            const auto node=stack.back();stack.pop_back();
            if(!attached.insert(node).second)continue;
            for(const auto next:adjacent.at(node))
                if(!attached.contains(next))stack.push_back(next);
        }
        std::optional<std::size_t> detached;
        for(const auto& [node,_]:adjacent)
            if(!attached.contains(node)) {
                detached=node;break;
            }
        if(!detached)break;
        std::set<std::size_t> island;
        stack.push_back(*detached);
        while(!stack.empty()) {
            const auto node=stack.back();stack.pop_back();
            if(!island.insert(node).second)continue;
            for(const auto next:adjacent.at(node))
                if(!island.contains(next))stack.push_back(next);
        }
        if(island.size()<3)return refuse(__LINE__);
        for(const auto node:island)
            if(adjacent.at(node).size()!=2)
                return refuse(__LINE__); // not a simple detached loop

        struct Connector {
            std::size_t from{},to{};
            double distance{};
        };
        std::vector<Connector> possible;
        for(const auto from:island)
            for(const auto to:boundaryPoints) {
                if(arrangementPredicates>=limits.maxArrangementPredicates)
                    throw std::length_error("Enclosed-section scaffold candidate budget");
                ++arrangementPredicates;
                const auto a=locate(from),b=locate(to);
                const auto span=std::hypot(a.x-b.x,a.y-b.y);
                const auto k=std::minmax(from,to);
                if(span<=tolerance||graph.contains({k.first,k.second})||
                   !visible(from,to))continue;
                possible.push_back({from,to,span});
            }
        std::sort(possible.begin(),possible.end(),
            [](const Connector& a,const Connector& b) {
                if(a.distance!=b.distance)return a.distance<b.distance;
                if(a.from!=b.from)return a.from<b.from;
                return a.to<b.to;
            });
        std::optional<std::pair<Connector,Connector>> selected;
        for(const auto& first:possible) {
            const auto endpoints=std::minmax(first.from,first.to);
            graph.insert({endpoints.first,endpoints.second}); // speculative visibility only
            for(const auto& second:possible) {
                if(first.from==second.from||first.to==second.to)continue;
                if(arrangementPredicates>=limits.maxArrangementPredicates)
                    throw std::length_error("Enclosed-section double-scaffold budget");
                ++arrangementPredicates;
                if(!visible(second.from,second.to))continue;
                selected=std::make_pair(first,second);
                break;
            }
            graph.erase({endpoints.first,endpoints.second});
            if(selected)break;
        }
        if(!selected)return refuse(__LINE__);
        if(!addEdge(selected->first.from,selected->first.to)||
           !addEdge(selected->second.from,selected->second.to))
            return refuse(__LINE__);
    }
    std::map<std::size_t,std::vector<std::size_t>> outgoing;
    for(const auto& [a,b]:graph) {
        outgoing[a].push_back(b);
        outgoing[b].push_back(a);
    }
    for(auto& [id,neighbors]:outgoing) {
        const auto p=locate(id);
        std::sort(neighbors.begin(),neighbors.end(),
            [&](std::size_t a,std::size_t b) {
                const auto pa=locate(a),pb=locate(b);
                return std::atan2(pa.y-p.y,pa.x-p.x)<
                       std::atan2(pb.y-p.y,pb.x-p.x);
            });
    }
    std::set<std::pair<std::size_t,std::size_t>> visited;
    std::size_t foundPositive=0;
    for(const auto& [start,neighbors]:outgoing)
        for(const auto neighbor:neighbors) {
            if(visited.contains({start,neighbor}))continue;
            std::vector<std::size_t> polygon;
            std::size_t u=start,v=neighbor;
            bool closed=false;
            for(std::size_t steps=0;steps<=graph.size()*2;++steps) {
                if(!visited.insert({u,v}).second)return refuse(__LINE__);
                polygon.push_back(u);
                const auto& at=outgoing.at(v);
                const auto reverse=std::find(at.begin(),at.end(),u);
                if(reverse==at.end())return refuse(__LINE__);
                const auto reversePosition=static_cast<std::size_t>(reverse-at.begin());
                const auto w=at[(reversePosition+at.size()-1)%at.size()];
                u=v;v=w;
                if(u==start&&v==neighbor) {
                    closed=true;break;
                }
            }
            if(!closed||polygon.size()<3)return refuse(__LINE__);
            std::set<std::size_t> unique(polygon.begin(),polygon.end());
            if(unique.size()!=polygon.size())return refuse(__LINE__);
            const auto signedArea=area2(polygon,positions);
            if(signedArea< -areaTolerance)continue; // unbounded exterior
            if(signedArea<=areaTolerance)return refuse(__LINE__);
            ++foundPositive;
            const auto allowed=limits.maxTriangles-totalTriangles;
            const auto before=result.size();
            if(!earClip(polygon,positions,areaTolerance,result,
                        before+allowed,arrangementPredicates,
                        limits.maxArrangementPredicates))return refuse(__LINE__);
            totalTriangles+=result.size()-before;
    }
    return foundPositive>0&&positiveArea(index,faceId,result,points);
}
} // namespace

// The second pass validates the COMPLETE arrangement's explicit face,
// point and contact-section mirror orbits before exposing any cut plan.
std::string pairMirroredMultiSectionPlan(
    const RegionTopologyIndex&,ConformingCutPlan&,
    const std::unordered_map<VertexId,VertexId>&,const ConformingCutLimits&);

ConformingCutPlan buildMultiSectionCutPlan(
    const RegionTopologyIndex& index,
    const std::vector<RegionTopologyIndex::CollisionPair>& contacts,
    const ConformingCutLimits& limits,
    const std::unordered_map<VertexId,VertexId>* mirrors) {
      const auto reject=[&](const char* reason) {
        ConformingCutPlan refusal;
        refusal.status=IntersectionSectionStatus::UnsupportedContact;
        refusal.refusal=reason;
        return refusal;
    };
    if(contacts.empty())return {};
    if(contacts.size()>limits.maxContacts)
        throw std::length_error("Multiple-section contact budget");
    ConformingCutPlan result;
    std::map<VertexId,std::size_t> originals;
    std::map<Edge,std::vector<std::pair<double,std::size_t>>> cuts;
    std::map<FaceId,std::vector<SectionLine>> perFace;
    std::map<FaceId,std::vector<std::size_t>> interiors;
    std::set<FaceId> affected;
    std::set<std::pair<FaceId,FaceId>> contactPairs;
    std::size_t newlyCreated=0,arrangementTests=0,
                arrangementEdges=0,arrangementPredicates=0,
                totalTriangles=0;
    const auto original=[&](VertexId id)->std::size_t {
        const auto it=originals.find(id);
        if(it!=originals.end())return it->second;
        const auto next=result.points.size();
        result.points.push_back({index.vertex(id).position,id,std::nullopt,0});
        originals.emplace(id,next);
        return next;
    };
    const auto newPoint=[&](ConformingCutPoint point)->std::size_t {
        if(newlyCreated>=limits.maxNewPoints)
            throw std::length_error("Multiple-section new-point budget");
        const auto id=result.points.size();
        result.points.push_back(point);
        ++newlyCreated;
        return id;
    };
    const auto addAffected=[&](FaceId face) {
        affected.insert(face);
        if(affected.size()>limits.maxAffectedFaces)
            throw std::length_error("Multiple-section affected-face budget");
    };
    const auto attach=[&](FaceId face,
                          const std::array<VertexId,3>& corners,
                          Vec3 p,const std::array<double,3>& bary)
                          ->std::optional<std::size_t> {
        int opposite=-1;
        for(unsigned j=0;j<3;++j)
            if(bary[j]==0) {
                if(opposite>=0)return std::nullopt; // corner grazing
                opposite=static_cast<int>(j);
            }
        if(opposite>=0) {
            const auto key=edge(corners[(opposite+1)%3],
                                corners[(opposite+2)%3]);
            const auto a=index.vertex(key.a).position,
                       b=index.vertex(key.b).position;
            const auto direction=b-a;
            const auto d2=dot(direction,direction);
            if(!(d2>0))return std::nullopt;
            const auto t=dot(p-a,direction)/d2;
            if(!(t>1e-9&&t<1-1e-9))return std::nullopt;
            auto& group=cuts[key];
            for(const auto& [previous,id]:group)
                if(std::abs(previous-t)<=1e-9) {
                    if(dist2(result.points[id].position,p)>1e-16*d2)
                        return std::nullopt;
                    return id;
                }
            const auto id=newPoint({p,std::nullopt,key,0});
            group.emplace_back(t,id);
            return id;
        }
        const auto local=frame(index,face);
        if(!local)return std::nullopt;
        const auto tolerance=1e-9*local->scale;
        for(const auto id:interiors[face])
            if(dist2(result.points[id].position,p)<=tolerance*tolerance)
                return id;
        const auto id=newPoint({p,std::nullopt,std::nullopt,face});
        interiors[face].push_back(id);
        return id;
    };
    for(const auto contact:contacts) {
        if(contact.ambiguous||contact.first==contact.second||
           !index.maybeFace(contact.first)||!index.maybeFace(contact.second))
            return reject("Invalid or ambiguous multi-section contact");
        const auto key=std::minmax(contact.first,contact.second);
        if(!contactPairs.insert({key.first,key.second}).second)
            return reject("Duplicate collision pair yields ambiguous constraints");
        const auto f0=index.face(contact.first).vertices;
        const auto f1=index.face(contact.second).vertices;
        const auto vertices=[&](std::array<VertexId,3> ids) {
            return std::array<Vec3,3>{index.vertex(ids[0]).position,
                index.vertex(ids[1]).position,index.vertex(ids[2]).position};
        };
        const auto section=splitTransverseTriangleContact(
            vertices(f0),f0,vertices(f1),f1);
        if(section.status!=IntersectionSectionStatus::TransverseSegment)
            return reject("Multi-section pair has unsupported transverse geometry");
        ConformingCutSegment match;
        match.firstFace=contact.first;
        match.secondFace=contact.second;
        for(unsigned side=0;side<2;++side) {
            const auto face=side?contact.second:contact.first;
            const auto corners=side?f1:f0;
            addAffected(face);
            std::array<std::size_t,2> ends{};
            for(unsigned endpoint=0;endpoint<2;++endpoint) {
                const auto id=attach(face,corners,
                    section.endpoints[endpoint],
                    section.patches[side].endpointBarycentrics[endpoint]);
                if(!id)return reject("Ambiguous multi-section endpoint provenance");
                ends[endpoint]=*id;
            }
            if(ends[0]==ends[1])
                return reject("Degenerate multi-section curve");
            perFace[face].push_back({ends[0],ends[1]});
            if(side)match.secondSide=ends;
            else match.firstSide=ends;
            for(const auto id:corners)(void)original(id);
        }
        result.segments.push_back(match);
    }
    for(auto& [key,group]:cuts) {
        std::sort(group.begin(),group.end());
        for(std::size_t j=1;j<group.size();++j)
            if(group[j].first-group[j-1].first<=1e-9)
                return reject("Unresolved close multiple cuts on an edge");
        const auto incident=index.edgeSides(key);
        if(incident.empty()||incident.size()>2)
            return reject("Nonmanifold multiple-section shared edge");
        for(const auto& side:incident)addAffected(side.face);
    }
    for(const auto face:affected) {
        const auto ids=index.face(face).vertices;
        std::array<std::size_t,3> corners{};
        for(unsigned j=0;j<3;++j)corners[j]=original(ids[j]);
        std::vector<Tri> triangles;
        std::string diagnostic;
        if(!arrangeFace(index,face,corners,cuts,perFace[face],
                        result.points,newlyCreated,arrangementTests,
                        arrangementEdges,arrangementPredicates,
                        totalTriangles,limits,triangles,diagnostic)) {
            const auto reason="Planar arrangement on face "+
                std::to_string(face)+": "+diagnostic;
            return reject(reason.c_str());
        }
        result.faces.push_back({face,std::move(triangles)});
    }
    result.status=IntersectionSectionStatus::TransverseSegment;
    if(mirrors) {
        const auto error=pairMirroredMultiSectionPlan(index,result,*mirrors,limits);
        if(!error.empty())return reject(error.c_str());
    }
    return result;
}
} // namespace mesher

#include "mesher/repair_quality_flip.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using Edge=std::pair<VertexId,VertexId>;
using Position=std::tuple<double,double,double>;
Edge canonical(VertexId a,VertexId b) {return {std::min(a,b),std::max(a,b)};}
Position key(Vec3 p) {
    return {p.x==0?0:p.x,p.y==0?0:p.y,p.z==0?0:p.z};
}
double quality(Vec3 a,Vec3 b,Vec3 c) {
    const double sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sum>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/sum:0;
}
template<class Pos>
double edgeLength(const Pos& p,VertexId a,VertexId b) {
    return length(p.at(a)-p.at(b));
}
struct Candidate {
    Edge diagonal{};
    std::array<std::size_t,2> faces{};
    std::array<PrivateRepairTriangle,2> replacement{};
    double gain=0;
};
using Adjacency=std::map<Edge,std::vector<std::size_t>>;
Adjacency adjacency(const std::vector<MixedClosureFace>& faces) {
    Adjacency edges;
    for(std::size_t i=0;i<faces.size();++i) {
        const auto& v=faces[i].vertices;
        for(unsigned k=0;k<3;++k)
            edges[canonical(v[k],v[(k+1)%3])].push_back(i);
    }
    return edges;
}
template<class Pos>
std::optional<Candidate> evaluate(
    Edge diag,const Adjacency& edges,
    const std::vector<MixedClosureFace>& faces,
    const Pos& positions,
    double capFloor,double zipperFloor,
    double capFactor,double zipperFactor) {
    const auto it=edges.find(diag);
    if(it==edges.end()||it->second.size()!=2)return std::nullopt;
    const auto i=it->second[0],j=it->second[1];
    const auto& first=faces[i];
    const auto& second=faces[j];
    if(first.stableId||second.stableId||
       first.kind==MixedClosureFaceKind::Survivor||
       first.kind!=second.kind)return std::nullopt;
    // The two old faces must traverse the common diagonal in opposite
    // directions (an orientable interior two-face patch).
    for(unsigned k=0;k<3;++k) {
        const auto a=first.vertices[k],b=first.vertices[(k+1)%3];
        if(canonical(a,b)!=diag)continue;
        const auto c=first.vertices[(k+2)%3];
        for(unsigned n=0;n<3;++n) {
            if(second.vertices[n]!=b||
               second.vertices[(n+1)%3]!=a)continue;
            const auto d=second.vertices[(n+2)%3];
            if(a==b||a==c||a==d||b==c||b==d||c==d||
               edges.contains(canonical(c,d)))return std::nullopt;
            const auto pa=positions.at(a),pb=positions.at(b);
            const auto pc=positions.at(c),pd=positions.at(d);
            const auto old1=cross(pb-pa,pc-pa);
            const auto old2=cross(pa-pb,pd-pb);
            const auto n1=length(old1),n2=length(old2);
            if(!(n1>1e-18&&n2>1e-18))return std::nullopt;
            // Only nearly coplanar convex quads: flipping a creased or
            // folded 3-D front can introduce crossings even if it raises q.
            if(dot(old1,old2)<0.965*n1*n2)return std::nullopt;
            const PrivateRepairTriangle left{c,d,b},right{d,c,a};
            const auto new1=cross(pd-pc,pb-pc);
            const auto new2=cross(pc-pd,pa-pd);
            if(dot(new1,old1)<=1e-12*n1*n1||
               dot(new2,old2)<=1e-12*n2*n2)return std::nullopt;
            const auto oldQ1=quality(pa,pb,pc);
            const auto oldQ2=quality(pb,pa,pd);
            const auto q1=quality(pc,pd,pb);
            const auto q2=quality(pd,pc,pa);
            if(!std::isfinite(q1)||!std::isfinite(q2))
                return std::nullopt;
            const auto floor=first.kind==MixedClosureFaceKind::Cap?
                capFloor:zipperFloor;
            if(oldQ1>=floor&&oldQ2>=floor)return std::nullopt;
            const double oldMin=std::min(oldQ1,oldQ2);
            const double newMin=std::min(q1,q2);
            if(!(newMin>oldMin+1e-5&&newMin>oldMin*1.02))
                return std::nullopt;
            const double factor=first.kind==MixedClosureFaceKind::Cap?
                capFactor:zipperFactor;
            const auto within=[&](const PrivateRepairTriangle& t,
                                  const MixedClosureFace& parent) {
                const auto maxEdge=std::max({edgeLength(positions,t[0],t[1]),
                    edgeLength(positions,t[1],t[2]),
                    edgeLength(positions,t[2],t[0])});
                return std::isfinite(maxEdge)&&
                    maxEdge<=parent.targetEdge*factor*(1+1e-8);
            };
            if(!within(left,first)||!within(right,second))
                return std::nullopt;
            return Candidate{diag,{i,j},{left,right},newMin-oldMin};
        }
    }
    return std::nullopt;
}
} // namespace

template<class Pos>
std::size_t flipImpl(
    std::vector<MixedClosureFace>& faces,
    const Pos& positions,
    bool requireReflection,
    double minCapQuality,double minZipperQuality,
    double maxCapEdgeFactor,double maxZipperEdgeFactor) {
    const auto edges=adjacency(faces);
    std::map<Position,VertexId> reflected;
    std::set<Position> ambiguous;
    if(requireReflection) {
        for(const auto& [id,p]:positions)
            if(!reflected.emplace(key(p),id).second)
                ambiguous.insert(key(p));
    }
    const auto counterpart=[&](Edge e)->std::optional<Edge> {
        const auto lookup=[&](VertexId id)->std::optional<VertexId> {
            const auto p=positions.at(id);
            const Position r=key(Vec3{-p.x,p.y,p.z});
            if(ambiguous.contains(r))return std::nullopt;
            const auto it=reflected.find(r);
            if(it==reflected.end())return std::nullopt;
            return it->second;
        };
        const auto a=lookup(e.first),b=lookup(e.second);
        if(!a||!b)return std::nullopt;
        return canonical(*a,*b);
    };
    std::optional<Candidate> best,bestMirror;
    for(const auto& [e,ids]:edges) {
        if(ids.size()!=2)continue;
        const auto local=evaluate(e,edges,faces,positions,
            minCapQuality,minZipperQuality,maxCapEdgeFactor,maxZipperEdgeFactor);
        if(!local)continue;
        std::optional<Candidate> mirror;
        double gain=local->gain;
        if(requireReflection) {
            const auto mapped=counterpart(e);
            // Self-mirror orbit requires a dedicated seam-specific proof.
            // Skip it rather than change just one half or cross X=0.
            if(!mapped||*mapped==e)continue;
            mirror=evaluate(*mapped,edges,faces,positions,
                minCapQuality,minZipperQuality,maxCapEdgeFactor,maxZipperEdgeFactor);
            if(!mirror)continue;
            const auto& a=local->faces;
            const auto& b=mirror->faces;
            if(a[0]==b[0]||a[0]==b[1]||a[1]==b[0]||a[1]==b[1])
                continue;
            gain=std::min(gain,mirror->gain);
        }
        if(!best||gain>std::min(best->gain,
           bestMirror?bestMirror->gain:best->gain)+1e-12) {
            best=local;
            bestMirror=mirror;
        }
    }
    if(!best)return 0;
    // Both orbits have been validated without mutating any positions or
    // connectivity. Commit in place atomically in this PRIVATE scratch mesh.
    const auto apply=[&](const Candidate& c) {
        faces[c.faces[0]].vertices=c.replacement[0];
        faces[c.faces[1]].vertices=c.replacement[1];
    };
    apply(*best);
    if(bestMirror)apply(*bestMirror);
    return bestMirror?2:1;
}
std::size_t flipOneInferiorClosureDiagonal(
    std::vector<MixedClosureFace>& faces,
    const std::map<VertexId,Vec3>& positions,
    bool reflect,double cap,double zip,double maxCap,double maxZip) {
    return flipImpl(faces,positions,reflect,cap,zip,maxCap,maxZip);
}
std::size_t flipOneInferiorClosureDiagonal(
    std::vector<MixedClosureFace>& faces,
    const MixedClosurePositions& positions,
    bool reflect,double cap,double zip,double maxCap,double maxZip) {
    return flipImpl(faces,positions,reflect,cap,zip,maxCap,maxZip);
}
} // namespace mesher

#include "mesher/repair_interior_collapse.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <limits>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

namespace mesher {
namespace {
using Point=std::tuple<double,double,double>;
Point key(Vec3 p){return {p.x==0?0:p.x,p.y==0?0:p.y,p.z==0?0:p.z};}
struct Fan {
    VertexId center{};
    std::array<std::size_t,3> faces{};
    PrivateRepairTriangle triangle{};
    double gain{};
};
double quality(Vec3 a,Vec3 b,Vec3 c) {
    const auto denominator=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return denominator>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/
        denominator:0;
}
std::optional<Fan> makeFan(
    VertexId center,const std::vector<MixedClosureFace>& faces,
    const std::map<VertexId,Vec3>& positions,
    const std::map<VertexId,std::vector<std::size_t>>& incidences,
    double minQuality,double maxEdgeFactor) {
    const auto entry=incidences.find(center);
    if(entry==incidences.end()||entry->second.size()!=3)return std::nullopt;
    const auto& indexes=entry->second;
    std::map<VertexId,VertexId> next;
    double minOld=1,maxTarget=std::numeric_limits<double>::infinity();
    for(const auto i:indexes) {
        const auto& f=faces[i];
        if(f.stableId||f.kind!=MixedClosureFaceKind::Cap||
           f.targetEdge<=0)return std::nullopt;
        maxTarget=std::min(maxTarget,f.targetEdge);
        const auto& v=f.vertices;
        unsigned centerSlot=3;
        for(unsigned k=0;k<3;++k)if(v[k]==center)centerSlot=k;
        if(centerSlot==3)return std::nullopt;
        const auto a=v[(centerSlot+1)%3],b=v[(centerSlot+2)%3];
        if(a==b||a==center||b==center||
           !next.emplace(a,b).second)return std::nullopt;
        minOld=std::min(minOld,
            quality(positions.at(v[0]),positions.at(v[1]),positions.at(v[2])));
    }
    if(minOld>=minQuality||next.size()!=3)return std::nullopt;
    const auto a=next.begin()->first;
    const auto b=next.at(a);
    const auto it=next.find(b);
    if(it==next.end())return std::nullopt;
    const auto c=it->second;
    if(a==b||b==c||c==a||next.at(c)!=a)return std::nullopt;
    const auto pa=positions.at(a),pb=positions.at(b),pc=positions.at(c);
    const auto n=cross(pb-pa,pc-pa);
    const auto lengthNormal=length(n);
    const auto q=quality(pa,pb,pc);
    const double longest=std::max({length(pa-pb),length(pb-pc),length(pc-pa)});
    if(lengthNormal<=1e-16||q<minQuality||
       longest>maxTarget*maxEdgeFactor*(1+1e-8))return std::nullopt;
    // Replacing a convex three-triangle star must not reverse any of
    // its oriented faces, including under a mirrored companion.
    for(const auto i:indexes) {
        const auto& v=faces[i].vertices;
        const auto old=cross(positions.at(v[1])-positions.at(v[0]),
                              positions.at(v[2])-positions.at(v[0]));
        if(length(old)<=1e-16||dot(n,old)<0.5*lengthNormal*length(old))
            return std::nullopt;
    }
    const std::array<VertexId,3> sorted{{a,b,c}};
    auto faceKey=sorted;
    std::sort(faceKey.begin(),faceKey.end());
    for(std::size_t i=0;i<faces.size();++i) {
        if(i==indexes[0]||i==indexes[1]||i==indexes[2])continue;
        auto v=faces[i].vertices;
        std::sort(v.begin(),v.end());
        if(v==faceKey)return std::nullopt;
    }
    return Fan{center,{indexes[0],indexes[1],indexes[2]},
               {a,b,c},q-minOld};
}
} // namespace

std::vector<VertexId> collapseOneInferiorCapFan(
    std::vector<MixedClosureFace>& faces,
    std::map<VertexId,Vec3>& positions,
    const std::vector<VertexId>& createdVertices,
    bool requireReflection,double minCapQuality,double maxCapEdgeFactor) {
    std::map<VertexId,std::vector<std::size_t>> incidence;
    for(std::size_t i=0;i<faces.size();++i)
        for(const auto v:faces[i].vertices)incidence[v].push_back(i);
    const std::set<VertexId> liveNew(createdVertices.begin(),createdVertices.end());
    std::map<Point,VertexId> lookup;
    std::set<Point> duplicates;
    if(requireReflection)
        for(const auto& [id,p]:positions)
            if(!lookup.emplace(key(p),id).second)
                duplicates.insert(key(p));
    const auto reflectId=[&](VertexId id)->VertexId {
        if(!requireReflection)return id;
        const auto p=positions.at(id);
        const auto r=key(Vec3{-p.x,p.y,p.z});
        if(duplicates.contains(r))return 0;
        const auto it=lookup.find(r);
        return it==lookup.end()?0:it->second;
    };
    std::optional<Fan> best,mirror;
    for(const auto id:liveNew) {
        if(!positions.contains(id))continue;
        const auto primary=makeFan(id,faces,positions,incidence,
                                   minCapQuality,maxCapEdgeFactor);
        if(!primary)continue;
        std::optional<Fan> companion;
        double gain=primary->gain;
        if(requireReflection) {
            const auto partner=reflectId(id);
            if(!partner||partner==id||!liveNew.contains(partner)||
               partner<id)continue;
            companion=makeFan(partner,faces,positions,incidence,
                              minCapQuality,maxCapEdgeFactor);
            if(!companion)continue;
            // Exactly mirrored new parent triangle corners and disjoint
            // face orbits, otherwise the paired operation is untrusted.
            const auto& v=primary->triangle;
            const auto& w=companion->triangle;
            for(const auto face:primary->faces)
                for(const auto otherFace:companion->faces)
                    if(face==otherFace)gain=-1;
            std::set<VertexId> expected,actual(w.begin(),w.end());
            for(const auto vertex:v) {
                const auto partnerId=reflectId(vertex);
                if(!partnerId)gain=-1;
                else expected.insert(partnerId);
            }
            if(expected!=actual)gain=-1;
            if(gain<=0)continue;
            gain=std::min(gain,companion->gain);
        }
        if(gain>0&&(!best||gain>std::min(best->gain,
             mirror?mirror->gain:best->gain)+1e-12)) {
            best=primary;mirror=companion;
        }
    }
    if(!best)return {};
    std::set<std::size_t> discard;
    std::map<std::size_t,PrivateRepairTriangle> replacements;
    const auto stage=[&](const Fan& fan) {
        replacements.emplace(fan.faces[0],fan.triangle);
        discard.insert(fan.faces[1]);
        discard.insert(fan.faces[2]);
    };
    stage(*best);
    if(mirror)stage(*mirror);
    // Commit the whole mirrored orbit in one ordered in-memory mutation.
    std::vector<MixedClosureFace> updated;
    updated.reserve(faces.size()-discard.size());
    for(std::size_t i=0;i<faces.size();++i) {
        if(discard.contains(i))continue;
        auto f=faces[i];
        if(const auto it=replacements.find(i);it!=replacements.end())
            f.vertices=it->second;
        updated.push_back(f);
    }
    faces=std::move(updated);
    std::vector<VertexId> retired{best->center};
    positions.erase(best->center);
    if(mirror) {
        retired.push_back(mirror->center);
        positions.erase(mirror->center);
    }
    return retired;
}
} // namespace mesher

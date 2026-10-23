#include "mesher/repair_interior_fairing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace mesher {
namespace {
using Point=std::tuple<double,double,double>;
Point key(Vec3 p) {
    return {p.x==0?0:p.x,p.y==0?0:p.y,p.z==0?0:p.z};
}
Vec3 reflect(Vec3 p){return {-p.x,p.y,p.z};}
double q(Vec3 a,Vec3 b,Vec3 c) {
    const auto denom=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return denom>0?2*std::sqrt(3.0)*length(cross(b-a,c-a))/denom:0;
}
struct Score {
    double deficit{};
    double worst{1};
    bool valid=true;
};
} // namespace

std::size_t fairOneConstrainedClosureVertex(
    const std::vector<MixedClosureFace>& faces,
    std::map<VertexId,Vec3>& positions,
    const std::vector<VertexId>& createdVertices,
    bool requireReflection,double minCapQuality,double minZipperQuality,
    double maxCapEdgeFactor,double maxZipperEdgeFactor) {
    if(faces.empty()||createdVertices.empty())return 0;
    std::map<VertexId,std::vector<std::size_t>> incident;
    std::set<VertexId> protectedVertices;
    for(std::size_t i=0;i<faces.size();++i) {
        const auto& face=faces[i];
        for(const auto v:face.vertices) {
            incident[v].push_back(i);
            if(face.stableId||face.kind==MixedClosureFaceKind::Survivor)
                protectedVertices.insert(v);
        }
    }
    std::set<VertexId> mobile(createdVertices.begin(),createdVertices.end());
    std::map<Point,VertexId> atPosition;
    std::set<Point> ambiguous;
    for(const auto& [id,p]:positions)
        if(!atPosition.emplace(key(p),id).second)
            ambiguous.insert(key(p));
    const auto mirror=[&](VertexId a)->VertexId {
        if(!requireReflection)return a;
        const auto at=positions.find(a);
        if(at==positions.end())return 0;
        const auto target=key(reflect(at->second));
        if(ambiguous.contains(target))return 0;
        const auto id=atPosition.find(target);
        return id==atPosition.end()?0:id->second;
    };
    const auto isMovable=[&](VertexId id) {
        return mobile.contains(id)&&!protectedVertices.contains(id)&&
            incident.contains(id)&&positions.contains(id);
    };
    const auto neighbors=[&](VertexId id) {
        std::set<VertexId> result;
        for(const auto i:incident.at(id))
            for(const auto v:faces[i].vertices)
                if(v!=id)result.insert(v);
        return result;
    };
    const auto affected=[&](VertexId a,VertexId b) {
        std::set<std::size_t> ids(incident.at(a).begin(),incident.at(a).end());
        if(a!=b)ids.insert(incident.at(b).begin(),incident.at(b).end());
        return ids;
    };
    const auto assess=[&](const std::set<std::size_t>& ids,
                          const std::map<VertexId,Vec3>& moved)->Score {
        Score score;
        for(const auto i:ids) {
            const auto& f=faces[i];
            const auto get=[&](VertexId id)->Vec3 {
                const auto it=moved.find(id);
                return it==moved.end()?positions.at(id):it->second;
            };
            const auto a=get(f.vertices[0]),b=get(f.vertices[1]),
                       c=get(f.vertices[2]);
            const auto area=length(cross(b-a,c-a));
            if(!std::isfinite(area)||area<=1e-16) {
                score.valid=false;
                return score;
            }
            const auto quality=q(a,b,c);
            if(!std::isfinite(quality)) {
                score.valid=false;
                return score;
            }
            const double floor=f.kind==MixedClosureFaceKind::Cap?
                minCapQuality:minZipperQuality;
            const double factor=f.kind==MixedClosureFaceKind::Cap?
                maxCapEdgeFactor:maxZipperEdgeFactor;
            score.deficit+=std::max(0.0,floor-quality);
            score.worst=std::min(score.worst,quality);
            const double longest=std::max({length(a-b),length(b-c),length(c-a)});
            if(f.targetEdge>0&&std::isfinite(factor))
                score.deficit+=0.25*std::max(0.0,
                    longest/(f.targetEdge*factor)-1.0);
        }
        return score;
    };
    struct Choice {
        VertexId a{},b{};
        Vec3 pa{},pb{};
        double gain{};
    };
    Choice best;
    for(const auto id:mobile) {
        if(!isMovable(id))continue;
        const VertexId other=mirror(id);
        if(!other||!isMovable(other)||other<id)continue;
        // Verify that both partners participate exclusively in NEW faces
        // of the same closure kind; original survivor geometry is immutable.
        const auto neighborsA=neighbors(id);
        if(neighborsA.empty())continue;
        const auto indexes=affected(id,other);
        bool safe=true;
        for(const auto i:indexes) {
            if(faces[i].stableId||
               faces[i].kind==MixedClosureFaceKind::Survivor) {
                safe=false;break;
            }
        }
        if(!safe)continue;
        const auto before=assess(indexes,{});
        if(!before.valid||before.deficit<1e-10)continue;
        Vec3 center{};
        for(const auto n:neighborsA)center=center+positions.at(n);
        center=center/static_cast<double>(neighborsA.size());
        const Vec3 previous=positions.at(id);
        if(id==other)center.x=0;
        // Limit movement so local fairing cannot jump a crease across
        // unrelated faces or violate fixed-rim geometric intent.
        double minNeighbor=std::numeric_limits<double>::infinity();
        for(const auto n:neighborsA)
            minNeighbor=std::min(minNeighbor,length(previous-positions.at(n)));
        if(!(minNeighbor>1e-12))continue;
        const auto targetDirection=center-previous;
        const auto distance=length(targetDirection);
        if(distance<=1e-12)continue;
        for(const double alpha:{0.125,0.25,0.5}) {
            auto candidate=previous+targetDirection*alpha;
            if(length(candidate-previous)>.5*minNeighbor)continue;
            if(id==other)candidate.x=0;
            else if((previous.x>0&&candidate.x<=0)||
                    (previous.x<0&&candidate.x>=0))
                continue;
            const auto reflected=id==other?candidate:reflect(candidate);
            if(!std::isfinite(candidate.x)||!std::isfinite(candidate.y)||
               !std::isfinite(candidate.z))continue;
            // Do not introduce collocated IDs; symbolic welds cannot be
            // accepted by the exact mirror/edge-incidence machinery.
            if(key(candidate)==key(reflected)&&id!=other)continue;
            const auto collision=[&](Point p) {
                const auto found=atPosition.find(p);
                return found!=atPosition.end()&&found->second!=id&&
                       found->second!=other;
            };
            if(collision(key(candidate))||collision(key(reflected)))continue;
            const std::map<VertexId,Vec3> proposed{
                {id,candidate}, {other,reflected}};
            // Respect orientation of EACH changed face, not just aggregate
            // quality, including mirror-side triangles.
            for(const auto i:indexes) {
                const auto& v=faces[i].vertices;
                const auto original=cross(positions.at(v[1])-positions.at(v[0]),
                                          positions.at(v[2])-positions.at(v[0]));
                const auto get=[&](VertexId a) {
                    const auto it=proposed.find(a);
                    return it==proposed.end()?positions.at(a):it->second;
                };
                const auto updated=cross(get(v[1])-get(v[0]),
                                         get(v[2])-get(v[0]));
                if(length(updated)<=1e-16||
                   dot(original,updated)<=0) {safe=false;break;}
            }
            if(!safe) {safe=true;continue;}
            const auto after=assess(indexes,proposed);
            if(!after.valid||after.worst+1e-10<before.worst||
               !(after.deficit+1e-8<before.deficit))continue;
            const auto gain=before.deficit-after.deficit;
            if(gain>best.gain+1e-12)best={id,other,candidate,reflected,gain};
        }
    }
    if(!best.a)return 0;
    positions.at(best.a)=best.pa;
    positions.at(best.b)=best.pb;
    return best.a==best.b?1:2;
}
} // namespace mesher

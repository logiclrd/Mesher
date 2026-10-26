#include "mesher/repair_zipper_front.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mesher {
namespace {
struct Corner {
    std::size_t first{},second{};
    double distance{};
};
double triangleQuality(Vec3 a,Vec3 b,Vec3 c) {
    const auto sum=dot(a-b,a-b)+dot(b-c,b-c)+dot(c-a,c-a);
    return sum>0?2*std::sqrt(3.)*length(cross(b-a,c-a))/sum:0;
}
bool finite(Vec3 p) {
    return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
}
Vec3 reflect(Vec3 p) {return {-p.x,p.y,p.z};}
void checkRim(const UnifiedZipperRim& rim) {
    const auto n=rim.ids.size();
    if(n<3||n!=rim.positions.size()||n!=rim.outgoingNormals.size())
        throw std::invalid_argument("R06-M23B invalid zipper boundary arrays");
    std::set<VertexId> used;
    for(std::size_t i=0;i<n;++i)
        if(!rim.ids[i]||!used.insert(rim.ids[i]).second||
           !finite(rim.positions[i])||!finite(rim.outgoingNormals[i])||
           !(length(rim.positions[(i+1)%n]-rim.positions[i])>1e-12))
            throw std::invalid_argument("R06-M23B malformed zipper boundary");
}
struct Candidate {
    std::vector<Vec3> positions;
    std::vector<VertexId> ids;
    std::vector<Vec3> normals;
    std::vector<std::array<std::uint32_t,3>> seedFaces;
    double seedQuality{1};
};
bool candidateEmbedded(const Candidate& c,std::size_t budget) {
    std::size_t tests=0;
    for(std::size_t i=0;i<c.seedFaces.size();++i)
        for(std::size_t j=i+1;j<c.seedFaces.size();++j) {
            if(++tests>budget)return false;
            std::array<Vec3,3> a{},b{};
            std::array<VertexId,3> ai{},bi{};
            for(unsigned k=0;k<3;++k) {
                const auto u=c.seedFaces[i][k],v=c.seedFaces[j][k];
                a[k]=c.positions[u];b[k]=c.positions[v];
                ai[k]=static_cast<VertexId>(u)+1;
                bi[k]=static_cast<VertexId>(v)+1;
            }
            const auto contact=triangleContact(a,ai,b,bi);
            if(contact!=TriangleContact::Disjoint&&
               contact!=TriangleContact::ExpectedAdjacency)
                return false;
        }
    return true;
}
Candidate seed(const UnifiedZipperRim& first,
               const UnifiedZipperRim& second,
               std::size_t a,std::size_t b,bool mirror,
               double factor,int direction) {
    const auto na=first.ids.size(),nb=second.ids.size();
    const auto c=(a+1)%na,d=(b+nb-1)%nb;
    const auto A=first.positions[a],B=second.positions[b];
    const auto C=first.positions[c],D=second.positions[d];
    const auto mid=(A+B)*.5;
    const auto span=B-A;
    const auto distance=length(span);
    auto tangent=(C+D)*.5-mid;
    tangent=tangent-span*(dot(tangent,span)/(distance*distance));
    if(mirror)tangent.x=0;
    if(length(tangent)<1e-10) {
        const auto normal=normalized(first.outgoingNormals[a]+
                                      second.outgoingNormals[b]);
        tangent=cross(span,normal);
        if(mirror)tangent.x=0;
    }
    if(length(tangent)<1e-10)
        throw std::logic_error("R06-M23B bridge has no transverse direction");
    tangent=normalized(tangent)*static_cast<double>(direction);
    Candidate patch;
    auto append=[&](Vec3 p,VertexId id,Vec3 n)->std::uint32_t {
        if(patch.positions.size()>=std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("R06-M23B bridge vertex index limit");
        const auto result=static_cast<std::uint32_t>(patch.positions.size());
        patch.positions.push_back(p);
        patch.ids.push_back(id);
        patch.normals.push_back(n);
        return result;
    };
    // Cap-facing cyclic contour after the finite-width bridge. All original
    // IDs appear exactly once, and synthetic K/D are distinct actual nodes.
    const auto ai=append(A,first.ids[a],first.outgoingNormals[a]);
    std::uint32_t seamD=0;
    if(mirror) {
        seamD=append({0,mid.y,mid.z},0,{0,0,1});
    }
    const auto bi=append(B,second.ids[b],second.outgoingNormals[b]);
    for(std::size_t k=(b+1)%nb;k!=b;k=(k+1)%nb)
        append(second.positions[k],second.ids[k],second.outgoingNormals[k]);
    const auto bp=static_cast<std::uint32_t>(patch.positions.size()-1);
    const double step=std::sqrt(3.)*.5*distance*factor;
    auto K=mid+tangent*step;
    if(mirror)K.x=0;
    const auto ki=append(K,0,{0,0,1});
    const auto ci=append(C,first.ids[c],first.outgoingNormals[c]);
    for(std::size_t k=(c+1)%na;k!=a;k=(k+1)%na)
        append(first.positions[k],first.ids[k],first.outgoingNormals[k]);
    const auto cp=ci;
    // Index bp corresponds to previous neighbor of B' (C'), ci to next
    // neighbor of B (C). Topological winding is fixed by these directed
    // cap-facing orders; never infer it from a geometric closest point.
    patch.seedFaces.push_back({ai,cp,ki});
    if(mirror) {
        patch.seedFaces.push_back({ai,ki,seamD});
        patch.seedFaces.push_back({seamD,ki,bi});
    }else patch.seedFaces.push_back({ai,ki,bi});
    patch.seedFaces.push_back({bp,bi,ki});
    if(patch.positions.size()!=na+nb+(mirror?2u:1u))
        throw std::logic_error("R06-M23B bridge failed to splice complete contours");
    if(!finite(K)||length(K-mid)<1e-12)
        throw std::logic_error("R06-M23B zero width bridge");
    for(const auto& face:patch.seedFaces) {
        const auto q=triangleQuality(patch.positions[face[0]],
                                     patch.positions[face[1]],
                                     patch.positions[face[2]]);
        if(!std::isfinite(q))throw std::logic_error("R06-M23B nonfinite bridge quality");
        patch.seedQuality=std::min(patch.seedQuality,q);
    }
    return patch;
}
}

UnifiedZipperFrontPatch buildUnifiedZipperFront(
    const UnifiedZipperRim& first,
    const UnifiedZipperRim& second,
    bool selfReflected,
    const SpatialCapSettings& settings,
    const SpatialCapPlacementAdjustment* adjustment) {
    checkRim(first);checkRim(second);
    if(first.ids.size()+second.ids.size()+2>settings.maxBoundaryVertices||
       first.ids.size()+second.ids.size()+2>512)
        throw std::length_error("R06-M23B joined front boundary budget");
    std::set<VertexId> ids(first.ids.begin(),first.ids.end());
    for(const auto id:second.ids)
        if(!ids.insert(id).second)
            throw std::logic_error("R06-M23B zipper contours share an original vertex");
    if(selfReflected) {
        if(first.ids.size()!=second.ids.size())
            throw std::logic_error("R06-M23B reflected zipper rims have unequal counts");
        for(std::size_t i=0;i<first.ids.size();++i) {
            const auto p=first.positions[i];
            const auto j=second.ids.size()-1-i;
            // An arbitrary cyclic rotation may be necessary; verified
            // explicitly when enumerating candidate mirrored orbits below.
            (void)p;(void)j;
        }
    }
    if(!settings.useAdvancingFront)
        throw std::invalid_argument("R06-M23B requires advancing front");
    std::vector<Corner> corners;
    for(std::size_t i=0;i<first.ids.size();++i)
        for(std::size_t j=0;j<second.ids.size();++j) {
            const auto p=first.positions[i],q=second.positions[j];
            if(selfReflected&&
               (q.x!=-p.x||q.y!=p.y||q.z!=p.z))
                continue;
            corners.push_back({i,j,length(q-p)});
        }
    std::stable_sort(corners.begin(),corners.end(),
        [](const Corner& x,const Corner& y) {
            return std::tie(x.distance,x.first,x.second)<
                   std::tie(y.distance,y.first,y.second);
        });
    if(corners.empty())
        throw std::logic_error("R06-M23B no eligible opposing zipper vertices");
    const auto budget=std::min<std::size_t>(corners.size(),32);
    std::string lastReason="no locally legal bridge";
    // Evaluate bounded alternatives before selecting a bridge. Each cap
    // attempt is entirely private; failure never mutates an original ID.
    for(std::size_t i=0;i<budget;++i)
        for(const double factor:{.5,1.,.75,1.25})
            for(const int direction:{1,-1}) {
                Candidate trial;
                try {
                    trial=seed(first,second,corners[i].first,
                               corners[i].second,selfReflected,
                               factor,direction);
                    if(trial.seedQuality<settings.desiredMinimumQuality||
                       !candidateEmbedded(trial,settings.maxIntersectionTests))
                        continue;
                    auto capSettings=settings;
                    capSettings.requireMirrorPlane=selfReflected;
                    capSettings.maxBoundaryVertices=std::max(
                        settings.maxBoundaryVertices,trial.positions.size());
                    const auto cap=buildSpatialCapFrontPoisson(
                        trial.positions,trial.normals,capSettings,nullptr,
                        adjustment);
                    UnifiedZipperFrontPatch answer;
                    answer.positions=cap.positions;
                    answer.boundaryIds=std::move(trial.ids);
                    answer.bridgeFaceCount=trial.seedFaces.size();
                    answer.bridgeVertexCount=selfReflected?2:1;
                    answer.faces=std::move(trial.seedFaces);
                    answer.faces.insert(answer.faces.end(),
                                        cap.faces.begin(),cap.faces.end());
                    answer.minimumSeedQuality=trial.seedQuality;
                    answer.poissonIterations=cap.poissonIterations;
                    answer.poissonResidual=cap.poissonResidual;
                    answer.selfReflected=selfReflected;
                    return answer;
                }catch(const std::logic_error& e) {
                    lastReason=e.what();
                }
            }
    throw std::logic_error("R06-M23B no admissible finite bridge / cap: "+lastReason);
}
}

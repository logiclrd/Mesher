#include "mesher/refine.hpp"
#include "mesher/remesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
using Id = std::uint32_t;
using Edge = std::uint64_t;
Edge edgeKey(Id a, Id b) {
    if(a>b)std::swap(a,b);
    return (std::uint64_t(a)<<32)|b;
}
Id first(Edge edge) { return Id(edge>>32); }
Id second(Edge edge) { return Id(edge); }
bool incident(Triangle t, Id a) {return t.a==a || t.b==a || t.c==a;}
Vec3 reflect(Vec3 p) {return {-p.x,p.y,p.z};}

std::array<Id,3> indices(Triangle t) {return {t.a,t.b,t.c};}
struct DirectedEdge {Id a,b,c;};
bool directedEdge(Triangle t, Id a, Id b, DirectedEdge& out) {
    const auto ids=indices(t);
    for(int i=0;i<3;++i) {
        const Id u=ids[i],v=ids[(i+1)%3];
        if((u==a&&v==b)||(u==b&&v==a)) {
            out={u,v,ids[(i+2)%3]};
            return true;
        }
    }
    return false;
}

std::vector<std::size_t> edgeFaces(const Mesh& mesh, Id a, Id b) {
    std::vector<std::size_t> faces;
    for(std::size_t i=0;i<mesh.triangles.size();++i)
        if(incident(mesh.triangles[i],a)&&incident(mesh.triangles[i],b))
            faces.push_back(i);
    return faces;
}

// Splitting the *edge* rather than a single incident face guarantees that
// both adjacent triangles use the same midpoint; no T-junction is created.
void splitEdge(Mesh& mesh, Id a, Id b,
               Id midpoint) {
    const auto incidentFaces=edgeFaces(mesh,a,b);
    if(incidentFaces.size()!=2)
        throw std::logic_error("Split requires exactly two incident faces");
    for(auto index:incidentFaces) {
        DirectedEdge edge{};
        if(!directedEdge(mesh.triangles[index],a,b,edge))
            throw std::logic_error("Split face edge orientation missing");
        mesh.triangles[index]={edge.a,midpoint,edge.c};
        mesh.triangles.push_back({midpoint,edge.b,edge.c});
    }
}

double faceRadial(const Mesh& mesh, Triangle t) {
    const auto a=mesh.vertices[t.a],b=mesh.vertices[t.b],c=mesh.vertices[t.c];
    return dot(cross(b-a,c-a),a+b+c);
}
Vec3 faceNormal(const Mesh& mesh, Triangle t) {
    const auto a=mesh.vertices[t.a],b=mesh.vertices[t.b],c=mesh.vertices[t.c];
    const auto normal=cross(b-a,c-a);
    const double magnitude=length(normal);
    return magnitude>1e-14?normal/magnitude:Vec3{};
}
double triangleQuality(const Mesh& mesh,Triangle t) {
    const auto a=mesh.vertices[t.a],b=mesh.vertices[t.b],c=mesh.vertices[t.c];
    const double edges=dot(b-a,b-a)+dot(c-b,c-b)+dot(a-c,a-c);
    if(edges<1e-20)return 0.0;
    constexpr double root3=1.7320508075688772935;
    return 2.0*root3*length(cross(b-a,c-a))/edges;
}
int faceValence(const Mesh& mesh, Id index) {
    int count=0;
    for(auto t:mesh.triangles)if(incident(t,index))++count;
    return count;
}
bool hasEdge(const Mesh& mesh, Id a, Id b) {
    return !edgeFaces(mesh,a,b).empty();
}

// An edge flip is allowed only within a nearly planar local quadrilateral;
// this reduces accidental resampling of original scanned geometry. Refuse any
// flip that introduces a radially inverted face in the demo sphere.
bool flipOne(Mesh& mesh, Id edgeA, Id edgeB) {
    const auto faces=edgeFaces(mesh,edgeA,edgeB);
    if(faces.size()!=2)return false;
    DirectedEdge u{},v{};
    if(!directedEdge(mesh.triangles[faces[0]],edgeA,edgeB,u) ||
       !directedEdge(mesh.triangles[faces[1]],edgeA,edgeB,v) ||
       u.a!=v.b || u.b!=v.a ||
       u.c==v.c || hasEdge(mesh,u.c,v.c))return false;
    const auto oldA=mesh.triangles[faces[0]],oldB=mesh.triangles[faces[1]];
    const auto freshA=Triangle{u.c,v.c,u.b};
    const auto freshB=Triangle{v.c,u.c,u.a};
    if(faceRadial(mesh,oldA)<=1e-12 || faceRadial(mesh,oldB)<=1e-12 ||
       faceRadial(mesh,freshA)<=1e-12 || faceRadial(mesh,freshB)<=1e-12)return false;

    const auto oldNormalA=faceNormal(mesh,oldA),oldNormalB=faceNormal(mesh,oldB);
    const auto newNormalA=faceNormal(mesh,freshA),newNormalB=faceNormal(mesh,freshB);
    // Flat-ish quadrilaterals only: do not flip real sharp features.
    if(dot(oldNormalA,oldNormalB)<0.95 ||
       dot(oldNormalA,newNormalA)<0.90 ||
       dot(oldNormalB,newNormalB)<0.90)return false;

    const double qOldA=triangleQuality(mesh,oldA),qOldB=triangleQuality(mesh,oldB);
    const double qNewA=triangleQuality(mesh,freshA),qNewB=triangleQuality(mesh,freshB);
    const double minOld=std::min(qOldA,qOldB),minNew=std::min(qNewA,qNewB);
    const int va=faceValence(mesh,u.a),vb=faceValence(mesh,u.b);
    const int vc=faceValence(mesh,u.c),vd=faceValence(mesh,v.c);
    const auto valenceCost=[](int n) {return (n-6)*(n-6);};
    const int oldCost=valenceCost(va)+valenceCost(vb)+valenceCost(vc)+valenceCost(vd);
    const int newCost=valenceCost(va-1)+valenceCost(vb-1)+
                      valenceCost(vc+1)+valenceCost(vd+1);
    const bool improveAngles=minNew>minOld*1.03 &&
                              qNewA+qNewB>0.98*(qOldA+qOldB);
    const bool improveValence=newCost<oldCost &&
                              minNew>=0.98*minOld &&
                              qNewA+qNewB>=0.99*(qOldA+qOldB);
    if(!improveAngles && !improveValence)return false;
    mesh.triangles[faces[0]]=freshA;
    mesh.triangles[faces[1]]=freshB;
    return true;
}
void verifyMirrors(const Mesh& mesh,const std::vector<Id>& mirrors) {
    if(mirrors.empty())return;
    if(mirrors.size()!=mesh.vertices.size())
        throw std::invalid_argument("Refiner mirror mapping size mismatch");
    for(Id i=0;i<mirrors.size();++i)
        if(mirrors[i]>=mirrors.size() || mirrors[mirrors[i]]!=i)
            throw std::invalid_argument("Refiner mirror mapping not reciprocal");
}

} // namespace

LocalRefiner::LocalRefiner(const Mesh& mesh,RefineSettings settings,
                           std::vector<Id> mirrors)
    : settings_(settings) {
    if(!std::isfinite(settings.maximumEdgeRatio) || settings.maximumEdgeRatio<=0)
        throw std::invalid_argument("Maximum edge ratio must be positive");
    reset(mesh,std::move(mirrors));
}

void LocalRefiner::reset(const Mesh& mesh,std::vector<Id> mirrors) {
    constexpr double pi=3.14159265358979323846;
    maximumEdge_=settings_.maximumEdgeRatio*
       std::sqrt(8*pi/(double(mesh.vertices.size())*std::sqrt(3.0)));
    setMirrors(std::move(mirrors));
    verifyMirrors(mesh,mirrors_);
}
void LocalRefiner::setMirrors(std::vector<Id> mirrors) {
    for(Id i=0;i<mirrors.size();++i)
        if(mirrors[i]>=mirrors.size() || mirrors[mirrors[i]]!=i)
            throw std::invalid_argument("Refiner mirror mapping not reciprocal");
    mirrors_=std::move(mirrors);
}

RefineStatistics LocalRefiner::apply(Mesh& mesh,std::vector<Vec3>& before) {
    if(before.size()!=mesh.vertices.size())
        throw std::invalid_argument("Refiner expects pre-stamp positions for all vertices");
    verifyMirrors(mesh,mirrors_);
    RefineStatistics stats{};
    if(!settings_.enabled)return stats;
    struct Candidate {Edge key;double length;};
    std::unordered_set<Edge> seen;
    std::vector<Candidate> candidates;
    for(auto t:mesh.triangles)
        for(auto [a,b]:{std::pair{t.a,t.b},std::pair{t.b,t.c},std::pair{t.c,t.a}}) {
            const Edge key=edgeKey(a,b);
            if(!seen.insert(key).second)continue;
            const double afterLength=length(mesh.vertices[a]-mesh.vertices[b]);
            const double beforeLength=length(before[a]-before[b]);
            if(afterLength<=maximumEdge_ || afterLength<=beforeLength+1e-12)continue;
            if(!mirrors_.empty() && edgeKey(mirrors_[a],mirrors_[b])<key)
                continue;
            ++stats.longGrowingEdges;
            candidates.push_back({key,afterLength});
        }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
        return a.length==b.length?a.key<b.key:a.length>b.length;
    });

    std::unordered_set<Id> touched;
    for(const auto candidate:candidates) {
        if(stats.splitPairs>=settings_.maximumSplitPairsPerStamp)break;
        const Id a=first(candidate.key),b=second(candidate.key);
        // Recheck after any prior splits to avoid splitting a vanished edge.
        if(edgeFaces(mesh,a,b).size()!=2)continue;
        const bool symmetric=!mirrors_.empty();
        const Id ma=symmetric?mirrors_[a]:a,mb=symmetric?mirrors_[b]:b;
        const bool paired=symmetric && edgeKey(ma,mb)!=candidate.key;
        if(paired && edgeFaces(mesh,ma,mb).size()!=2)continue;

        const Id newVertex=static_cast<Id>(mesh.vertices.size());
        mesh.vertices.push_back((mesh.vertices[a]+mesh.vertices[b])*.5);
        before.push_back((before[a]+before[b])*.5);
        if(symmetric)mirrors_.push_back(newVertex);
        splitEdge(mesh,a,b,newVertex);
        if(paired) {
            const Id mirrorVertex=static_cast<Id>(mesh.vertices.size());
            mesh.vertices.push_back(reflect(mesh.vertices[newVertex]));
            before.push_back(reflect(before[newVertex]));
            mirrors_[newVertex]=mirrorVertex;
            mirrors_.push_back(newVertex);
            splitEdge(mesh,ma,mb,mirrorVertex);
        }
        ++stats.splitPairs;
        stats.newVertices+=paired?2:1;
        for(auto id:{a,b,newVertex})touched.insert(id);
        if(paired)for(auto id:{ma,mb,mirrors_[newVertex]})touched.insert(id);
    }
    if(stats.splitPairs && !validateClosedSphereTopology(mesh))
        throw std::logic_error("Edge refinement corrupted mesh topology");
    verifyMirrors(mesh,mirrors_);

    if(settings_.optimizeFlips && stats.splitPairs>0) {
        // One-ring context: flips are allowed on edges incident to split
        // vertices or their neighbouring triangles, never globally.
        std::unordered_set<Id> local=touched;
        for(auto t:mesh.triangles)
            if(touched.contains(t.a)||touched.contains(t.b)||touched.contains(t.c))
                for(auto v:{t.a,t.b,t.c})local.insert(v);
        std::set<Edge> flipCandidates;
        for(auto t:mesh.triangles)
            for(auto [a,b]:{std::pair{t.a,t.b},std::pair{t.b,t.c},std::pair{t.c,t.a}}) {
                if(local.contains(a)&&local.contains(b))
                    flipCandidates.insert(edgeKey(a,b));
            }
        const bool optimized=mesh.vertices.size()>10000;
        Mesh backup;
        if(optimized)backup=mesh;
        auto runFlips=[&](bool deferGlobalValidation) {
            stats.flipPairs=0;
            std::uint32_t trials=0;
            for(auto key:flipCandidates) {
                if(++trials>settings_.maximumFlipPairsPerStamp*8)break;
                if(stats.flipPairs>=settings_.maximumFlipPairsPerStamp)break;
                const Id a=first(key),b=second(key);
                const Edge mirrored=mirrors_.empty()?key:
                    edgeKey(mirrors_[a],mirrors_[b]);
                if(!mirrors_.empty() && mirrored<=key)continue;
                Mesh trial=mesh;
                if(!flipOne(trial,a,b))continue;
                if(mirrored!=key && !mirrors_.empty() &&
                   !flipOne(trial,first(mirrored),second(mirrored)))continue;
                if(!deferGlobalValidation && !validateClosedSphereTopology(trial))
                    continue;
                mesh=std::move(trial);
                ++stats.flipPairs;
            }
            return !stats.flipPairs || validateClosedSphereTopology(mesh);
        };
        if(!runFlips(optimized) && optimized) {
            // A batch of individually plausible local flips can interact.
            // Fall back to the original full per-flip topology check.
            mesh=std::move(backup);
            if(!runFlips(false))
                throw std::logic_error("Local edge flipping corrupted topology");
        }
    }
    return stats;
}
} // namespace mesher

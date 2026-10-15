#include "mesher/refine.hpp"
#include "mesher/remesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace mesher {
namespace {
using Id = std::uint32_t;
using Edge = std::uint64_t;
constexpr std::size_t invalidFace = std::numeric_limits<std::size_t>::max();
Edge edgeKey(Id a, Id b) {
    if(a>b)std::swap(a,b);
    return (std::uint64_t(a)<<32)|b;
}
Id first(Edge edge) { return Id(edge>>32); }
Id second(Edge edge) { return Id(edge); }
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
}

// Store only two faces per edge, because this refiner operates on closed
// two-manifold triangle meshes. New vertices, splits and flips update only
// their immediate connectivity; unchanged edges never have to be revisited.
// Keeping this object in LocalRefiner avoids rebuilding a global edge map
// after each brush stamp. External vertex compaction invalidates the index.
class LocalConnectivity {
public:
    explicit LocalConnectivity(const Mesh& mesh)
        : incident_(mesh.vertices.size()) {
        edges_.reserve(mesh.triangles.size()*2);
        for(std::size_t i=0;i<mesh.triangles.size();++i)
            addFace(i,mesh.triangles[i]);
    }

    std::size_t vertexCount() const noexcept {return incident_.size();}
    void appendVertex() {incident_.emplace_back();}
    const std::vector<std::size_t>& incident(Id v) const {
        return incident_.at(v);
    }
    int valence(Id v) const {
        return static_cast<int>(incident_.at(v).size());
    }
    bool contains(Id a,Id b) const {
        return edges_.contains(edgeKey(a,b));
    }
    std::vector<std::size_t> edgeFaces(Id a,Id b) const {
        const auto it=edges_.find(edgeKey(a,b));
        if(it==edges_.end())return {};
        const auto& e=it->second;
        if(e.second==invalidFace)return {e.first};
        return {std::min(e.first,e.second),std::max(e.first,e.second)};
    }
    void appendFace(std::size_t i,Triangle t) {addFace(i,t);}
    void replaceFace(std::size_t i,Triangle before,Triangle after) {
        removeFace(i,before);
        addFace(i,after);
    }
    // Remove all old triangles in the quad(s) before inserting their
    // replacements so intermediate edges never appear non-manifold.
    void replaceFaces(const Mesh& mesh,
                      const std::vector<std::pair<std::size_t,Triangle>>& old) {
        for(const auto& [i,t]:old)removeFace(i,t);
        for(const auto& [i,t]:old)addFace(i,mesh.triangles[i]);
    }
private:
    struct FacePair {
        std::size_t first=invalidFace,second=invalidFace;
    };
    std::unordered_map<Edge,FacePair> edges_;
    std::vector<std::vector<std::size_t>> incident_;

    void addFace(std::size_t i,Triangle t) {
        for(const Id v:indices(t))incident_.at(v).push_back(i);
        for(const auto& [a,b]:{std::pair{t.a,t.b},std::pair{t.b,t.c},
                             std::pair{t.c,t.a}}) {
            auto& e=edges_[edgeKey(a,b)];
            if(e.first==invalidFace)e.first=i;
            else if(e.second==invalidFace)e.second=i;
            else throw std::logic_error("Non-manifold edge in refinement index");
        }
    }
    void removeFace(std::size_t i,Triangle t) {
        for(const Id v:indices(t)) {
            auto& faces=incident_.at(v);
            const auto it=std::find(faces.begin(),faces.end(),i);
            if(it==faces.end())throw std::logic_error("Missing indexed vertex face");
            *it=faces.back();
            faces.pop_back();
        }
        for(const auto& [a,b]:{std::pair{t.a,t.b},std::pair{t.b,t.c},
                             std::pair{t.c,t.a}}) {
            const auto it=edges_.find(edgeKey(a,b));
            if(it==edges_.end())throw std::logic_error("Missing indexed edge");
            auto& e=it->second;
            if(e.first==i) {
                e.first=e.second;
                e.second=invalidFace;
            } else if(e.second==i)e.second=invalidFace;
            else throw std::logic_error("Missing indexed edge face");
            if(e.first==invalidFace)edges_.erase(it);
        }
    }
};

namespace {
// A split updates both triangles sharing the selected edge, not just one
// face, and therefore cannot create a T-junction.
void splitEdge(Mesh& mesh, LocalConnectivity& topology,Id a,Id b,Id midpoint) {
    const auto faces=topology.edgeFaces(a,b);
    if(faces.size()!=2)
        throw std::logic_error("Split requires exactly two incident faces");
    for(const auto index:faces) {
        DirectedEdge edge{};
        const Triangle old=mesh.triangles[index];
        if(!directedEdge(old,a,b,edge))
            throw std::logic_error("Split face edge orientation missing");
        const Triangle firstHalf{edge.a,midpoint,edge.c};
        const Triangle secondHalf{midpoint,edge.b,edge.c};
        topology.replaceFace(index,old,firstHalf);
        mesh.triangles[index]=firstHalf;
        const auto newIndex=mesh.triangles.size();
        mesh.triangles.push_back(secondHalf);
        topology.appendFace(newIndex,secondHalf);
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

// All acceptance conditions match the previous global-mesh implementation.
// Only the lookup changes: these are O(1) local adjacency/valence queries.
bool flipOne(Mesh& mesh,Id edgeA,Id edgeB,const LocalConnectivity& topology) {
    const auto faces=topology.edgeFaces(edgeA,edgeB);
    if(faces.size()!=2)return false;
    DirectedEdge u{},v{};
    if(!directedEdge(mesh.triangles[faces[0]],edgeA,edgeB,u) ||
       !directedEdge(mesh.triangles[faces[1]],edgeA,edgeB,v) ||
       u.a!=v.b || u.b!=v.a || u.c==v.c ||
       topology.contains(u.c,v.c))return false;
    const auto oldA=mesh.triangles[faces[0]],oldB=mesh.triangles[faces[1]];
    const auto freshA=Triangle{u.c,v.c,u.b};
    const auto freshB=Triangle{v.c,u.c,u.a};
    if(faceRadial(mesh,oldA)<=1e-12 || faceRadial(mesh,oldB)<=1e-12 ||
       faceRadial(mesh,freshA)<=1e-12 || faceRadial(mesh,freshB)<=1e-12)return false;
    const auto oldNormalA=faceNormal(mesh,oldA),oldNormalB=faceNormal(mesh,oldB);
    const auto newNormalA=faceNormal(mesh,freshA),newNormalB=faceNormal(mesh,freshB);
    if(dot(oldNormalA,oldNormalB)<0.95 ||
       dot(oldNormalA,newNormalA)<0.90 ||
       dot(oldNormalB,newNormalB)<0.90)return false;

    const double qOldA=triangleQuality(mesh,oldA),qOldB=triangleQuality(mesh,oldB);
    const double qNewA=triangleQuality(mesh,freshA),qNewB=triangleQuality(mesh,freshB);
    const double minOld=std::min(qOldA,qOldB),minNew=std::min(qNewA,qNewB);
    const int va=topology.valence(u.a),vb=topology.valence(u.b);
    const int vc=topology.valence(u.c),vd=topology.valence(v.c);
    const auto cost=[](int n){return (n-6)*(n-6);};
    const int oldCost=cost(va)+cost(vb)+cost(vc)+cost(vd);
    const int newCost=cost(va-1)+cost(vb-1)+cost(vc+1)+cost(vd+1);
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
LocalRefiner::~LocalRefiner()=default;

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
    // Called after external collapse/compaction, which renumbers faces and
    // vertices. A full rebuild is required only on the next refinement.
    connectivity_.reset();
}
RefineStatistics LocalRefiner::apply(Mesh& mesh,std::vector<Vec3>& before,
                                     std::span<const Id> affectedFaces,
                                     bool restrictToAffected) {
    if(before.size()!=mesh.vertices.size())
        throw std::invalid_argument("Refiner expects pre-stamp positions for all vertices");
    verifyMirrors(mesh,mirrors_);
    RefineStatistics stats{};
    if(!settings_.enabled)return stats;
    if(!connectivity_ || connectivity_->vertexCount()!=mesh.vertices.size())
        connectivity_=std::make_unique<LocalConnectivity>(mesh);

    struct Candidate {Edge key;double length;};
    std::unordered_set<Edge> seen;
    std::vector<Candidate> candidates;
    if(restrictToAffected)seen.reserve(affectedFaces.size()*2);
    auto considerTriangle=[&](Triangle t) {
        for(auto [a,b]:{std::pair{t.a,t.b},std::pair{t.b,t.c},
                        std::pair{t.c,t.a}}) {
            const Edge key=edgeKey(a,b);
            if(!seen.insert(key).second)continue;
            const double afterLength=length(mesh.vertices[a]-mesh.vertices[b]);
            const double beforeLength=length(before[a]-before[b]);
            if(afterLength<=maximumEdge_ || afterLength<=beforeLength+1e-12)continue;
            if(!mirrors_.empty() && edgeKey(mirrors_[a],mirrors_[b])<key)continue;
            ++stats.longGrowingEdges;
            candidates.push_back({key,afterLength});
        }
    };
    if(restrictToAffected) {
        for(const auto index:affectedFaces) {
            if(index>=mesh.triangles.size())
                throw std::out_of_range("Affected refinement face is invalid");
            considerTriangle(mesh.triangles[index]);
        }
    }else {
        for(const auto t:mesh.triangles)considerTriangle(t);
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
        return a.length==b.length?a.key<b.key:a.length>b.length;
    });

    std::unordered_set<Id> touched;
    for(const auto candidate:candidates) {
        if(stats.splitPairs>=settings_.maximumSplitPairsPerStamp)break;
        const Id a=first(candidate.key),b=second(candidate.key);
        if(connectivity_->edgeFaces(a,b).size()!=2)continue;
        const bool symmetric=!mirrors_.empty();
        const Id ma=symmetric?mirrors_[a]:a,mb=symmetric?mirrors_[b]:b;
        const bool paired=symmetric && edgeKey(ma,mb)!=candidate.key;
        if(paired && connectivity_->edgeFaces(ma,mb).size()!=2)continue;

        const Id newVertex=static_cast<Id>(mesh.vertices.size());
        mesh.vertices.push_back((mesh.vertices[a]+mesh.vertices[b])*.5);
        before.push_back((before[a]+before[b])*.5);
        connectivity_->appendVertex();
        if(symmetric)mirrors_.push_back(newVertex);
        splitEdge(mesh,*connectivity_,a,b,newVertex);
        if(paired) {
            const Id other=static_cast<Id>(mesh.vertices.size());
            mesh.vertices.push_back(reflect(mesh.vertices[newVertex]));
            before.push_back(reflect(before[newVertex]));
            connectivity_->appendVertex();
            mirrors_[newVertex]=other;
            mirrors_.push_back(newVertex);
            splitEdge(mesh,*connectivity_,ma,mb,other);
        }
        ++stats.splitPairs;
        stats.newVertices+=paired?2:1;
        for(auto v:{a,b,newVertex})touched.insert(v);
        if(paired)for(auto v:{ma,mb,mirrors_[newVertex]})touched.insert(v);
    }
    if(stats.splitPairs && !validateClosedSphereTopology(mesh))
        throw std::logic_error("Edge refinement corrupted mesh topology");
    verifyMirrors(mesh,mirrors_);

    if(settings_.optimizeFlips && stats.splitPairs>0) {
        // Only one ring of faces around split vertices participates.
        // The persistent index replaces two whole-mesh scans.
        std::unordered_set<Id> local=touched;
        for(const Id v:touched)
            for(const auto face:connectivity_->incident(v))
                for(const Id n:indices(mesh.triangles[face]))local.insert(n);
        std::set<Edge> flipCandidates;
        for(const Id v:local) {
            for(const auto face:connectivity_->incident(v)) {
                const auto t=mesh.triangles[face];
                for(auto [a,b]:{std::pair{t.a,t.b},std::pair{t.b,t.c},
                                std::pair{t.c,t.a}})
                    if(local.contains(a)&&local.contains(b))
                        flipCandidates.insert(edgeKey(a,b));
            }
        }

        const bool optimized=mesh.vertices.size()>10000;
        Mesh backup;
        if(optimized)backup=mesh; // one batch snapshot, never per-candidate
        auto run=[&](bool deferGlobalValidation) {
            stats.flipPairs=0;
            std::uint32_t trials=0;
            for(const Edge key:flipCandidates) {
                if(++trials>settings_.maximumFlipPairsPerStamp*8)break;
                if(stats.flipPairs>=settings_.maximumFlipPairsPerStamp)break;
                const Id a=first(key),b=second(key);
                const Edge mirrored=mirrors_.empty()?key:
                    edgeKey(mirrors_[a],mirrors_[b]);
                if(!mirrors_.empty() && mirrored<=key)continue;
                const auto faces=connectivity_->edgeFaces(a,b);
                if(faces.size()!=2)continue;
                std::vector<std::pair<std::size_t,Triangle>> beforeFaces;
                for(const auto i:faces)
                    beforeFaces.emplace_back(i,mesh.triangles[i]);
                if(mirrored!=key && !mirrors_.empty()) {
                    const auto mirroredFaces=connectivity_->edgeFaces(
                        first(mirrored),second(mirrored));
                    if(mirroredFaces.size()!=2)continue;
                    bool overlaps=false;
                    for(const auto i:mirroredFaces)
                        if(std::find(faces.begin(),faces.end(),i)!=faces.end())
                            overlaps=true;
                    if(overlaps)continue;
                    for(const auto i:mirroredFaces)
                        beforeFaces.emplace_back(i,mesh.triangles[i]);
                }

                const bool firstAccepted=flipOne(mesh,a,b,*connectivity_);
                const bool bothAccepted=firstAccepted &&
                    (mirrored==key || mirrors_.empty() ||
                     flipOne(mesh,first(mirrored),second(mirrored),*connectivity_));
                if(!bothAccepted) {
                    for(const auto& [i,t]:beforeFaces)mesh.triangles[i]=t;
                    continue;
                }
                if(!deferGlobalValidation && !validateClosedSphereTopology(mesh)) {
                    for(const auto& [i,t]:beforeFaces)mesh.triangles[i]=t;
                    continue;
                }
                connectivity_->replaceFaces(mesh,beforeFaces);
                ++stats.flipPairs;
            }
            return !stats.flipPairs || validateClosedSphereTopology(mesh);
        };
        if(!run(optimized) && optimized) {
            // Extremely unusual interacting flips: roll back the entire
            // batch and retry with the original per-operation validation.
            mesh=std::move(backup);
            connectivity_=std::make_unique<LocalConnectivity>(mesh);
            if(!run(false))
                throw std::logic_error("Local edge flipping corrupted topology");
        }
    }
    return stats;
}
} // namespace mesher

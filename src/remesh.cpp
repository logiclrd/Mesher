#include "mesher/remesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace mesher {
namespace {
using Index = std::uint32_t;
using Edge = std::uint64_t;
Edge edgeKey(Index a, Index b) {
    if (a > b) std::swap(a,b);
    return (std::uint64_t(a)<<32) | b;
}
Index first(Edge e) { return Index(e >> 32); }
Index second(Edge e) { return Index(e); }
double signedRadial(const Mesh& mesh, Triangle t) {
    const auto a=mesh.vertices[t.a], b=mesh.vertices[t.b], c=mesh.vertices[t.c];
    return dot(cross(b-a,c-a),a+b+c);
}
bool usedByFace(Triangle t, Index a) { return t.a==a || t.b==a || t.c==a; }
bool linkCondition(const Mesh& mesh, Index a, Index b) {
    std::unordered_set<Index> an, bn, opposite;
    std::size_t sharedFaces=0;
    for (const auto t:mesh.triangles) {
        const bool hasA=usedByFace(t,a), hasB=usedByFace(t,b);
        if (hasA) {
            if (t.a!=a) an.insert(t.a);
            if (t.b!=a) an.insert(t.b);
            if (t.c!=a) an.insert(t.c);
        }
        if (hasB) {
            if (t.a!=b) bn.insert(t.a);
            if (t.b!=b) bn.insert(t.b);
            if (t.c!=b) bn.insert(t.c);
        }
        if (hasA && hasB) {
            ++sharedFaces;
            opposite.insert(t.a!=a && t.a!=b ? t.a :
                            t.b!=a && t.b!=b ? t.b : t.c);
        }
    }
    if (sharedFaces!=2 || opposite.size()!=2) return false;
    std::size_t intersection=0;
    for (auto i:an) if (bn.contains(i)) {
        ++intersection;
        if (!opposite.contains(i)) return false;
    }
    return intersection==2;
}
std::vector<Index> compact(Mesh& mesh) {
    const auto n=mesh.vertices.size();
    std::vector<Index> newIndex(n,std::numeric_limits<Index>::max());
    std::vector<Vec3> vertices;
    vertices.reserve(n);
    for (auto& t:mesh.triangles) {
        for (auto* i : {&t.a, &t.b, &t.c}) {
            if (newIndex[*i] == std::numeric_limits<Index>::max()) {
                newIndex[*i]=static_cast<Index>(vertices.size());
                vertices.push_back(mesh.vertices[*i]);
            }
            *i = newIndex[*i];
        }
    }
    mesh.vertices=std::move(vertices);
    return newIndex;
}
} // namespace

MeshDiagnostics measureMesh(const Mesh& mesh, std::span<const Vec3> before,
                            double minimumAllowedEdge) {
    if (!before.empty() && before.size()!=mesh.vertices.size())
        throw std::invalid_argument("Previous positions must match current mesh");
    MeshDiagnostics d;
    d.minimumEdgeLength=std::numeric_limits<double>::infinity();
    d.minimumTriangleAltitude=std::numeric_limits<double>::infinity();
    std::unordered_set<Edge> visited;
    visited.reserve(mesh.triangles.size()*2);
    for (const auto t:mesh.triangles) {
        const auto a=mesh.vertices.at(t.a), b=mesh.vertices.at(t.b), c=mesh.vertices.at(t.c);
        const double ab=length(b-a), bc=length(c-b), ca=length(a-c);
        const double longest=std::max({ab,bc,ca});
        const double area2=length(cross(b-a,c-a));
        if (longest>0) d.minimumTriangleAltitude=std::min(d.minimumTriangleAltitude,area2/longest);
        else d.minimumTriangleAltitude=0;
        if (dot(cross(b-a,c-a),a+b+c)<=1e-13) ++d.invertedFaces;
        for (const auto& pair : {std::pair{t.a,t.b},std::pair{t.b,t.c},std::pair{t.c,t.a}}) {
            if (!visited.insert(edgeKey(pair.first,pair.second)).second) continue;
            const double after=length(mesh.vertices[pair.first]-mesh.vertices[pair.second]);
            d.minimumEdgeLength=std::min(d.minimumEdgeLength,after);
            if (!before.empty() && after < minimumAllowedEdge &&
                after + 1e-12 < length(before[pair.first]-before[pair.second]))
                ++d.shrinkingShortEdges;
        }
    }
    if (!std::isfinite(d.minimumEdgeLength)) d.minimumEdgeLength=0;
    if (!std::isfinite(d.minimumTriangleAltitude)) d.minimumTriangleAltitude=0;
    return d;
}

bool validateClosedSphereTopology(const Mesh& mesh) {
    const auto n=mesh.vertices.size();
    if (n<4 || mesh.triangles.size()<4) return false;
    struct Incidence {int count=0, orientation=0;};
    std::unordered_map<Edge,Incidence> edges;
    std::vector<Index> parent(n);
    std::vector<unsigned char> referenced(n);
    std::iota(parent.begin(),parent.end(),0);
    auto root=[&](Index a) {
        while(parent[a]!=a) { parent[a]=parent[parent[a]]; a=parent[a]; }
        return a;
    };
    for (const auto t:mesh.triangles) {
        if (t.a>=n||t.b>=n||t.c>=n||t.a==t.b||t.b==t.c||t.c==t.a)
            return false;
        for (Index i:{t.a,t.b,t.c}) referenced[i]=1;
        for (auto [a,b]:{std::pair{t.a,t.b},std::pair{t.b,t.c},std::pair{t.c,t.a}}) {
            auto& e=edges[edgeKey(a,b)];
            ++e.count;
            e.orientation += a < b ? 1 : -1;
            if(e.count>2) return false;
        }
        parent[root(t.a)]=root(t.b);
        parent[root(t.b)]=root(t.c);
    }
    for (const auto& [_,e]:edges)
        if(e.count!=2 || e.orientation!=0) return false;
    std::size_t used=0;
    Index connected=std::numeric_limits<Index>::max();
    for (Index i=0;i<n;++i) if(referenced[i]) {
        ++used;
        if(connected==std::numeric_limits<Index>::max()) connected=root(i);
        else if(root(i)!=connected) return false;
    }
    return std::int64_t(used)-std::int64_t(edges.size())+
           std::int64_t(mesh.triangles.size())==2;
}

std::vector<Index> symmetricSphereMirrors(Index n) {
    constexpr double pi=3.1415926535897932384626433832795;
    if (n<8) throw std::invalid_argument("Sphere requires at least 8 vertices");
    const double spacing=std::sqrt(8*pi/(double(n)*std::sqrt(3.0)));
    auto m=static_cast<Index>(std::floor(2*pi/spacing))+1;
    if((n-m)%2)++m;
    if(m>=n) throw std::invalid_argument("No symmetric hemisphere vertices");
    const Index p=(n-m)/2;
    std::vector<Index> result(n);
    for(Index i=0;i<p;++i) {
        result[i]=i+p+m;
        result[i+p+m]=i;
    }
    for(Index i=p;i<p+m;++i) result[i]=i;
    return result;
}

DirectionalCollapser::DirectionalCollapser(const Mesh& mesh, CollapseSettings settings,
                                           std::vector<Index> mirrors)
    : settings_(settings), minimumEdge_(0), mirrors_(std::move(mirrors)) {
    if (!std::isfinite(settings.minimumEdgeRatio) || settings.minimumEdgeRatio<=0)
        throw std::invalid_argument("Minimum edge ratio must be positive");
    reset(mesh, mirrors_);
}

void DirectionalCollapser::reset(const Mesh& mesh, std::vector<Index> mirrors) {
    const double pi=3.1415926535897932384626433832795;
    minimumEdge_=settings_.minimumEdgeRatio*
        std::sqrt(8*pi/(double(mesh.vertices.size())*std::sqrt(3.0)));
    mirrors_=std::move(mirrors);
    if (!mirrors_.empty()) {
        if (mirrors_.size()!=mesh.vertices.size())
            throw std::invalid_argument("Mirror map size mismatch");
        for(Index i=0;i<mirrors_.size();++i)
            if(mirrors_[i]>=mirrors_.size() || mirrors_[mirrors_[i]]!=i)
                throw std::invalid_argument("Mirror map must be an involution");
    }
}

void DirectionalCollapser::setMirrors(std::vector<Index> mirrors) {
    if(!mirrors.empty())
        for(Index i=0;i<mirrors.size();++i)
            if(mirrors[i]>=mirrors.size() || mirrors[mirrors[i]]!=i)
                throw std::invalid_argument("Mirror map must be an involution");
    mirrors_=std::move(mirrors);
}

bool DirectionalCollapser::attempt(Mesh& mesh, Index a, Index b) {
    if(a==b || !linkCondition(mesh,a,b)) return false;
    const bool symmetry=!mirrors_.empty();
    // Keep a seam vertex rather than displacing it outside its plane.
    if(symmetry && mirrors_[b]==b && mirrors_[a]!=a) std::swap(a,b);
    const Index ma=symmetry ? mirrors_[a] : a;
    const Index mb=symmetry ? mirrors_[b] : b;
    const bool paired=symmetry && edgeKey(ma,mb)!=edgeKey(a,b);
    if(paired && !linkCondition(mesh,ma,mb)) return false;
    if(paired && (b==ma || b==mb || a==mb)) return false;

    Mesh trial=mesh;
    auto nextMirrors=mirrors_;
    const Vec3 midpoint=(mesh.vertices[a]+mesh.vertices[b])*.5;
    // A collapsed edge joining a seam vertex to a hemisphere vertex keeps
    // the original seam position. All other collapses use the midpoint.
    if(!symmetry || mirrors_[a]!=a || mirrors_[b]==b)
        trial.vertices[a]=midpoint;
    if(paired && ma!=a) {
        trial.vertices[ma]=(mesh.vertices[ma]+mesh.vertices[mb])*.5;
    }
    for (std::size_t i=0;i<trial.triangles.size();++i) {
        auto& t=trial.triangles[i];
        auto redirect=[&](Index& v) {
            if(v==b) v=a;
            else if(paired && v==mb) v=ma;
        };
        const auto old=mesh.triangles[i];
        redirect(t.a);redirect(t.b);redirect(t.c);
        if(t.a==t.b || t.b==t.c || t.a==t.c) continue;
        const bool affected=usedByFace(old,a)||usedByFace(old,b)||
                            (paired && (usedByFace(old,ma)||usedByFace(old,mb)));
        if(affected) {
            const auto pa=trial.vertices[t.a],pb=trial.vertices[t.b],pc=trial.vertices[t.c];
            if(length(cross(pb-pa,pc-pa))<1e-12) return false;
            // Don't introduce a new inverted face; existing bad faces are
            // retained for experimentation rather than erased or hidden.
            if(signedRadial(mesh,old)>1e-13 && signedRadial(trial,t)<=1e-13)
                return false;
        }
    }
    std::erase_if(trial.triangles, [](Triangle t) {
        return t.a==t.b || t.b==t.c || t.c==t.a;
    });
    if(!validateClosedSphereTopology(trial)) return false;
    if(symmetry) {
        nextMirrors[a]=ma;
        nextMirrors[ma]=a;
        // Removed vertices are orphaned until the final compact pass.
        nextMirrors[b]=b;
        if(paired) nextMirrors[mb]=mb;
    }
    mesh=std::move(trial);
    mirrors_=std::move(nextMirrors);
    return true;
}

CollapseStatistics DirectionalCollapser::apply(Mesh& mesh, std::span<const Vec3> before) {
    if(before.size()!=mesh.vertices.size())
        throw std::invalid_argument("Must supply all pre-stamp positions");
    CollapseStatistics stats{};
    if(!settings_.enabled || settings_.maximumPairsPerStamp==0) return stats;
    struct Candidate {Edge edge; double length;};
    std::unordered_set<Edge> seen;
    std::vector<Candidate> candidates;
    seen.reserve(mesh.triangles.size()*2);
    for(const auto t:mesh.triangles) {
        for(auto [a,b]:{std::pair{t.a,t.b},std::pair{t.b,t.c},std::pair{t.c,t.a}}) {
            const Edge e=edgeKey(a,b);
            if(!seen.insert(e).second) continue;
            if(!mirrors_.empty() && edgeKey(mirrors_[a],mirrors_[b])<e)
                continue;  // process mirrored edge once
            const double after=length(mesh.vertices[a]-mesh.vertices[b]);
            if(after >= minimumEdge_) continue;
            if(after+1e-12 >= length(before[a]-before[b])) continue;
            candidates.push_back({e,after});
        }
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){
        return a.length==b.length ? a.edge<b.edge : a.length<b.length;
    });
    const auto originalCount=mesh.vertices.size();
    for(const auto& candidate:candidates) {
        if(stats.accepted>=settings_.maximumPairsPerStamp) break;
        const auto a=first(candidate.edge), b=second(candidate.edge);
        // Since previously accepted collapses may delete either endpoint,
        // linkCondition() also checks that the candidate edge still exists.
        ++stats.attempted;
        if(attempt(mesh,a,b)) ++stats.accepted;
    }
    if(stats.accepted) {
        const auto index=compact(mesh);
        if(!mirrors_.empty()) {
            std::vector<Index> remapped(mesh.vertices.size());
            for(Index i=0;i<index.size();++i) {
                if(index[i]==std::numeric_limits<Index>::max()) continue;
                const Index partner=mirrors_[i];
                if(index[partner]==std::numeric_limits<Index>::max())
                    throw std::logic_error("Paired remesh left unpaired vertex");
                remapped[index[i]]=index[partner];
            }
            mirrors_=std::move(remapped);
        }
        stats.removedVertices=static_cast<Index>(originalCount-mesh.vertices.size());
        if(!validateClosedSphereTopology(mesh))
            throw std::logic_error("Edge collapse corrupted mesh topology");
    }
    return stats;
}

} // namespace mesher

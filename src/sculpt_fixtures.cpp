#include "mesher/sculpt_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mesher {
namespace {
constexpr double pi=3.1415926535897932384626433832795;
constexpr double tau=2*pi;
constexpr double majorRadius=.8,minorRadius=.2;
using Index=std::uint32_t;
using Loop=std::vector<Index>;
using Edge=std::pair<Index,Index>;
Edge edge(Index a,Index b){return std::minmax(a,b);}
double unit(std::mt19937_64& engine) {
    return std::generate_canonical<double,53>(engine);
}
double wrap(double value) {
    value=std::fmod(value,tau);
    return value<0?value+tau:value;
}
Vec3 torusPoint(double u,double v) {
    const double r=majorRadius+minorRadius*std::cos(v);
    // Toroidal hole is centered on the Y axis, facing the default camera.
    return {r*std::cos(u),minorRadius*std::sin(v),r*std::sin(u)};
}
double areaFraction(double v) {
    // The exact torus surface-area Jacobian is r(R+r*cos(v)).
    return (majorRadius*v+minorRadius*std::sin(v))/(tau*majorRadius);
}
double inverseAreaFraction(double area) {
    // Strictly increasing: majorRadius > minorRadius.
    double v=tau*area;
    for(int i=0;i<10;++i) {
        const double delta=(areaFraction(v)-area)*tau*majorRadius/
            (majorRadius+minorRadius*std::cos(v));
        v-=delta;
        if(std::abs(delta)<1e-13)break;
    }
    return v;
}
Loop orientedAzimuthLoop(const Mesh& mesh,Loop loop) {
    if(loop.size()<3)throw std::runtime_error("Connector boundary too short");
    // Preserve actual topological neighbor order. Reversing and rotating it
    // is safe; SORTING by angle would incorrectly rewire an irregular loop.
    double projectedArea=0;
    for(std::size_t i=0;i<loop.size();++i) {
        const auto a=mesh.vertices[loop[i]],b=mesh.vertices[loop[(i+1)%loop.size()]];
        projectedArea+=a.x*b.z-a.z*b.x;
    }
    if(std::abs(projectedArea)<1e-12)
        throw std::runtime_error("Mouth boundary has ambiguous projected winding");
    if(projectedArea<0)std::reverse(loop.begin(),loop.end());
    const auto first=std::min_element(loop.begin(),loop.end(),[&](Index a,Index b){
        return wrap(std::atan2(mesh.vertices[a].z,mesh.vertices[a].x))<
               wrap(std::atan2(mesh.vertices[b].z,mesh.vertices[b].x));
    });
    std::rotate(loop.begin(),first,loop.end());
    return loop;
}
std::vector<double> perimeterProgress(const Mesh& mesh,const Loop& loop) {
    std::vector<double> result(loop.size()+1,0);
    for(std::size_t i=0;i<loop.size();++i) {
        // XZ arclength enforces azimuth matching even when Y varies on a
        // jagged cut. World-space topology retains the original loop order.
        const auto a=mesh.vertices[loop[i]],b=mesh.vertices[loop[(i+1)%loop.size()]];
        const double dist=std::hypot(b.x-a.x,b.z-a.z);
        if(!(dist>1e-12))throw std::runtime_error("Degenerate connector perimeter edge");
        result[i+1]=result[i]+dist;
    }
    const double total=result.back();
    for(auto& p:result)p/=total;
    return result;
}
void stitch(Mesh& mesh,const Loop& a,const Loop& b) {
    if(a.size()<3||b.size()<3)
        throw std::runtime_error("Cannot stitch fewer than three boundary vertices");
    const auto sa=perimeterProgress(mesh,a),sb=perimeterProgress(mesh,b);
    std::size_t i=0,j=0;
    // Every source boundary edge participates in exactly one triangle.
    // The other strip uses the opposite direction along shared rings.
    while(i<a.size()||j<b.size()) {
        const bool advanceA=j==b.size()||
            (i<a.size()&&sa[i+1]<=sb[j+1]);
        if(advanceA) {
            mesh.triangles.push_back({a[i%a.size()],a[(i+1)%a.size()],
                                       b[j%b.size()]});
            ++i;
        }else {
            mesh.triangles.push_back({a[i%a.size()],b[(j+1)%b.size()],
                                       b[j%b.size()]});
            ++j;
        }
    }
}
struct EdgeUse {std::size_t face;bool forward;};
using EdgeMap=std::map<Edge,std::vector<EdgeUse>>;
EdgeMap edgesOf(const Mesh& mesh) {
    EdgeMap result;
    for(std::size_t i=0;i<mesh.triangles.size();++i) {
        const auto t=mesh.triangles[i];
        for(const auto p:std::array<Index,3>{t.a,t.b,t.c})
            if(p>=mesh.vertices.size())throw std::runtime_error("Bad fixture vertex reference");
        if(t.a==t.b||t.b==t.c||t.c==t.a)
            throw std::runtime_error("Degenerate fixture triangle IDs");
        const auto a=mesh.vertices[t.a],b=mesh.vertices[t.b],c=mesh.vertices[t.c];
        if(!(length(cross(b-a,c-a))>1e-12))
            throw std::runtime_error("Geometrically degenerate fixture triangle");
        for(const auto& e:std::array<std::pair<Index,Index>,3>{{
            {t.a,t.b},{t.b,t.c},{t.c,t.a}}})
            result[edge(e.first,e.second)].push_back({
                i,e.first<e.second});
    }
    return result;
}
void orientConnectedSurface(Mesh& mesh) {
    const auto edges=edgesOf(mesh);
    std::vector<std::vector<std::pair<std::size_t,bool>>> neighbors(mesh.triangles.size());
    for(const auto& [id,uses]:edges) {
        (void)id;
        if(uses.size()!=2)
            throw std::runtime_error("Fixture does not have two faces per edge");
        const bool equal=uses[0].forward==uses[1].forward;
        neighbors[uses[0].face].push_back({uses[1].face,equal});
        neighbors[uses[1].face].push_back({uses[0].face,equal});
    }
    // First face belongs to the outward-wound *outer* original sphere.
    std::vector<int> parity(mesh.triangles.size(),-1);
    std::queue<std::size_t> work;
    parity[0]=0;
    work.push(0);
    while(!work.empty()) {
        const auto face=work.front();work.pop();
        for(const auto& [next,needFlip]:neighbors[face]) {
            const int wanted=parity[face]^static_cast<int>(needFlip);
            if(parity[next]==-1) {
                parity[next]=wanted;
                work.push(next);
            }else if(parity[next]!=wanted)
                throw std::runtime_error("Fixture surface is non-orientable");
        }
    }
    if(std::find(parity.begin(),parity.end(),-1)!=parity.end())
        throw std::runtime_error("Fixture unexpectedly has disconnected face components");
    for(std::size_t i=0;i<mesh.triangles.size();++i)
        if(parity[i])std::swap(mesh.triangles[i].b,mesh.triangles[i].c);
}
Loop boundaryLoop(const Mesh& mesh,std::size_t fromFace,std::size_t untilFace) {
    std::map<Edge,unsigned> counts;
    for(std::size_t f=fromFace;f<untilFace;++f) {
        const auto t=mesh.triangles[f];
        ++counts[edge(t.a,t.b)];
        ++counts[edge(t.b,t.c)];
        ++counts[edge(t.c,t.a)];
    }
    std::map<Index,std::vector<Index>> adjacency;
    for(const auto& [key,count]:counts) {
        if(count==1) {
            adjacency[key.first].push_back(key.second);
            adjacency[key.second].push_back(key.first);
        }else if(count!=2)throw std::runtime_error("Open shell has non-manifold edges");
    }
    if(adjacency.size()<3)throw std::runtime_error(
        "Not enough random cap vertices fell in the 0.1-radius cutter; change --seed or increase --vertices");
    for(const auto& [vertex,links]:adjacency) {
        (void)vertex;
        if(links.size()!=2)
            throw std::runtime_error("Sphere cap deletion has a branching boundary");
    }
    Loop ring;
    const auto start=adjacency.begin()->first;
    Index previous=std::numeric_limits<Index>::max(),current=start;
    while(true) {
        ring.push_back(current);
        const auto& nexts=adjacency.at(current);
        const auto next=nexts[0]==previous?nexts[1]:nexts[0];
        previous=current;current=next;
        if(current==start)break;
        if(ring.size()>adjacency.size())
            throw std::runtime_error("Sphere cap deletion formed an invalid boundary cycle");
    }
    if(ring.size()!=adjacency.size())
        throw std::runtime_error("Sphere cap deletion produced multiple disconnected holes");
    return orientedAzimuthLoop(mesh,std::move(ring));
}
} // namespace

FixtureTopology auditFixtureTopology(const Mesh& mesh) {
    if(mesh.vertices.size()<4||mesh.triangles.size()<4)
        throw std::runtime_error("Fixture is empty or has too few triangles");
    const auto edges=edgesOf(mesh);
    std::vector<std::map<Index,std::vector<Index>>> fanLinks(mesh.vertices.size());
    std::vector<std::vector<Index>> graph(mesh.vertices.size());
    for(const auto& [pair,uses]:edges) {
        if(uses.size()!=2||uses[0].forward==uses[1].forward)
            throw std::runtime_error("Fixture is not a consistently oriented closed edge manifold");
        graph[pair.first].push_back(pair.second);
        graph[pair.second].push_back(pair.first);
    }
    for(const auto t:mesh.triangles) {
        // Build each directed vertex-link fan ONCE, in linear total time.
        // Every active vertex must have a single cyclic manifold link.
        const auto append=[&](Index v,Index a,Index b) {
            fanLinks[v][a].push_back(b);
            fanLinks[v][b].push_back(a);
        };
        append(t.a,t.b,t.c);
        append(t.b,t.c,t.a);
        append(t.c,t.a,t.b);
    }
    for(Index v=0;v<mesh.vertices.size();++v) {
        const auto& adjacency=fanLinks[v];
        if(adjacency.empty())throw std::runtime_error("Unused fixture vertex");
        for(const auto& [a,others]:adjacency) {
            (void)a;
            if(others.size()!=2)throw std::runtime_error("Branching fixture vertex fan");
        }
        std::set<Index> reached;
        std::vector<Index> pending{adjacency.begin()->first};
        while(!pending.empty()) {
            const auto id=pending.back();pending.pop_back();
            if(!reached.insert(id).second)continue;
            for(const auto next:adjacency.at(id))pending.push_back(next);
        }
        if(reached.size()!=adjacency.size())
            throw std::runtime_error("Multiple fixture vertex-link fans");
    }
    std::vector<bool> visited(mesh.vertices.size(),false);
    std::size_t components=0;
    for(Index i=0;i<mesh.vertices.size();++i) {
        if(visited[i])continue;
        ++components;
        std::vector<Index> pending{i};
        visited[i]=true;
        while(!pending.empty()) {
            const auto v=pending.back();pending.pop_back();
            for(const auto n:graph[v])if(!visited[n]) {
                visited[n]=true;pending.push_back(n);
            }
        }
    }
    return {mesh.vertices.size(),edges.size(),mesh.triangles.size(),components,
            static_cast<std::int64_t>(mesh.vertices.size())-
            static_cast<std::int64_t>(edges.size())+
            static_cast<std::int64_t>(mesh.triangles.size())};
}

Mesh generateTorusFixture(std::uint32_t vertexCount,std::uint64_t seed,
                          std::uint32_t relaxationIterations) {
    if(vertexCount<128||vertexCount>1000000||relaxationIterations>10000)
        throw std::invalid_argument("Torus needs 128..1000000 vertices and <=10000 relaxation passes");
    const auto rows=std::max(std::uint32_t{8},
        static_cast<std::uint32_t>(std::sqrt(double(vertexCount)/4.0)));
    std::mt19937_64 rng(seed);
    Mesh mesh;
    mesh.vertices.reserve(vertexCount);
    std::vector<Loop> rings(rows);
    std::vector<std::pair<double,double>> homes;
    std::vector<double> halfCellsU;
    homes.reserve(vertexCount);
    halfCellsU.reserve(vertexCount);
    for(std::uint32_t j=0;j<rows;++j) {
        const auto count=vertexCount/rows+(j<vertexCount%rows?1u:0u);
        auto& ring=rings[j];
        ring.reserve(count);
        for(std::uint32_t i=0;i<count;++i) {
            // Area-uniform STRATIFIED random samples. Strictly monotone
            // cells make it possible to triangulate the resulting topology
            // on the doubly-periodic parameter domain without a convex hull.
            const double homeU=(double(i)+.5)/count;
            const double homeV=(double(j)+.5)/rows;
            const double u=(double(i)+.5+.40*(unit(rng)-.5))/count;
            const double v=(double(j)+.5+.40*(unit(rng)-.5))/rows;
            ring.push_back(static_cast<Index>(mesh.vertices.size()));
            homes.emplace_back(homeU,homeV);
            halfCellsU.push_back(.43/double(count));
            mesh.vertices.push_back(torusPoint(tau*u,inverseAreaFraction(v)));
        }
    }
    for(std::uint32_t j=0;j<rows;++j)
        stitch(mesh,rings[j],rings[(j+1)%rows]);
    // The XZ azimuth and increasing minor-meridian parameter define
    // a negative coordinate basis. Reverse the strip winding to face out.
    for(auto& tri:mesh.triangles)std::swap(tri.b,tri.c);

    // Relax against the already-defined 6-neighbor periodic topology.
    // Project displacements back to the analytic torus and keep each point
    // within its original stratified cell; faces cannot trade cyclic order.
    std::vector<std::vector<Index>> adjacent(vertexCount);
    for(const auto& [e,uses]:edgesOf(mesh)) {
        (void)uses;
        adjacent[e.first].push_back(e.second);
        adjacent[e.second].push_back(e.first);
    }
    for(std::uint32_t pass=0;pass<relaxationIterations;++pass) {
        auto next=mesh.vertices;
        for(Index i=0;i<vertexCount;++i) {
            Vec3 average{};
            for(const auto j:adjacent[i])average=average+mesh.vertices[j];
            average=average/double(adjacent[i].size());
            const auto p=mesh.vertices[i]+(average-mesh.vertices[i])*.18;
            double u=wrap(std::atan2(p.z,p.x))/tau;
            double v=wrap(std::atan2(p.y,std::hypot(p.x,p.z)-majorRadius))/tau;
            const auto [hu,hv]=homes[i];
            const auto unwrap=[](double x,double origin) {
                while(x-origin> .5)x-=1;
                while(x-origin<-.5)x+=1;
                return x;
            };
            u=unwrap(u,hu);
            v=unwrap(areaFraction(tau*v),hv);
            u=std::clamp(u,hu-halfCellsU[i],hu+halfCellsU[i]);
            v=std::clamp(v,hv-.43/rows,hv+.43/rows);
            next[i]=torusPoint(tau*u,inverseAreaFraction(wrap(tau*v)/tau));
        }
        mesh.vertices=std::move(next);
    }
    const auto audit=auditFixtureTopology(mesh);
    if(audit.components!=1||audit.eulerCharacteristic!=0||
       audit.faces!=2*vertexCount)
        throw std::runtime_error("Torus fixture has incorrect genus or connectivity");
    return mesh;
}

Mesh generateHollowSphereFixture(std::uint32_t vertexCount,std::uint64_t seed,
                                  std::uint32_t relaxationIterations) {
    if(vertexCount<1000||vertexCount>1000000||vertexCount%2||
       relaxationIterations>10000)
        throw std::invalid_argument("Hollow sphere needs an even 1000..1000000 vertices");
    SphereOptions options;
    options.symmetric=false;
    options.vertexCount=vertexCount/2;
    options.seed=seed;
    options.relaxationIterations=relaxationIterations;
    const auto outer=generateSphere(options);
    options.seed=seed+0x9e3779b97f4a7c15ULL;
    const auto inner=generateSphere(options);
    Mesh mesh;
    mesh.vertices.reserve(vertexCount+64);
    std::vector<Index> fromOuter(outer.vertices.size(),std::numeric_limits<Index>::max());
    std::vector<Index> fromInner(inner.vertices.size(),std::numeric_limits<Index>::max());
    const auto appendShell=[&](const Mesh& shell,double radius,
                               std::vector<Index>& remap,bool inverted) {
        for(std::size_t i=0;i<shell.vertices.size();++i) {
            const auto p=shell.vertices[i];
            // Clip on the NEGATIVE Y hemisphere against the Y-axis cylinder.
            if(p.y<0&&std::hypot(p.x,p.z)<.1)continue;
            remap[i]=static_cast<Index>(mesh.vertices.size());
            mesh.vertices.push_back(p*radius);
        }
        for(const auto t:shell.triangles) {
            if(remap[t.a]==std::numeric_limits<Index>::max()||
               remap[t.b]==std::numeric_limits<Index>::max()||
               remap[t.c]==std::numeric_limits<Index>::max())continue;
            const auto a=remap[t.a],b=remap[t.b],c=remap[t.c];
            mesh.triangles.push_back(inverted?Triangle{a,c,b}:Triangle{a,b,c});
        }
    };
    appendShell(outer,1,fromOuter,false);
    const auto afterOuter=mesh.triangles.size();
    appendShell(inner,.95,fromInner,true);
    const auto afterInner=mesh.triangles.size();
    // Boundary extraction is on the preserved ORIGINAL topology. Exactly
    // one oriented simple opening must exist on each sphere.
    const auto outerHole=boundaryLoop(mesh,0,afterOuter);
    const auto innerHole=boundaryLoop(mesh,afterOuter,afterInner);
    const auto steps=std::max<std::size_t>({8,outerHole.size(),innerHole.size()});
    if(steps>256)throw std::runtime_error("Excessive hollow-shell connector density");
    Loop outerRing,innerRing;
    for(std::size_t i=0;i<steps;++i) {
        const double angle=tau*i/steps;
        outerRing.push_back(static_cast<Index>(mesh.vertices.size()));
        mesh.vertices.push_back({.095*std::cos(angle),-.96,.095*std::sin(angle)});
        const double shifted=tau*(i+.5)/steps;
        innerRing.push_back(static_cast<Index>(mesh.vertices.size()));
        mesh.vertices.push_back({.095*std::cos(shifted),-.93,.095*std::sin(shifted)});
    }
    stitch(mesh,outerHole,outerRing);
    stitch(mesh,outerRing,innerRing);
    stitch(mesh,innerRing,innerHole);
    orientConnectedSurface(mesh);
    const auto audit=auditFixtureTopology(mesh);
    if(audit.components!=1||audit.eulerCharacteristic!=2)
        throw std::runtime_error("Hollow shell is not one closed genus-zero manifold");
    return mesh;
}
} // namespace mesher

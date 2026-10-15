#include "mesher/mesh.hpp"

#include <algorithm>
#include <chrono>
#ifdef _OPENMP
#include <omp.h>
#endif
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace mesher {
#ifdef MESHER_USE_QHULL
std::vector<Triangle> quickHull(const std::vector<Vec3>& points);
#endif
namespace {
constexpr double pi = 3.1415926535897932384626433832795;

struct Face { std::uint32_t a,b,c; Vec3 normal; };
struct Edge { std::uint32_t a,b; bool operator==(const Edge&) const = default; };
struct EdgeHash {
    std::size_t operator()(Edge e) const noexcept {
        return (std::uint64_t(e.a)<<32) | e.b;
    }
};
Edge sortedEdge(std::uint32_t a, std::uint32_t b) {
    return {std::min(a,b), std::max(a,b)};
}
Face oriented(std::uint32_t a, std::uint32_t b, std::uint32_t c,
              const std::vector<Vec3>& points, Vec3 interior) {
    Vec3 normal = cross(points[b]-points[a], points[c]-points[a]);
    if (dot(normal, interior-points[a]) > 0) {
        std::swap(b,c);
        normal = normal * -1;
    }
    return {a,b,c,normal};
}

// Incremental convex hull. All source points lie on the sphere, so every
// point is an extreme vertex. The fixed tetrahedron centroid remains strictly
// inside the hull and gives us a stable orientation test.
std::vector<Face> convexHull(const std::vector<Vec3>& points) {
    if (points.size()<4) throw std::invalid_argument("Hull needs >= 4 points");
    const auto N=static_cast<std::uint32_t>(points.size());
    std::vector<std::uint32_t> order(N);
    std::iota(order.begin(),order.end(),0);
    const auto a=order[0];
    auto maxDistance = [&](auto score, std::uint32_t begin) {
        std::uint32_t best=begin;
        double value=-1;
        for (std::uint32_t i=begin;i<N;++i) {
            double s=score(points[order[i]]);
            if(s>value) { best=i; value=s; }
        }
        std::swap(order[begin],order[best]);
        return value;
    };
    const double d=maxDistance([&](Vec3 p){return dot(p-points[a],p-points[a]);},1);
    if(d<1e-20) throw std::runtime_error("Coincident hull vertices");
    const auto b=order[1];
    const double area=maxDistance([&](Vec3 p){auto n=cross(points[b]-points[a],p-points[a]); return dot(n,n);},2);
    if(area<1e-20) throw std::runtime_error("Collinear hull vertices");
    const auto c=order[2];
    const Vec3 normal=cross(points[b]-points[a],points[c]-points[a]);
    const double volume=maxDistance([&](Vec3 p){return std::abs(dot(normal,p-points[a]));},3);
    if(volume<1e-14) throw std::runtime_error("Coplanar hull vertices");
    const auto dIndex=order[3];
    const Vec3 interior=(points[a]+points[b]+points[c]+points[dIndex])/4;
    std::vector<Face> faces;
    faces.reserve(2*N+8);
    faces.push_back(oriented(a,b,c,points,interior));
    faces.push_back(oriented(a,dIndex,b,points,interior));
    faces.push_back(oriented(b,dIndex,c,points,interior));
    faces.push_back(oriented(c,dIndex,a,points,interior));

    struct Horizon { std::uint32_t a,b; int count=0; };
    for(std::uint32_t j=4;j<N;++j) {
        auto index=order[j];
        const Vec3 p=points[index];
        std::unordered_map<Edge,Horizon,EdgeHash> boundary;
        std::size_t out=0;
        bool visible=false;
        for(std::size_t i=0;i<faces.size();++i) {
            const auto f=faces[i];
            // Normal is unnormalized. Scale tolerance by its length.
            if(dot(f.normal,p-points[f.a]) > 1e-12*length(f.normal)) {
                visible=true;
                auto add=[&](std::uint32_t u,std::uint32_t v) {
                    auto& e=boundary[sortedEdge(u,v)];
                    e.a=u; e.b=v; ++e.count;
                };
                add(f.a,f.b); add(f.b,f.c); add(f.c,f.a);
            } else faces[out++]=f;
        }
        faces.resize(out);
        if(!visible) {
            // With points on a sphere this should not occur (except under
            // pathological floating-point coincidence).
            throw std::runtime_error("Hull omitted a point");
        }
        for(const auto& [key,e]:boundary) {
            if(e.count==1) {
                auto f=oriented(e.a,e.b,index,points,interior);
                if(length(f.normal)<1e-14) throw std::runtime_error("Degenerate hull face");
                faces.push_back(f);
            } else if(e.count!=2) throw std::runtime_error("Non-manifold hull horizon");
        }
    }
    return faces;
}

// A regularly sized spatial grid keeps each relaxation iteration near O(n).
// Neighbours within 2.2 nominal edge lengths repel tangentially on the sphere.
void relax(std::vector<Vec3>& points, std::size_t movable, double spacing,
           std::uint32_t iterations, bool mirrorHemisphere, Vec3 axis) {
    const double radius=2.2*spacing;
    const double radiusSquared=radius*radius;
    const double cell=radius;
    const int count=static_cast<int>(std::ceil(2.0/cell))+2;
    auto key=[&](Vec3 v) -> std::uint64_t {
        auto coord=[&](double x){return std::clamp(static_cast<int>(std::floor((x+1.01)/cell)),0,count-1);};
        return (std::uint64_t(coord(v.x))*count+coord(v.y))*count+coord(v.z);
    };
    auto gridPos=[&](Vec3 v){
        return std::array<int,3>{static_cast<int>(std::floor((v.x+1.01)/cell)),
                                 static_cast<int>(std::floor((v.y+1.01)/cell)),
                                 static_cast<int>(std::floor((v.z+1.01)/cell))};
    };
    // The mirrored hemisphere is included as a repulsive ghost population;
    // this prevents crowding against the seam without moving its ring vertices.
    const std::size_t mirrored=mirrorHemisphere ? movable : 0;
    std::vector<Vec3> samples;
    samples.reserve(points.size()+mirrored);
    for(std::uint32_t step=0;step<iterations;++step) {
        samples=points;
        if(mirrorHemisphere)
            for(std::size_t i=0;i<movable;++i)
                samples.push_back(points[i]-axis*(2*dot(points[i],axis)));
        std::unordered_map<std::uint64_t,std::vector<std::uint32_t>> bins;
        bins.reserve(samples.size()*2);
        for(std::uint32_t i=0;i<samples.size();++i) bins[key(samples[i])].push_back(i);
        std::vector<Vec3> next(points.begin(),points.begin()+movable);
        // Every vertex reads the frozen sample grid and writes only next[i].
        // Parallel relaxation is deterministic for a fixed input and thread
        // count, without atomic forces or in-place Gauss-Seidel effects.
        const auto& lookup=bins;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) if(movable>10000)
#endif
        for(std::int64_t i=0;i<static_cast<std::int64_t>(movable);++i) {
            const auto pos=points[i];
            const auto ijk=gridPos(pos);
            Vec3 force{};
            for(int dx=-1;dx<=1;++dx) for(int dy=-1;dy<=1;++dy) for(int dz=-1;dz<=1;++dz) {
                int ix=ijk[0]+dx,iy=ijk[1]+dy,iz=ijk[2]+dz;
                if(ix<0||iy<0||iz<0||ix>=count||iy>=count||iz>=count) continue;
                const auto it=lookup.find((std::uint64_t(ix)*count+iy)*count+iz);
                if(it==lookup.end()) continue;
                for(auto j:it->second) {
                    if(j==i) continue;
                    const Vec3 diff=pos-samples[j];
                    const double squared=dot(diff,diff);
                    if(squared<1e-20 || squared>=radiusSquared)continue;
                    const double dist=std::sqrt(squared);
                    // Repulsion is strongest at small separation.
                    const double weight=(radius-dist)/(radius*dist);
                    force=force+diff*weight;
                }
            }
            force=force-pos*dot(force,pos);
            const double f=length(force);
            if(f>1e-12) {
                const double stepSize=std::min(.18*spacing,.018*spacing*f);
                Vec3 candidate=normalized(pos+force*(stepSize/f));
                if(mirrorHemisphere && dot(candidate,axis)<1e-5) {
                    candidate=normalized(candidate+axis*(1e-5-dot(candidate,axis)));
                }
                next[i]=candidate;
            }
        }
        std::copy(next.begin(),next.end(),points.begin());
    }
}

Vec3 randomSphere(std::mt19937_64& rng) {
    std::uniform_real_distribution<double> unit(0.0,1.0);
    const double z=unit(rng)*2-1, phi=unit(rng)*2*pi;
    const double radial=std::sqrt(std::max(0.0,1-z*z));
    return {radial*std::cos(phi),radial*std::sin(phi),z};
}

} // namespace

double length(Vec3 v){return std::sqrt(dot(v,v));}
Vec3 normalized(Vec3 v){double m=length(v); if(m<1e-20) throw std::invalid_argument("Zero vector");return v/m;}

const char* sphereHullBackend() noexcept {
#ifdef MESHER_USE_QHULL
    return "Qhull (Quickhull)";
#else
    return "legacy quadratic fallback";
#endif
}

unsigned sphereRelaxationThreads() noexcept {
#ifdef _OPENMP
    return static_cast<unsigned>(omp_get_max_threads());
#else
    return 1;
#endif
}

Mesh generateSphere(const SphereOptions& opt, SphereGenerationTimings* timings) {
    using Clock=std::chrono::steady_clock;
    const auto t0=Clock::now();
    auto duration=[](auto a,auto b) {
        return std::chrono::duration<double>(b-a).count();
    };
    SphereGenerationTimings measured{};
#ifndef MESHER_USE_QHULL
    // Without Qhull the original incremental hull repeatedly checks all
    // existing faces. This is roughly quadratic and can silently cause
    // minute-long startup on otherwise ordinary 100k-point experiments.
    // Fail *before* relaxation so the missing dependency is unmistakable.
    if(opt.vertexCount>10000)
        throw std::runtime_error(
            "Large sphere generation requires Qhull; this build uses the "
            "slow quadratic fallback. Install libqhull-dev (Ubuntu/Debian), "
            "then reconfigure CMake and rebuild. "
            "Check CMake's 'Using Qhull' message.");
#endif
    const auto n=opt.vertexCount;
    if(n<8||n>1000000) throw std::invalid_argument("vertexCount must be in [8, 1000000]");
    const Vec3 axis=normalized(opt.seamNormal);
    std::mt19937_64 rng(opt.seed);
    Mesh result;
    const double spacing=std::sqrt(8*pi/(double(n)*std::sqrt(3.0)));
    auto triangulate=[&](const std::vector<Vec3>& vertices) {
#ifdef MESHER_USE_QHULL
        return quickHull(vertices);
#else
        std::vector<Triangle> triangles;
        for(const auto f:convexHull(vertices))
            triangles.push_back({f.a,f.b,f.c});
        return triangles;
#endif
    };
    if(!opt.symmetric) {
        result.vertices.reserve(n);
        for(std::uint32_t i=0;i<n;++i)result.vertices.push_back(randomSphere(rng));
        auto t=Clock::now();
        relax(result.vertices,n,spacing,opt.relaxationIterations,false,axis);
        measured.relaxationSeconds=duration(t,Clock::now());
        t=Clock::now();
        result.triangles=triangulate(result.vertices);
        measured.triangulationSeconds=duration(t,Clock::now());
    } else {
        // Minimum ring population consistent with approximately equal triangle
        // edge lengths. Same parity as n gives p=n-m even.
        std::uint32_t m=static_cast<std::uint32_t>(std::floor(2*pi/spacing))+1;
        if((n-m)%2!=0) ++m;
        if(m>=n) throw std::invalid_argument("Too few vertices for symmetric sphere");
        const std::uint32_t p=(n-m)/2;
        // Pick an orthonormal coordinate system for the symmetry plane.
        const Vec3 helper=std::abs(axis.z)<.9 ? Vec3{0,0,1}:Vec3{0,1,0};
        const Vec3 tangent=normalized(cross(helper,axis));
        const Vec3 bitangent=cross(axis,tangent);
        std::uniform_real_distribution<double> unit(0.0,1.0);
        const double phase=unit(rng)*2*pi;
        // Hemisphere's vertices are first so pinned ring vertices are after
        // the movable population for the relaxation function.
        std::vector<Vec3> half;
        half.reserve(p+m);
        for(std::uint32_t i=0;i<p;++i){
            Vec3 v=randomSphere(rng);
            if(dot(v,axis)<0)v=v-axis*(2*dot(v,axis));
            half.push_back(v);
        }
        // Start with random positive angular gaps, then relax the gaps
        // iteratively toward the equal-spacing equilibrium around the circle.
        // This preserves the ring's order and never moves it during the later
        // hemisphere relaxation.
        std::exponential_distribution<double> gapDistribution(1.0);
        std::vector<double> gaps(m);
        double total=0;
        for(auto& gap:gaps){gap=gapDistribution(rng);total+=gap;}
        for(auto& gap:gaps)gap*=2*pi/total;
        const double idealGap=2*pi/double(m);
        for(int iteration=0;iteration<100;++iteration)
            for(auto& gap:gaps)gap=0.85*gap+0.15*idealGap;
        double phi=phase;
        for(auto gap:gaps){
            half.push_back(tangent*std::cos(phi)+bitangent*std::sin(phi));
            phi+=gap;
        }
        auto t=Clock::now();
        relax(half,p,spacing,opt.relaxationIterations,true,axis);
        measured.relaxationSeconds=duration(t,Clock::now());
        t=Clock::now();
        auto faces=triangulate(half);
        measured.triangulationSeconds=duration(t,Clock::now());
        result.vertices=half;
        // Only the p non-seam points are duplicated and reflected.
        for(std::uint32_t i=0;i<p;++i)
            result.vertices.push_back(half[i]-axis*(2*dot(half[i],axis)));
        for(auto f:faces) {
            // Discard the coplanar triangulated disk closing the hemisphere.
            if(f.a>=p&&f.b>=p&&f.c>=p) continue;
            result.triangles.push_back({f.a,f.b,f.c});
            const auto mirror=[p,m](std::uint32_t i){return i<p ? i+p+m : i;};
            result.triangles.push_back({mirror(f.a),mirror(f.c),mirror(f.b)});
        }
    }
    const auto validationStart=Clock::now();
    if(result.vertices.size()!=n || !validateSphere(result))
        throw std::runtime_error("Sphere construction failed manifold validation");
    measured.validationSeconds=duration(validationStart,Clock::now());
    measured.totalSeconds=duration(t0,Clock::now());
    if(timings)*timings=measured;
    return result;
}

bool validateSphere(const Mesh& m) {
    if(m.vertices.size()<4 || m.triangles.size()<4)return false;
    std::unordered_map<Edge,int,EdgeHash> edges;
    edges.reserve(m.triangles.size()*2);
    for(auto t:m.triangles) {
        const auto n=m.vertices.size();
        if(t.a>=n||t.b>=n||t.c>=n||t.a==t.b||t.b==t.c||t.a==t.c)return false;
        const auto a=m.vertices[t.a],b=m.vertices[t.b],c=m.vertices[t.c];
        if(dot(cross(b-a,c-a),a+b+c)<1e-13)return false;
        ++edges[sortedEdge(t.a,t.b)];
        ++edges[sortedEdge(t.b,t.c)];
        ++edges[sortedEdge(t.c,t.a)];
    }
    if(std::any_of(edges.begin(),edges.end(),[](const auto& e){return e.second!=2;}))return false;
    if(std::int64_t(m.vertices.size())-std::int64_t(edges.size())+
       std::int64_t(m.triangles.size())!=2)return false;
    // A closed orientable surface with Euler characteristic two is a sphere
    // if it is connected. The generator also requires every vertex to be used.
    std::vector<std::uint32_t> parent(m.vertices.size());
    std::iota(parent.begin(),parent.end(),0);
    auto root=[&](std::uint32_t a){while(parent[a]!=a){parent[a]=parent[parent[a]];a=parent[a];}return a;};
    for(const auto t:m.triangles) {auto a=root(t.a);parent[a]=root(t.b);a=root(t.b);parent[a]=root(t.c);}
    const auto r=root(0);
    for(std::uint32_t i=1;i<parent.size();++i)
        if(root(i)!=r)return false;
    return true;
}
} // namespace mesher

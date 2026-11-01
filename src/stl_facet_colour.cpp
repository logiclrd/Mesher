#include "mesher/stl_facet_colour.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mesher {
namespace {
using RGB=std::array<double,3>;
using Clock=std::chrono::steady_clock;
constexpr std::uint32_t noFace=std::numeric_limits<std::uint32_t>::max();

RGB add(RGB a,RGB b) {for(int k=0;k<3;++k)a[k]+=b[k];return a;}
RGB sub(RGB a,RGB b) {for(int k=0;k<3;++k)a[k]-=b[k];return a;}
RGB mul(RGB a,double x) {for(auto& value:a)value*=x;return a;}
RGB bounded(RGB a) {for(auto& value:a)value=std::clamp(value,0.0,1.0);return a;}
double difference(RGB a,RGB b) {
    double sum=0;
    for(int k=0;k<3;++k)sum+=(a[k]-b[k])*(a[k]-b[k]);
    return sum;
}
std::uint64_t mix(std::uint64_t x) {
    x+=0x9e3779b97f4a7c15ull;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;
    x=(x^(x>>27))*0x94d049bb133111ebull;
    return x^(x>>31);
}
std::uint16_t pack(RGB c) {
    const auto q=[](double v) {
        return static_cast<std::uint16_t>(std::lround(std::clamp(v,0.0,1.0)*31));
    };
    return static_cast<std::uint16_t>(q(c[0])|(q(c[1])<<5)|(q(c[2])<<10));
}
RGB unpack(std::uint16_t code) {
    return {double(code&31u)/31.0,double((code>>5)&31u)/31.0,
            double((code>>10)&31u)/31.0};
}
struct Neighbour {std::uint32_t id=noFace;double length=0;};
struct Face {
    RGB mean{};
    std::array<std::uint16_t,3> candidates{};
    std::array<Neighbour,3> neighbours{};
    Vec3 centroid{};
    double area=0;
};
struct Owner {std::uint32_t face,side;};
struct Order {
    unsigned degree;
    std::uint64_t tie;
    std::uint32_t face;
    bool operator>(const Order& b) const {
        if(degree!=b.degree)return degree>b.degree;
        if(tie!=b.tie)return tie>b.tie;
        return face>b.face;
    }
};
double millis(Clock::time_point a,Clock::time_point b) {
    return std::chrono::duration<double,std::milli>(b-a).count();
}
std::uint16_t choose(const Face& face,RGB target,double sourcePenalty) {
    auto best=face.candidates[0];
    double bestError=std::numeric_limits<double>::infinity();
    for(const auto candidate:face.candidates) {
        const auto rgb=unpack(candidate);
        const double error=difference(target,rgb)+
                           sourcePenalty*difference(face.mean,rgb);
        if(error<bestError-1e-12) {
            bestError=error;
            best=candidate;
        }
    }
    return best;
}
} // namespace

std::vector<std::uint16_t> stlFacetColourRGB555(
    const Mesh& mesh,std::uint64_t seed,
    StlFacetColourMetrics* metrics) {
    const auto began=Clock::now();
    if(mesh.colors.size()!=mesh.vertices.size()||mesh.triangles.empty()||
       mesh.triangles.size()>std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Facet RGB555 requires a complete vertex-colour mesh");

    std::vector<Face> faces;
    faces.reserve(mesh.triangles.size());
    for(const auto& tri:mesh.triangles) {
        const std::array<std::uint32_t,3> ids{tri.a,tri.b,tri.c};
        RGB sum{};
        std::array<std::uint16_t,3> candidates{};
        for(unsigned k=0;k<3;++k) {
            if(ids[k]>=mesh.vertices.size())
                throw std::invalid_argument("Facet RGB555 vertex index outside mesh");
            const auto c=mesh.colors[ids[k]];
            const RGB rgb{c.r,c.g,c.b};
            for(double value:rgb)
                if(!std::isfinite(value)||value<0||value>1)
                    throw std::invalid_argument("Facet RGB555 requires finite RGB");
            sum=add(sum,rgb);
            candidates[k]=pack(rgb);
        }
        const auto a=mesh.vertices[tri.a],b=mesh.vertices[tri.b],
                   c=mesh.vertices[tri.c];
        const auto area=0.5*length(cross(b-a,c-a));
        if(!(area>0)||!std::isfinite(area))
            throw std::invalid_argument("Facet RGB555 requires positive finite area");
        faces.push_back({mul(sum,1.0/3.0),candidates,{},
                         (a+b+c)/3.0,area});
    }

    // Original mesh faces are dual nodes, connected across shared primal
    // edges. No polygonal dual faces need to be constructed.
    {
        std::unordered_map<std::uint64_t,Owner> owners;
        owners.reserve(faces.size()*3/2+1);
        for(std::uint32_t fi=0;fi<faces.size();++fi) {
            const auto tri=mesh.triangles[fi];
            const std::array<std::uint32_t,3> ids{tri.a,tri.b,tri.c};
            for(unsigned side=0;side<3;++side) {
                const auto a=ids[side],b=ids[(side+1)%3];
                const auto lo=std::min(a,b),hi=std::max(a,b);
                const std::uint64_t key=(std::uint64_t{lo}<<32)|hi;
                const auto [it,inserted]=owners.emplace(key,Owner{fi,side});
                if(!inserted) {
                    const auto first=it->second;
                    const double edgeLength=length(mesh.vertices[a]-mesh.vertices[b]);
                    faces[fi].neighbours[side]={first.face,edgeLength};
                    faces[first.face].neighbours[first.side]={fi,edgeLength};
                    owners.erase(it);
                }
            }
        }
        if(!owners.empty())
            throw std::invalid_argument("Dual STL diffusion requires a closed mesh");
    }
    const auto prepared=Clock::now();

    std::vector<std::uint16_t> assigned(faces.size());
    {
        const auto n=faces.size();
        std::vector<RGB> oneHop(n),twoHop(n);
        std::vector<unsigned> degrees(n);
        std::vector<bool> done(n,false);
        std::priority_queue<Order,std::vector<Order>,std::greater<Order>> queue;
        for(std::uint32_t i=0;i<n;++i) {
            unsigned degree=0;
            for(const auto& e:faces[i].neighbours)if(e.id!=noFace)++degree;
            degrees[i]=degree;
            queue.push({degree,mix(seed^i),i});
        }
        while(!queue.empty()) {
            const auto next=queue.top();
            queue.pop();
            const auto i=next.face;
            if(done[i]||next.degree!=degrees[i])continue;
            const auto& face=faces[i];
            const RGB target=bounded(add(add(face.mean,oneHop[i]),twoHop[i]));
            const auto code=choose(face,target,0.03);
            assigned[i]=code;
            done[i]=true;
            const RGB residual=sub(face.mean,unpack(code));

            std::array<double,3> weights{};
            double totalWeight=0;
            for(unsigned side=0;side<3;++side) {
                const auto e=face.neighbours[side];
                if(e.id==noFace||done[e.id])continue;
                const auto& neighbour=faces[e.id];
                const Vec3 d=face.centroid-neighbour.centroid;
                const double span=std::max(face.area+neighbour.area,1e-20);
                const double spatial=std::exp(-dot(d,d)/(2.0*span));
                const double contrast=std::exp(
                    -difference(face.mean,neighbour.mean)/(2.0*0.35*0.35));
                weights[side]=e.length*neighbour.area*spatial*contrast;
                totalWeight+=weights[side];
            }
            if(totalWeight>0)for(unsigned side=0;side<3;++side) {
                const auto e=face.neighbours[side];
                if(e.id==noFace||done[e.id]||weights[side]<=0)continue;
                const double fraction=weights[side]/totalWeight;
                // Area compensation approximates conservation of the RGB
                // residual when source triangles differ in surface area.
                const double areaRatio=std::clamp(
                    face.area/faces[e.id].area,0.25,4.0);
                oneHop[e.id]=add(oneHop[e.id],
                    mul(residual,0.75*fraction*areaRatio));
                // Incoming one-hop error may travel one extra edge; the
                // two-hop component cannot be retransmitted indefinitely.
                twoHop[e.id]=add(twoHop[e.id],
                    mul(oneHop[i],0.20*fraction*areaRatio));
            }
            for(const auto e:face.neighbours)if(e.id!=noFace&&!done[e.id]) {
                --degrees[e.id];
                queue.push({degrees[e.id],mix(seed^e.id),e.id});
            }
        }
    }
    const auto finished=Clock::now();
    if(metrics) {
        double totalArea=0,totalEdge=0,contrast=0,rgbError=0;
        RGB bias{};
        for(std::size_t i=0;i<faces.size();++i) {
            const auto& face=faces[i];
            const RGB diff=sub(unpack(assigned[i]),face.mean);
            rgbError+=face.area*difference(unpack(assigned[i]),face.mean);
            bias=add(bias,mul(diff,face.area));
            totalArea+=face.area;
            for(const auto e:face.neighbours)if(e.id!=noFace&&e.id>i) {
                totalEdge+=e.length;
                contrast+=e.length*std::sqrt(
                    difference(unpack(assigned[i]),unpack(assigned[e.id])));
            }
        }
        *metrics={millis(began,prepared),millis(prepared,finished),
                  std::sqrt(rgbError/(3.0*totalArea)),
                  std::sqrt(difference(mul(bias,1.0/totalArea),RGB{})),
                  totalEdge>0?contrast/totalEdge:0};
    }
    return assigned;
}
} // namespace mesher

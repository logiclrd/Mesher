#include "mesher/mesh_stl_color.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <stdexcept>
#include <utility>

namespace mesher {
namespace {
using Clock=std::chrono::steady_clock;
using Colour=std::array<double,4>;
double millis(Clock::time_point a,Clock::time_point b) {
    return std::chrono::duration<double,std::milli>(b-a).count();
}
Colour toColour(const VertexRGBA& c) {return {c.r,c.g,c.b,c.a};}
VertexRGBA toVertex(Colour c) {
    for(auto& x:c)x=std::clamp(x,0.0,1.0);
    return {static_cast<float>(c[0]),static_cast<float>(c[1]),
            static_cast<float>(c[2]),static_cast<float>(c[3])};
}
Colour plus(Colour a,Colour b) {for(int k=0;k<4;++k)a[k]+=b[k];return a;}
Colour minus(Colour a,Colour b) {for(int k=0;k<4;++k)a[k]-=b[k];return a;}
Colour times(Colour a,double s) {for(auto& x:a)x*=s;return a;}
Colour bounded(Colour a) {for(auto& x:a)x=std::clamp(x,0.0,1.0);return a;}
double difference(Colour a,Colour b,int n=4) {
    double sum=0;
    for(int i=0;i<n;++i)sum+=(a[i]-b[i])*(a[i]-b[i]);
    return sum;
}
std::uint64_t hash(std::uint64_t x) {
    x+=0x9e3779b97f4a7c15ull;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;
    x=(x^(x>>27))*0x94d049bb133111ebull;
    return x^(x>>31);
}
double random01(std::uint64_t seed,std::uint64_t vertex) {
    return double(hash(seed^hash(vertex))>>11)*0x1.0p-53;
}
std::array<std::uint32_t,3> ids(Triangle t) {return {t.a,t.b,t.c};}
struct Face {std::array<std::uint32_t,3> vertex;Colour original;double area;};
struct SharedEdge {std::uint32_t first,second;double length;};
struct Graph {
    const Mesh& mesh;
    std::vector<Face> faces;
    std::vector<std::vector<std::uint32_t>> stars,neighbours;
    std::vector<Colour> average;
    std::vector<double> areaSum;
    std::vector<SharedEdge> edges;
    double totalArea=0;
    std::size_t missing=0;
    Graph(const Mesh& m,const std::vector<std::optional<VertexRGBA>>& source)
        :mesh(m),stars(m.vertices.size()),neighbours(m.vertices.size()),
         average(m.vertices.size()),areaSum(m.vertices.size(),0) {
        if(source.size()!=m.triangles.size()||source.empty())
            throw std::invalid_argument("STL facet colour count mismatch");
        faces.reserve(source.size());
        std::map<std::pair<std::uint32_t,std::uint32_t>,std::uint32_t> edgeOwners;
        for(std::size_t i=0;i<source.size();++i) {
            const auto v=ids(m.triangles[i]);
            for(auto id:v)if(id>=m.vertices.size())
                throw std::out_of_range("STL colour analysis vertex index invalid");
            const auto a=m.vertices[v[0]],b=m.vertices[v[1]],c=m.vertices[v[2]];
            const double area=0.5*length(cross(b-a,c-a));
            if(!std::isfinite(area)||area<=0)
                throw std::invalid_argument("STL colour analysis invalid triangle area");
            const Colour colour=source[i]?toColour(*source[i]):Colour{1,1,1,1};
            if(!source[i])++missing;
            faces.push_back({v,colour,area});
            totalArea+=area;
            for(int k=0;k<3;++k) {
                const auto x=v[k],y=v[(k+1)%3];
                neighbours[x].push_back(y);
                neighbours[y].push_back(x);
                const auto edge=std::minmax(x,y);
                const auto key=std::make_pair(edge.first,edge.second);
                const auto [it,newEdge]=edgeOwners.emplace(key,
                    static_cast<std::uint32_t>(i));
                if(!newEdge) {
                    edges.push_back({it->second,static_cast<std::uint32_t>(i),
                                     length(m.vertices[x]-m.vertices[y])});
                    edgeOwners.erase(it);
                }
            }
            for(const auto id:v) {
                stars[id].push_back(static_cast<std::uint32_t>(i));
                areaSum[id]+=area/3.0;
                average[id]=plus(average[id],times(colour,area/3.0));
            }
        }
        for(std::size_t i=0;i<m.vertices.size();++i) {
            if(!(areaSum[i]>0))throw std::invalid_argument("STL isolated vertex");
            average[i]=times(average[i],1.0/areaSum[i]);
            auto& n=neighbours[i];
            std::sort(n.begin(),n.end());
            n.erase(std::unique(n.begin(),n.end()),n.end());
        }
    }
};
std::vector<Colour> stochastic(const Graph& g,std::uint64_t seed) {
    std::vector<Colour> colours(g.average.size());
    for(std::uint32_t i=0;i<g.average.size();++i) {
        double choice=random01(seed,i)*g.areaSum[i]*3.0;
        std::uint32_t chosen=g.stars[i].back();
        for(const auto face:g.stars[i]) {
            chosen=face;
            choice-=g.faces[face].area;
            if(choice<=0)break;
        }
        colours[i]=g.faces[chosen].original;
    }
    return colours;
}
struct Order {
    std::size_t degree;
    std::uint64_t tie;
    std::uint32_t vertex;
    bool operator>(const Order& b) const {
        if(degree!=b.degree)return degree>b.degree;
        if(tie!=b.tie)return tie>b.tie;
        return vertex>b.vertex;
    }
};
std::vector<Colour> diffuse(const Graph& g,std::uint64_t seed) {
    const std::size_t count=g.average.size();
    std::vector<Colour> assigned(count),oneHop(count),twoHop(count);
    std::vector<std::size_t> degree(count);
    std::vector<bool> done(count,false);
    std::priority_queue<Order,std::vector<Order>,std::greater<Order>> queue;
    for(std::uint32_t i=0;i<count;++i) {
        degree[i]=g.neighbours[i].size();
        queue.push({degree[i],hash(seed^i),i});
    }
    while(!queue.empty()) {
        const auto entry=queue.top();queue.pop();
        const auto v=entry.vertex;
        if(done[v]||degree[v]!=entry.degree)continue;
        // Limit the source-colour palette to 12 representative incident
        // colours, plus up to 12 *pairwise* similar-colour blends.
        std::vector<Colour> palette;
        const auto& star=g.stars[v];
        const std::size_t stride=1+(star.size()-1)/12;
        for(std::size_t i=0;i<star.size();i+=stride) {
            auto c=g.faces[star[i]].original;
            bool found=false;
            for(const auto& p:palette)if(difference(c,p)<1e-12)found=true;
            if(!found)palette.push_back(c);
        }
        const std::size_t originals=palette.size();
        for(std::size_t i=0;i<originals&&palette.size()<24;++i)
            for(std::size_t j=i+1;j<originals&&palette.size()<24;++j)
                if(difference(palette[i],palette[j],3)<0.24)
                    palette.push_back(times(plus(palette[i],palette[j]),0.5));
        const auto target=bounded(plus(plus(g.average[v],oneHop[v]),twoHop[v]));
        double minError=std::numeric_limits<double>::infinity();
        Colour best=palette.front();
        for(const auto& candidate:palette) {
            double error=0;
            for(const auto faceId:star) {
                const auto& face=g.faces[faceId];
                Colour sum{};
                for(const auto id:face.vertex)
                    sum=plus(sum,id==v?candidate:
                        done[id]?assigned[id]:g.average[id]);
                const auto centroid=times(sum,1.0/3.0);
                error+=face.area*(difference(centroid,face.original,3)+
                    0.5*(centroid[3]-face.original[3])*
                        (centroid[3]-face.original[3]));
            }
            error+=0.12*g.areaSum[v]*difference(candidate,target);
            if(error<minError-1e-12) {minError=error;best=candidate;}
        }
        assigned[v]=best;
        done[v]=true;
        const auto error=minus(g.average[v],best);
        std::vector<std::pair<std::uint32_t,double>> recipients;
        double weightSum=0;
        for(const auto n:g.neighbours[v]) {
            if(done[n])continue;
            const double d=length(g.mesh.vertices[v]-g.mesh.vertices[n]);
            const double scale=g.areaSum[v]+g.areaSum[n];
            const double spatial=std::exp(-d*d/(2.0*scale));
            const double contrast=std::exp(
                -difference(g.average[v],g.average[n],3)/(2.0*0.35*0.35));
            const double weight=spatial*contrast*g.areaSum[n];
            if(weight>1e-15) {recipients.push_back({n,weight});weightSum+=weight;}
        }
        if(weightSum>0)for(const auto& [n,weight]:recipients) {
            const double fraction=weight/weightSum;
            oneHop[n]=plus(oneHop[n],times(error,0.6*fraction));
            // One-hop incoming residual can travel one additional edge.
            // Two-hop residual dies at this vertex: bounded propagation.
            twoHop[n]=plus(twoHop[n],times(oneHop[v],0.24*fraction));
        }
        for(const auto n:g.neighbours[v])if(!done[n]) {
            --degree[n];
            queue.push({degree[n],hash(seed^n),n});
        }
    }
    return assigned;
}
StlColourMethodMetrics evaluate(const Graph& g,
    const std::vector<Colour>& c,StlColourMethod method,double conversionMs) {
    const auto began=Clock::now();
    double rgb=0,alpha=0,contrast=0,sourceContrast=0;
    std::vector<Colour> centroids;
    centroids.reserve(g.faces.size());
    for(const auto& face:g.faces) {
        Colour mean{};
        for(const auto id:face.vertex)mean=plus(mean,c[id]);
        mean=times(mean,1.0/3.0);
        centroids.push_back(mean);
        rgb+=face.area*difference(mean,face.original,3);
        alpha+=face.area*(mean[3]-face.original[3])*
                         (mean[3]-face.original[3]);
        // Integrated barycentric variance in a triangle: 1/12 sum
        // of squared vertex differences from its mean.
        for(const auto id:face.vertex) {
            rgb+=face.area*difference(c[id],mean,3)/12.0;
            alpha+=face.area*(c[id][3]-mean[3])*(c[id][3]-mean[3])/12.0;
        }
    }
    for(const auto& edge:g.edges) {
        const auto& a=g.faces[edge.first].original;
        const auto& b=g.faces[edge.second].original;
        const double ideal=std::sqrt(difference(a,b,3));
        if(ideal<1e-6)continue;
        const double actual=std::sqrt(difference(centroids[edge.first],
                                                centroids[edge.second],3));
        sourceContrast+=edge.length*ideal;
        contrast+=edge.length*std::abs(actual-ideal);
    }
    return {method,conversionMs,millis(began,Clock::now()),
            std::sqrt(rgb/(3.0*g.totalArea)),
            std::sqrt(alpha/g.totalArea),
            sourceContrast>0?contrast/sourceContrast:0.0};
}
std::vector<Colour> applyStlColourMethod(
    const Graph& graph,StlColourMethod method,std::uint64_t seed) {
    switch(method) {
        case StlColourMethod::Average:return graph.average;
        case StlColourMethod::Stochastic:return stochastic(graph,seed);
        case StlColourMethod::Diffusion:return diffuse(graph,seed);
    }
    throw std::invalid_argument("Unrecognized STL colour method");
}
std::vector<VertexRGBA> asVertexColours(const std::vector<Colour>& input) {
    std::vector<VertexRGBA> output;
    output.reserve(input.size());
    for(const auto& rgba:input)output.push_back(toVertex(rgba));
    return output;
}
void logStlColourMetrics(const StlColourMethodMetrics& m) {
    std::cout<<"[stl-colour] method="<<stlColourMethodName(m.method)
             <<" convert_ms="<<m.conversionMs<<" eval_ms="<<m.evaluationMs
             <<" rgb_rms="<<m.rgbRms<<" alpha_rms="<<m.alphaRms
             <<" contrast_error="<<m.contrastError<<"\n";
}
} // namespace

const char* stlColourMethodName(StlColourMethod method) noexcept {
    switch(method) {
        case StlColourMethod::Average:return "average";
        case StlColourMethod::Stochastic:return "stochastic";
        case StlColourMethod::Diffusion:return "diffusion";
    }
    return "unknown";
}
std::vector<VertexRGBA> convertStlFacetColours(
    const Mesh& mesh,
    const std::vector<std::optional<VertexRGBA>>& faceColours,
    StlColourMethod selected,std::uint64_t seed,
    StlColourMethodMetrics* profile) {
    const auto before=Clock::now();
    Graph graph(mesh,faceColours);
    const auto graphMs=millis(before,Clock::now());
    const auto start=Clock::now();
    const auto colours=applyStlColourMethod(graph,selected,seed);
    const auto metric=evaluate(graph,colours,selected,
                               millis(start,Clock::now()));
    if(profile)*profile=metric;
    std::cout<<"[stl-colour] V="<<mesh.vertices.size()
             <<" F="<<graph.faces.size()
             <<" uncoloured_faces="<<graph.missing
             <<" seed="<<seed<<" selected="<<stlColourMethodName(selected)
             <<" graph_ms="<<graphMs<<"\n";
    logStlColourMetrics(metric);
    return asVertexColours(colours);
}

std::vector<VertexRGBA> compareStlFacetColours(
    const Mesh& mesh,
    const std::vector<std::optional<VertexRGBA>>& faceColours,
    StlColourMethod selected,std::uint64_t seed,StlColourComparison* report) {
    const auto before=Clock::now();
    Graph graph(mesh,faceColours);
    StlColourComparison result;
    result.graphPreparationMs=millis(before,Clock::now());
    result.vertices=mesh.vertices.size();
    result.faces=graph.faces.size();
    result.uncolouredFacets=graph.missing;
    result.seed=seed;
    result.selected=selected;
    std::vector<VertexRGBA> output;
    for(const auto candidate:{StlColourMethod::Average,
                              StlColourMethod::Stochastic,
                              StlColourMethod::Diffusion}) {
        const auto start=Clock::now();
        const auto c=applyStlColourMethod(graph,candidate,seed);
        const auto duration=millis(start,Clock::now());
        auto metric=evaluate(graph,c,candidate,duration);
        result.methods[static_cast<std::size_t>(candidate)]=metric;
        if(candidate==selected) {
            output=asVertexColours(c);
        }
    }
    if(output.size()!=mesh.vertices.size())
        throw std::invalid_argument("Unrecognized STL colour method");
    std::cout<<"[stl-colour] V="<<result.vertices<<" F="<<result.faces
             <<" uncoloured_faces="<<result.uncolouredFacets
             <<" seed="<<seed<<" selected="<<stlColourMethodName(selected)
             <<" graph_ms="<<result.graphPreparationMs<<"\n";
    for(const auto& m:result.methods)logStlColourMetrics(m);
    if(report)*report=result;
    return output;
}
} // namespace mesher

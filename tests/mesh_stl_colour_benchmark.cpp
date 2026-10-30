#include "mesher/mesh_stl_color.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;

namespace {
void run(const char* name,bool bands) {
    constexpr std::uint32_t rings=64,segments=96;
    constexpr double pi=3.1415926535897932384626433832795;
    Mesh mesh;
    mesh.vertices.reserve(rings*segments);
    mesh.triangles.reserve(rings*segments*2);
    std::vector<std::optional<VertexRGBA>> source;
    source.reserve(rings*segments*2);
    for(std::uint32_t i=0;i<rings;++i) {
        const double a=2*pi*i/rings;
        for(std::uint32_t j=0;j<segments;++j) {
            const double b=2*pi*j/segments;
            const double radius=3+std::cos(b);
            mesh.vertices.push_back({radius*std::cos(a),radius*std::sin(a),
                                    std::sin(b)});
        }
    }
    for(std::uint32_t i=0;i<rings;++i)for(std::uint32_t j=0;j<segments;++j) {
        const auto idx=[&](std::uint32_t a,std::uint32_t b) {
            return (a%rings)*segments+b%segments;
        };
        const auto a=idx(i,j),b=idx(i+1,j),c=idx(i,j+1),d=idx(i+1,j+1);
        mesh.triangles.push_back({a,b,c});
        mesh.triangles.push_back({b,d,c});
        for(int t=0;t<2;++t) {
            VertexRGBA rgba;
            if(bands) {
                const bool red=((i/8+j/8)%2)==0;
                rgba={red?1.f:0.f,0,red?0.f:1.f,1};
            }else{
                rgba={static_cast<float>(i)/rings,
                      static_cast<float>(j)/segments,0.3f,1.f};
            }
            source.push_back(rgba);
        }
    }
    StlColourComparison result;
    const auto colors=compareStlFacetColours(
        mesh,source,StlColourMethod::Diffusion,42,&result);
    if(colors.size()!=mesh.vertices.size()||result.faces!=source.size())
        throw std::runtime_error("STL colour benchmark produced invalid mesh");
    for(const auto& m:result.methods) {
        if(!(std::isfinite(m.conversionMs)&&m.conversionMs>=0&&
             std::isfinite(m.evaluationMs)&&m.evaluationMs>=0&&
             std::isfinite(m.rgbRms)&&std::isfinite(m.alphaRms)&&
             std::isfinite(m.contrastError)))
            throw std::runtime_error("Invalid STL colour profiling result");
        std::cout<<"STL_BENCH,"<<name<<","
                 <<stlColourMethodName(m.method)<<","
                 <<mesh.vertices.size()<<","<<mesh.triangles.size()<<","
                 <<m.conversionMs<<","<<m.evaluationMs<<","
                 <<m.rgbRms<<","<<m.alphaRms<<","
                 <<m.contrastError<<","<<result.graphPreparationMs<<"\n";
    }
}
}
int main() {
    try{
        std::cout<<"STL_BENCH_COLUMNS,case,method,vertices,faces,convert_ms,"
                    "eval_ms,rgb_rms,alpha_rms,contrast_error,graph_ms\n";
        run("hard-boundaries",true);
        run("smooth-gradient",false);
    }catch(const std::exception& e){
        std::cerr<<e.what()<<"\n";
        return 1;
    }
}

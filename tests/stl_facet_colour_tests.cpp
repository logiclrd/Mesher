#include "mesher/stl_facet_colour.hpp"
#include "mesher/mesh_export_stl.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool okay,const char* message) {
    if(!okay)throw std::runtime_error(message);
}

// Closed, outward-oriented triangulated ellipsoid with high-frequency
// black/white vertex dither. The contrast is deliberately not representable
// as a smooth per-face arithmetic mean.
mesher::Mesh ditheredEllipsoid() {
    using namespace mesher;
    constexpr std::uint32_t rings=24,circumference=32;
    Mesh m;
    for(std::uint32_t j=0;j<rings;++j) {
        const double phi=(j+1)*std::numbers::pi/(rings+1);
        for(std::uint32_t i=0;i<circumference;++i) {
            const double theta=(0.3+double(i)/circumference)*
                               2.0*std::numbers::pi;
            m.vertices.push_back({1.4*std::cos(phi),
                std::sin(phi)*std::cos(theta),
                std::sin(phi)*std::sin(theta)});
            const float v=(i+j)%2u?1.f:0.f;
            m.colors.push_back({v,v,v,1.f});
        }
    }
    const auto north=static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({1.4,0,0});
    m.colors.push_back({0,0,0,1});
    const auto south=static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({-1.4,0,0});
    m.colors.push_back({1,1,1,1});
    for(std::uint32_t i=0;i<circumference;++i)
        m.triangles.push_back({north,i,(i+1)%circumference});
    for(std::uint32_t j=0;j<rings-1;++j)
        for(std::uint32_t i=0;i<circumference;++i) {
            const auto next=(i+1)%circumference;
            const auto a=j*circumference+i,b=j*circumference+next;
            const auto c=(j+1)*circumference+i,d=(j+1)*circumference+next;
            m.triangles.push_back({a,c,b});
            m.triangles.push_back({b,c,d});
        }
    const auto last=(rings-1)*circumference;
    for(std::uint32_t i=0;i<circumference;++i)
        m.triangles.push_back({last+i,south,last+(i+1)%circumference});
    return m;
}
std::uint16_t attribute(const std::string& bytes,std::size_t face) {
    const auto at=84+50*face+48;
    require(at+2<=bytes.size(),"Facet attribute read outside STL");
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(bytes[at])|
        (static_cast<unsigned>(static_cast<unsigned char>(bytes[at+1]))<<8));
}
void compareStrategies() {
    using namespace mesher;
    const auto mesh=ditheredEllipsoid();
    StlFacetColourMetrics meanMetrics{},paletteMetrics{},diffusionMetrics{};
    const auto mean=stlFacetColourRGB555(
        mesh,StlFacetColourStrategy::Mean,42,&meanMetrics);
    const auto palette=stlFacetColourRGB555(
        mesh,StlFacetColourStrategy::Palette,42,&paletteMetrics);
    const auto diffuse=stlFacetColourRGB555(
        mesh,StlFacetColourStrategy::Diffusion,42,&diffusionMetrics);
    require(mean.size()==mesh.triangles.size()&&
            palette.size()==mean.size()&&diffuse.size()==mean.size(),
            "Wrong experimental STL facet-colour count");
    require(diffuse==stlFacetColourRGB555(
                mesh,StlFacetColourStrategy::Diffusion,42),
            "Seeded dual-graph export diffusion is nondeterministic");
    std::size_t greyMeans=0,different=0,white=0,black=0;
    for(std::size_t i=0;i<mean.size();++i) {
        const auto isPure=[](std::uint16_t colour) {
            return colour==0u||colour==0x7fffu;
        };
        if(!isPure(mean[i]))++greyMeans;
        require(isPure(palette[i]),"Source-palette export invented grey RGB");
        require(isPure(diffuse[i]),"Dual diffusion invented grey RGB");
        if(diffuse[i]!=palette[i])++different;
        if(diffuse[i]==0)++black;
        else ++white;
    }
    require(greyMeans>mean.size()/2,
            "The averaging baseline unexpectedly preserved binary dithering");
    require(black>0&&white>0,"Diffusion lost one of the source colour classes");
    require(different>0,"Dual error diffusion had no effect beyond palette selection");
    require(meanMetrics.rgbRms<=paletteMetrics.rgbRms+1e-10,
            "Mean is not the expected continuous per-facet RGB RMS optimum");
    for(const auto& m:{meanMetrics,paletteMetrics,diffusionMetrics}) {
        require(std::isfinite(m.preparationMs)&&m.preparationMs>=0&&
                std::isfinite(m.assignmentMs)&&m.assignmentMs>=0&&
                std::isfinite(m.rgbRms)&&m.rgbRms>=0&&
                std::isfinite(m.rgbBias)&&m.rgbBias>=0&&
                std::isfinite(m.edgeContrast)&&m.edgeContrast>=0,
                "Experimental STL export reported invalid measurements");
    }
    std::cout<<"[stl-dual] mean: rms="<<meanMetrics.rgbRms
             <<" bias="<<meanMetrics.rgbBias
             <<" contrast="<<meanMetrics.edgeContrast
             <<" assign_ms="<<meanMetrics.assignmentMs<<"\n";
    std::cout<<"[stl-dual] palette: rms="<<paletteMetrics.rgbRms
             <<" bias="<<paletteMetrics.rgbBias
             <<" contrast="<<paletteMetrics.edgeContrast
             <<" assign_ms="<<paletteMetrics.assignmentMs<<"\n";
    std::cout<<"[stl-dual] diffusion: rms="<<diffusionMetrics.rgbRms
             <<" bias="<<diffusionMetrics.rgbBias
             <<" contrast="<<diffusionMetrics.edgeContrast
             <<" assign_ms="<<diffusionMetrics.assignmentMs<<"\n";

    for(const auto encoding:{StlExportEncoding::BinaryViscam,
                             StlExportEncoding::BinaryMagicsFacet}) {
        for(const auto strategy:{StlFacetColourStrategy::Mean,
                                 StlFacetColourStrategy::Palette,
                                 StlFacetColourStrategy::Diffusion}) {
            StlExportOptions options{encoding};
            options.strategy=strategy;
            options.seed=42;
            std::ostringstream out(std::ios::binary);
            writeStlMesh(out,mesh,options);
            const auto bytes=out.str();
            require(bytes.size()==84+50*mesh.triangles.size(),
                    "Facet strategy changed STL byte count");
            const auto& expected=strategy==StlFacetColourStrategy::Mean?mean:
                strategy==StlFacetColourStrategy::Palette?palette:diffuse;
            for(std::size_t i=0;i<expected.size();++i) {
                const auto rgb=expected[i];
                const auto r=rgb&31u,g=(rgb>>5)&31u,b=(rgb>>10)&31u;
                const auto encoded=encoding==StlExportEncoding::BinaryViscam?
                    static_cast<std::uint16_t>(
                        0x8000u|(r<<10)|(g<<5)|b):rgb;
                require(attribute(bytes,i)==encoded,
                        "STL export dispatched incorrect strategy or bit layout");
            }
        }
    }
}
void uniformAndInvalid() {
    using namespace mesher;
    auto mesh=ditheredEllipsoid();
    mesh.colors.assign(mesh.vertices.size(),VertexRGBA{0,1,0,1});
    for(const auto mode:{StlFacetColourStrategy::Mean,
                         StlFacetColourStrategy::Palette,
                         StlFacetColourStrategy::Diffusion})
        for(const auto value:stlFacetColourRGB555(mesh,mode))
            require(value==static_cast<std::uint16_t>(31u<<5),
                    "Uniform vertex colours changed under facet conversion");
    StlExportOptions invalid{StlExportEncoding::BinaryUncoloured};
    invalid.strategy=StlFacetColourStrategy::Diffusion;
    std::ostringstream out(std::ios::binary);
    bool threw=false;
    try {writeStlMesh(out,mesh,invalid);}
    catch(const std::invalid_argument&) {threw=true;}
    require(threw&&out.str().empty(),
            "Experimental strategy bypassed facet-only validation gate");
}
} // namespace

int main() {
    try {
        compareStrategies();
        uniformAndInvalid();
        std::cout<<"Dual-graph STL export: mean/palette/diffusion and codec tests passed\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"Dual-graph STL colour test failed: "<<error.what()<<"\n";
        return 1;
    }
}

#include "mesher/stl_facet_colour.hpp"
#include "mesher/mesh_export_stl.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
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
void checkDiffusion() {
    using namespace mesher;
    const auto mesh=ditheredEllipsoid();
    StlFacetColourMetrics metrics{};
    const auto colours=stlFacetColourRGB555(mesh,42,&metrics);
    require(colours.size()==mesh.triangles.size(),
            "Dual diffusion changed the number of STL facet colours");
    require(colours==stlFacetColourRGB555(mesh,42),
            "Seeded dual-graph diffusion is nondeterministic");

    std::size_t black=0,white=0,mixedSourceFaces=0;
    for(std::size_t i=0;i<colours.size();++i) {
        const auto code=colours[i];
        const auto tri=mesh.triangles[i];
        const std::array<std::uint32_t,3> vertices{tri.a,tri.b,tri.c};
        const auto red=[&](std::uint32_t index) {
            return mesh.colors[index].r;
        };
        const double sum=red(tri.a)+red(tri.b)+red(tri.c);
        if(sum>0&&sum<3)++mixedSourceFaces;
        require(code==0u||code==0x7fffu,
                "Dual facet diffusion blended black/white source colours into grey");
        bool fromSource=false;
        for(const auto vertex:vertices)
            fromSource|=(red(vertex)==0.f?code==0u:code==0x7fffu);
        require(fromSource,
                "Dual facet diffusion chose an RGB absent from its source triangle");
        if(code==0u)++black;
        else ++white;
    }
    require(mixedSourceFaces>colours.size()/2,
            "Test fixture lacks enough mixed RGB vertex triangles");
    require(black>0&&white>0,
            "Dual diffusion failed to preserve both black/white source classes");
    require(std::isfinite(metrics.preparationMs)&&metrics.preparationMs>=0&&
            std::isfinite(metrics.assignmentMs)&&metrics.assignmentMs>=0&&
            std::isfinite(metrics.rgbRms)&&metrics.rgbRms>=0&&
            std::isfinite(metrics.rgbBias)&&metrics.rgbBias>=0&&
            std::isfinite(metrics.edgeContrast)&&metrics.edgeContrast>=0,
            "Facet diffusion reported nonfinite or negative metrics");
    std::cout<<"[stl-dual] rgb_rms="<<metrics.rgbRms
             <<" bias="<<metrics.rgbBias
             <<" contrast="<<metrics.edgeContrast
             <<" prepare_ms="<<metrics.preparationMs
             <<" assign_ms="<<metrics.assignmentMs<<"\n";

    for(const auto encoding:{StlExportEncoding::BinaryViscam,
                             StlExportEncoding::BinaryMagicsFacet}) {
        std::ostringstream out(std::ios::binary);
        writeStlMesh(out,mesh,{encoding});
        const auto bytes=out.str();
        require(bytes.size()==84+50*mesh.triangles.size(),
                "Facet diffusion changed STL geometry/facet byte count");
        for(std::size_t i=0;i<colours.size();++i) {
            const auto rgb=colours[i];
            const auto r=rgb&31u,g=(rgb>>5)&31u,b=(rgb>>10)&31u;
            const auto expected=encoding==StlExportEncoding::BinaryViscam?
                static_cast<std::uint16_t>(0x8000u|(r<<10)|(g<<5)|b):rgb;
            require(attribute(bytes,i)==expected,
                    "STL facet encoding did not write diffusion's RGB555 choice");
        }
    }
}
void checkUniformAndRejections() {
    using namespace mesher;
    auto mesh=ditheredEllipsoid();
    mesh.colors.assign(mesh.vertices.size(),VertexRGBA{0,1,0,1});
    for(const auto value:stlFacetColourRGB555(mesh))
        require(value==static_cast<std::uint16_t>(31u<<5),
                "Uniform vertex colours did not survive facet diffusion");

    // A conversion failure may not leave a partially written STL stream.
    mesh.colors[1].g=std::numeric_limits<float>::quiet_NaN();
    for(const auto encoding:{StlExportEncoding::BinaryViscam,
                             StlExportEncoding::BinaryMagicsFacet}) {
        std::ostringstream out(std::ios::binary);
        bool threw=false;
        try {writeStlMesh(out,mesh,{encoding});}
        catch(const std::invalid_argument&) {threw=true;}
        require(threw&&out.str().empty(),
                "Invalid colour data partly published a coloured STL file");
    }
}
} // namespace

int main() {
    try {
        checkDiffusion();
        checkUniformAndRejections();
        std::cout<<"Dual-graph facet-colour diffusion and STL codecs passed\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"Dual-graph STL colour test failed: "<<error.what()<<"\n";
        return 1;
    }
}

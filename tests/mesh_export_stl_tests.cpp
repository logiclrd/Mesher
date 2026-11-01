#include "mesher/mesh_export_stl.hpp"
#include "mesher/stl_facet_colour.hpp"
#include "mesher/mesh_stl.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition,const char* why) {
    if(!condition)throw std::runtime_error(why);
}
mesher::Mesh tetra() {
    mesher::Mesh mesh;
    mesh.vertices={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    mesh.triangles={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    mesh.colors.assign(mesh.vertices.size(),mesher::VertexRGBA{1,0,0,1});
    return mesh;
}
std::string serialize(const mesher::Mesh& mesh,
                      mesher::StlExportOptions options) {
    std::ostringstream result(std::ios::binary);
    mesher::writeStlMesh(result,mesh,options);
    return result.str();
}
std::uint16_t attribute(const std::string& data,std::size_t face) {
    const auto offset=84+50*face+48;
    require(data.size()>=offset+2,"STL attribute offset outside payload");
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(data[offset])|
        (static_cast<unsigned>(static_cast<unsigned char>(
             data[offset+1]))<<8));
}
void roundTrip(mesher::StlExportEncoding encoding) {
    using namespace mesher;
    auto mesh=tetra();
    StlExportOptions options{encoding};
    if(encoding==StlExportEncoding::BinaryMagicsObject)
        options.objectColour=VertexRGBA{0.25f,0.5f,0.75f,0.6f};
    const auto content=serialize(mesh,options);
    const bool ascii=encoding==StlExportEncoding::Ascii;
    if(ascii)
        require(content.starts_with("solid Mesher\n")&&
                content.find("endsolid Mesher")!=std::string::npos,
                "ASCII STL header/terminator changed");
    else
        require(content.size()==84+50*mesh.triangles.size(),
                "Binary STL exact facet byte layout changed");
    if(encoding==StlExportEncoding::BinaryUncoloured)
        require(attribute(content,0)==0,
                "Colourless STL must have zero attribute word");
    if(encoding==StlExportEncoding::BinaryViscam)
        require(attribute(content,0)==0xfc00u,
                "VisCAM R-high red/valid-bit packing changed");
    if(encoding==StlExportEncoding::BinaryMagicsFacet)
        require(content.substr(32,6)=="COLOR="&&attribute(content,0)==31u,
                "Magics R-low facet colour packing changed");
    if(encoding==StlExportEncoding::BinaryMagicsObject)
        require(content.substr(32,6)=="COLOR="&&attribute(content,0)==0x8000u &&
                static_cast<unsigned char>(content[38])==64u &&
                static_cast<unsigned char>(content[39])==128u &&
                static_cast<unsigned char>(content[40])==191u &&
                static_cast<unsigned char>(content[41])==153u,
                "Magics object RGBA header/override-bit semantics changed");
    const auto path=std::filesystem::temp_directory_path()/
        ("mesher-ux07c-"+std::to_string(static_cast<int>(encoding))+".stl");
    struct Clean {
        std::filesystem::path path;
        ~Clean() {std::error_code ignored;std::filesystem::remove(path,ignored);}
    } clean{path};
    {
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out.exceptions(std::ios::badbit|std::ios::failbit);
        out.write(content.data(),static_cast<std::streamsize>(content.size()));
        out.close();
    }
    const auto imported=parseStlMesh(path);
    require(imported.vertices.size()==mesh.vertices.size()&&
            imported.triangles.size()==mesh.triangles.size(),
            "STL import after export changed closed tetra topology");
    for(const auto& point:imported.vertices) {
        bool found=false;
        for(const auto& source:mesh.vertices)
            found|=point.x==source.x&&point.y==source.y&&point.z==source.z;
        require(found,"STL import after export changed tetra coordinates");
    }
    const bool coloured=encoding==StlExportEncoding::BinaryViscam||
                        encoding==StlExportEncoding::BinaryMagicsFacet||
                        encoding==StlExportEncoding::BinaryMagicsObject;
    require(imported.colors.size()==(coloured?mesh.vertices.size():0),
            "STL colour stream presence changed on round trip");
    for(const auto& colour:imported.colors) {
        const VertexRGBA expected=options.objectColour.value_or(
            VertexRGBA{1,0,0,1});
        const double tolerance=1.0/255.0+1e-5;
        require(std::abs(colour.r-expected.r)<tolerance&&
                std::abs(colour.g-expected.g)<tolerance&&
                std::abs(colour.b-expected.b)<tolerance&&
                std::abs(colour.a-expected.a)<tolerance,
                "Uniform STL facet/object colour did not round trip");
    }
}
void reject(mesher::Mesh mesh,mesher::StlExportOptions options) {
    std::ostringstream out;
    bool refused=false;
    try {mesher::writeStlMesh(out,mesh,options);}
    catch(const std::invalid_argument&) {refused=true;}
    require(refused,"Unsupported STL output was not rejected");
    require(out.str().empty(),"Rejected STL unexpectedly wrote bytes");
}
} // namespace
int main() {
    try {
        using namespace mesher;
        for(const auto encoding:{StlExportEncoding::Ascii,
                                 StlExportEncoding::BinaryUncoloured,
                                 StlExportEncoding::BinaryViscam,
                                 StlExportEncoding::BinaryMagicsFacet,
                                 StlExportEncoding::BinaryMagicsObject})
            roundTrip(encoding);
        auto mesh=tetra();
        // Each facet chooses from the original source-vertex palette.
        // Blue/red input must not produce the old averaged intermediate purple.
        mesh.colors[0]={0,0,1,1};
        const auto selected=stlFacetColourRGB555(mesh);
        const auto seam=serialize(mesh,{StlExportEncoding::BinaryViscam});
        for(std::size_t i=0;i<selected.size();++i) {
            const auto rgb=selected[i];
            const auto r=rgb&31u,g=(rgb>>5)&31u,b=(rgb>>10)&31u;
            require(attribute(seam,i)==static_cast<std::uint16_t>(
                    0x8000u|(r<<10)|(g<<5)|b),
                    "Default STL facet export did not use dual diffusion");
            require(rgb==31u||rgb==static_cast<std::uint16_t>(31u<<10),
                    "Default facet diffusion introduced a blended RGB");
        }
        const auto magics=serialize(mesh,{StlExportEncoding::BinaryMagicsFacet});
        for(std::size_t i=0;i<selected.size();++i)
            require(attribute(magics,i)==selected[i],
                    "Magics facet export did not use dual diffusion");
        mesh=tetra();
        mesh.triangles.pop_back();
        reject(mesh,{StlExportEncoding::BinaryUncoloured});
        mesh=tetra();
        mesh.triangles[0].a=99999;
        reject(mesh,{StlExportEncoding::BinaryUncoloured});
        mesh=tetra();
        mesh.vertices[0].x=std::numeric_limits<double>::infinity();
        reject(mesh,{StlExportEncoding::Ascii});
        mesh=tetra();
        mesh.vertices[1].x=1e-50;
        reject(mesh,{StlExportEncoding::BinaryUncoloured});
        mesh=tetra();
        mesh.vertices[0].x=1e100;
        reject(mesh,{StlExportEncoding::BinaryUncoloured});
        mesh=tetra();
        mesh.colors.pop_back();
        reject(mesh,{StlExportEncoding::BinaryViscam});
        mesh=tetra();
        mesh.colors[0].r=-1.f;
        reject(mesh,{StlExportEncoding::BinaryMagicsFacet});
        mesh=tetra();
        reject(mesh,{StlExportEncoding::BinaryMagicsObject});
        mesh=tetra();
        reject(mesh,{StlExportEncoding::BinaryViscam,VertexRGBA{1,0,0,1}});
        mesh=tetra();
        reject(mesh,{StlExportEncoding::BinaryMagicsObject,
                     VertexRGBA{1,0,0,std::numeric_limits<float>::quiet_NaN()}});
        // Saving must preserve caller flags, precision and exception mask,
        // including when the caller already enables iostream exceptions.
        std::ostringstream formatted;
        formatted.exceptions(std::ios::badbit|std::ios::failbit);
        formatted.precision(3);
        formatted.setf(std::ios::scientific,std::ios::floatfield);
        const auto flags=formatted.flags();
        const auto mask=formatted.exceptions();
        writeStlMesh(formatted,tetra(),{StlExportEncoding::Ascii});
        require(!formatted.str().empty()&&formatted.precision()==3&&
                formatted.flags()==flags&&formatted.exceptions()==mask,
                "STL serializer did not restore live output formatting");
        std::cout<<"UX07C STL dialects, geometry, colour conversion and refusals passed\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"UX07C failure: "<<e.what()<<'\n';
        return 1;
    }
}

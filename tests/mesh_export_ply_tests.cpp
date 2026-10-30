#include "mesher/mesh_export_ply.hpp"
#include "mesher/mesh_ply.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool result,const char* message) {
    if(!result)throw std::runtime_error(message);
}
mesher::Mesh tetra() {
    mesher::Mesh mesh;
    mesh.vertices={{0.125,0.25,0.5},{1.125,0.25,0.5},
                   {0.125,1.25,0.5},{0.125,0.25,1.5}};
    mesh.triangles={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    mesh.colors={{1,0,0,1,0.1f,true},
                 {0,1,0,0.25f,0.9f,true},
                 {0,0,1,0.5f,0.2f,true},
                 {1,1,0,0.75f,0.8f,true}};
    return mesh;
}
bool close(double a,double b,double eps=1e-7) {
    return std::abs(a-b)<=eps;
}
void roundTrip(mesher::PlyExportEncoding encoding,
               mesher::PlyExportColour colour) {
    using namespace mesher;
    const auto source=tetra();
    const auto name=std::to_string(static_cast<int>(encoding))+"-"+
                    std::to_string(static_cast<int>(colour));
    const auto path=std::filesystem::temp_directory_path()/
                   ("mesher-ux07b-"+name+".ply");
    struct Remove {
        std::filesystem::path path;
        ~Remove() {std::error_code ignored;std::filesystem::remove(path,ignored);}
    } cleanup{path};
    {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.exceptions(std::ios::failbit|std::ios::badbit);
        writePlyMesh(file,source,{encoding,colour});
        file.close();
    }
    const auto loaded=parsePlyMesh(path);
    require(loaded.vertices.size()==source.vertices.size(),
            "PLY round-trip vertex count changed");
    require(loaded.triangles.size()==source.triangles.size(),
            "PLY round-trip face count changed");
    for(std::size_t i=0;i<source.vertices.size();++i) {
        const auto a=source.vertices[i],b=loaded.vertices[i];
        require(close(a.x,b.x)&&close(a.y,b.y)&&close(a.z,b.z),
                "PLY round-trip changed coordinates");
    }
    for(std::size_t i=0;i<source.triangles.size();++i) {
        const auto a=source.triangles[i],b=loaded.triangles[i];
        require(a.a==b.a&&a.b==b.b&&a.c==b.c,
                "PLY round-trip changed winding or face indices");
    }
    const bool coloured=colour!=PlyExportColour::None;
    require(loaded.colors.size()==(coloured?source.vertices.size():0),
            "PLY colour stream presence changed");
    for(std::size_t i=0;i<loaded.colors.size();++i) {
        const auto a=source.colors[i],b=loaded.colors[i];
        constexpr double tolerance=0.5/255.0+0.000001;
        require(close(a.r,b.r,tolerance)&&close(a.g,b.g,tolerance)&&
                close(a.b,b.b,tolerance),"PLY RGB quantization exceeds 8-bit tolerance");
        const bool alpha=colour==PlyExportColour::RgbaFromMesh||
                         colour==PlyExportColour::RgbaFromFile;
        require(b.sourceAlpha.has_value()==alpha,
                "PLY round-trip alpha property presence changed");
        if(alpha) {
            const double expected=colour==PlyExportColour::RgbaFromFile?
                                  *a.sourceAlpha:a.a;
            require(close(*b.sourceAlpha,expected,tolerance),
                    "PLY alpha selection or quantization changed");
        }
    }
}
void mustReject(const mesher::Mesh& mesh,mesher::PlyExportColour colour) {
    std::ostringstream output;
    bool rejected=false;
    try {mesher::writePlyMesh(output,mesh,{{},colour});}
    catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Invalid PLY serialization was accepted");
    require(output.str().empty(),"Rejected PLY emitted partial output");
}
} // namespace
int main() {
    try {
        using namespace mesher;
        for(auto encoding:{PlyExportEncoding::Ascii,
                           PlyExportEncoding::BinaryLittleEndian,
                           PlyExportEncoding::BinaryBigEndian}) {
            for(auto colour:{PlyExportColour::None,PlyExportColour::Rgb,
                             PlyExportColour::RgbaFromMesh,
                             PlyExportColour::RgbaFromFile})
                roundTrip(encoding,colour);
        }
        auto invalid=tetra();
        invalid.triangles[0].b=999999;
        mustReject(invalid,PlyExportColour::None);
        invalid=tetra();
        invalid.vertices[1].x=std::numeric_limits<double>::infinity();
        mustReject(invalid,PlyExportColour::None);
        invalid=tetra();
        invalid.colors.pop_back();
        mustReject(invalid,PlyExportColour::Rgb);
        invalid=tetra();
        invalid.colors[0].sourceAlpha.reset();
        mustReject(invalid,PlyExportColour::RgbaFromFile);
        invalid=tetra();
        invalid.colors[0].a=-0.5f;
        mustReject(invalid,PlyExportColour::RgbaFromMesh);
        invalid=tetra();
        invalid.colors[1].r=std::numeric_limits<float>::quiet_NaN();
        mustReject(invalid,PlyExportColour::Rgb);
        invalid=tetra();
        invalid.colors.clear();
        mustReject(invalid,PlyExportColour::Rgb);
        std::cout<<"UX07B PLY ASCII/binary, RGB(A), alpha provenance and refusals passed\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"UX07B failure: "<<error.what()<<'\n';
        return 1;
    }
}

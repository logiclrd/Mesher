#include "mesher/mesh_export_obj.hpp"
#include "mesher/mesh_open_candidate.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
mesher::Mesh tetra() {
    mesher::Mesh mesh;
    mesh.vertices={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    mesh.triangles={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    mesh.colors={{1,0,0,1},{0,1,0,0.25f},
                 {0,0,1,0.5f},{1,1,0,0.75f}};
    return mesh;
}
std::string serialize(const mesher::Mesh& mesh,
                      mesher::ObjExportEncoding encoding) {
    std::ostringstream buffer;
    mesher::writeObjMesh(buffer,mesh,encoding);
    return buffer.str();
}
void mustReject(const mesher::Mesh& mesh,
                mesher::ObjExportEncoding encoding) {
    bool rejected=false;
    try {(void)serialize(mesh,encoding);}
    catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"Invalid OBJ output was accepted");
}
void roundTrip(const mesher::Mesh& original,
               mesher::ObjExportEncoding encoding,bool colour) {
    const auto path=std::filesystem::temp_directory_path()/
        ("mesher-ux07a-"+std::to_string(static_cast<int>(encoding))+".obj");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {std::error_code ignored;std::filesystem::remove(path,ignored);}
    } cleanup{path};
    {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.exceptions(std::ios::badbit|std::ios::failbit);
        mesher::writeObjMesh(file,original,encoding);
        file.close();
    }
    const auto parsed=mesher::readMeshOpenCandidate(
        path,mesher::MeshOpenFilter::WavefrontObj);
    require(parsed.vertexCount==original.vertices.size(),
            "OBJ round-trip vertex count changed");
    require(parsed.faceCount==original.triangles.size(),
            "OBJ round-trip triangle count changed");
    require(parsed.hasVertexColors==colour,
            "OBJ round-trip vertex colour presence changed");
}
} // namespace
int main() {
    try {
        using mesher::ObjExportEncoding;
        auto mesh=tetra();
        for(auto encoding:{ObjExportEncoding::GeometryOnly,
                           ObjExportEncoding::InlineRgb,
                           ObjExportEncoding::InlineRgba,
                           ObjExportEncoding::ZbrushMrgb})
            roundTrip(mesh,encoding,encoding!=ObjExportEncoding::GeometryOnly);
        // A live stream with an enabled exception mask must not trigger
        // basic_ios::clear in the serializer's RAII format guard.
        std::ostringstream formatted;
        formatted.exceptions(std::ios::badbit|std::ios::failbit);
        formatted.precision(4);
        formatted.setf(std::ios::scientific,std::ios::floatfield);
        const auto priorFlags=formatted.flags();
        const auto priorExceptions=formatted.exceptions();
        mesher::writeObjMesh(formatted,mesh,ObjExportEncoding::GeometryOnly);
        require(!formatted.str().empty(),"Serializer produced no output");
        require(formatted.precision()==4&&formatted.flags()==priorFlags&&
                formatted.exceptions()==priorExceptions,
                "Serializer did not restore caller stream format");
        auto plain=serialize(mesh,ObjExportEncoding::GeometryOnly);
        require(plain.find("#MRGB")==std::string::npos,
                "Standard OBJ unexpectedly contains ZBrush colours");
        require(plain.find("f 1 3 2\n")!=std::string::npos,
                "OBJ face winding/index mapping changed");
        auto mrgb=serialize(mesh,ObjExportEncoding::ZbrushMrgb);
        // Exactly four 8-hex-digit MMRRGGBB words: red, green, blue,
        // yellow, each prefixed by the FF (fully unmasked) ZBrush mask.
        // Earlier test inadvertently omitted an FF byte pair from yellow.
        require(mrgb.find("#MRGB FFFF0000FF00FF00FF0000FFFFFFFF00\n")!=
                    std::string::npos,
                "ZBrush MMRRGGBB words differ from expected mask/RGB bytes");
        auto rgba=serialize(mesh,ObjExportEncoding::InlineRgba);
        require(rgba.find("0.25")!=std::string::npos,
                "Inline OBJ RGBA did not preserve alpha");
        auto invalid=mesh;
        invalid.triangles[0].a=123456;
        mustReject(invalid,ObjExportEncoding::GeometryOnly);
        invalid=mesh;
        invalid.colors.pop_back();
        mustReject(invalid,ObjExportEncoding::InlineRgb);
        invalid=mesh;
        invalid.vertices[0].x=std::numeric_limits<double>::infinity();
        mustReject(invalid,ObjExportEncoding::GeometryOnly);
        invalid=mesh;
        invalid.colors[0].r=-0.1f;
        mustReject(invalid,ObjExportEncoding::ZbrushMrgb);
        invalid=mesh;
        invalid.colors.clear();
        mustReject(invalid,ObjExportEncoding::InlineRgb);
        std::cout<<"UX07A OBJ export formats, refusal cases and imports passed\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"UX07A failure: "<<e.what()<<'\n';
        return 1;
    }
}

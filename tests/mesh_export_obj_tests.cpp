#include "mesher/mesh_export_obj.hpp"
#include "mesher/mesh_open_candidate.hpp"

#include <algorithm>

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
    // Partitioning reorders vertices, so compare imported colours by exact
    // source coordinates, not by coincidentally matching flat array indices.
    std::size_t checked=0;
    parsed.snapshot.index->revision().forEachPartition(
        [&](mesher::PartitionId,const mesher::MeshPartition& part) {
            for(const auto& vertex:part.vertices) {
                const auto it=std::find_if(original.vertices.begin(),
                    original.vertices.end(),[&](mesher::Vec3 point) {
                        return point.x==vertex.position.x &&
                               point.y==vertex.position.y &&
                               point.z==vertex.position.z;
                    });
                require(it!=original.vertices.end(),
                        "OBJ round-trip changed source coordinates");
                const auto i=static_cast<std::size_t>(
                    it-original.vertices.begin());
                const auto& got=vertex.color;
                if(!colour) {
                    require(got==mesher::VertexRGBA{},
                            "Geometry-only OBJ manufactured vertex colours");
                }else {
                    const auto& want=original.colors[i];
                    const double tolerance=
                        encoding==mesher::ObjExportEncoding::ZbrushMrgb?
                        0.5/255.0+1e-6:1e-6;
                    const auto near=[&](float a,float b) {
                        return std::abs(static_cast<double>(a)-b)<=tolerance;
                    };
                    require(near(got.r,want.r)&&near(got.g,want.g)&&
                            near(got.b,want.b),
                            "OBJ round-trip RGB channel mismatch");
                    const float expectedAlpha=
                        encoding==mesher::ObjExportEncoding::InlineRgba?
                        want.a:1.f;
                    require(near(got.a,expectedAlpha),
                            "OBJ export alpha policy mismatch");
                    require(!got.sourceAlpha.has_value()&&
                            !got.sourceAlphaOriginallyVaried,
                            "OBJ import invented source-alpha provenance");
                }
                ++checked;
            }
        });
    require(checked==original.vertices.size(),
            "OBJ round-trip partition vertex inventory mismatch");
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
        // ZBrush permits at most 64 complete MMRRGGBB words per line.
        // A 68-colour stream must produce a full record and a 4-word tail.
        auto large=mesh;
        for(std::size_t i=4;i<68;++i) {
            large.vertices.push_back({static_cast<double>(i),0,0});
            large.colors.push_back(mesh.colors[i%mesh.colors.size()]);
        }
        const auto multiple=serialize(large,ObjExportEncoding::ZbrushMrgb);
        const auto first=multiple.find("#MRGB ");
        require(first!=std::string::npos,"ZBrush initial record missing");
        const auto firstEnd=multiple.find('\n',first);
        require(firstEnd!=std::string::npos&&firstEnd-first==6+64*8,
                "ZBrush first record must contain exactly 64 words");
        const auto second=multiple.find("#MRGB ",firstEnd+1);
        require(second!=std::string::npos,"ZBrush tail record missing");
        const auto secondEnd=multiple.find('\n',second);
        require(secondEnd!=std::string::npos&&secondEnd-second==6+4*8 &&
                multiple.find("#MRGB ",secondEnd+1)==std::string::npos,
                "ZBrush final record must contain exactly four words");
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
    return 0;
}

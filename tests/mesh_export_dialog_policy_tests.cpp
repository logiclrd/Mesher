#include "mesher/mesh_export_dispatch.hpp"
#include "mesher/mesh_open_candidate.hpp"
#include "mesher/mesh_ply.hpp"
#include "mesher/mesh_stl.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace fs=std::filesystem;
void require(bool ok,const char* msg) {
    if(!ok)throw std::runtime_error(msg);
}
mesher::Mesh tetra() {
    mesher::Mesh m;
    m.vertices={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    m.triangles={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    m.colors={{1,0,0,1,0.1f,true},
              {0,1,0,0.5f,0.3f,true},
              {0,0,1,0.25f,0.9f,true},
              {1,1,0,0.75f,0.7f,true}};
    return m;
}
void testPreset(mesher::MeshExportPreset preset) {
    const auto& info=mesher::meshExportFormat(preset);
    const auto encoder=mesher::meshExportEncoder(preset);
    const auto original=tetra();
    std::ostringstream output(std::ios::out|std::ios::binary);
    encoder(output,original);
    const auto payload=output.str();
    require(!payload.empty(),"Export preset produced an empty stream");
    const auto index=static_cast<std::size_t>(preset);
    const auto path=fs::temp_directory_path()/
        ("mesher-export-preset-"+std::to_string(index)+"."+
         std::string(info.extension));
    struct Remove {
        fs::path path;
        ~Remove(){std::error_code ignored;fs::remove(path,ignored);}
    } remove{path};
    {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.exceptions(std::ios::failbit|std::ios::badbit);
        file.write(payload.data(),static_cast<std::streamsize>(payload.size()));
        file.close();
    }
    if(info.extension=="obj") {
        require(payload.find("\nv ")!=std::string::npos ||
                payload.starts_with("v ") ||
                payload.starts_with("# Mesher Wavefront OBJ export\nv "),
                "Selected OBJ codec did not write OBJ vertices");
        const auto candidate=mesher::readMeshOpenCandidate(
            path,mesher::MeshOpenFilter::WavefrontObj);
        require(candidate.vertexCount==4&&candidate.faceCount==4,
                "OBJ export preset cannot import closed source");
        require(candidate.hasVertexColors==(preset!=
                    mesher::MeshExportPreset::ObjGeometry),
                "OBJ export preset changed colour policy");
        require((payload.find("#MRGB ")!=std::string::npos)==
                (preset==mesher::MeshExportPreset::ObjZbrushMrgb),
                "MRGB used with incorrect OBJ dropdown entry");
    }else if(info.extension=="ply") {
        require(payload.starts_with("ply\nformat "),
                "Selected PLY codec did not write PLY header");
        const auto loaded=mesher::parsePlyMesh(path);
        require(loaded.vertices.size()==4&&loaded.triangles.size()==4,
                "PLY preset did not round trip tetrahedron");
        const auto colourIndex=(index-4u)%4u;
        require(loaded.colors.size()==(colourIndex==0?0u:4u),
                "PLY dropdown colour mode not dispatched");
        if(colourIndex>=2u) {
            for(std::size_t i=0;i<4;++i) {
                const auto want=colourIndex==2u?
                    original.colors[i].a:*original.colors[i].sourceAlpha;
                require(loaded.colors[i].sourceAlpha&&
                        std::abs(*loaded.colors[i].sourceAlpha-want)<
                            0.5f/255.f+0.00001f,
                        "PLY dropdown selected wrong alpha provenance");
            }
        }
    }else {
        require(info.extension=="stl","Unknown export codec extension");
        const auto loaded=mesher::parseStlMesh(path);
        require(loaded.vertices.size()==4&&loaded.triangles.size()==4,
                "STL preset did not round trip closed geometry");
        const auto coloured=preset>=mesher::MeshExportPreset::StlViscamRgb;
        require(loaded.colors.size()==(coloured?4u:0u),
                "STL dropdown selected wrong colour dialect");
        if(preset==mesher::MeshExportPreset::StlMagicsObjectRgba) {
            require(payload.size()>=84&&payload.substr(32,6)=="COLOR=",
                    "Magics object export lacks COLOR= header");
            const std::array<unsigned,4> expected{128u,128u,64u,159u};
            for(std::size_t i=0;i<4;++i)
                require(static_cast<unsigned char>(payload[38+i])==expected[i],
                        "Magics object RGBA did not use disclosed vertex mean");
        }
    }
}
void testNames() {
    using namespace mesher;
    require(meshExportFormats.size()==21,
            "Export dialog must expose every supported codec/policy");
    std::size_t obj=0,ply=0,stl=0;
    for(std::size_t i=0;i<meshExportFormats.size();++i) {
        const auto preset=static_cast<MeshExportPreset>(i);
        const auto& entry=meshExportFormat(preset);
        require(!entry.label.empty()&&!entry.disclosure.empty(),
                "Export dropdown entry lacks name or disclosure");
        const auto checked=validateExportFilename(
            fs::path("test"),preset);
        require(checked.extension()=="."+std::string(entry.extension),
                "Missing filename suffix was not appended from selected filter");
        const auto already=validateExportFilename(
            fs::path("test")+"."+std::string(entry.extension),preset);
        require(already==checked,"Matching filename suffix was changed");
        ++(entry.extension=="obj"?obj:entry.extension=="ply"?ply:stl);
        const auto different=entry.extension=="obj"?"stl":"obj";
        const auto normalized=validateExportFilename(
            fs::path("test")+"."+different,preset);
        require(normalized==checked,
                "Changing a known export file type failed to replace its suffix");
        bool refused=false;
        try {
            (void)validateExportFilename(fs::path("test.jpg"),preset);
        }catch(const std::invalid_argument&) {refused=true;}
        require(refused,"Unrelated explicit suffix was silently replaced");
    }
    require(obj==4&&ply==12&&stl==5,
            "Wrong number of format/encoding dropdown entries");
}
} // namespace
int main() {
    try {
        testNames();
        for(std::size_t i=0;i<mesher::meshExportFormats.size();++i)
            testPreset(static_cast<mesher::MeshExportPreset>(i));
        std::cout<<"UX07E 21 dialog presets, codecs, suffixes and disclosure passed\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"UX07E failure: "<<e.what()<<'\n';
        return 1;
    }
}

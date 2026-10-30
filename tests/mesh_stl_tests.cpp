#include "mesher/mesh_open_candidate.hpp"
#include "mesher/mesh_stl.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;

namespace {
void check(bool ok,const char* why) {
    if(!ok)throw std::runtime_error(why);
}
constexpr std::array<std::array<std::array<int,3>,3>,4> tetra{{
    {{{0,0,0},{0,1,0},{1,0,0}}},
    {{{0,0,0},{1,0,0},{0,0,1}}},
    {{{1,0,0},{0,1,0},{0,0,1}}},
    {{{0,1,0},{0,0,0},{0,0,1}}}
}};
void put32(std::string& output,std::uint32_t value) {
    for(int i=0;i<4;++i)output.push_back(static_cast<char>((value>>(8*i))&255));
}
void put16(std::string& output,std::uint16_t value) {
    output.push_back(static_cast<char>(value&255));
    output.push_back(static_cast<char>((value>>8)&255));
}
void putFloat(std::string& out,float v) {put32(out,std::bit_cast<std::uint32_t>(v));}
std::string makeBinary(const std::vector<std::array<std::array<int,3>,3>>& facets,
                       const std::vector<std::uint16_t>& attributes,
                       const std::string& header="STL test") {
    std::string data(80,'\0');
    data.replace(0,std::min(header.size(),data.size()),
                 header.substr(0,80));
    put32(data,static_cast<std::uint32_t>(facets.size()));
    for(std::size_t i=0;i<facets.size();++i) {
        for(int j=0;j<3;++j)putFloat(data,0);
        for(const auto& v:facets[i])
            for(int coord:v)putFloat(data,static_cast<float>(coord));
        put16(data,attributes[i]);
    }
    return data;
}
std::vector<std::array<std::array<int,3>,3>> tetraFacets() {
    return {tetra.begin(),tetra.end()};
}
std::string makeAscii(const std::vector<std::array<std::array<int,3>,3>>& facets) {
    std::string text="solid tetra\n";
    for(const auto& f:facets) {
        text+="facet normal 0 0 0\n outer loop\n";
        for(const auto& v:f)
            text+="vertex "+std::to_string(v[0])+" "+
                   std::to_string(v[1])+" "+std::to_string(v[2])+"\n";
        text+="endloop\nendfacet\n";
    }
    return text+"endsolid tetra\n";
}
void run() {
    namespace fs=std::filesystem;
    const auto dir=fs::temp_directory_path()/"mesher-stl-import-positive";
    fs::create_directories(dir);
    struct Cleanup {
        fs::path dir;
        ~Cleanup(){std::error_code ec;fs::remove_all(dir,ec);}
    } cleanup{dir};
    const auto put=[&](const std::string& name,const std::string& content) {
        const auto filename=dir/name;
        std::ofstream out(filename,std::ios::binary|std::ios::trunc);
        out.write(content.data(),static_cast<std::streamsize>(content.size()));
        check(static_cast<bool>(out),"STL fixture write");
        return filename;
    };
    const auto vertices=tetraFacets();
    const auto ascii=put("mesh.without-stl-suffix",makeAscii(vertices));
    const auto source=readMeshOpenCandidate(ascii,MeshOpenFilter::AllFiles);
    check(source.format==MeshOpenFormat::StereolithographyStl&&
          source.vertexCount==4&&source.faceCount==4,
          "ASCII STL is detected by content and welded to four vertices");
    check(!source.hasVertexColors&&source.snapshot.index,
          "Ordinary ASCII STL uses opaque white original source");
    const auto binary=put("binary.obj",makeBinary(vertices,{0,0,0,0},"solid not ascii"));
    const auto restored=readMeshOpenCandidate(binary);
    check(restored.format==MeshOpenFormat::StereolithographyStl&&
          restored.vertexCount==4&&restored.faceCount==4,
          "Binary STL header beginning 'solid' is NOT mistaken for ASCII");
    check(readMeshOpenCandidate(binary,MeshOpenFilter::StereolithographyStl).
          vertexCount==4,"Explicit STL selection ignores misleading extension");
    const auto inherited=source.snapshot.index;
    const auto refuse=[&](const std::string& name,const std::string& content,
                          const char* diagnostic) {
        bool rejected=false;
        try {
            (void)readMeshOpenCandidate(
                put(name,content),MeshOpenFilter::StereolithographyStl);
        }catch(const std::exception& e) {
            rejected=std::string(e.what()).find(diagnostic)!=std::string::npos;
        }
        check(rejected,"STL must refuse with the expected precise diagnostic");
        check(source.snapshot.index==inherited,
              "Failed STL candidate must not change existing indexed source");
    };
    auto open=vertices;
    open.pop_back();
    refuse("open.stl",makeAscii(open),"closed");
    auto flipped=vertices;
    std::swap(flipped[0][1],flipped[0][2]);
    refuse("inside-out-facet.stl",makeAscii(flipped),"consistently wound");
    auto zero=vertices;
    zero[0][2]=zero[0][1];
    refuse("degenerate.stl",makeAscii(zero),"collapses");
    auto broken=makeBinary(vertices,{0,0,0,0});
    broken.resize(broken.size()-2);
    refuse("truncated.stl",broken,"count/length mismatch");
    auto badAttribute=makeBinary(vertices,{8,8,8,8});
    refuse("unknown-colour.stl",badAttribute,"ambiguous");
    // VisCAM/SolidView: 1 valid bit, 5-bit B,G,R, all-red facets.
    auto red=makeBinary(vertices,{0xfc00,0xfc00,0xfc00,0xfc00});
    auto colored=readMeshOpenCandidate(put("viscam.stl",red));
    check(colored.hasVertexColors&&colored.vertexCount==4,
          "VisCAM per-facet colours convert to a welded vertex stream");
    colored.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part){
            for(const auto& v:part.vertices)
                check(v.color.r==1&&v.color.g==0&&v.color.b==0,
                      "VisCAM red/green/blue channel ordering is correct");
        });
    // Magics: COLOR= RGBA object default, bit 15 set uses object colour.
    std::string magicsHeader="Materialise COLOR=";
    for(unsigned char byte:std::array<unsigned char,4>{255,128,0,64})
        magicsHeader.push_back(static_cast<char>(byte));
    auto magics=readMeshOpenCandidate(put("magics.stl",
        makeBinary(vertices,{0x8000,0x8000,0x8000,0x8000},magicsHeader)));
    check(magics.hasVertexColors,"Magics object colour is applied to facets");
    magics.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part){
            for(const auto& v:part.vertices)
                check(v.color.r==1&&
                      std::abs(v.color.g-128.f/255.f)<1e-6f&&
                      v.color.b==0&&
                      std::abs(v.color.a-64.f/255.f)<1e-6f,
                      "Magics header RGBA and per-facet default-bit semantics");
        });
    // Magics per-facet colour: red occupies LOW five bits (opposite VisCAM).
    const auto magicRed=readMeshOpenCandidate(put("magics-red.stl",
        makeBinary(vertices,{31,31,31,31},magicsHeader)));
    magicRed.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& v:part.vertices)
                check(v.color.r==1&&v.color.g==0&&v.color.b==0,
                      "Magics red-low packing / override-bit polarity");
        });
    // Default STL imports must use the visually selected diffusion path;
    // alternate methods remain explicit diagnostic comparisons.
    check(StlImportOptions{}.method==StlColourMethod::Diffusion&&
          StlColourMethodMetrics{}.method==StlColourMethod::Diffusion,
          "Diffusion is the production STL colour-conversion default");
    // Discontinuous face colours are now an approximation task, never a
    // reason to refuse geometrically valid closed source topology.
    const auto seams=put("colour-seam.stl",
        makeBinary(vertices,{0xfc00,0x83e0,0xfc00,0xfc00}));
    const auto mixed=put("mixed.stl",
        makeBinary(vertices,{0xfc00,0,0xfc00,0xfc00}));
    for(const auto method:{StlColourMethod::Average,
                           StlColourMethod::Stochastic,
                           StlColourMethod::Diffusion}) {
        const StlImportOptions options{method,12345};
        const auto seam=readMeshOpenCandidate(
            seams,MeshOpenFilter::StereolithographyStl,options);
        const auto white=readMeshOpenCandidate(
            mixed,MeshOpenFilter::StereolithographyStl,options);
        check(seam.hasVertexColors&&seam.vertexCount==4&&
              white.hasVertexColors&&white.vertexCount==4,
              "All three methods must import conflicting/missing facet colour");
        const auto a=parseStlMesh(seams,options);
        const auto b=parseStlMesh(seams,options);
        check(a.colors==b.colors,
              "Every STL candidate must be deterministic for a fixed seed");
        for(const auto& color:a.colors)
            check(std::isfinite(color.r)&&std::isfinite(color.g)&&
                  std::isfinite(color.b)&&std::isfinite(color.a)&&
                  color.r>=0&&color.r<=1&&color.g>=0&&color.g<=1&&
                  color.b>=0&&color.b<=1&&color.a>=0&&color.a<=1,
                  "Converted RGBA must remain finite and within [0,1]");
    }
    // The default import must exactly match explicit diffusion, with the
    // same seed, on a source with conflicting incident facet colours.
    const auto defaultSeam=parseStlMesh(seams);
    const auto explicitDiffusion=parseStlMesh(
        seams,StlImportOptions{StlColourMethod::Diffusion,1});
    check(defaultSeam.colors==explicitDiffusion.colors &&
          defaultSeam.vertices.size()==explicitDiffusion.vertices.size() &&
          defaultSeam.triangles.size()==explicitDiffusion.triangles.size(),
          "Default STL Open uses deterministic diffusion with unchanged topology");
    for(std::size_t i=0;i<defaultSeam.vertices.size();++i) {
        const auto a=defaultSeam.vertices[i],b=explicitDiffusion.vertices[i];
        check(a.x==b.x&&a.y==b.y&&a.z==b.z,
              "Diffusion selection must not alter imported coordinates");
    }
    for(std::size_t i=0;i<defaultSeam.triangles.size();++i) {
        const auto a=defaultSeam.triangles[i],b=explicitDiffusion.triangles[i];
        check(a.a==b.a&&a.b==b.b&&a.c==b.c,
              "Diffusion selection must not alter welded triangles");
    }
    // Evaluate and profile the same original facet stream without
    // committing a worker or changing any source coordinates.
    const auto geometry=defaultSeam;
    const std::vector<std::optional<VertexRGBA>> sourceColours{
        VertexRGBA{1,0,0,1},VertexRGBA{0,1,0,1},
        VertexRGBA{1,0,0,1},VertexRGBA{1,0,0,1}};
    StlColourComparison metrics;
    const auto output=compareStlFacetColours(
        geometry,sourceColours,StlColourMethod::Diffusion,777,&metrics);
    check(metrics.vertices==4&&metrics.faces==4&&
          output.size()==geometry.vertices.size()&&
          metrics.selected==StlColourMethod::Diffusion,
          "Comparison reports exact input size and selected candidate");
    StlColourMethodMetrics selectedMetric;
    const auto production=convertStlFacetColours(
        geometry,sourceColours,StlColourMethod::Diffusion,777,&selectedMetric);
    check(production==output&&
          selectedMetric.method==StlColourMethod::Diffusion&&
          std::isfinite(selectedMetric.conversionMs)&&
          std::isfinite(selectedMetric.rgbRms),
          "Production diffusion must match the full A/B/C benchmark exactly");
    for(int i=0;i<3;++i) {
        const auto& stat=metrics.methods[static_cast<std::size_t>(i)];
        check(static_cast<int>(stat.method)==i&&
              std::isfinite(stat.conversionMs)&&stat.conversionMs>=0&&
              std::isfinite(stat.evaluationMs)&&stat.evaluationMs>=0&&
              std::isfinite(stat.rgbRms)&&stat.rgbRms>=0&&
              std::isfinite(stat.alphaRms)&&stat.alphaRms>=0&&
              std::isfinite(stat.contrastError)&&stat.contrastError>=0,
              "All three candidate timings and fidelity metrics are finite");
    }
    check(metrics.graphPreparationMs>=0&&
          std::isfinite(metrics.graphPreparationMs),
          "Graph preparation reports its separate import-time cost");
    // Exact uniform facet colour is a lossless case under ALL methods.
    const Mesh whiteGeometry=parseStlMesh(binary);
    const std::vector<std::optional<VertexRGBA>> uniform(
        whiteGeometry.triangles.size(),VertexRGBA{.2f,.4f,.6f,.8f});
    for(const auto method:{StlColourMethod::Average,
                           StlColourMethod::Stochastic,
                           StlColourMethod::Diffusion}) {
        const auto colors=compareStlFacetColours(
            whiteGeometry,uniform,method,99);
        for(const auto& color:colors)
            check(std::abs(color.r-.2f)<1e-6f&&
                  std::abs(color.g-.4f)<1e-6f&&
                  std::abs(color.b-.6f)<1e-6f&&
                  std::abs(color.a-.8f)<1e-6f,
                  "Uniform original facet colours remain exact in each method");
    }
    refuse("materials.stl",makeBinary(vertices,{0,0,0,0},"MATERIAL="),
           "MATERIAL=");
    // Two closed tetrahedra touching only at an exactly coincident corner
    // are not a valid single-fan vertex even though every edge has two faces.
    auto touching=vertices;
    for(auto f:vertices) {
        for(auto& v:f)for(auto& c:v)c=-c;
        touching.push_back(f);
    }
    refuse("point-touch.stl",makeAscii(touching),"disconnected vertex fans");
    // Imported candidates survive durable checkpoint re-open without
    // inventing vertex colours or geometry provenance.
    const auto checkpoint=dir/"from-stl.mshr";
    writeCheckpoint(checkpoint,colored.snapshot);
    const auto roundtrip=readMeshOpenCandidate(checkpoint);
    check(roundtrip.vertexCount==4&&roundtrip.hasVertexColors,
          "STL vertex-colour conversion survives .mshr checkpoint round trip");
    check(source.snapshot.index==inherited,
          "Successful subsequent imports do not mutate older private roots");
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==2) {
            const auto generated=readMeshOpenCandidate(
                argv[1],MeshOpenFilter::StereolithographyStl);
            check(generated.faceCount>=736&&generated.vertexCount>300&&
                  generated.hasVertexColors,
                  "Generator output must import as a closed coloured STL");
            return 0;
        }
        run();
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

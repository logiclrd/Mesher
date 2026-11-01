#include "mesher/mesh_export_dispatch.hpp"
#include "mesher/mesh_open_candidate.hpp"
#include "mesher/mesh_ply.hpp"
#include "mesher/mesh_stl.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <vector>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

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
        require(candidate.detectedExportPreset==preset,
                "OBJ importer lost exact source colour encoding");
        require(candidate.hasVertexColors==(preset!=
                    mesher::MeshExportPreset::ObjGeometry),
                "OBJ export preset changed colour policy");
        require((payload.find("#MRGB ")!=std::string::npos)==
                (preset==mesher::MeshExportPreset::ObjZbrushMrgb),
                "MRGB used with incorrect OBJ dropdown entry");
    }else if(info.extension=="ply") {
        require(payload.starts_with("ply\nformat "),
                "Selected PLY codec did not write PLY header");
        mesher::PlyDetectedFormat detected{};
        const auto loaded=mesher::parsePlyMesh(path,&detected);
        require(static_cast<unsigned>(detected.encoding)*4u+
                    static_cast<unsigned>(detected.colour)==index-4u,
                "PLY importer lost endian/colour/alpha provenance selection");
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
        mesher::StlDetectedEncoding detected{};
        const auto loaded=mesher::parseStlMesh(path,{},&detected);
        const unsigned expectedDialect=index<21u?
            static_cast<unsigned>(index-16u):
            index==21u||index==22u?2u:3u;
        require(static_cast<unsigned>(detected)==expectedDialect,
                "STL importer lost exact colour dialect or ASCII/binary format");
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

// UX07F's end-to-end integration boundary is the *accepted indexed root*
// through the *actual background writer* and selected dialog encoder into
// UX06's private Open candidate. Codec-only tests above deliberately remain
// separate; they do not exercise atomic publication, frozen roots, content
// auto-detection or native-format import admission.
bool near(double a,double b,double tolerance=1e-6) {
    return std::abs(a-b)<=tolerance;
}
bool samePosition(mesher::Vec3 a,mesher::Vec3 b,double tolerance) {
    return near(a.x,b.x,tolerance)&&near(a.y,b.y,tolerance)&&
           near(a.z,b.z,tolerance);
}
std::shared_ptr<const mesher::RegionTopologyIndex> indexed(
    const mesher::Mesh& mesh) {
    return mesher::RegionTopologyIndex::build(
        mesher::partitionFlatMesh(mesh).revision);
}
struct RoundTripDirectory {
    fs::path path;
    RoundTripDirectory() {
        path=fs::temp_directory_path()/
            ("mesher-ux07f-"+std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~RoundTripDirectory() {
        std::error_code ignored;
        fs::remove_all(path,ignored);
    }
};
void checkNoExportTemps(const fs::path& folder) {
    for(const auto& entry:fs::directory_iterator(folder)) {
        require(entry.path().filename().string().find(".mesher-export-")==
                    std::string::npos,
                "UX07F left an atomic export temporary file");
    }
}
void checkGeometryAndWinding(const mesher::Mesh& source,
                             const mesher::Mesh& loaded,
                             std::vector<std::size_t>& mapping,
                             double tolerance) {
    require(source.vertices.size()==loaded.vertices.size()&&
            source.triangles.size()==loaded.triangles.size(),
            "UX07F lost imported vertices or oriented faces");
    mapping.assign(source.vertices.size(),loaded.vertices.size());
    std::vector<bool> used(loaded.vertices.size(),false);
    // STL may weld and reorder vertices, so never compare imported index
    // numbers to original stable IDs. Map by uniquely matching positions.
    for(std::size_t i=0;i<source.vertices.size();++i) {
        for(std::size_t j=0;j<loaded.vertices.size();++j) {
            if(!samePosition(source.vertices[i],loaded.vertices[j],tolerance))
                continue;
            require(mapping[i]==loaded.vertices.size()&&!used[j],
                    "UX07F ambiguous/multiply matched imported coordinates");
            mapping[i]=j;
            used[j]=true;
        }
        require(mapping[i]!=loaded.vertices.size(),
                "UX07F changed exported vertex coordinates");
    }
    std::vector<bool> usedFaces(loaded.triangles.size(),false);
    for(const auto& face:source.triangles) {
        const std::array<std::size_t,3> want{
            mapping[face.a],mapping[face.b],mapping[face.c]};
        bool found=false;
        for(std::size_t j=0;j<loaded.triangles.size();++j) {
            if(usedFaces[j])continue;
            const auto t=loaded.triangles[j];
            const std::array<std::size_t,3> got{t.a,t.b,t.c};
            for(std::size_t rotation=0;rotation<3;++rotation) {
                if(got[rotation]!=want[0]||
                   got[(rotation+1)%3]!=want[1]||
                   got[(rotation+2)%3]!=want[2])continue;
                usedFaces[j]=true;
                found=true;
                break;
            }
            if(found)break;
        }
        require(found,"UX07F lost or reversed an oriented triangle");
    }
}
void checkRoundTrip(const mesher::Mesh& source,
                    const mesher::MeshExportFormatEntry& entry,
                    const fs::path& path) {
    using namespace mesher;
    const auto preset=entry.preset;
    auto candidate=readMeshOpenCandidate(path,MeshOpenFilter::AllFiles);
    const auto index=static_cast<std::size_t>(preset);
    const MeshOpenFormat expected=index<=3?MeshOpenFormat::WavefrontObj:
        index<=15?MeshOpenFormat::StanfordPly:
                  MeshOpenFormat::StereolithographyStl;
    require(candidate.format==expected,
            "UX07F content auto-detection selected the wrong importer");
    require(candidate.detectedExportPreset==preset,
            "UX07F private Open lost the exact re-export format preference");
    require(candidate.vertexCount==source.vertices.size()&&
            candidate.faceCount==source.triangles.size(),
            "UX07F private Open candidate did not admit the export");
    const bool coloured=(index<=3?index!=0:
                        index<=15?(index-4)%4!=0:index>=18);
    require(candidate.hasVertexColors==coloured,
            "UX07F imported a wrong colour-presence policy");

    // PLY intentionally holds original file alpha separate from the
    // editable mesh alpha until the user has explicitly chosen to use it.
    // Exercise that *real* UX06 admission decision rather than treating
    // source alpha as if the importer had already adopted it.
    const bool ply=index>=4&&index<=15;
    const std::size_t plyMode=ply?(index-4)%4:0;
    if(ply&&plyMode>=2) {
        require(candidate.alphaDecisionRequired,
                "UX07F lost required variable-alpha interpretation choice");
        const auto before=candidate.snapshot.index;
        applyPlySourceAlphaAsOpacity(candidate);
        require(before!=candidate.snapshot.index,
                "UX07F PLY alpha decision did not create a new private root");
    }else {
        require(!candidate.alphaDecisionRequired,
                "UX07F invented a PLY alpha decision for an unrelated codec");
    }
    const Mesh opened=materializeMeshExport(
        captureMeshExportRequest(path,candidate.snapshot.index));
    std::vector<std::size_t> mapping;
    checkGeometryAndWinding(source,opened,mapping,index>=16?1e-5:1e-7);
    require(opened.colors.size()==opened.vertices.size(),
            "UX07F imported revision has incomplete vertex colours");

    std::array<double,4> mean{};
    for(const auto& c:source.colors) {
        mean[0]+=c.r;mean[1]+=c.g;mean[2]+=c.b;mean[3]+=c.a;
    }
    for(auto& value:mean)value/=static_cast<double>(source.colors.size());
    bool foundNonwhite=false;
    bool differsFromOriginal=false;
    for(std::size_t i=0;i<source.colors.size();++i) {
        const auto& a=source.colors[i];
        const auto& b=opened.colors[mapping[i]];
        require(std::isfinite(b.r)&&std::isfinite(b.g)&&
                std::isfinite(b.b)&&std::isfinite(b.a)&&
                b.r>=0&&b.r<=1&&b.g>=0&&b.g<=1&&
                b.b>=0&&b.b<=1&&b.a>=0&&b.a<=1,
                "UX07F imported nonfinite/out-of-range editable RGBA");
        foundNonwhite|=!near(b.r,1)||!near(b.g,1)||!near(b.b,1);
        differsFromOriginal|=!near(a.r,b.r,1e-5)||
                             !near(a.g,b.g,1e-5)||
                             !near(a.b,b.b,1e-5);
        if(!coloured)continue;
        if(index==20) {
            // Magics single-object mode deliberately averages *all*
            // source vertex RGBA, not the facet mean at this vertex.
            constexpr double precision=1.0/255.0+1e-4;
            require(near(b.r,mean[0],precision)&&
                    near(b.g,mean[1],precision)&&
                    near(b.b,mean[2],precision)&&
                    near(b.a,mean[3],precision),
                    "UX07F Magics single-object RGBA differs from chosen average");
        }else if(index>=18) {
            // VisCAM/Magics facet colour is 5-bit, then UX06 diffuses
            // per-facet colours to a vertex field. Exact source RGB is
            // mathematically unrecoverable; never assert false fidelity.
            require(near(b.a,1),
                    "UX07F facet-colour STL invented editable alpha");
        }else {
            const double precision=(index==3||ply)?0.5/255.0+1e-5:1e-6;
            require(near(a.r,b.r,precision)&&
                    near(a.g,b.g,precision)&&
                    near(a.b,b.b,precision),
                    "UX07F editable RGB differs beyond codec precision");
            const bool alphaKept=index==2||(ply&&plyMode>=2);
            const double expectedAlpha=alphaKept?
                ply&&plyMode==3?*a.sourceAlpha:a.a:1.0;
            require(near(b.a,expectedAlpha,
                         ply?0.5/255.0+1e-5:1e-6),
                    "UX07F editable alpha did not follow disclosed codec policy");
            if(ply&&plyMode>=2) {
                require(b.sourceAlpha&&
                        near(*b.sourceAlpha,expectedAlpha,0.5/255.0+1e-5),
                        "UX07F lost original PLY alpha provenance");
            }else {
                require(!b.sourceAlpha,
                        "UX07F invented original-source alpha provenance");
            }
        }
    }
    if(coloured)
        require(foundNonwhite,
                "UX07F selected coloured export reimported as plain white");
    if(index==18||index==19||index==20)
        require(differsFromOriginal,
                "UX07F STL lossy colour mode unexpectedly retained every source RGB");
}
// Real checked-in third-party source files exercise the same end-to-end
// path beyond a synthetic tetrahedron. Compare a deterministic sampling of
// large OBJ/PLY positions; STL welding may reorder vertices, so compare
// spatial bounds instead of pretending file indices are stable IDs.
void testRealFixtureExport(const fs::path& input) {
    using namespace mesher;
    RoundTripDirectory tmp;
    const auto original=readMeshOpenCandidate(input,MeshOpenFilter::AllFiles);
    const auto root=original.snapshot.index;
    const auto source=materializeMeshExport(
        captureMeshExportRequest(tmp.path/"source",root));
    const MeshExportPreset preset=
        original.format==MeshOpenFormat::WavefrontObj?
            MeshExportPreset::ObjZbrushMrgb:
        original.format==MeshOpenFormat::StanfordPly?
            MeshExportPreset::PlyLittleRgb:
        original.format==MeshOpenFormat::StereolithographyStl?
            MeshExportPreset::StlMagicsRgb:
            throw std::invalid_argument("UX07F real fixture is not a foreign mesh");
    const auto destination=validateExportFilename(
        tmp.path/"real-round-trip",preset);
    const auto request=captureMeshExportRequest(destination,root);
    AsyncMeshExportWriter writer;
    writer.request(request,meshExportEncoder(preset));
    writer.drain();
    const auto completed=writer.takeCompletions();
    require(completed.size()==1&&completed[0].success&&
            completed[0].destination==destination,
            "UX07F real-file asynchronous export failed");
    const auto loaded=readMeshOpenCandidate(destination,MeshOpenFilter::AllFiles);
    const auto roundTripped=materializeMeshExport(
        captureMeshExportRequest(destination,loaded.snapshot.index));
    require(roundTripped.vertices.size()==source.vertices.size()&&
            roundTripped.triangles.size()==source.triangles.size(),
            "UX07F real fixture lost live vertices or oriented triangles");
    if(preset==MeshExportPreset::StlMagicsRgb) {
        // STL has no global vertex table. Compare full spatial extrema,
        // including float32 quantization, without assuming welded order.
        const auto box=[](const Mesh& mesh) {
            std::array<double,6> b{
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity(),
                -std::numeric_limits<double>::infinity()};
            for(const auto& v:mesh.vertices) {
                b[0]=std::min(b[0],v.x);b[1]=std::min(b[1],v.y);
                b[2]=std::min(b[2],v.z);b[3]=std::max(b[3],v.x);
                b[4]=std::max(b[4],v.y);b[5]=std::max(b[5],v.z);
            }
            return b;
        };
        const auto before=box(source),after=box(roundTripped);
        for(std::size_t i=0;i<before.size();++i)
            require(near(before[i],after[i],
                         1e-5*std::max(1.0,std::abs(before[i]))),
                    "UX07F real STL round trip shifted geometric bounds");
    }else {
        const auto interval=std::max<std::size_t>(
            1,source.vertices.size()/37);
        for(std::size_t i=0;i<source.vertices.size();i+=interval) {
            require(samePosition(source.vertices[i],roundTripped.vertices[i],1e-6),
                    "UX07F real OBJ/PLY round trip changed sampled coordinates");
            if(original.hasVertexColors) {
                const auto& a=source.colors[i];
                const auto& b=roundTripped.colors[i];
                require(near(a.r,b.r,0.5/255.0+1e-5)&&
                        near(a.g,b.g,0.5/255.0+1e-5)&&
                        near(a.b,b.b,0.5/255.0+1e-5),
                        "UX07F real OBJ/PLY round trip changed sampled RGB");
            }
        }
    }
    require(materializeMeshExport(
            captureMeshExportRequest(tmp.path/"unchanged",root)).vertices.size()
                ==source.vertices.size(),
            "UX07F real source root changed during export");
    checkNoExportTemps(tmp.path);
    std::cout<<"UX07F real foreign mesh "<<input.filename().string()
             <<" -> selected codec -> private Open passed\n";
}

void testUniformMagicsObjectRoundTrip() {
    // A fully painted mesh is different from the multicolour UX07F tetra.
    // An object-level COLOR= must survive real async export and private Open
    // as a single uniform vertex RGBA, including its 8-bit alpha.
    using namespace mesher;
    for(const VertexRGBA paint : {VertexRGBA{0.2f,0.6f,0.1f,1.f},
                                  VertexRGBA{0.4f,0.3f,0.8f,0.4f}}) {
        RoundTripDirectory tmp;
        Mesh solid=tetra();
        solid.colors.assign(solid.vertices.size(),paint);
        const auto root=indexed(solid);
        const auto path=tmp.path/"uniform-magics.stl";
        AsyncMeshExportWriter writer;
        writer.request(captureMeshExportRequest(path,root),
                       meshExportEncoder(MeshExportPreset::StlMagicsObjectRgba));
        writer.drain();
        const auto completions=writer.takeCompletions();
        require(completions.size()==1&&completions.front().success,
                "Solid-colour Magics object export failed");
        const auto reopened=readMeshOpenCandidate(path,MeshOpenFilter::AllFiles);
        require(reopened.detectedExportPreset==
                    MeshExportPreset::StlMagicsObjectRgba,
                "Solid-colour Magics object STL lost its dialect");
        require(reopened.hasVertexColors,
                "Solid-colour Magics object STL reimported as uncoloured");
        const Mesh back=materializeMeshExport(
            captureMeshExportRequest(path,reopened.snapshot.index));
        require(back.colors.size()==back.vertices.size(),
                "Solid-colour Magics object imported incomplete colour stream");
        for(const auto& colour:back.colors)
            require(near(colour.r,paint.r,0.5/255.0+1e-5)&&
                    near(colour.g,paint.g,0.5/255.0+1e-5)&&
                    near(colour.b,paint.b,0.5/255.0+1e-5)&&
                    near(colour.a,paint.a,0.5/255.0+1e-5),
                    "Solid-colour Magics object RGB(A) changed on private reopen");
    }
}

void testFullAsyncExportAndPrivateReopen() {
    using namespace mesher;
    RoundTripDirectory tmp;
    const auto accepted=indexed(tetra());
    const auto before=accepted.get();
    const auto original=materializeMeshExport(
        captureMeshExportRequest(tmp.path/"source.ply",accepted));
    AsyncMeshExportWriter writer;
    std::vector<fs::path> destinations;
    destinations.reserve(meshExportFormats.size());
    for(std::size_t i=0;i<meshExportFormats.size();++i) {
        const auto preset=static_cast<MeshExportPreset>(i);
        const auto selected=validateExportFilename(
            tmp.path/("preset-"+std::to_string(i)),preset);
        destinations.push_back(selected);
        // An existing file must be atomically overwritten, never exposed
        // as a half-written OBJ/PLY/STL during asynchronous serialization.
        {
            std::ofstream old(selected,std::ios::binary|std::ios::trunc);
            old<<"PRESERVE IF EXPORT FAILS";
        }
        writer.request(captureMeshExportRequest(selected,accepted),
                       meshExportEncoder(preset));
    }
    writer.drain();
    const auto stats=writer.statistics();
    require(stats.requested==meshExportFormats.size()&&
            stats.completed==meshExportFormats.size()&&
            stats.failed==0&&stats.pending==0&&!stats.busy,
            "UX07F full selected-codec writer matrix did not complete");
    const auto outcomes=writer.takeCompletions();
    require(outcomes.size()==destinations.size(),
            "UX07F lost one or more real writer outcomes");
    for(std::size_t i=0;i<outcomes.size();++i) {
        require(outcomes[i].success&&outcomes[i].error.empty()&&
                outcomes[i].destination==destinations[i],
                "UX07F async export did not preserve selected path/order");
        checkRoundTrip(original,meshExportFormats[i],destinations[i]);
    }
    require(accepted.get()==before&&
            materializeMeshExport(captureMeshExportRequest(
                tmp.path/"source.ply",accepted)).colors==original.colors,
            "UX07F altered the accepted indexed source during export");
    checkNoExportTemps(tmp.path);

    // An invalid original-source-alpha request must fail through the
    // *actual* selected encoder and async atomic transaction, without
    // damaging existing bytes or fabricating a success event.
    auto missingSourceAlpha=tetra();
    missingSourceAlpha.colors[0].sourceAlpha.reset();
    const auto invalid=indexed(missingSourceAlpha);
    const auto preserved=tmp.path/"preserved.ply";
    {
        std::ofstream file(preserved,std::ios::binary|std::ios::trunc);
        file<<"OLD GOOD OUTPUT";
    }
    writer.request(captureMeshExportRequest(preserved,invalid),
                   meshExportEncoder(MeshExportPreset::PlyLittleSourceAlpha));
    writer.drain();
    auto failures=writer.takeCompletions();
    require(failures.size()==1&&!failures[0].success&&
            !failures[0].error.empty()&&
            failures[0].destination==preserved,
            "UX07F missing source alpha was incorrectly reported successful");
    {
        std::ifstream file(preserved,std::ios::binary);
        std::string originalBytes;
        std::getline(file,originalBytes);
        require(originalBytes=="OLD GOOD OUTPUT",
                "UX07F failed conversion replaced a valid previous export");
    }
    checkNoExportTemps(tmp.path);
    std::cout<<"UX07F async selected codec -> atomic file -> private Open "
                "round trips and failure preservation passed\n";
}

void testNames() {
    using namespace mesher;
    require(meshExportFormats.size()==25,
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
            fs::path(std::string("test.")+std::string(entry.extension)),preset);
        require(already==checked,"Matching filename suffix was changed");
        ++(entry.extension=="obj"?obj:entry.extension=="ply"?ply:stl);
        const auto different=entry.extension=="obj"?"stl":"obj";
        require(validateExportFilename(
                    fs::path(std::string("test.")+different),preset)==
                    fs::path(std::string("test.")+different+"."+
                             std::string(entry.extension)),
                "An unquoted mismatched suffix was not preserved and extended");
        require(validateExportFilename("test.jpg",preset)==
                    fs::path(std::string("test.jpg.")+std::string(entry.extension)),
                "An unrelated suffix was incorrectly replaced");
        require(validateExportFilename(
                    fs::path(std::string("\"test.")+different+"\""),preset)==
                    fs::path(std::string("test.")+different),
                "Quoted filename override was ignored");
        require(validateExportFilename("\"test\"",preset)=="test",
                "Quoted extensionless export was given an unwanted suffix");
    }
    constexpr std::string_view facetDisclosure=
        "Colourized STL is based on facet colours instead of vertex colours. "
        "Mesher makes a best effort to distribute vertex colours across the faces "
        "of the mesh, but the exported file is unavoidably not an exact "
        "representation of the Mesher document.";
    require(meshExportFormat(MeshExportPreset::StlViscamRgb).disclosure==
                facetDisclosure &&
            meshExportFormat(MeshExportPreset::StlMagicsRgb).disclosure==
                facetDisclosure,
            "Facet-coloured STL confirmation did not use the updated wording");
    require(changeExportFilenameExtension("model.obj","obj","ply")=="model.ply",
            "OBJ -> PLY did not replace its matching prior suffix");
    require(changeExportFilenameExtension("model.stl","obj","ply")==
            "model.stl.ply","OBJ -> PLY stripped a different suffix");
    require(changeExportFilenameExtension("model.obj","stl","ply")==
            "model.obj.ply","STL -> PLY stripped a different suffix");
    require(changeExportFilenameExtension("model.stl","stl","ply")=="model.ply",
            "STL -> PLY did not remove the matching STL suffix");
    require(changeExportFilenameExtension("Duck.stl","ply","stl")=="Duck.stl",
            "PLY -> STL duplicated the already present STL suffix");
    require(changeExportFilenameExtension("Duck.STL","ply","stl")=="Duck.STL",
            "PLY -> STL duplicated a case-insensitive matching suffix");
    require(changeExportFilenameExtension("Duck.stl","obj","stl")=="Duck.stl",
            "OBJ -> STL duplicated the new filter's existing suffix");
    require(changeExportFilenameExtension("Duck.ply","ply","ply")=="Duck.ply",
            "Switching within one file-format family duplicated its suffix");
    require(changeExportFilenameExtension("Duck.stl","ply","obj")==
            "Duck.stl.obj",
            "An unrelated suffix must still be preserved and extended");
    require(changeExportFilenameExtension("model.PLY","ply","obj")=="model.obj",
            "Previous suffix comparison was case-sensitive");
    require(changeExportFilenameExtension("\"model.obj\"","obj","ply")==
            "\"model.obj\"","Filter transition rewrote a quoted filename");
    require(validateExportFilename("model.PLY",MeshExportPreset::PlyLittleRgb)==
            "model.PLY","Case-only difference created a double suffix");
    require(obj==4&&ply==12&&stl==9,
            "Wrong number of format/encoding dropdown entries");
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==3&&std::string_view(argv[1])=="--real-fixture") {
            testRealFixtureExport(argv[2]);
            return 0;
        }
        require(argc==1,"UX07F unknown test arguments");
        testNames();
        testUniformMagicsObjectRoundTrip();
        testFullAsyncExportAndPrivateReopen();
        for(std::size_t i=0;i<mesher::meshExportFormats.size();++i)
            testPreset(static_cast<mesher::MeshExportPreset>(i));
        std::cout<<"UX07E 25 dialog presets, codecs, suffixes and disclosure passed\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"UX07E failure: "<<e.what()<<'\n';
        return 1;
    }
}

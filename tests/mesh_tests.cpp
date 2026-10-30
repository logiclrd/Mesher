#include "mesher/mesh.hpp"
#include "mesher/mesh_open_candidate.hpp"
#include "mesher/mesh_partitioning.hpp"
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

using namespace mesher;

void check(bool condition, const char* msg) { if(!condition) throw std::runtime_error(msg); }
void test(std::uint32_t n, bool symmetric, std::uint64_t seed) {
    SphereOptions options;
    options.vertexCount=n; options.symmetric=symmetric; options.seed=seed;
    options.relaxationIterations=20;
    const auto mesh=generateSphere(options);
    check(mesh.vertices.size()==n,"Exact vertex count");
    check(validateSphere(mesh),"Manifold");
    check(mesh.triangles.size()==2*n-4,"Sphere triangle count");
    for(auto vertex:mesh.vertices) check(std::abs(length(vertex)-1)<1e-10,"Unit radius");
    if(symmetric){
        const auto spacing=std::sqrt(8*3.14159265358979323846/(double(n)*std::sqrt(3.0)));
        auto m=static_cast<std::uint32_t>(std::floor(2*3.14159265358979323846/spacing))+1;
        if((n-m)%2)++m;
        const auto p=(n-m)/2;
        for(std::uint32_t i=0;i<p;++i) {
            auto a=mesh.vertices[i],b=mesh.vertices[i+p+m];
            check(std::abs(a.x+b.x)<1e-12 && std::abs(a.y-b.y)<1e-12 && std::abs(a.z-b.z)<1e-12,"Mirrored vertex");
        }
        for(auto i=p;i<p+m;++i)check(std::abs(mesh.vertices[i].x)<1e-12,"Fixed seam ring");
    }
    std::cout<<(symmetric?"sym":"asym")<<" n="<<n<<" faces="<<mesh.triangles.size()<<" OK\n";
}

void testPrivateMeshOpenCandidates() {
    namespace fs=std::filesystem;
    const auto dir=fs::temp_directory_path()/"mesher-ux06-private-open-test";
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() {std::error_code ignored;fs::remove_all(path,ignored);}
    } cleanup{dir};
    const auto put=[&](const std::string& name,const std::string& body) {
        const auto path=dir/name;
        std::ofstream output(path,std::ios::binary|std::ios::trunc);
        output<<body;
        if(!output)throw std::runtime_error("Cannot create import fixture");
        return path;
    };
    const std::string tetra=
        "v 0 0 0 1 0 0 0.25\n"
        "v 1 0 0 0 1 0 0.5\n"
        "v 0 1 0 0 0 1 0.75\n"
        "v 0 0 1 1 1 0 1\n"
        "f 1 3 2\nf 1 2 4\nf 2 3 4\nf 3 1 4\n";
    auto candidate=readMeshOpenCandidate(put("valid.OBJ",tetra));
    check(candidate.format==MeshOpenFormat::WavefrontObj,
          "OBJ case-insensitive extension must load a private candidate");
    check(candidate.vertexCount==4&&candidate.faceCount==4&&
          candidate.hasVertexColors,"OBJ candidate preserves vertex RGBA");
    check(candidate.snapshot.index&&candidate.snapshot.mirrorPairs.empty(),
          "OBJ candidate has an index but no invented symmetry");
    std::size_t colored=0;
    candidate.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& vertex:part.vertices)
                if(vertex.color.a<1)++colored;
        });
    check(colored==3,"OBJ normalized alpha values survive indexed conversion");
    const auto snapshotPath=dir/"valid.mshr";
    writeCheckpoint(snapshotPath,candidate.snapshot);
    const auto restored=readMeshOpenCandidate(snapshotPath);
    check(restored.format==MeshOpenFormat::Checkpoint&&
          restored.vertexCount==4&&restored.faceCount==4&&
          restored.hasVertexColors,
          "Private mshr import keeps indexed revision and per-vertex colours");
    // Every failure must leave the previously parsed private candidate
    // untouched; the API performs no global or worker publication.
    const auto oldRoot=candidate.snapshot.index;
    const auto reject=[&](const std::string& filename,
                          const std::string& contents) {
        bool rejected=false;
        try {(void)readMeshOpenCandidate(put(filename,contents));}
        catch(const std::exception&) {rejected=true;}
        check(rejected,("Invalid/ambiguous import must refuse: "+filename).c_str());
        check(candidate.snapshot.index==oldRoot,
              "Rejected import must not mutate an earlier candidate root");
    };
    reject("open.obj","v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    reject("mixed.obj",
           "v 0 0 0 1 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    reject("conflicting-inline-mrgb.obj",tetra+"#MRGB 0000ff00\n");
    reject("badindex.obj",tetra+"f 1 2 99\n");
    reject("unhandled.ply","ply\nformat ascii 1.0\nend_header\n");
    reject("unhandled.stl","solid test\nendsolid test\n");
    reject("bad.mshr","This is not a checkpoint");

    // The selected filter, never the file extension, determines parsing.
    const auto renamedCheckpoint=dir/"archive.r06case";
    fs::copy_file(snapshotPath,renamedCheckpoint,
                  fs::copy_options::overwrite_existing);
    check(readMeshOpenCandidate(renamedCheckpoint,
              MeshOpenFilter::MesherCheckpoint).snapshot.index!=nullptr,
          "Explicit checkpoint filter must ignore misleading extension");
    check(readMeshOpenCandidate(renamedCheckpoint,
              MeshOpenFilter::AllFiles).format==MeshOpenFormat::Checkpoint,
          "All Files must detect valid checkpoint magic regardless of name");
    check(readMeshOpenCandidate(put("obj.data",tetra),
              MeshOpenFilter::WavefrontObj).format==MeshOpenFormat::WavefrontObj,
          "Explicit Wavefront filter must ignore filename extension");
    check(readMeshOpenCandidate(put("obj.xyz",tetra),
              MeshOpenFilter::AllFiles).format==MeshOpenFormat::WavefrontObj,
          "All Files uses OBJ record heuristics rather than extension");
    const auto refuseWith=[&](const fs::path& path,MeshOpenFilter filter,
                              const std::string& diagnostic) {
        bool refused=false;
        try {(void)readMeshOpenCandidate(path,filter);}
        catch(const std::exception& e) {
            refused=true;
            check(std::string(e.what()).find(diagnostic)!=std::string::npos,
                  "Import refused for an unexpected reason");
        }
        check(refused,"Wrong forced format must be refused");
    };
    refuseWith(put("really.obj",tetra),MeshOpenFilter::MesherCheckpoint,
               "Unsupported checkpoint magic/version");
    refuseWith(renamedCheckpoint,MeshOpenFilter::WavefrontObj,
               "unsupported OBJ record");
    const auto repairArchive=dir/"old-repair-case.mshr";
    {
        std::ofstream output(repairArchive,std::ios::binary);
        output<<"MSHRCAS1";
        output.put(2);output.put(0);output.put(0);output.put(0);
    }
    refuseWith(repairArchive,MeshOpenFilter::AllFiles,"repair-case archive");
    refuseWith(repairArchive,MeshOpenFilter::MesherCheckpoint,
               "repair-case archive");
    refuseWith(put("bad_header.dat","garbage contents"),
               MeshOpenFilter::AllFiles,"Unrecognized mesh file header");
    refuseWith(put("bad_contents.mshr","MESHRV12"),
               MeshOpenFilter::AllFiles,"Truncated checkpoint");
    check(readMeshOpenCandidate(put("actually.mshr",tetra),
              MeshOpenFilter::AllFiles).format==MeshOpenFormat::WavefrontObj,
          "All Files must ignore a false .mshr extension");
    check(candidate.snapshot.index==oldRoot,
          "All forced and automatically detected failures leave root intact");
    check(candidate.snapshot.index==oldRoot,
          "Successful import remains immutable across failed attempts");
}


void testZbrushMrgb() {
    namespace fs=std::filesystem;
    const auto dir=fs::temp_directory_path()/"mesher-ux06-mrgb";
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}
    } cleanup{dir};
    const auto put=[&](const std::string& name,const std::string& contents){
        const auto path=dir/name;
        std::ofstream out(path,std::ios::binary|std::ios::trunc);
        out<<contents;
        check(static_cast<bool>(out),"MRGB test fixture written");
        return path;
    };
    const std::string xyz=
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n";
    const std::string faces=
        "f 1 3 2\nf 1 2 4\nf 2 3 4\nf 3 1 4\n";
    const std::string payload=
        "#MRGB 00ff0000 FF00ff00 7F0000ff 40000000\n";
    const auto original=readMeshOpenCandidate(put("ZBrush.obj",
        xyz+"# The following MRGB block contains ZBrush Vertex Color\n"+
        payload+faces),MeshOpenFilter::AllFiles);
    check(original.format==MeshOpenFormat::WavefrontObj&&
          original.vertexCount==4&&original.faceCount==4&&
          original.hasVertexColors,"ZBrush MRGB creates a coloured OBJ candidate");

    const std::array<VertexRGBA,4> expected{{
        {1.f,0.f,0.f,1.f},{0.f,1.f,0.f,1.f},
        {0.f,0.f,1.f,1.f},{0.f,0.f,0.f,1.f}
    }};
    const auto verify=[&](const MeshOpenCandidate& candidate){
        std::size_t seen=0;
        candidate.snapshot.index->revision().forEachPartition(
            [&](PartitionId,const MeshPartition& part){
                for(const auto& v:part.vertices) {
                    const std::size_t i=v.position.x==1?1:
                        v.position.y==1?2:v.position.z==1?3:0;
                    const auto& c=expected[i];
                    check(v.color.r==c.r&&v.color.g==c.g&&
                          v.color.b==c.b&&v.color.a==1.f&&
                          !v.color.sourceAlpha.has_value(),
                          "MRGB RGB maps by source vertex order; mask is NOT alpha");
                    ++seen;
                }
            });
        check(seen==4,"All four MRGB colours survive private partitioning");
    };
    verify(original);
    verify(readMeshOpenCandidate(put("different-mask.obj",xyz+
        "#MRGB ffff0000 0000ff00 ff0000ff FF000000\n"+faces)));
    verify(readMeshOpenCandidate(put("before-vertices.obj",payload+xyz+faces)));
    verify(readMeshOpenCandidate(put("after-faces.obj",xyz+faces+payload)));
    const auto checkpoint=dir/"converted.mshr";
    writeCheckpoint(checkpoint,original.snapshot);
    verify(readMeshOpenCandidate(checkpoint));

    // Test two ZBrush records: 64 packed words, then 4 packed words,
    // preserving sequence across disjoint closed tetrahedra.
    const auto word=[](unsigned mask,unsigned red,unsigned green,unsigned blue){
        const char h[]="0123456789abcdef";
        std::string result;
        for(unsigned channel:{mask,red,green,blue}){
            result.push_back(h[(channel>>4)&15]);
            result.push_back(h[channel&15]);
        }
        return result;
    };
    std::string multi;
    for(int k=0;k<17;++k){
        const int x=k*4;
        multi+="v "+std::to_string(x)+" 0 0\n";
        multi+="v "+std::to_string(x+1)+" 0 0\n";
        multi+="v "+std::to_string(x)+" 1 0\n";
        multi+="v "+std::to_string(x)+" 0 1\n";
    }
    multi+="#MRGB ";
    for(unsigned i=0;i<68;++i){
        if(i==64)multi+="\n#MRGB ";
        multi+=word((i*11)%256,(i*7+1)%256,
                    (i*3+2)%256,(i*5+3)%256);
    }
    multi+="\n";
    for(int k=0;k<17;++k){
        const int i=k*4+1;
        multi+="f "+std::to_string(i)+" "+std::to_string(i+2)+
               " "+std::to_string(i+1)+"\n";
        multi+="f "+std::to_string(i)+" "+std::to_string(i+1)+
               " "+std::to_string(i+3)+"\n";
        multi+="f "+std::to_string(i+1)+" "+std::to_string(i+2)+
               " "+std::to_string(i+3)+"\n";
        multi+="f "+std::to_string(i+2)+" "+std::to_string(i)+
               " "+std::to_string(i+3)+"\n";
    }
    const auto many=readMeshOpenCandidate(put("68-vertices.obj",multi));
    check(many.vertexCount==68&&many.faceCount==68&&many.hasVertexColors,
          "Multiple MRGB lines span the full vertex array");
    std::size_t observed=0;
    many.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part){
            for(const auto& v:part.vertices){
                const auto x=static_cast<unsigned>(v.position.x);
                const unsigned i=(x/4)*4+
                    (x%4==1?1u:v.position.y==1?2u:v.position.z==1?3u:0u);
                check(v.color.r==static_cast<float>((i*7+1)%256)/255.f&&
                      v.color.g==static_cast<float>((i*3+2)%256)/255.f&&
                      v.color.b==static_cast<float>((i*5+3)%256)/255.f&&
                      v.color.a==1.f,
                      "Packed MRGB blocks retain exact vertex order");
                ++observed;
            }
        });
    check(observed==68,"Every vertex of the multi-line MRGB fixture visited");

    // Bad inputs are refused while retaining an earlier immutable source.
    const auto root=original.snapshot.index;
    const auto refuse=[&](const std::string& name,const std::string& data,
                          const std::string& diagnostic){
        bool rejected=false;
        try {(void)readMeshOpenCandidate(put(name,data));}
        catch(const std::exception& e){
            rejected=std::string(e.what()).find(diagnostic)!=std::string::npos;
        }
        check(rejected,("MRGB refusal: "+name).c_str());
        check(original.snapshot.index==root,
              "Invalid MRGB leaves earlier private candidate unchanged");
    };
    refuse("missing.obj",xyz+"#MRGB 00ff0000\n"+faces,
           "MRGB colour count");
    refuse("surplus.obj",xyz+payload+"#MRGB 00000000\n"+faces,
           "MRGB colour count");
    refuse("empty.obj",xyz+"#MRGB\n"+faces,"no colour words");
    refuse("short.obj",xyz+"#MRGB 00ff000\n"+faces,"eight-hex-digit");
    refuse("bad-hex.obj",xyz+"#MRGB 0xff0000\n"+faces,"non-hexadecimal");
    refuse("split-word.obj",xyz+
           "#MRGB 00ff 0000 0000ff00 000000ff 00000000\n"+faces,
           "eight-hex-digit");
    refuse("bad-marker.obj",xyz+"#MRGB00ff0000\n"+faces,
           "malformed OBJ MRGB marker");
    refuse("inline-first.obj",
           "v 0 0 0 1 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"+payload+faces,
           "conflicts with inline");
    refuse("mrgb-first.obj",payload+
           "v 0 0 0 1 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\n"+faces,
           "conflicts with MRGB");
    refuse("unsupported-metadata.obj",xyz+"#vc 0 1 0\n"+faces,
           "metadata vertex-colour convention");
    std::string tooMany=xyz+"#MRGB ";
    for(int i=0;i<65;++i)tooMany+="00ff0000";
    refuse("65-on-one-line.obj",tooMany+"\n"+faces,"at most 64");
}

void testPlyImportCandidates() {
    namespace fs=std::filesystem;
    const auto dir=fs::temp_directory_path()/"mesher-ux06-ply-import-tests";
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() {std::error_code ignored;fs::remove_all(path,ignored);}
    } cleanup{dir};
    const auto put=[&](const std::string& name,const std::string& contents) {
        const auto path=dir/name;
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.write(contents.data(),static_cast<std::streamsize>(contents.size()));
        check(static_cast<bool>(file),"PLY test fixture write");
        return path;
    };
    const std::string geometry=
        "0 0 0 255 0 0 64\n"
        "1 0 0 0 255 0 128\n"
        "0 1 0 0 0 255 192\n"
        "0 0 1 255 255 0 255\n";
    const std::string faces="3 0 2 1\n3 0 1 3\n3 1 2 3\n3 2 0 3\n";
    const std::string prefix=
        "ply\nformat ascii 1.0\n"
        "comment A standard vertex-colour PLY fixture\n"
        "element vertex 4\n"
        "property float x\nproperty float y\nproperty float z\n"
        "property uchar red\nproperty uchar green\n"
        "property uchar blue\nproperty uchar alpha\n"
        "element face 4\n"
        "property list uchar int vertex_indices\n"
        "end_header\n";
    const auto path=put("unrelated.dat",prefix+geometry+faces);
    auto candidate=readMeshOpenCandidate(path);
    check(candidate.format==MeshOpenFormat::StanfordPly&&
          candidate.vertexCount==4&&candidate.faceCount==4&&
          candidate.hasVertexColors,
          "All Files detects ASCII PLY and preserves vertex RGBA");
    check(readMeshOpenCandidate(path,MeshOpenFilter::StanfordPly).format==
          MeshOpenFormat::StanfordPly,
          "Explicit PLY filter ignores the extension");
    check(candidate.alphaDecisionRequired,
          "Wide-range PLY alpha requires explicit interpretation");
    bool opaqueBeforeChoice=true;
    candidate.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& v:part.vertices) {
                opaqueBeforeChoice&=v.color.a==1.f;
                check(v.color.sourceAlpha.has_value(),
                      "PLY retains source alpha separately from editable RGBA");
            }
        });
    check(opaqueBeforeChoice,
          "PLY source alpha must not hide RGB before user interpretation");
    applyPlySourceAlphaAsOpacity(candidate);
    auto oldRoot=candidate.snapshot.index;
    std::array<bool,4> found{};
    candidate.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& vertex:part.vertices) {
                const auto alpha=vertex.color.a;
                check(vertex.color.sourceAlphaOriginallyVaried,
                      "Varied source-alpha provenance survives interpretation");
                check(vertex.color.sourceAlpha.has_value()&&
                      std::abs(*vertex.color.sourceAlpha-alpha)<1e-6f,
                      "Interpreted alpha retains exact independent provenance");
                for(std::size_t i=0;i<found.size();++i)
                    if(std::abs(alpha-static_cast<float>((i+1)*64-((i==3)?1:0))/255.f)<1e-5f)
                        found[i]=true;
            }
        });
    check(found[0]&&found[1]&&found[2]&&found[3],
          "All four PLY alpha values must survive candidate partitioning");
    const auto checkpoint=dir/"roundtrip.mshr";
    writeCheckpoint(checkpoint,candidate.snapshot);
    const auto restored=readMeshOpenCandidate(checkpoint);
    check(restored.hasVertexColors&&restored.vertexCount==4,
          "PLY candidate RGBA survives durable mshr round trip");
    std::array<bool,4> restoredAlpha{};
    restored.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& v:part.vertices) {
                check(v.color.sourceAlphaOriginallyVaried,
                      "V16 checkpoint preserves original variation provenance");
                check(v.color.sourceAlpha.has_value()&&
                      std::abs(*v.color.sourceAlpha-v.color.a)<1e-6f,
                      "V16 checkpoint must preserve both source and editable alpha");
                for(std::size_t i=0;i<restoredAlpha.size();++i)
                    if(std::abs(v.color.a-
                        static_cast<float>((i+1)*64-((i==3)?1:0))/255.f)<1e-5f)
                        restoredAlpha[i]=true;
            }
        });
    check(restoredAlpha[0]&&restoredAlpha[1]&&restoredAlpha[2]&&
          restoredAlpha[3],"All four source alphas survive .mshr reopen");
    const std::string allZeroGeometry=
        "0 0 0 35 56 37 0\n"
        "1 0 0 45 57 43 0\n"
        "0 1 0 36 61 40 0\n"
        "0 0 1 32 52 27 0\n";
    const auto zero=readMeshOpenCandidate(
        put("meshlab-zero-alpha.ply",prefix+allZeroGeometry+faces));
    check(!zero.alphaDecisionRequired,
          "Constant zero PLY source alpha must not prompt");
    zero.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& v:part.vertices)
                check(v.color.a==1.f&&v.color.sourceAlpha==0.f&&
                      !v.color.sourceAlphaOriginallyVaried,
                      "Zero-alpha VCGLIB colour remains visible and source preserved");
        });
    const std::string nearRangeGeometry=
        "0 0 0 255 0 0 0\n"
        "1 0 0 0 255 0 1\n"
        "0 1 0 0 0 255 2\n"
        "0 0 1 255 255 0 3\n";
    const auto near=readMeshOpenCandidate(
        put("near-alpha.ply",prefix+nearRangeGeometry+faces));
    check(!near.alphaDecisionRequired&&
          near.sourceAlphaMaximum-near.sourceAlphaMinimum<0.015f,
          "Sub-1.5% alpha range automatically presents opaque RGB");
    near.snapshot.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& v:part.vertices)
                check(v.color.a==1.f&&v.color.sourceAlphaOriginallyVaried,
                      "Narrow, nonconstant alpha still requires source-choice on export");
        });
    VertexRGBA first{.r=0.2f,.g=0.3f,.b=0.4f,.a=0.7f,
                     .sourceAlpha=0.25f,.sourceAlphaOriginallyVaried=true};
    VertexRGBA second{.r=0.8f,.g=0.7f,.b=0.6f,.a=0.9f,
                      .sourceAlpha=0.75f,.sourceAlphaOriginallyVaried=true};
    const auto midpoint=interpolateVertexColor(first,second,0.5);
    check(midpoint.sourceAlpha&&
          std::abs(*midpoint.sourceAlpha-0.5f)<1e-6f&&
          midpoint.sourceAlphaOriginallyVaried&&
          std::abs(midpoint.a-0.8f)<1e-6f,
          "Source scalar interpolates independently of editable mesh alpha");
    const std::string thresholdGeometry=
        "0 0 0 255 0 0 0\n"
        "1 0 0 0 255 0 1\n"
        "0 1 0 0 0 255 2\n"
        "0 0 1 255 255 0 4\n";
    const auto threshold=readMeshOpenCandidate(
        put("threshold-alpha.ply",prefix+thresholdGeometry+faces));
    check(threshold.alphaDecisionRequired,
          "4/255 PLY alpha span must prompt for opacity");

    const auto refuse=[&](const std::string& filename,
                          const std::string& content,
                          MeshOpenFilter filter,const char* why) {
        bool rejected=false;
        try {(void)readMeshOpenCandidate(put(filename,content),filter);}
        catch(const std::exception& error) {
            rejected=std::string(error.what()).find(why)!=std::string::npos;
        }
        check(rejected,"PLY must refuse unsupported/corrupted variant explicitly");
        check(candidate.snapshot.index==oldRoot,
              "Failed PLY candidate cannot mutate previous private source");
    };
    refuse("bad.ply","not ply\n",MeshOpenFilter::StanfordPly,
           "missing 'ply' magic");
    refuse("truncated.ply",prefix+geometry+"3 0 2 1\n",
           MeshOpenFilter::AllFiles,"truncated ASCII");
    refuse("quadrilateral.ply",prefix+geometry+"4 0 1 2 3\n"+faces,
           MeshOpenFilter::StanfordPly,"only triangle");
    refuse("invalid-index.ply",prefix+geometry+
           "3 0 2 9\n3 0 1 3\n3 1 2 3\n3 2 0 3\n",
           MeshOpenFilter::AllFiles,"index out of range");
    std::string sixteen=prefix;
    const auto p=sixteen.find("property uchar red");
    check(p!=std::string::npos,"Fixture has colour declaration");
    sixteen.replace(p,strlen("property uchar red"),"property ushort red");
    refuse("ambiguous-colour.ply",sixteen+geometry+faces,
           MeshOpenFilter::StanfordPly,"must be uchar/uint8");
    std::string incomplete=prefix;
    const auto b=incomplete.find("property uchar blue\n");
    incomplete.erase(b,strlen("property uchar blue\n"));
    refuse("incomplete-colour.ply",incomplete+geometry+faces,
           MeshOpenFilter::StanfordPly,"incomplete vertex RGB");
    std::string facet=prefix;
    facet.replace(facet.find("end_header\n"),strlen("end_header\n"),
                  "property uchar red\nend_header\n");
    refuse("facet-colour.ply",facet+geometry+faces,
           MeshOpenFilter::StanfordPly,"facet colour");
    std::string geometryNoColor=
        "0 0 0\n1 0 0\n0 1 0\n0 0 1\n";
    std::string noColor=prefix;
    const auto colorAt=noColor.find("property uchar red\n");
    const auto faceAt=noColor.find("element face 4");
    noColor.erase(colorAt,faceAt-colorAt);
    const auto plain=readMeshOpenCandidate(
        put("plain.ply",noColor+geometryNoColor+faces));
    check(plain.format==MeshOpenFormat::StanfordPly&&!plain.hasVertexColors,
          "Colourless PLY imports opaque default white");

    // Binary fixtures with explicit byte order ensure X86 and Apple
    // endian-independent reading. Standard uchar channels include alpha.
    auto appendWord=[](std::string& dest,std::uint32_t bits,bool little) {
        for(int i=0;i<4;++i) {
            const unsigned shift=static_cast<unsigned>(8*(little?i:3-i));
            dest.push_back(static_cast<char>((bits>>shift)&255u));
        }
    };
    for(const bool little:{true,false}) {
        const std::string binaryPrefix=
            "ply\nformat "+std::string(little?"binary_little_endian":
                "binary_big_endian")+" 1.0\n"
            "element vertex 4\nproperty float x\nproperty float y\n"
            "property float z\nproperty uchar red\nproperty uchar green\n"
            "property uchar blue\nproperty uchar alpha\n"
            "element face 4\nproperty list uchar int vertex_indices\nend_header\n";
        const std::array<std::array<float,3>,4> xyz{{
            {{0.f,0.f,0.f}},{{1.f,0.f,0.f}},
            {{0.f,1.f,0.f}},{{0.f,0.f,1.f}}}};
        const std::array<std::array<unsigned char,4>,4> rgba{{
            {{255,0,0,64}},{{0,255,0,128}},
            {{0,0,255,192}},{{255,255,0,255}}}};
        const std::array<std::array<std::uint32_t,3>,4> tris{{
            {{0,2,1}},{{0,1,3}},{{1,2,3}},{{2,0,3}}}};
        std::string binary=binaryPrefix;
        for(std::size_t i=0;i<4;++i) {
            for(float value:xyz[i])
                appendWord(binary,std::bit_cast<std::uint32_t>(value),little);
            for(unsigned char c:rgba[i])binary.push_back(static_cast<char>(c));
        }
        for(const auto& tri:tris) {
            binary.push_back(static_cast<char>(3));
            for(const auto idx:tri)appendWord(binary,idx,little);
        }
        const auto binaryPath=put(little?"little.stl":"big.obj",binary);
        const auto imported=readMeshOpenCandidate(binaryPath);
        check(imported.format==MeshOpenFormat::StanfordPly&&
              imported.vertexCount==4&&imported.faceCount==4&&
              imported.hasVertexColors,
              "All Files detects binary PLY by magic regardless of suffix");
        auto broken=binary;
        broken.resize(broken.size()-2);
        refuse(little?"little-truncated.ply":"big-truncated.ply",broken,
               MeshOpenFilter::AllFiles,"truncated binary payload");
    }
}

int main(){
    try {
        testPrivateMeshOpenCandidates();
        testZbrushMrgb();
        testPlyImportCandidates();
        for(auto n:{8u,10u,11u,12u,30u,100u,500u,5000u})
            for(bool symmetric:{false,true})test(n,symmetric,42);
        for(auto seed:{1ull,2ull,3ull,4ull}) {test(1000,true,seed);test(1000,false,seed);}
        // Backends must be explicit. The quadratic fallback is deliberately
        // refused at large N rather than allowing a minute-long startup.
        if(std::string(sphereHullBackend()).find("quadratic")!=std::string::npos) {
            SphereOptions large;
            large.vertexCount=100000;
            large.relaxationIterations=0;
            bool rejected=false;
            try{(void)generateSphere(large);}
            catch(const std::runtime_error& e) {
                rejected=std::string(e.what()).find("Qhull")!=std::string::npos;
            }
            check(rejected,"Large fallback hull must fail fast with install guidance");
        }
        SphereGenerationTimings stats{};
        SphereOptions timingOpt;
        timingOpt.vertexCount=500;
        (void)generateSphere(timingOpt,&stats);
        check(stats.totalSeconds>=stats.relaxationSeconds &&
              stats.totalSeconds>=stats.triangulationSeconds &&
              stats.totalSeconds>=stats.validationSeconds,
              "Generator timing breakdown is valid");
        SphereOptions reproducible;
        reproducible.vertexCount=500;
        const auto one=generateSphere(reproducible),two=generateSphere(reproducible);
        check(one.triangles.size()==two.triangles.size(),"Reproducible face count");
        for(std::size_t i=0;i<one.vertices.size();++i)
            check(length(one.vertices[i]-two.vertices[i])==0.0,"Deterministic vertices");
    }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}
}

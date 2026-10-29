#include "mesher/mesh.hpp"
#include "mesher/mesh_open_candidate.hpp"
#include "mesher/mesh_partitioning.hpp"
#include <cmath>
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
    reject("unsupported.obj",tetra+"#MRGB 0000ff00\n");
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
               "Unsupported checkpoint magic/version");
    refuseWith(put("bad_header.dat","garbage contents"),
               MeshOpenFilter::AllFiles,"Unrecognized mesh file header");
    check(candidate.snapshot.index==oldRoot,
          "All forced and automatically detected failures leave root intact");
    check(candidate.snapshot.index==oldRoot,
          "Successful import remains immutable across failed attempts");
}

int main(){
    try {
        testPrivateMeshOpenCandidates();
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

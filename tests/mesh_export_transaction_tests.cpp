#include "mesher/mesh_export_transaction.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/mesh_ply.hpp"

#include <chrono>
#include <cmath>
#include <ostream>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
namespace fs=std::filesystem;
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
mesher::Mesh tetra(bool alternate=false) {
    mesher::Mesh mesh;
    mesh.vertices={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    mesh.triangles={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    const auto red=alternate?0.f:1.f;
    const auto blue=alternate?1.f:0.f;
    mesh.colors.assign(4,mesher::VertexRGBA{
        red,0.f,blue,0.5f,0.75f,true});
    return mesh;
}
std::shared_ptr<const mesher::RegionTopologyIndex> indexed(
    const mesher::Mesh& mesh) {
    return mesher::RegionTopologyIndex::build(
        mesher::partitionFlatMesh(mesh).revision);
}
void writeBytes(const fs::path& path,const std::string& text) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out.exceptions(std::ios::failbit|std::ios::badbit);
    out<<text;
    out.close();
}
std::string bytes(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);
    if(!file)throw std::runtime_error("Cannot read test output");
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}
void assertNoTemps(const fs::path& folder) {
    for(const auto& entry:fs::directory_iterator(folder))
        require(entry.path().filename().string().find(".mesher-export-")==
                std::string::npos,
                "Failed export left temporary files behind");
}
void checkColour(const fs::path& path,float r,float b,float alpha) {
    const auto imported=mesher::parsePlyMesh(path);
    require(imported.vertices.size()==4&&imported.triangles.size()==4,
            "PLY export lost vertices/faces");
    for(const auto& c:imported.colors) {
        require(c.r==r&&c.b==b&&c.g==0,
                "Atomic export used wrong frozen vertex RGB");
        require(c.sourceAlpha&&
                std::abs(*c.sourceAlpha-alpha)<1.f/255.f+1e-6f,
                "Atomic export lost chosen alpha");
    }
}
struct TestDirectory {
    fs::path oldCwd=fs::current_path();
    fs::path root;
    TestDirectory() {
        root=fs::temp_directory_path()/
            ("mesher-ux07d-"+std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root/"one");
        fs::create_directories(root/"two");
    }
    ~TestDirectory() {
        std::error_code ec;
        fs::current_path(oldCwd,ec);
        fs::remove_all(root,ec);
    }
};
void testSnapshotAndOverwrite(TestDirectory& tmp) {
    const auto original=tetra();
    const auto first=indexed(original);
    auto request=mesher::captureMeshExportRequest(
        tmp.root/"one"/"snapshot.ply",first);
    const auto revisionBefore=first.get();
    const auto colorsBefore=mesher::materializeMeshExport(request).colors;
    writeBytes(request.absoluteDestination,"previous complete export");
    mesher::writePlyExportAtomic(
        request,{mesher::PlyExportEncoding::BinaryLittleEndian,
                 mesher::PlyExportColour::RgbaFromFile});
    checkColour(request.absoluteDestination,1,0,0.75f);
    require(first.get()==revisionBefore &&
            mesher::materializeMeshExport(request).colors==colorsBefore,
            "Export mutated source indexed revision or colours");
    assertNoTemps(tmp.root/"one");

    // Encoder can emit arbitrary prefixes before failing; none may become
    // visible at the final destination.
    const auto before=bytes(request.absoluteDestination);
    bool threw=false;
    try {
        mesher::writeMeshExportAtomic(
            request,[](std::ostream& out,const mesher::Mesh&) {
                out<<"PARTIAL EXPORT PREFIX";
                throw std::runtime_error("injected serializer failure");
            });
    }catch(const std::runtime_error& e) {
        threw=std::string(e.what())=="injected serializer failure";
    }
    require(threw,"Injected serializer failure did not propagate");
    require(bytes(request.absoluteDestination)==before,
            "Failed serializer damaged existing destination bytes");
    assertNoTemps(tmp.root/"one");

    // A destination that is an existing directory must not be removed or
    // replaced, even when its staging file was written successfully.
    threw=false;
    const auto directoryDestination=mesher::captureMeshExportRequest(
        tmp.root/"two",first);
    try {
        mesher::writePlyExportAtomic(directoryDestination);
    }catch(const fs::filesystem_error&) {threw=true;}
    require(threw&&fs::is_directory(tmp.root/"two"),
            "Failed rename did not preserve target directory");
    assertNoTemps(tmp.root);
}

void testCwdAndAsync(TestDirectory& tmp) {
    namespace chrono=std::chrono;
    fs::current_path(tmp.root/"one");
    auto first=indexed(tetra());
    const auto request=mesher::captureMeshExportRequest(
        "../two/accepted.ply",first);
    const auto absoluteBefore=request.absoluteDestination;
    const auto initialLive=mesher::partitionFlatMesh(tetra()).revision;
    mesher::MeshRepository live(initialLive);
    std::mutex mutex;
    std::condition_variable cv;
    bool entered=false,allow=false;
    mesher::AsyncMeshExportWriter writer;
    writer.request(request,[&](std::ostream& out,const mesher::Mesh& mesh) {
        {
            std::lock_guard lock(mutex);
            entered=true;
        }
        cv.notify_all();
        {
            std::unique_lock lock(mutex);
            cv.wait(lock,[&] {return allow;});
        }
        mesher::writePlyMesh(
            out,mesh,{mesher::PlyExportEncoding::Ascii,
                      mesher::PlyExportColour::RgbaFromMesh});
    });
    {
        std::unique_lock lock(mutex);
        const bool started=cv.wait_for(lock,chrono::seconds(10),
            [&] {return entered;});
        if(!started) {
            allow=true;
            lock.unlock();
            cv.notify_all();
            throw std::runtime_error("Async exporter did not enter callback");
        }
    }
    // Both the current revision and CWD are changed while the saved
    // encoder is blocked; only the captured original may be exported.
    live.publish(mesher::partitionFlatMesh(tetra(true)).revision);
    fs::current_path(tmp.root);
    {
        std::lock_guard lock(mutex);
        allow=true;
    }
    cv.notify_all();
    writer.drain();
    const auto stats=writer.statistics();
    require(stats.requested==1&&stats.completed==1&&stats.failed==0&&
            stats.pending==0&&!stats.busy,
            "Async explicit export did not finish successfully");
    require(request.absoluteDestination==absoluteBefore,
            "CWD change redirected captured export destination");
    checkColour(absoluteBefore,1,0,0.5f);
    require(mesher::materializeMeshExport(request).colors.front().r==1.f,
            "Captured immutable root changed with live publication");
    require(live.snapshot()!=initialLive,
            "Live publication was unexpectedly rolled back by exporter");
    assertNoTemps(tmp.root/"two");

    // Background writer retains each accepted request; two writes to the
    // same path complete FIFO with no half-file visible at the destination.
    writer.request(request,[](std::ostream& out,const mesher::Mesh&) {
        out<<"FIRST";
    });
    writer.request(request,[](std::ostream& out,const mesher::Mesh&) {
        out<<"SECOND";
    });
    writer.drain();
    const auto after=writer.statistics();
    require(after.requested==3&&after.completed==3&&
            bytes(absoluteBefore)=="SECOND",
            "Async exporter dropped, merged or reordered explicit requests");
    // The UI must be able to attribute every completion to its exact
    // explicit request, even when three FIFO writes finish between frames.
    const auto outcomes=writer.takeCompletions();
    require(outcomes.size()==3,
            "Writer did not preserve per-request completion identity");
    for(const auto& outcome:outcomes)
        require(outcome.success&&outcome.error.empty()&&
                outcome.destination==absoluteBefore,
                "Successful FIFO completion reported the wrong destination");
    require(writer.takeCompletions().empty(),
            "Consumption did not clear completed outcome queue");
    assertNoTemps(tmp.root/"two");
}

void testFailures(TestDirectory& tmp) {
    const auto root=indexed(tetra());
    auto request=mesher::captureMeshExportRequest(tmp.root/"one"/"original",root);
    bool refused=false;
    try {
        mesher::writeMeshExportAtomic(request,{});
    }catch(const std::invalid_argument&) {refused=true;}
    require(refused,"Absent encoder did not fail before output");
    refused=false;
    try {
        (void)mesher::captureMeshExportRequest({},root);
    }catch(const std::invalid_argument&) {refused=true;}
    require(refused,"Empty destination was accepted");
    refused=false;
    try {
        (void)mesher::captureMeshExportRequest(tmp.root/"bad.ply",nullptr);
    }catch(const std::invalid_argument&) {refused=true;}
    require(refused,"Null snapshot was accepted");
    refused=false;
    try {
        mesher::writePlyExportAtomic(
            mesher::captureMeshExportRequest(
                tmp.root/"nonexistent"/"mesh.ply",root));
    }catch(const fs::filesystem_error&) {refused=true;}
    catch(const std::system_error&) {refused=true;}
    require(refused,"Absent export parent directory unexpectedly worked");
    // A failed writer task leaves a previously good output intact and
    // reports its failure rather than claiming completion.
    writeBytes(request.absoluteDestination,"KEPT");
    mesher::AsyncMeshExportWriter writer;
    writer.request(request,[](std::ostream& out,const mesher::Mesh&) {
        out<<"BROKEN";
        throw std::runtime_error("deliberate background write failure");
    });
    writer.drain();
    const auto stats=writer.statistics();
    require(stats.requested==1&&stats.failed==1&&stats.completed==0&&
            stats.lastError=="deliberate background write failure",
            "Background export failure accounting is incorrect");
    const auto outcomes=writer.takeCompletions();
    require(outcomes.size()==1&&!outcomes[0].success&&
            outcomes[0].destination==request.absoluteDestination&&
            outcomes[0].error=="deliberate background write failure",
            "Failed write did not report correct per-request outcome");
    require(writer.takeCompletions().empty(),
            "Failed completion was reported more than once");
    require(bytes(request.absoluteDestination)=="KEPT",
            "Failed background write damaged old export");
    assertNoTemps(tmp.root/"one");

    // Exercise buffered output beyond its 64 KiB buffer without collecting
    // the complete export in application memory.
    const auto large=mesher::captureMeshExportRequest(
        tmp.root/"one"/"large.bin",root);
    mesher::writeMeshExportAtomic(large,[](std::ostream& out,const mesher::Mesh&) {
        const std::string block(8192,'X');
        for(unsigned i=0;i<256;++i)out.write(block.data(),block.size());
    });
    require(fs::file_size(large.absoluteDestination)==2097152,
            "Multi-megabyte streamed export has wrong byte count");
    assertNoTemps(tmp.root/"one");
}
} // namespace

int main() {
    try {
        TestDirectory temp;
        testSnapshotAndOverwrite(temp);
        testCwdAndAsync(temp);
        testFailures(temp);
        std::cout<<"UX07D atomic immutable export snapshot and FIFO tests passed\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"UX07D failure: "<<e.what()<<'\n';
        return 1;
    }
}

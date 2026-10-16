#include "mesher/async_sculpt.hpp"

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace mesher;
namespace {
void require(bool pass,const char* label) {
    if(!pass)throw std::runtime_error(label);
}

void testThreadedMatchesExactSculpt() {
    SphereOptions options;
    options.vertexCount=800;
    options.relaxationIterations=12;
    auto initial=generateSphere(options);
    auto expected=initial;
    const Vec3 start{-.2,-.60,.75};
    const Vec3 a{-.24,-.65,.69};
    const Vec3 b{-.31,-.74,.59};
    constexpr double step=.02, strength=.012, speed=.2, radius=.1;
    CurvatureSettings curvature{true,.25};
    CollapseSettings collapse{};
    RefineSettings refine{};
    // Exact synchronous 4D input path is the reference output.
    AdaptiveInflateBrush brush(expected,strength,curvature,collapse,refine,true);
    BrushStroke direct(expected,brush,step);
    direct.startTimed({{start,radius,.3},{{-start.x,start.y,start.z},radius,.3}},
                      speed);
    auto mirrored=[](Vec3 p) {
        return std::vector<Vec3>{p,{-p.x,p.y,p.z}};
    };
    direct.updateTimed(mirrored(a),.12);
    direct.finishTimed(mirrored(b),.24);

    AsyncSculptWorker worker(initial,step,strength,speed,curvature,
                             collapse,refine,true);
    worker.begin(start,radius);
    require(!worker.canBegin(),"First stroke must be reserved immediately");
    worker.update(a,.12);
    worker.end(b,.24);
    // Deliberately do not consume any snapshots while processing: stamps
    // must advance independently of the render thread's frame rate.
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(10);
    while(!worker.canBegin() && std::chrono::steady_clock::now()<deadline) {
        (void)worker.status();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(worker.canBegin(),"Worker must drain a completed stroke");
    const auto status=worker.status();
    require(status.completedStamps==brush.completedStamps(),
            "Worker must execute every exact 4D stamp");
    require(status.pendingSteps==0 && status.pendingSegments==0,
            "Drained worker has no remaining stroke segments");
    auto snapshot=worker.takeSnapshot();
    require(bool(snapshot),"Last completed mesh must be published");
    require(snapshot->completedStamps==status.completedStamps,
            "Published snapshot must contain all completed stamps");
    require(snapshot->index&&snapshot->originalOrder,
            "Worker publishes immutable root/index, not copied Mesh");
    require(!snapshot->dirtyPartitions.empty(),
            "Coalesced stamps include dirty partitions since last display");
    ViewportFlattenStatistics timing{};
    const auto flat=flattenRevisionForViewport(
        *snapshot->index,*snapshot->originalOrder,&timing);
    require(timing.vertices==flat.vertices.size() &&
            timing.triangles==flat.triangles.size() &&
            timing.bytes>0&&timing.milliseconds>=0,
            "Full viewport-only flatten conversion is explicitly accounted");
    require(flat.vertices.size()==expected.vertices.size() &&
            flat.triangles.size()==expected.triangles.size(),
            "Async snapshot retains exact mesh topology");
    for(std::size_t i=0;i<expected.vertices.size();++i)
        require(length(expected.vertices[i]-flat.vertices[i])<1e-8,
                "Async versioned and synchronous brush vertex geometry agree");
    for(std::size_t i=0;i<expected.triangles.size();++i) {
        const auto x=expected.triangles[i],y=flat.triangles[i];
        require(x.a==y.a&&x.b==y.b&&x.c==y.c,
                "Worker and synchronous brush oriented faces agree");
    }
    require(status.details.preStampCopiedVertices==0,
            "Versioned worker never copies full pre-stamp positions");
    require(status.publishedRevisions>0 &&
            status.publishedRevisions<=status.completedStamps,
            "Each completed stamp publishes at most one validated root");
    require(status.modifiedPartitions>0 &&
            status.copiedDirectoryNodes>=status.modifiedPartitions,
            "Worker records bounded partition-path copy accounting");
    require(!worker.takeSnapshot().has_value(),
            "Consuming the final snapshot leaves no stale update");
}

void testWorkerCanRestartAndCancel() {
    auto original=generateSphere(SphereOptions{500,true,2,6});
    AsyncSculptWorker worker(original,.05,.01,.2,{},{},{},true);
    const Vec3 position{-.2,-.6,.75};
    worker.begin(position,.1);
    worker.end(position,0);
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(10);
    while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(worker.canBegin(),"First stroke can finish without any movement");
    const auto first=worker.status().completedStamps;
    worker.begin(position,.1);
    worker.update(position,.25);
    worker.end(position,.25);
    while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(worker.canBegin(),"Second stroke terminates without blocking UI");
    require(worker.status().completedStamps>first,
            "Subsequent strokes and stationary time are processed");
    // A worker destroyed while input is queued must join safely rather
    // than keeping a GLFW window or Vulkan device alive.
    auto dropping=std::make_unique<AsyncSculptWorker>(
        original,.005,.01,.2,CurvatureSettings{},CollapseSettings{},
        RefineSettings{},false);
    dropping->begin(position,.1);
    dropping->update({.3,-.7,.6},.1);
    dropping->end({.3,-.7,.6},.1);
    dropping.reset();
}
void testCoalescedRootsAndCompatibilityBridge() {
    SphereOptions options;
    options.vertexCount=500;
    options.relaxationIterations=6;
    auto initial=generateSphere(options);
    AsyncSculptWorker worker(initial,.015,.006,.2,{true,.25},{},{},true);
    const Vec3 first{-.25,-.65,.70};
    worker.begin(first,.35);
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(worker.status().completedStamps==0 &&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(worker.status().completedStamps==1,
            "Initial mouse-down produces exactly one Step stamp");
    auto held=worker.takeSnapshot();
    require(bool(held) && bool(held->index),
            "One-stamp immutable snapshot is available without mesh copying");
    require(!held->dirtyPartitions.empty(),
            "Mouse-down's synchronous first stamp reports dirty partitions");
    const auto preserved=flattenRevisionForViewport(
        *held->index,*held->originalOrder);
    worker.update({-.30,-.69,.65},.15);
    worker.end({-.35,-.72,.62},.30);
    deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(!worker.canBegin() &&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(worker.canBegin(),"All queued 4D Step stamps must drain");
    const auto status=worker.status();
    auto latest=worker.takeSnapshot();
    require(latest && latest->index!=held->index,
            "Worker publishes new roots while reader retains earlier root");
    require(latest->completedStamps==status.completedStamps &&
            latest->completedStamps>held->completedStamps,
            "Coalesced UI snapshot contains every processed stamp");
    require(status.publishedRevisions>1 &&
            status.pendingSteps==0 && status.inputCommands==0,
            "No Step is dropped when the viewport consumes no intermediate frames");
    require(!worker.takeSnapshot(),
            "Latest snapshot exchange leaves no obsolete frame pending");
    auto oldAgain=flattenRevisionForViewport(
        *held->index,*held->originalOrder);
    require(oldAgain.vertices.size()==preserved.vertices.size() &&
            oldAgain.triangles.size()==preserved.triangles.size(),
            "Older reader holds complete immutable mesh topology");
    for(std::size_t i=0;i<preserved.vertices.size();++i)
        require(length(oldAgain.vertices[i]-preserved.vertices[i])==0,
                "Old version is never mutated by subsequent worker stamps");
    ViewportFlattenStatistics measured;
    const auto final=flattenRevisionForViewport(
        *latest->index,*latest->originalOrder,&measured);
    require(validateClosedSphereTopology(final),
            "Coalesced final revision has valid closed manifold topology");
    require(measured.vertices==final.vertices.size() &&
            measured.triangles==final.triangles.size(),
            "Viewport bridge reports real converted element counts");
}
void testViewportAdapterAfterStableIdSplit() {
    Mesh original;
    original.vertices={{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
                       {0,0,1},{0,0,-1}};
    original.triangles={{4,0,1},{4,1,2},{4,2,3},{4,3,0},
                        {5,1,0},{5,2,1},{5,3,2},{5,0,3}};
    const auto imported=partitionFlatMesh(original,1);
    const auto initial=RegionTopologyIndex::build(imported.revision);
    const auto id=imported.flatIndex.vertexIds[0];
    RegionEdit move(initial,{id},{},1);
    move.moveVertex(id,{1.25,0,0});
    (void)move.commit();
    const auto deformed=move.updatedIndex();
    LocalStampBaseline baseline{initial,{id},
                                initial->vertex(id).incidentFaces};
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=1;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refiner(6,settings,{},1);
    const auto split=refiner.apply(deformed,baseline);
    require(split.statistics.splitPairs==1,
            "Adapter fixture must actually grow topology");
    ViewportFlattenStatistics stats{};
    const auto output=flattenRevisionForViewport(
        *split.index,imported.flatIndex,&stats);
    require(output.vertices.size()==7&&output.triangles.size()==10,
            "Viewport adapter includes inserted stable vertex and both new faces");
    require(length(output.vertices[0]-Vec3{1.25,0,0})==0,
            "Surviving original vertices preserve original flat array order");
    require(length(output.vertices.back()-
                   split.index->vertex(split.insertedVertices[0]).position)==0,
            "New stable-ID vertex is appended deterministically");
    require(validateClosedSphereTopology(output),
            "Converted split revision has closed oriented topology");
    require(stats.vertices==7&&stats.triangles==10,
            "Flatten diagnostics reflect dynamically changed topology");
}
void testVersionedWorkerWithRemeshingEnabled() {
    SphereOptions options;
    options.vertexCount=500;
    options.relaxationIterations=6;
    const auto original=generateSphere(options);
    RefineSettings refine;
    refine.enabled=true;
    refine.maximumEdgeRatio=.85;
    refine.maximumSplitPairsPerStamp=2;
    refine.maximumFlipPairsPerStamp=4;
    CollapseSettings collapse;
    collapse.enabled=true;
    collapse.minimumEdgeRatio=.85;
    collapse.maximumPairsPerStamp=2;
    AsyncSculptWorker worker(original,.025,.008,.2,{true,.25},
                             collapse,refine,false);
    const Vec3 point{.25,-.65,.70};
    worker.begin(point,.35);
    worker.update({.27,-.66,.68},.125);
    worker.end({.28,-.67,.67},.25);
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
    while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline) {
        (void)worker.status();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(worker.canBegin(),"Refinement/collapse worker must drain");
    const auto status=worker.status();
    require(status.completedStamps>1 &&
            status.details.preStampCopiedVertices==0,
            "Remeshing mode never creates a full pre-stamp positions copy");
    auto snapshot=worker.takeSnapshot();
    require(bool(snapshot)&&snapshot->index!=nullptr,
            "Pipeline publishes one final indexed revision");
    const auto flat=flattenRevisionForViewport(
        *snapshot->index,*snapshot->originalOrder);
    require(validateClosedSphereTopology(flat),
            "Complete remesh pipeline retains closed oriented mesh");
    require(flat.vertices.size()==status.details.vertices &&
            flat.triangles.size()==status.details.triangles,
            "Versioned mesh element telemetry remains accurate after remeshing");
}
}
int main() {
    try {
        testThreadedMatchesExactSculpt();
        testWorkerCanRestartAndCancel();
        testCoalescedRootsAndCompatibilityBridge();
        testViewportAdapterAfterStableIdSplit();
        testVersionedWorkerWithRemeshingEnabled();
        std::cout<<"Asynchronous sculpt worker tests passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Async worker test failed: "<<e.what()<<'\n';
        return 1;
    }
}

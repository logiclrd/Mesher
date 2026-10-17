#include "mesher/async_sculpt.hpp"
#include "mesher/partition_render.hpp"

#include <chrono>
#include <filesystem>
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
    PartitionRenderCache renderer;
    renderer.initialize(worker.initialRevision());
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
    renderer.advance(held->index,held->dirtyPartitions);
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
    renderer.advance(latest->index,latest->dirtyPartitions);
    PartitionRenderCache fullRebuild;
    fullRebuild.initialize(latest->index);
    require(renderer.partitions().size()==fullRebuild.partitions().size(),
            "Coalesced worker deltas leave no stale/missing render partitions");
    auto identical=[](const std::vector<PartitionRenderVertex>& a,
                      const std::vector<PartitionRenderVertex>& b) {
        if(a.size()!=b.size())return false;
        for(std::size_t i=0;i<a.size();++i) {
            const auto& x=a[i],&y=b[i];
            if(x.px!=y.px||x.py!=y.py||x.pz!=y.pz||
               x.nx!=y.nx||x.ny!=y.ny||x.nz!=y.nz||
               x.material!=y.material)return false;
        }
        return true;
    };
    for(const auto& [id,partition]:fullRebuild.partitions()) {
        const auto found=renderer.partitions().find(id);
        require(found!=renderer.partitions().end() &&
                identical(found->second->solid,partition->solid) &&
                identical(found->second->wire,partition->wire),
                "Async coalesced dirty union matches full-revision rendering");
    }
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
void testWorkerHistoryAndRecoveredCheckpoint() {
    auto initial=generateSphere(SphereOptions{500,true,7,6});
    const auto file=std::filesystem::temp_directory_path()/
        ("mesher-worker-v12-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())+".mshr");
    try {
        std::shared_ptr<const RegionTopologyIndex> published;
        std::shared_ptr<const RegionTopologyIndex> original;
        {
            AsyncSculptWorker worker(initial,.025,.006,.2,{true,.25},{},{},true);
            original=worker.initialRevision();
            const Vec3 point{-.2,-.65,.72};
            worker.begin(point,.3);
            worker.end(point,0);
            const auto limit=std::chrono::steady_clock::now()+
                             std::chrono::seconds(12);
            while(!worker.canBegin()&&std::chrono::steady_clock::now()<limit)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            require(worker.canBegin(),"One-stamp worker completes before undo");
            auto saved=worker.takeSnapshot();
            require(saved&&saved->index&&saved->index!=original,
                    "Changed stamp publishes a distinct current root");
            published=saved->index;
            worker.undo();
            auto undoLimit=std::chrono::steady_clock::now()+
                           std::chrono::seconds(12);
            while(worker.status().history.redoDepth==0&&
                  std::chrono::steady_clock::now()<undoLimit)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto undone=worker.takeSnapshot();
            require(undone&&undone->index==original,
                    "Ctrl+Z worker command publishes exact old immutable root");
            worker.redo();
            auto redoLimit=std::chrono::steady_clock::now()+
                           std::chrono::seconds(12);
            while(worker.status().history.redoDepth!=0&&
                  std::chrono::steady_clock::now()<redoLimit)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto restored=worker.takeSnapshot();
            require(restored&&restored->index==published,
                    "Ctrl+Y reuses original persistent root without rebuilding");
            require(!restored->dirtyPartitions.empty(),
                    "Undo/redo publish existing partition deltas to GPU mailbox");
            worker.saveCheckpoint(file);
            auto fileLimit=std::chrono::steady_clock::now()+
                           std::chrono::seconds(12);
            while(worker.checkpointStatistics().completed==0&&
                  std::chrono::steady_clock::now()<fileLimit)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            require(worker.checkpointStatistics().completed>0,
                    "Asynchronous Ctrl+S checkpoint writes without blocking stamps");
        }
        auto recovered=readCheckpoint(file);
        require(recovered.index->revision().partitionCount()==
                published->revision().partitionCount()&&
                !recovered.mirrorPairs.empty(),
                "Reload preserves partitioned geometry and symmetry pairs");
        AsyncSculptWorker resumed(std::move(recovered),.025,.006,.2,
                                  {true,.25},{},{});
        resumed.begin({-.25,-.65,.70},.3);
        resumed.end({-.25,-.65,.70},0);
        const auto limit=std::chrono::steady_clock::now()+
                         std::chrono::seconds(12);
        while(!resumed.canBegin()&&std::chrono::steady_clock::now()<limit)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(resumed.canBegin()&&resumed.status().completedStamps>=1,
                "Restored checkpoint resumes interactive worker with exact Step");
        auto next=resumed.takeSnapshot();
        require(next&&next->index,
                "Recovered worker publishes new immutable mesh revisions");
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(file,ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(file,ec);
}
void testRejectedSymmetricStepRollsBackMirrorsAndHistory() {
    SphereOptions options;
    options.vertexCount=500;
    options.relaxationIterations=6;
    const auto initial=generateSphere(options);
    RefineSettings refine;
    refine.enabled=true;
    refine.maximumEdgeRatio=.25;
    refine.maximumSplitPairsPerStamp=2;
    refine.optimizeFlips=false;
    std::size_t validated=0;
    std::size_t candidateVertices=0;
    VersionedSculptBrush brush(initial,.065,{}, {},refine,true,256,
        [&](const ProposedSculptStep& proposed)->std::string {
            ++validated;
            require(proposed.before==brush.snapshot(),
                    "Validator sees exactly the original published Step root");
            require(proposed.candidate!=proposed.before&&
                    !proposed.changedVertices.empty()&&
                    !proposed.changedFaces.empty(),
                    "Validator observes complete private candidate and dirty geometry");
            candidateVertices=0;
            proposed.candidate->revision().forEachPartition(
                [&](PartitionId,const MeshPartition& p) {
                    candidateVertices+=p.vertices.size();
                });
            return validated==1?"Intentional V17c symmetric Step refusal":"";
        });
    Mesh dummy;
    const std::array<BrushArea,2> areas{{
        {{-.25,-.65,.7},.55,.3},{{.25,-.65,.7},.55,.3}
    }};
    const auto old=brush.snapshot();
    const auto originalMirrors=brush.checkpoint().mirrorPairs;
    const auto originalHistory=brush.historyStatistics();
    brush.stamp(dummy,areas);
    require(validated==1&&brush.rejectedStamps()==1&&
            brush.completedStamps()==1&&brush.publishedRevisions()==0,
            "Rejected Step is accounted without publishing a new revision");
    require(brush.snapshot()==old&&brush.lastDirtyPartitions().empty()&&
            brush.historyStatistics().undoDepth==originalHistory.undoDepth&&
            brush.checkpoint().mirrorPairs==originalMirrors,
            "Rejected symmetric Step restores root, render, mirror and history state");
    require(brush.lastRejection()=="Intentional V17c symmetric Step refusal",
            "Rejection reason remains available for diagnostics");
    require(candidateVertices>initial.vertices.size(),
            "Rejected private Step exercised actual symmetric vertex splitting");
    brush.stamp(dummy,areas);
    require(validated==2&&brush.rejectedStamps()==1&&
            brush.completedStamps()==2&&brush.publishedRevisions()==1&&
            brush.snapshot()!=old,
            "Next Step successfully remeshes after rollback");
    const auto accepted=brush.snapshot();
    const auto acceptedMirrors=brush.checkpoint().mirrorPairs;
    require(acceptedMirrors.size()>originalMirrors.size()&&
            brush.historyStatistics().undoDepth==1,
            "Accepted symmetric split adds mirror pairs and exactly one undo entry");
    require(brush.undo()&&brush.snapshot()==old&&
            brush.checkpoint().mirrorPairs==originalMirrors,
            "Undo after rejection returns to exact original root and mirror map");
    require(brush.redo()&&brush.snapshot()==accepted&&
            brush.checkpoint().mirrorPairs==acceptedMirrors,
            "Redo reinstates accepted Step without resurrecting rejected geometry");
}
void testWorkerRejectsOneStepAndDrainsRemainingInput() {
    SphereOptions options;
    options.vertexCount=500;
    options.relaxationIterations=6;
    auto initial=generateSphere(options);
    std::size_t checked=0; // callback only executes on the worker thread
    AsyncSculptWorker worker(initial,.02,.015,.2,{}, {},{},false,
        [&](const ProposedSculptStep& proposed)->std::string {
            ++checked;
            require(proposed.candidate!=proposed.before,
                    "Worker validates unpublished candidate before final CAS");
            if(checked==1)
                return "Expected worker refusal (no geometry publication)";
            if(checked==2)
                throw std::length_error("Expected bounded geometry refusal");
            return {};
        });
    const auto firstRoot=worker.initialRevision();
    worker.begin({-.25,-.65,.7},.35);
    const auto firstLimit=std::chrono::steady_clock::now()+
                          std::chrono::seconds(12);
    while(worker.status().completedStamps==0&&
          std::chrono::steady_clock::now()<firstLimit)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto first=worker.status();
    require(first.completedStamps==1&&first.rejectedStamps==1&&
            first.publishedRevisions==0&&
            first.history.undoDepth==0&&
            worker.initialRevision()==firstRoot,
            "Worker consumes rejected mouse-down Step without publishing or stopping");
    auto notification=worker.takeSnapshot();
    require(notification&&notification->index==firstRoot&&
            notification->dirtyPartitions.empty(),
            "Rejected input may update status but emits no GPU dirty partitions");
    worker.update({-.30,-.67,.67},.2);
    worker.end({-.32,-.68,.66},.32);
    const auto limit=std::chrono::steady_clock::now()+
                     std::chrono::seconds(15);
    while(!worker.canBegin()&&std::chrono::steady_clock::now()<limit)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto final=worker.status();
    require(worker.canBegin()&&final.pendingSteps==0&&
            final.pendingSegments==0&&final.inputCommands==0,
            "Worker drains the full exact 4D path after recoverable refusal");
    require(final.rejectedStamps==2&&final.completedStamps>2&&
            final.publishedRevisions>0&&
            final.history.undoDepth==final.publishedRevisions,
            "Counted refused Steps add neither history entries nor revisions");
    const auto last=worker.takeSnapshot();
    require(last&&last->index&&last->index!=firstRoot,
            "After two refusals later Step still publishes a valid indexed revision");
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
        testWorkerHistoryAndRecoveredCheckpoint();
        testRejectedSymmetricStepRollsBackMirrorsAndHistory();
        testWorkerRejectsOneStepAndDrainsRemainingInput();
        testVersionedWorkerWithRemeshingEnabled();
        std::cout<<"Asynchronous sculpt worker tests passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Async worker test failed: "<<e.what()<<'\n';
        return 1;
    }
}

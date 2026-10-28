#include "mesher/async_sculpt.hpp"
#include "mesher/desktop_merge_input.hpp"
#include "mesher/desktop_merge_fixture.hpp"
#include "mesher/partition_render.hpp"

#include <chrono>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
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

void testOneShotStampIsExactlyOneVisibleWorkerRevision() {
    // A timed LMB press/release can enqueue several 4D samples. A
    // diagnostic one-shot must instead be exactly one worker stamp, with
    // no queued stroke samples and an inspectable root at each step.
    auto sphere=generateSphere(SphereOptions{500,true,2,6});
    AsyncSculptWorker worker(sphere,.05,.01,.2,{},{},{},true);
    const Vec3 center{-.2,-.6,.75};
    const auto wait=[&] {
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(10);
        while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(worker.canBegin(),
                "One-shot worker must publish and release reservation");
    };
    require(worker.canBegin(),"Fresh one-shot worker must accept input");
    worker.stampOnce(center,.1);
    require(!worker.canBegin(),
            "One-shot must reserve the worker until its snapshot publishes");
    wait();
    const auto first=worker.status();
    require(first.completedStamps==1&&first.pendingSteps==0&&
            first.pendingSegments==0&&!first.active&&
            first.inputCommands==0,
            "One-shot must complete ONE stamp without a timed stroke or residual input");
    const auto frame1=worker.takeSnapshot();
    require(frame1&&frame1->completedStamps==1,
            "First one-shot must expose its exact immutable stamp-1 root");
    require(!worker.takeSnapshot(),
            "One-shot snapshot cannot conceal a queued second stamp");
    worker.stampOnce(center,.1);
    wait();
    const auto second=worker.status();
    require(second.completedStamps==2&&second.pendingSteps==0&&
            second.pendingSegments==0&&!second.active,
            "Second deliberate one-shot must complete exactly one additional stamp");
    const auto frame2=worker.takeSnapshot();
    require(frame2&&frame2->completedStamps==2,
            "Second one-shot must expose its exact stamp-2 root without coalescing");
    require(!worker.takeSnapshot(),
            "Second one-shot must not queue an automatic third stamp");
    // Intentionally rejected first one-shot stays private and does not
    // silently enqueue a second step; a later manually requested stamp
    // can still succeed on the original root.
    auto fresh=generateSphere(SphereOptions{500,true,2,6});
    std::size_t validations=0;
    AsyncSculptWorker guarded(fresh,.05,.01,.2,{},{},{},true,
        [&](const ProposedSculptStep& candidate)->std::string {
            require(candidate.candidate!=candidate.before,
                    "One-shot validator sees unpublished candidate");
            return ++validations==1?"Deliberate one-shot geometry refusal":"";
        });
    const auto baseline=guarded.initialRevision();
    const auto finishGuarded=[&] {
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(10);
        while(!guarded.canBegin()&&std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(guarded.canBegin(),"Rejected or accepted one-shot releases worker");
    };
    guarded.stampOnce(center,.2);
    finishGuarded();
    const auto refused=guarded.status();
    const auto refusedFrame=guarded.takeSnapshot();
    require(refused.completedStamps==1&&refused.rejectedStamps==1&&
            refused.publishedRevisions==0&&
            refused.lastRejection=="Deliberate one-shot geometry refusal"&&
            refusedFrame&&refusedFrame->index==baseline,
            "Refused F8 step must retain original published source and exact refusal");
    guarded.stampOnce(center,.2);
    finishGuarded();
    const auto accepted=guarded.status();
    const auto acceptedFrame=guarded.takeSnapshot();
    require(accepted.completedStamps==2&&accepted.rejectedStamps==1&&
            accepted.publishedRevisions==1&&acceptedFrame&&
            acceptedFrame->index!=baseline&&validations==2,
            "Explicit second one-shot succeeds without replaying rejected step");
    std::cout<<"[sculpt-step] worker exact one-shot publication and input isolation PASS\n";
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
void testReversibleMirrorDeltaJournals() {
    const std::vector<std::pair<VertexId,VertexId>> base{{1,2},{3,3}};
    VersionedLocalRefiner refiner(6,{},base,1);
    VersionedDirectionalCollapser collapser(6,{},base,1);
    const auto expectedRefiner=refiner.mirrors();
    const auto expectedCollapse=collapser.mirrors();
    refiner.beginStep();
    collapser.beginStep();
    // One Step can add a new midpoint pair then retire an old pair.
    refiner.addMirrorPair(4,5);
    collapser.addMirrorPair(4,5);
    refiner.removeMirrorPair(1,2);
    collapser.removeMirrorPair(1,2);
    require(refiner.mirrors().size()==3&&collapser.mirrors().size()==3,
            "Both journals stage only the changed symmetric correspondences");
    collapser.rollbackStep();
    refiner.rollbackStep();
    require(refiner.mirrors()==expectedRefiner&&
            collapser.mirrors()==expectedCollapse,
            "Rollback reverses mirror retirement and addition in reverse order");
    refiner.beginStep();
    collapser.beginStep();
    refiner.addMirrorPair(4,5);
    collapser.addMirrorPair(4,5);
    collapser.commitStep();
    refiner.commitStep();
    require(refiner.mirrors().contains(4)&&
            collapser.mirrors().contains(5)&&
            refiner.mirrors().at(4)==5&&
            collapser.mirrors().at(5)==4,
            "Committed Step retains inserted mirror pairs for future stamps");
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
    const auto noticeLimit=std::chrono::steady_clock::now()+
                           std::chrono::seconds(5);
    while(!notification&&std::chrono::steady_clock::now()<noticeLimit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        notification=worker.takeSnapshot();
    }
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
void testOptInCollisionAssessmentOnCleanAsyncStrokes() {
    SphereOptions options;
    options.vertexCount=500;
    options.relaxationIterations=6;
    const auto initial=generateSphere(options);
    SculptCollisionSettings collision;
    collision.enabled=true;
    AsyncSculptWorker worker(initial,.025,.006,.2,{}, {},{},false,
                            {},collision);
    const auto first=worker.initialRevision();
    worker.begin({-.25,-.65,.7},.35);
    worker.update({-.26,-.66,.69},.1);
    worker.end({-.27,-.67,.68},.2);
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(15);
    while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto state=worker.status();
    require(worker.canBegin()&&state.pendingSteps==0&&
            state.collisionCheckedStamps>0&&
            state.collisionRepairedStamps==0&&
            state.rejectedStamps==0&&
            state.contact.episodes==0&&
            state.contact.cleanChecks==state.collisionCheckedStamps&&
            state.contact.contactChecks==0&&
            state.contact.uncertainChecks==0&&
            state.publishedRevisions==state.history.undoDepth,
            "Opt-in complete-candidate collision policy preserves clean worker commits");
    auto last=worker.takeSnapshot();
    require(last&&last->index&&last->index!=first,
            "Clean collision-checked Steps still publish immutable index snapshots");
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

// Opt-in TRUE union requests never originate from the desktop UI. They
// are worker commands carrying the EXACT historical root and full V13
// contact graph. The existing stamp/history/checkpoint mailbox is reused.
Mesh twoIntersectingTetrahedra(bool reflected=false) {
    // Both authentic production developer fixtures and every older
    // M47/M49 positive use the EXACT same original coordinates/face winding.
    return reflected?desktopMergeReflectedTetraFixture()
                    :desktopMergeAsymmetricTetraFixture();
}
// Two disjoint transverse four-section loops in ONE original triangle.
// Every face of each small protrusion crosses the top of the large closed
// tetrahedron; no unchanged wholly buried face needs unauthorized deletion.
Mesh twoSeparateFullSectionLoops() {
    Mesh mesh;
    for(const auto p:std::array<Vec3,12>{{
        {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
        {.375,.375,.25},{.625,.375,.25},
        {.625,.625,-.25},{.375,.25,-.25},
        {1,.25,.25},{1.25,.25,.25},
        {1.25,.5,-.25},{1,.125,-.25}
    }})mesh.vertices.push_back(p);
    for(unsigned solid=0;solid<3;++solid) {
        const auto v=solid*4;
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})mesh.triangles.push_back({v+t[0],v+t[1],v+t[2]});
    }
    return mesh;
}
std::vector<RegionTopologyIndex::CollisionPair> fullOriginalContacts(
    const RegionTopologyIndex& index) {
    std::vector<VertexId> vertices;
    index.revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& vertex:part.vertices)vertices.push_back(vertex.id);
        });
    return index.triangleIntersections(vertices,{});
}
ExactCutStepLimits supportedUnionLimits(bool mirror) {
    ExactCutStepLimits limits;
    auto& contours=limits.join.removal.contours;
    contours.maximumPlanarityInRadii=4;
    contours.maximumSingleCapPlanarityInRadii=4;
    contours.maximumIsoperimetricRatio=20;
    contours.maximumSingleCapIsoperimetricRatio=20;
    contours.maximumPerimeterRatio=20;
    contours.maximumGapInEdges=mirror?2.5:20;
    contours.minimumNormalAlignment=0;
    limits.requireMirrors=mirror;
    return limits;
}
void waitForUnion(AsyncSculptWorker& worker,std::size_t total) {
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
    while(std::chrono::steady_clock::now()<deadline) {
        const auto status=worker.status();
        if(status.completedUnions+status.rejectedUnions>=total)return;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    throw std::runtime_error("Queued exact-Boolean worker command did not drain");
}
void testQueuedExactUnionOneStepHistoryAndRefusal() {
    AsyncSculptWorker worker(twoIntersectingTetrahedra(),.025,.006,.2,
                             {},{},{},false);
    const auto original=worker.initialRevision();
    const auto oldRoot=original->pickingRootIdentity();
    const auto contacts=fullOriginalContacts(*original);
    require(contacts.size()>=8,"Unmirrored worker test has full source contacts");
    const auto limits=supportedUnionLimits(false);
    worker.mergeExactContacts(original,contacts,limits);
    waitForUnion(worker,1);
    const auto status=worker.status();
    if(status.completedUnions!=1)
        throw std::runtime_error(
            "Worker rejected supported exact union: "+status.lastRejection);
    require(status.rejectedUnions==0&&
            status.publishedRevisions==1&&status.history.undoDepth==1&&
            status.completedStamps==0,
            "One Boolean worker request creates one history Step, no sculpt stamp");
    auto joined=worker.takeSnapshot();
    require(joined&&joined->index&&joined->index!=original&&
            !joined->dirtyPartitions.empty(),
            "Renderer mailbox receives only final connected union and dirty partitions");
    const auto final=joined->index;
    require(original->pickingRootIdentity()==oldRoot&&
            original->retiredFaceCount()==0&&final->retiredFaceCount()>0,
            "Published union leaves held original reader and stable IDs unchanged");
    worker.mergeExactContacts(original,contacts,limits);
    waitForUnion(worker,2);
    require(worker.status().rejectedUnions==1&&
            worker.status().publishedRevisions==1&&
            worker.status().history.undoDepth==1&&
            !worker.takeSnapshot(),
            "Stale queued union refuses without new root or history change");
    worker.undo();
    const auto undoDeadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(15);
    while(worker.status().history.redoDepth==0&&
          std::chrono::steady_clock::now()<undoDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto undone=worker.takeSnapshot();
    require(undone&&undone->index==original&&
            worker.status().history.undoDepth==0,
            "ONE worker undo restores the exact original root after real union");
    worker.redo();
    const auto redoDeadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(15);
    while(worker.status().history.redoDepth!=0&&
          std::chrono::steady_clock::now()<redoDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto redone=worker.takeSnapshot();
    require(redone&&redone->index==final&&
            !redone->dirtyPartitions.empty(),
            "ONE worker redo republishes exact final root and accumulated GPU dirties");
    const auto file=std::filesystem::temp_directory_path()/
                    "mesher-worker-v17-exact-union.mshr";
    try {
        worker.saveCheckpoint(file);
        const auto saveDeadline=std::chrono::steady_clock::now()+
                                std::chrono::seconds(15);
        while(worker.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<saveDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.checkpointStatistics().completed==1,
                "Union checkpoint reaches async durable writer");
        const auto loaded=readCheckpoint(file);
        require(loaded.index->retiredFaceIds()==final->retiredFaceIds()&&
                loaded.index->retiredVertexIds()==final->retiredVertexIds(),
                "Worker checkpoint restores net union and permanently retired IDs");
    }catch(...) {
        std::error_code ec;std::filesystem::remove(file,ec);
        throw;
    }
    std::error_code ec;std::filesystem::remove(file,ec);
}
void testWorkerUnionRefusalsDoNotBlockLaterInput() {
    AsyncSculptWorker worker(twoIntersectingTetrahedra(),.025,.006,.2,
                             {},{},{},false);
    const auto initial=worker.initialRevision();
    const auto contacts=fullOriginalContacts(*initial);
    auto limits=supportedUnionLimits(false);
    limits.maxNetChangedIds=1; // guaranteed full transaction refusal
    worker.mergeExactContacts(initial,contacts,limits);
    waitForUnion(worker,1);
    require(worker.status().rejectedUnions==1&&
            worker.status().completedUnions==0&&
            worker.status().history.undoDepth==0&&
            worker.status().publishedRevisions==0&&
            !worker.takeSnapshot(),
            "Rejected work-bounded union publishes no partial root or mailbox");
    worker.mergeExactContacts(initial,contacts,supportedUnionLimits(false));
    waitForUnion(worker,2);
    require(worker.status().rejectedUnions==1&&
            worker.status().completedUnions==1&&
            worker.status().history.undoDepth==1,
            "Valid queued union still commits after an earlier budget refusal");
    const auto merged=worker.takeSnapshot();
    require(merged&&merged->index&&merged->index!=initial,
            "Worker is usable and publishes one complete Step after refusal");
}
void testWorkerMultipleCompleteSectionsOneHistoryStep() {
    AsyncSculptWorker worker(twoSeparateFullSectionLoops(),.025,.006,.2,
                             {},{},{},false);
    const auto original=worker.initialRevision();
    const auto historical=original->pickingRootIdentity();
    const auto contacts=fullOriginalContacts(*original);
    require(contacts.size()==8,
            "Worker multiple-section fixture has all eight genuine original V13 pairs");
    auto limits=supportedUnionLimits(false);
    // The two spatially separated real seam orbits are not candidate
    // partners for a hypothetical V15 zipper across protrusions.
    limits.join.removal.contours.maximumGapInEdges=0.1;
    worker.mergeExactContacts(original,contacts,limits);
    waitForUnion(worker,1);
    const auto status=worker.status();
    if(status.completedUnions!=1)
        throw std::runtime_error(
            "Worker refused certified multiple-section union: "+
            status.lastRejection);
    require(status.rejectedUnions==0&&status.completedStamps==0&&
            status.publishedRevisions==1&&status.history.undoDepth==1,
            "Two section loops publish in one queued command and one history Step");
    const auto ready=worker.takeSnapshot();
    require(ready&&ready->index&&ready->index!=original&&
            !ready->dirtyPartitions.empty()&&
            original->pickingRootIdentity()==historical,
            "Only final multiple-seam union is delivered to renderer/picker");
    const auto final=ready->index;
    worker.mergeExactContacts(original,contacts,limits);
    waitForUnion(worker,2);
    require(worker.status().rejectedUnions==1&&
            worker.status().history.undoDepth==1&&
            !worker.takeSnapshot(),
            "Original-root stale multi-loop request cannot publish or append history");
    worker.undo();
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(15);
    while(worker.status().history.redoDepth==0&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto reverted=worker.takeSnapshot();
    require(reverted&&reverted->index==original,
            "Multi-loop union has one undo to exact historical pointer");
    worker.redo();
    const auto redoDeadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(15);
    while(worker.status().history.redoDepth!=0&&
          std::chrono::steady_clock::now()<redoDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto restored=worker.takeSnapshot();
    require(restored&&restored->index==final,
            "Multi-loop union redo returns same single terminal revision");
    const auto file=std::filesystem::temp_directory_path()/
                    "mesher-worker-v17-multiple-loop-union.mshr";
    try {
        worker.saveCheckpoint(file);
        const auto saveDeadline=std::chrono::steady_clock::now()+
                                std::chrono::seconds(15);
        while(worker.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<saveDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.checkpointStatistics().completed==1,
                "Multi-loop union checkpoint reaches durable writer");
        const auto loaded=readCheckpoint(file);
        require(loaded.index->retiredFaceIds()==final->retiredFaceIds()&&
                loaded.index->retiredVertexIds()==final->retiredVertexIds(),
                "Multiple independent real contact seams preserve exact tombstones after reload");
    }catch(...) {
        std::error_code ec;std::filesystem::remove(file,ec);
        throw;
    }
    std::error_code ec;std::filesystem::remove(file,ec);
}
void testWorkerMirroredThreeBodyOneHistoryStep() {
    Mesh mesh;
    const std::array<Vec3,12> positions{{
        {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
        {.375,.375,.25},{.625,.375,.25},
        {.625,.625,-.25},{.375,.25,-.25},
        {.21875,.15625,.34375},{.46875,.125,.34375},
        {.53125,.40625,-.1875},{.28125,.09375,-.125}
    }};
    for(const auto p:positions)mesh.vertices.push_back({p.x+5,p.y,p.z});
    for(const auto p:positions)mesh.vertices.push_back({-p.x-5,p.y,p.z});
    for(unsigned side=0;side<2;++side)
        for(unsigned solid=0;solid<3;++solid)
            for(const auto tri:std::array<std::array<unsigned,3>,4>{{
                {0,1,2},{1,0,3},{2,1,3},{0,2,3}
            }}) {
                const auto base=side*12+solid*4;
                mesh.triangles.push_back({
                    base+tri[0],base+tri[side?2:1],base+tri[side?1:2]});
            }
    const auto imported=partitionFlatMesh(mesh,256);
    const auto original=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> pairs;
    for(std::size_t k=0;k<12;++k)
        pairs.emplace_back(imported.flatIndex.vertexIds[k],
                           imported.flatIndex.vertexIds[k+12]);
    AsyncSculptWorker worker({original,pairs,24},
                             .025,.006,.2,{},{},{});
    const auto contacts=fullOriginalContacts(*original);
    require(contacts.size()==24,
            "Mirrored worker triple fixture must pass ALL original V13 contacts");
    auto limits=supportedUnionLimits(true);
    limits.join.removal.contours.maximumGapInEdges=.1;
    worker.mergeExactContacts(original,contacts,limits);
    waitForUnion(worker,1);
    const auto first=worker.status();
    if(first.completedUnions!=1)
        throw std::runtime_error(
            "Worker mirrored 24-contact three-body Step refused: "+
            first.lastRejection);
    require(first.rejectedUnions==0&&first.completedStamps==0&&
            first.publishedRevisions==1&&first.history.undoDepth==1,
            "One worker input yields one paired three-body Boolean CAS and one history Step");
    const auto complete=worker.takeSnapshot();
    require(complete&&complete->index&&complete->index!=original&&
            !complete->dirtyPartitions.empty(),
            "Worker snapshot never contains unpublished mirrored cut or refinement roots");
    worker.mergeExactContacts(original,contacts,limits);
    waitForUnion(worker,2);
    require(worker.status().completedUnions==1&&
            worker.status().rejectedUnions==1&&
            worker.status().history.undoDepth==1,
            "A stale three-body worker CAS refuses without mutating either mirror controller");
    worker.undo();
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
    while(worker.status().history.redoDepth==0&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto undone=worker.takeSnapshot();
    require(undone&&undone->index==original&&
            worker.status().history.redoDepth==1,
            "One undo restores both original reflected sets with all old mirrored IDs");
    worker.redo();
    while(worker.status().history.redoDepth!=0&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto redone=worker.takeSnapshot();
    require(redone&&redone->index==complete->index&&
            worker.status().history.undoDepth==1,
            "One redo restores both reflected three-solid welds and mirror-map journals");
    const auto filename=std::filesystem::temp_directory_path()/
        "mesher-worker-v17-m05-reflected-three-body.mshr";
    try {
        worker.saveCheckpoint(filename);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(worker.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.checkpointStatistics().completed==1,
                "One reflected three-body worker checkpoint is durable");
        const auto recovered=readCheckpoint(filename);
        require(recovered.index&&
                recovered.index->retiredFaceIds()==redone->index->retiredFaceIds()&&
                recovered.index->retiredVertexIds()==redone->index->retiredVertexIds()&&
                recovered.mirrorPairs.size()>12,
                "Paired three-body union checkpoint records exact surviving NEW mirror pairs");
        for(const auto& [a,b]:recovered.mirrorPairs) {
            require(a!=b&&recovered.index->maybeVertex(a)&&
                    recovered.index->maybeVertex(b),
                    "Checkpoint contains no retired or self-paired reflected vertex");
            const auto p=recovered.index->vertex(a).position,
                       q=recovered.index->vertex(b).position;
            require(q.x==-p.x&&q.y==p.y&&q.z==p.z,
                    "All three worker controller mirror-map deltas survived durable reload");
        }
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(filename,ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(filename,ec);
}

// Deliberate test-only caller scope for positive source-exact physical edits.
// The worker never chooses these FaceIds from the brush's candidate XYZ.
std::vector<FaceId> firstOriginalFaces(const Mesh& mesh) {
    return partitionFlatMesh(mesh,256).flatIndex.faceIds;
}
void testExactPhysicalRemeshIsOneSpeculativeTransaction() {
    Mesh mesh;
    for(const auto p:std::array<Vec3,8>{{
        {-1,0,0},{1,0,0},{0,1.2,0},{0,.2,-1.3},
        {-.65,.1,.35},{.65,.1,.35},{0,.9,.35},{0,.3125,-.65}
    }})mesh.vertices.push_back({p.x+5,p.y,p.z});
    for(unsigned tetra=0;tetra<2;++tetra)
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})mesh.triangles.push_back({
            4*tetra+t[0],4*tetra+t[1],4*tetra+t[2]});
    RefineSettings refine;
    refine.enabled=true;
    refine.maximumEdgeRatio=.1;
    refine.maximumSplitPairsPerStamp=1;
    refine.optimizeFlips=false;
    SculptCollisionSettings opts;
    opts.physicalExact=true;
    auto& quality=opts.physicalExactLimits.join.removal.contours;
    quality.maximumPlanarityInRadii=4.0;
    quality.maximumSingleCapPlanarityInRadii=4.0;
    quality.maximumIsoperimetricRatio=20.0;
    quality.maximumSingleCapIsoperimetricRatio=20.0;
    quality.maximumPerimeterRatio=20.0;
    quality.maximumGapInEdges=2.5;
    quality.minimumNormalAlignment=0;
    const auto imported=partitionFlatMesh(mesh,256);
    const auto original=RegionTopologyIndex::build(imported.revision);
    Mesh dummy;
    bool accepted=false;
    std::size_t exercised=0,rejected=0;
    std::string lastRefusal;
    // Every candidate is an INDEPENDENT actual physical source stamp.
    // A refused locally split candidate cannot leave a residual original
    // root or a born-only stable ID behind. A successful one must
    // construct the same FULL exact original-root union plus local split.
    for(const double strength:{.0005,.002,.01}) {
        if(accepted)break;
        for(const auto center:mesh.vertices) {
            if(accepted)break;
            const std::array<BrushArea,1> areas{{{center,.5,.9}}};
            VersionedInflateBrush inflate(strength,{});
            const auto moved=inflate.stamp(original,areas,256);
            if(moved.moved==0)continue;
            VersionedLocalRefiner local(mesh.vertices.size(),refine,{},256);
            const auto split=local.apply(moved.index,moved.baseline);
            if(split.statistics.splitPairs!=1)continue;
            ++exercised;
            VersionedSculptBrush brush(mesh,strength,{},{},refine,
                                      false,256,{},opts);
            const auto before=brush.snapshot();
            brush.setOriginalPatchIntent(firstOriginalFaces(mesh));
            brush.stamp(dummy,areas);
            if(brush.completedUnions()==1) {
                require(brush.snapshot()!=before&&
                        brush.historyStatistics().undoDepth==1&&
                        brush.publishedRevisions()==1&&
                        brush.lastDetails().splitPairs==1&&
                        brush.undo()&&brush.snapshot()==before,
                        "Physical split PLUS accepted exact Boolean has ONE immutable original-root Step");
                accepted=true;
            }else {
                ++rejected;
                lastRefusal=brush.lastRejection();
                require(brush.snapshot()==before&&
                        brush.rejectedStamps()==1&&
                        brush.publishedRevisions()==0&&
                        brush.historyStatistics().undoDepth==0&&
                        brush.checkpoint().index==before&&
                        brush.lastDirtyPartitions().empty()&&
                        !lastRefusal.empty(),
                        "Unsupported source-remesh contact must roll back BOTH edits and journals");
            }
        }
    }
    if(!accepted)
        throw std::runtime_error(
            "No split + exact physical terminal join proved positive, tested "+
            std::to_string(exercised)+" refused "+std::to_string(rejected)+
            " last source failure: "+lastRefusal);
}
void testPhysicallyCreatedContactCanPassExactSourcePlanner() {
    // Unlike the already intersecting M16 acceptance source, this
    // stimulus begins with TWO disjoint closed solids. The only way to
    // see source contacts is to apply a real normal-driven Inflate stamp.
    auto opts=SculptCollisionSettings{};
    opts.physicalExact=true;
    auto& quality=opts.physicalExactLimits.join.removal.contours;
    quality.maximumPlanarityInRadii=4.0;
    quality.maximumSingleCapPlanarityInRadii=4.0;
    quality.maximumIsoperimetricRatio=20.0;
    quality.maximumSingleCapIsoperimetricRatio=20.0;
    quality.maximumPerimeterRatio=20.0;
    quality.maximumGapInEdges=2.5;
    quality.minimumNormalAlignment=0;
    bool found=false;
    std::string last;
    for(const double halfGap:{.15,.25,.35}) {
        if(found)break;
        Mesh gap;
        const auto tetra=[&](Vec3 center) {
            const auto offset=static_cast<std::uint32_t>(gap.vertices.size());
            for(const auto p:std::array<Vec3,4>{{
                {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}}})
                gap.vertices.push_back(center+p);
            for(const auto t:std::array<std::array<unsigned,3>,4>{{
                {0,1,2},{0,2,3},{0,3,1},{1,3,2}}})
                gap.triangles.push_back({
                    offset+t[0],offset+t[1],offset+t[2]});
        };
        tetra({-1-halfGap,0,0});
        tetra({1+halfGap,.19,-.11});
        const auto imported=partitionFlatMesh(gap,256);
        const auto root=RegionTopologyIndex::build(imported.revision);
        std::vector<VertexId> ids;
        root->revision().forEachPartition([&](PartitionId,const MeshPartition& part) {
            for(const auto& vertex:part.vertices)ids.push_back(vertex.id);
        });
        require(root->triangleIntersections(ids,{}).empty(),
                "Before M18 physical stimulus the source shells are disjoint");
        for(const double strength:{.35,.55,.8,1.0,1.3}) {
            const std::array<BrushArea,1> areas{{{{0,0,0},6,.95}}};
            VersionedInflateBrush trial(strength,{});
            const auto physicallyMoved=trial.stamp(root,areas,256);
            const auto observed=assessPostSculptOriginalContacts(
                {root,physicallyMoved.index,
                 physicallyMoved.baseline.movedVertices,
                 physicallyMoved.baseline.affectedFaces},false);
            if(!observed.ready()) {
                last=observed.refusal.empty()?
                    "no finite original source pair":observed.refusal;
                continue;
            }
            const auto valid=prepareExactCutStep(
                observed.originalCandidate,observed.contacts,
                opts.physicalExactLimits);
            if(!valid.complete) {
                last=valid.refusal;
                continue;
            }
            VersionedSculptBrush real(gap,strength,{},{},{},false,256,{},opts);
            const auto original=real.snapshot();
            Mesh dummy;
            real.setOriginalPatchIntent(firstOriginalFaces(gap));
            real.stamp(dummy,areas);
            if(real.completedUnions()!=1) {
                last=real.lastRejection();
                continue;
            }
            require(real.snapshot()!=original&&
                    real.publishedRevisions()==1&&
                    real.historyStatistics().undoDepth==1&&
                    real.checkpoint().index==real.snapshot()&&
                    real.undo()&&real.snapshot()==original,
                    "Clean-to-contact TRUE physical union publishes one original-root CAS/history");
            found=true;
            std::cout<<"[m18] genuinely induced source-exact contact half-gap="
                     <<halfGap<<" strength="<<strength
                     <<" pairCount="<<observed.contacts.size()<<'\n';
            break;
        }
    }
    if(!found)
        throw std::runtime_error(
            "No complete clean-to-contact candidate proved an EXACT Boolean: "+last);
}
// M11: sixteen independently eligible ORIGINAL source regions arranged in
// TWO alternated symmetric towers. All shells start DISJOINT. A stamp
// touches ONE pair in each reflected tower half, not 16 pre-existing seams.
// An early all-root scan must therefore be clean, and only the actually
// physically deformed region may join at each scheduled input.
struct M11TowerSource {
    Mesh mesh;
    std::vector<std::pair<std::size_t,std::size_t>> originalPairs;
    std::vector<std::array<BrushArea,2>> input;
};
M11TowerSource makeM11Towers(std::size_t count,double gap) {
    M11TowerSource out;
    const std::array<Vec3,4> tetra{{
        {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}}};
    const std::array<std::array<unsigned,3>,4> faces{{
        {0,1,2},{0,2,3},{0,3,1},{1,3,2}}};
    for(std::size_t i=0;i<count;++i) {
        const double y=i%2==0?-14.0:14.0;
        const double z=20.0*static_cast<double>(i/2);
        const Vec3 center{5,y,z};
        const auto start=out.mesh.vertices.size();
        for(const Vec3 shift:{Vec3{-1-gap,0,0},
                              Vec3{1+gap,.19,-.11}}) {
            const auto off=static_cast<std::uint32_t>(
                out.mesh.vertices.size());
            for(const auto p:tetra)
                out.mesh.vertices.push_back(center+shift+p);
            for(const auto tri:faces)
                out.mesh.triangles.push_back({
                    off+tri[0],off+tri[1],off+tri[2]});
        }
        const auto mirroredStart=out.mesh.vertices.size();
        const auto facesAtStart=out.mesh.triangles.size();
        for(std::size_t j=0;j<8;++j) {
            const auto p=out.mesh.vertices[start+j];
            out.mesh.vertices.push_back({-p.x,p.y,p.z});
            out.originalPairs.emplace_back(start+j,mirroredStart+j);
        }
        for(std::size_t j=0;j<8;++j) {
            const auto t=out.mesh.triangles[facesAtStart-8+j];
            // Mirror reverses triangle orientation and preserves the
            // original four independently closed source components.
            const auto d=static_cast<std::uint32_t>(mirroredStart-start);
            out.mesh.triangles.push_back({
                t.a+d,t.c+d,t.b+d});
        }
        out.input.push_back({{
            {{5,y,z},6,.95},{{-5,y,z},6,.95}
        }});
    }
    return out;
}
DurableCheckpoint checkpointM11Source(const M11TowerSource& source) {
    const auto imported=partitionFlatMesh(source.mesh,256);
    DurableCheckpoint out;
    out.index=RegionTopologyIndex::build(imported.revision);
    out.initialVertexCount=source.mesh.vertices.size();
    for(const auto& [i,j]:source.originalPairs)
        out.mirrorPairs.emplace_back(
            imported.flatIndex.vertexIds[i],imported.flatIndex.vertexIds[j]);
    // This is a freshly built ORIGINAL geometry source, not a terminal
    // checkpoint replay. Explicitly retain trusted first-root authority.
    out.verifiedOriginalFidelity=std::make_shared<const OriginalPatchLifetime>(
        beginOriginalPatchLifetime(captureOriginalSourceAnchors(
            out.index,imported.flatIndex.faceIds,
            7*imported.flatIndex.faceIds.size())));
    out.verifiedFirstOriginalMirrorPairs=out.mirrorPairs;
    out.originalFidelityRequired=true;
    return out;
}
// The caller continues to explicitly authorize previously sculpted
// original towers as well as the new one. Future untouched towers remain
// protected. M32 excludes ONLY independently certified wholly retired
// origins from the subsequent M21 active intent declaration.
std::vector<FaceId> m11RegionIntent(const M11TowerSource& source,
                                  std::size_t region) {
    if(region>=source.input.size())
        throw std::out_of_range("Missing M11 original contact region");
    const auto all=firstOriginalFaces(source.mesh);
    return {all.begin(),all.begin()+static_cast<std::ptrdiff_t>(16*(region+1))};
}
SculptCollisionSettings m11PhysicalExactSettings() {
    SculptCollisionSettings result;
    result.physicalExact=true;
    auto& p=result.physicalExactLimits.join.removal.contours;
    p.maximumPlanarityInRadii=4.0;
    p.maximumSingleCapPlanarityInRadii=4.0;
    p.maximumIsoperimetricRatio=20.0;
    p.maximumSingleCapIsoperimetricRatio=20.0;
    p.maximumPerimeterRatio=20.0;
    p.maximumGapInEdges=2.5;
    p.minimumNormalAlignment=0;
    result.physicalSourceLimits.maxSectionTests=128;
    return result;
}
double chooseM11TowerStrength() {
    // Find a single supported physical strength from the same source
    // topology, without hardcoding manually fabricated intersection pairs.
    // The chosen strength is fixed for the entire sixteen-input run.
    constexpr double halfGap=.15;
    const auto trial=makeM11Towers(1,halfGap);
    const auto initialTrial=checkpointM11Source(trial);
    Mesh dummy;
    double strength=0;
    std::string last;
    for(const double testStrength:{.35,.55,.8,1.0,1.3}) {
        VersionedSculptBrush one(initialTrial,testStrength,
            {},{},{},256,{},m11PhysicalExactSettings());
        one.setOriginalPatchIntent(m11RegionIntent(trial,0));
        one.stamp(dummy,trial.input.front());
        if(one.completedUnions()==1&&one.publishedRevisions()==1) {
            strength=testStrength;
            break;
        }
        last=one.lastRejection();
    }
    if(strength==0)
        throw std::runtime_error(
            "M11 no supported single symmetric clean-to-contact tower union: "+last);

    return strength;
}

void testM23ConclusiveEpisodesAndReal4DFIFO() {
    // M23's exact state-machine contract. An uncertain classification,
    // refused input or exhausted work limit is neither Contact nor Clean.
    // In particular C,C,?,C must NOT close a three-consecutive-C episode.
    SculptContactTracker check;
    check.observe(true);
    require(check.statistics().active&&check.statistics().episodes==1&&
            check.statistics().contactRun==1&&
            check.statistics().closedEpisodes==0,
            "M23 first conclusive contact opens one episode");
    check.observe(false);
    check.observe(false);
    require(check.statistics().active&&check.statistics().quietRun==2,
            "M23 two conclusive Clean checks keep the episode open");
    check.observe(std::nullopt);
    check.observe(false);
    require(check.statistics().active&&check.statistics().quietRun==1&&
            check.statistics().uncertainChecks==1&&
            check.statistics().closedEpisodes==0,
            "M23 unknown breaks consecutive Clean run rather than preserving two credits");
    check.observe(true);
    require(check.statistics().episodes==1&&check.statistics().quietRun==0&&
            check.statistics().active&&check.statistics().contactRun==1,
            "M23 recontact before closure extends SAME episode and resets quiet");
    check.observe(false);
    check.observe(false);
    check.observe(std::nullopt);
    require(check.statistics().active&&check.statistics().quietRun==0,
            "M23 uncertain after two Clean prevents false closure");
    check.observe(false);
    check.observe(false);
    require(check.statistics().active&&check.statistics().quietRun==2,
            "M23 two new consecutive Clean are still insufficient");
    check.observe(false);
    require(!check.statistics().active&&check.statistics().quietRun==0&&
            check.statistics().closedEpisodes==1&&check.statistics().episodes==1,
            "M23 precisely third successive conclusive Clean ends first episode");
    check.observe(true);
    require(check.statistics().episodes==2&&check.statistics().closedEpisodes==1&&
            check.statistics().active,
            "M23 first new conclusive recontact opens fresh episode");
    bool threw=false;
    try {(void)SculptContactTracker(0);}
    catch(const std::invalid_argument&){threw=true;}
    require(threw,"M23 refuses zero-length quiet hysteresis");

    // Physical FIFO, not fabricated Contact/Clean observations. Start
    // with a genuine symmetric original-source M11 exact union from
    // disconnected solids, then move the untouched EXTERIOR corner of
    // one already joined band to obtain independently V13-classified
    // actual Clean proposals without additional unions.
    const auto source=makeM11Towers(2,.15);
    const auto starting=checkpointM11Source(source);
    const auto strength=chooseM11TowerStrength();
    AsyncSculptWorker worker(starting,.025,strength,.2,{},{},{},
                             {},m11PhysicalExactSettings());
    const auto original=worker.initialRevision();
    const auto stroke=[&](Vec3 p,double radius,
                          std::vector<FaceId> intended) {
        require(worker.canBegin(),
                "M23 FIFO producer reservation clear before new real stroke");
        worker.begin(p,radius,std::move(intended));
        worker.end(p,0);
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(25);
        while(!worker.canBegin()&&
              std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(worker.canBegin(),
                "M23 real physical Begin/End fully drained before contact status inspection");
        return worker.status();
    };
    const auto contact=stroke(source.input[0][0].center,6,
                              m11RegionIntent(source,0));
    if(contact.completedUnions!=1)
        throw std::runtime_error(
            "M23 initial true physical M11 Contact did not source-weld: "+
            contact.lastRejection);
    require(contact.contact.episodes==1&&
            contact.contact.contactChecks==1&&contact.contact.active&&
            contact.contact.cleanChecks==0&&
            contact.contact.closedEpisodes==0&&
            contact.publishedRevisions==1,
            "M23 genuine physical FIFO contact starts active episode and one exact terminal CAS");
    const auto joined=worker.takeSnapshot();
    require(joined&&joined->index!=original&&
            !joined->dirtyPartitions.empty(),
            "M23 original-to-contact terminal only reaches renderer/picker once");

    // Localized exterior-corner input leaves other tower original
    // identities and contacts untouched; two Clean, a REFUSED bad scope,
    // then exactly three further Clean must be necessary to close.
    // The initial exact join can retire original import VertexIds.
    // Choose an ACTUALLY LIVE positive-X outer corner from the welded
    // immutable root, never a possibly tombstoned imported XYZ point.
    Vec3 exterior{};
    bool selected=false;
    joined->index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& vertex:part.vertices) {
                const auto v=vertex.position;
                if(v.x<=0||v.y>=-12||std::abs(v.z)>2)continue;
                if(!selected||v.x<exterior.x) {
                    exterior=v;
                    selected=true;
                }
            }
        });
    require(selected,"M23 joined true tower has a live exterior positive-X vertex");
    const auto good=m11RegionIntent(source,0);
    auto latestAccepted=joined->index;
    const auto clean=[&](std::size_t expectedClean,
                         std::size_t expectedQuiet,bool active) {
        const auto status=stroke(exterior,.5,good);
        if(status.contact.cleanChecks!=expectedClean||
           status.contact.quietRun!=expectedQuiet||
           status.contact.active!=active||
           status.rejectedStamps!=(expectedClean>2?1u:0u))
            throw std::runtime_error(
                "M23 actual clean physical 4D classification mismatch: clean="+
                std::to_string(status.contact.cleanChecks)+
                " quiet="+std::to_string(status.contact.quietRun)+
                " active="+std::to_string(status.contact.active)+
                " contact="+std::to_string(status.contact.contactChecks)+
                " uncertain="+std::to_string(status.contact.uncertainChecks)+
                " rejected="+std::to_string(status.rejectedStamps)+
                " completed="+std::to_string(status.completedStamps)+
                " candidateVertices="+std::to_string(status.details.influenced)+
                " moved="+std::to_string(status.details.moved)+
                " collisionChecked="+std::to_string(status.collisionCheckedStamps)+
                " reason="+status.lastRejection);
        require(status.contact.episodes==1&&
                status.contact.closedEpisodes==(active?0u:1u),
                "M23 actual clean FIFO observations use one episode and only conclusive closure");
        // The renderer MAY combine accepted dirty owners until they are
        // consumed. Drain EACH accepted mailbox before asserting that a
        // later rejected Step contributed no new dirty changes.
        const auto published=worker.takeSnapshot();
        require(published&&published->index!=latestAccepted&&
                !published->dirtyPartitions.empty()&&
                status.history.undoDepth==status.publishedRevisions,
                "M23 each conclusive real Clean still publishes an independent full root and GPU dirty owners");
        latestAccepted=published->index;
    };
    clean(1,1,true);
    clean(2,2,true);
    const auto beforeInvalid=worker.status();
    const auto refused=stroke(exterior,.28,{FaceId{99999999}});
    require(refused.rejectedStamps==beforeInvalid.rejectedStamps+1&&
            refused.contact.uncertainChecks==
                beforeInvalid.contact.uncertainChecks+1&&
            refused.contact.quietRun==0&&refused.contact.active&&
            refused.contact.cleanChecks==beforeInvalid.contact.cleanChecks&&
            refused.contact.episodes==1&&
            refused.publishedRevisions==beforeInvalid.publishedRevisions,
            "M23 rejected real FIFO source scope cannot finish two-Clean contact episode");
    const auto snap=worker.takeSnapshot();
    require(snap&&snap->index==latestAccepted&&
            snap->dirtyPartitions.empty(),
            "M23 refused quiet candidate leaves SAME last accepted root and adds no renderer dirty owners");
    clean(3,1,true);
    clean(4,2,true);
    clean(5,0,false);
    const auto closed=worker.status();
    require(closed.contact.closedEpisodes==1&&
            closed.contact.episodes==1&&
            closed.contact.cleanChecks==5&&
            closed.contact.uncertainChecks==1,
            "M23 actual third-after-refusal consecutive Clean closes one active episode");
    const auto recontact=stroke(source.input[1][0].center,6,
                                m11RegionIntent(source,1));
    if(recontact.completedUnions!=2)
        throw std::runtime_error(
            "M23 recontact after three Clean refused actual second tower source union: "+
            recontact.lastRejection);
    require(recontact.contact.episodes==2&&
            recontact.contact.closedEpisodes==1&&
            recontact.contact.contactChecks==2&&
            recontact.contact.active&&
            recontact.contact.quietRun==0&&
            recontact.history.undoDepth==recontact.publishedRevisions,
            "M23 genuine second source-weld opens new contact episode WITHOUT caching earlier approval");
}


// M35: TWO positive physical Boolean unions in the SAME overlapping brush
// neighborhood, not a second distant M11 tower. Each actual union is made
// by a normal-driven physical Step and enters one immutable M29 history.
void testM35RepeatedPhysicalUnionsInOneOverlappingPatch() {
    // Each mirrored source pair is independently closed and disjoint.
    // Move the second M11 source band into the first band's LOCAL brush
    // footprint, while keeping distinct original FaceIds and mirror IDs.
    // This does NOT introduce any initial contact or turn off provenance.
    Mesh dummy;
    std::string last;
    bool completed=false;
    for(const double separation:{5.0,5.5,6.0,6.5}) {
        if(completed)break;
        for(const double gap:{.15,.25,.35}) {
            if(completed)break;
            const auto base=makeM11Towers(2,gap);
            auto source=base;
            const double secondY=-14+separation;
            const double shift=secondY-14;
            for(std::size_t i=16;i<32;++i)
                source.mesh.vertices[i].y+=shift;
            for(auto& area:source.input.at(1))
                area.center.y+=shift;
            const auto starting=checkpointM11Source(source);
            const auto first=starting.index;
            const auto firstPicker=first->pickingRootIdentity();
            require(first->triangleIntersections({},{}).empty(),
                    "M35 original local multiple-shell source starts without intersections");
            const auto nearA=first->querySphere(
                source.input[0][0].center,6);
            const auto nearB=first->querySphere(
                source.input[1][0].center,6);
            bool sharesLiveOriginalVertex=false;
            for(const auto id:nearA)
                if(std::find(nearB.begin(),nearB.end(),id)!=nearB.end())
                    sharesLiveOriginalVertex=true;
            require(sharesLiveOriginalVertex,
                    "M35 both overlapping local brush patches influence at least one SAME original source vertex");
            const auto intended=firstOriginalFaces(source.mesh);
            for(const double strength:{.35,.55,.8,1.0,1.3}) {
                auto opts=m11PhysicalExactSettings();
                VersionedSculptBrush brush(starting,strength,
                    {},{},{},256,{},opts);
                brush.setOriginalPatchIntent(intended);
                brush.stamp(dummy,source.input.at(0));
                if(brush.completedUnions()!=1) {
                    last="first local union: "+brush.lastRejection();
                    continue;
                }
                const auto joined=brush.snapshot();
                const auto firstMirrors=brush.checkpoint().mirrorPairs;
                if(joined==first||brush.historyStatistics().undoDepth!=1) {
                    last="first local source union missing published M29 root";
                    continue;
                }
                brush.setOriginalPatchIntent(intended);
                brush.stamp(dummy,source.input.at(1));
                if(brush.completedUnions()!=2) {
                    last="second local union at separation="+
                         std::to_string(separation)+
                         ", gap="+std::to_string(gap)+
                         ", strength="+std::to_string(strength)+
                         ": "+brush.lastRejection();
                    continue;
                }
                const auto final=brush.snapshot();
                const auto finalProof=brush.checkpoint();
                require(final!=joined&&
                        final->retiredFaceCount()>joined->retiredFaceCount()&&
                        joined->retiredFaceCount()>first->retiredFaceCount()&&
                        brush.rejectedStamps()==0&&
                        brush.publishedRevisions()==2&&
                        brush.historyStatistics().undoDepth==2&&
                        brush.contactStatistics().contactChecks==2&&
                        brush.contactStatistics().episodes==1&&
                        brush.contactStatistics().active&&
                        !brush.lastDirtyPartitions().empty()&&
                        finalProof.verifiedOriginalFidelity&&
                        finalProof.verifiedOriginalFidelity->anchors.original==first&&
                        finalProof.mirrorPairs.size()>=firstMirrors.size()&&
                        first->pickingRootIdentity()==firstPicker,
                        "M35 TWO true local symmetric physical source joins publish distinct protected M29 terminal roots in one still-active Contact episode");
                for(const auto& [a,b]:finalProof.mirrorPairs) {
                    require(final->maybeVertex(a)&&final->maybeVertex(b),
                            "M35 post-second local union preserves all live mirror IDs");
                    const auto p=final->vertex(a).position,
                               q=final->vertex(b).position;
                    require(p.x==-q.x&&p.y==q.y&&p.z==q.z,
                            "M35 second local union preserves exact reflected XYZ");
                }
                require(brush.undo()&&brush.snapshot()==joined&&
                        brush.undo()&&brush.snapshot()==first&&
                        brush.redo()&&brush.snapshot()==joined&&
                        brush.redo()&&brush.snapshot()==final,
                        "M35 both same-local-patch physical source joins undo and redo as independent immutable history roots");
                std::cout<<"[m35] genuine TWO overlapping-patch physical unions, separation="
                         <<separation<<" gap="<<gap
                         <<" strength="<<strength<<'\n';
                completed=true;
                break;
            }
        }
    }
    if(!completed)
        throw std::runtime_error(
            "M35 no two genuine supported close-neighborhood physical joins: "+last);
}

// M37 retains M35's genuinely overlapping two physical source unions but
// adds untouched FIRST-original reflected outer and inward cavity walls.
// The cavity is real signed-winding geometry, not an intentionally selected
// FaceId disguised as a protected wall or a coincident XYZ owner.
void addM37ProtectedMirroredCavity(M11TowerSource& source) {
    constexpr std::array<Vec3,10> local{{
        {3,0,0},{0,3,0},{-3,0,0},{0,-3,0},{0,0,3},{0,0,-3},
        {.7,0,0},{-.5,-.5,-.4},{-.5,.5,-.4},{0,0,.8}
    }};
    constexpr std::array<std::array<unsigned,3>,8> outer{{
        {4,0,1},{4,1,2},{4,2,3},{4,3,0},
        {5,1,0},{5,2,1},{5,3,2},{5,0,3}
    }};
    constexpr std::array<std::array<unsigned,3>,4> tetra{{
        {0,1,2},{0,2,3},{0,3,1},{1,3,2}
    }};
    const auto positive=static_cast<std::uint32_t>(source.mesh.vertices.size());
    for(const auto p:local)
        source.mesh.vertices.push_back({p.x+5,p.y+80,p.z});
    const auto negative=static_cast<std::uint32_t>(source.mesh.vertices.size());
    for(std::size_t i=0;i<local.size();++i) {
        const auto p=source.mesh.vertices[positive+static_cast<unsigned>(i)];
        source.mesh.vertices.push_back({-p.x,p.y,p.z});
        source.originalPairs.emplace_back(positive+i,negative+i);
    }
    const auto mirroredFace=[&](unsigned a,unsigned b,unsigned c) {
        source.mesh.triangles.push_back({positive+a,positive+b,positive+c});
        source.mesh.triangles.push_back({negative+a,negative+c,negative+b});
    };
    for(const auto t:outer)mirroredFace(t[0],t[1],t[2]);
    // An inner negatively oriented closed tetra in each positive outer
    // octahedron makes a genuine zero-winding cavity bounded by a wall.
    for(const auto t:tetra)
        mirroredFace(6+t[0],6+t[2],6+t[1]);
}

void testM37ScopedSamePatchUnionsWithProtectedCavity() {
    Mesh dummy;
    std::string last;
    bool completed=false;
    for(const double separation:{5.0,5.5,6.0,6.5}) {
        if(completed)break;
        for(const double gap:{.15,.25,.35}) {
            if(completed)break;
            auto source=makeM11Towers(3,gap);
            const double shift=(-14+separation)-14;
            for(std::size_t i=16;i<32;++i)
                source.mesh.vertices[i].y+=shift;
            for(auto& area:source.input.at(1))
                area.center.y+=shift;
            addM37ProtectedMirroredCavity(source);
            const auto initial=checkpointM11Source(source);
            const auto root=initial.index;
            const auto originalFaces=firstOriginalFaces(source.mesh);
            require(originalFaces.size()==72,
                    "M37 three reflected tower bands plus both outer/cavity sources are complete");
            require(root->triangleIntersections({},{}).empty(),
                    "M37 nested inward cavity is disjoint from all original face intersections");
            for(const double x:{5.0,-5.0}) {
                const auto voidPoint=root->positiveWindingAt({x,80,0});
                const auto materialPoint=root->positiveWindingAt({
                    x+(x>0?1.5:-1.5),80,0});
                const auto exteriorPoint=root->positiveWindingAt({
                    x+(x>0?4.0:-4.0),80,0});
                require(voidPoint.reliable&&materialPoint.reliable&&
                        exteriorPoint.reliable&&
                        voidPoint.winding==0&&
                        materialPoint.winding==1&&exteriorPoint.winding==0,
                        "M37 inward reversed cavity has actual positive signed-winding material shell");
            }
            const auto a=root->querySphere(source.input[0][0].center,6);
            const auto b=root->querySphere(source.input[1][0].center,6);
            bool overlap=false;
            for(const auto id:a)
                overlap|=std::find(b.begin(),b.end(),id)!=b.end();
            require(overlap,
                    "M37 two physical original patches overlap on a shared live source VertexId");
            // Only the first TWO bands (32 FaceIds) may deliberately move
            // or be certified Buried/Outside; the third far exterior band
            // and all 24 original inward-cavity/outer faces stay protected.
            const auto intent=m11RegionIntent(source,1);
            require(intent.size()==32&&intent.back()==originalFaces[31],
                    "M37 original caller scope is strictly narrower than first-root sources");
            for(const double strength:{.35,.55,.8,1.0,1.3}) {
                VersionedSculptBrush brush(initial,strength,
                    {},{},{},256,{},m11PhysicalExactSettings());
                brush.setOriginalPatchIntent(intent);
                brush.stamp(dummy,source.input[0]);
                if(brush.completedUnions()!=1) {
                    last="M37 scoped first physical union: "+brush.lastRejection();
                    continue;
                }
                const auto firstJoined=brush.checkpoint();
                brush.setOriginalPatchIntent(intent);
                brush.stamp(dummy,source.input[1]);
                if(brush.completedUnions()!=2) {
                    last="M37 scoped second physical union: "+brush.lastRejection();
                    continue;
                }
                const auto accepted=brush.checkpoint();
                const auto final=accepted.index;
                require(accepted.verifiedOriginalFidelity&&
                        accepted.verifiedOriginalFidelity->anchors.original==root&&
                        firstJoined.verifiedOriginalFidelity&&
                        firstJoined.verifiedOriginalFidelity->anchors.original==root&&
                        final!=firstJoined.index&&
                        final->retiredFaceCount()>firstJoined.index->retiredFaceCount()&&
                        firstJoined.index->retiredFaceCount()>root->retiredFaceCount()&&
                        brush.publishedRevisions()==2&&
                        brush.historyStatistics().undoDepth==2&&
                        brush.rejectedStamps()==0&&
                        !brush.lastDirtyPartitions().empty()&&
                        brush.contactStatistics().contactChecks==2&&
                        brush.contactStatistics().active,
                        "M37 two same-local REAL contact unions preserve first-origin proofs while protected walls remain unscoped");
                const auto protectedAudit=auditOriginalPatchLifetime(
                    *accepted.verifiedOriginalFidelity,root);
                require(protectedAudit.originalSources==originalFaces.size()&&
                        protectedAudit.total.distanceTests>=7*40,
                        "M37 protected original FaceIds remain in complete first-reference descendant witness audit");
                for(std::size_t i=32;i<originalFaces.size();++i) {
                    const auto fid=originalFaces[i];
                    const auto before=root->maybeFace(fid);
                    const auto after=final->maybeFace(fid);
                    require(before&&after&&before->vertices==after->vertices,
                            "M37 protected exterior and reversed cavity wall FaceIds retain exact source ownership");
                    for(const auto vid:before->vertices)
                        { const auto p=root->vertex(vid).position;
                          const auto q=final->vertex(vid).position;
                          require(p.x==q.x&&p.y==q.y&&p.z==q.z,
                                  "M37 protected first-original vertices never moved or changed identity"); }
                }
                for(const auto& [u,v]:accepted.mirrorPairs) {
                    require(final->maybeVertex(u)&&final->maybeVertex(v),
                            "M37 all original and newly certified reflected orbits remain live");
                    const auto p=final->vertex(u).position;
                    const auto q=final->vertex(v).position;
                    require(p.x==-q.x&&p.y==q.y&&p.z==q.z,
                            "M37 certified mirror ids have exact reflection coordinates");
                }
                // An explicitly UNSCOPED exterior wall deformation is not
                // made safe by proximity to the previous intended region.
                const auto history=brush.historyStatistics();
                const auto picker=final->pickingRootIdentity();
                const auto rejectCount=brush.rejectedStamps();
                const std::array<BrushArea,2> wall{{
                    {{8,80,0},2,.95},{{-8,80,0},2,.95}
                }};
                brush.setOriginalPatchIntent(intent);
                brush.stamp(dummy,wall);
                const auto refused=brush.checkpoint();
                require(brush.rejectedStamps()==rejectCount+1&&
                        refused.index==final&&
                        refused.verifiedOriginalFidelity==
                            accepted.verifiedOriginalFidelity&&
                        refused.mirrorPairs==accepted.mirrorPairs&&
                        brush.historyStatistics().undoDepth==history.undoDepth&&
                        brush.historyStatistics().redoDepth==history.redoDepth&&
                        brush.publishedRevisions()==2&&
                        brush.lastDirtyPartitions().empty()&&
                        final->pickingRootIdentity()==picker,
                        "M37 unscoped mirrored outer wall motion refuses before CAS with exact root, source, mirror, history and dirty rollback");

                // The inward reversed-cavity surface is separately
                // protected, not merely present as a passive bystander
                // while an unrelated outer-wall stroke is refused.
                const std::array<BrushArea,2> cavity{{
                    {{5.7,80,0},1.2,.95},{{-5.7,80,0},1.2,.95}
                }};
                const auto secondRejectCount=brush.rejectedStamps();
                brush.setOriginalPatchIntent(intent);
                brush.stamp(dummy,cavity);
                const auto cavityRefusal=brush.checkpoint();
                require(brush.rejectedStamps()==secondRejectCount+1&&
                        cavityRefusal.index==final&&
                        cavityRefusal.verifiedOriginalFidelity==
                            accepted.verifiedOriginalFidelity&&
                        cavityRefusal.mirrorPairs==accepted.mirrorPairs&&
                        brush.historyStatistics().undoDepth==history.undoDepth&&
                        brush.historyStatistics().redoDepth==history.redoDepth&&
                        brush.publishedRevisions()==2&&
                        brush.lastDirtyPartitions().empty(),
                        "M37 independently unscoped reversed-cavity wall motion cannot mutate COW/source/mirrors/dirty or history");

                require(brush.undo()&&brush.snapshot()==firstJoined.index&&
                        brush.undo()&&brush.snapshot()==root&&
                        brush.redo()&&brush.snapshot()==firstJoined.index&&
                        brush.redo()&&brush.snapshot()==final,
                        "M37 separately accepted protected same-patch original-root histories undo/redo exactly");
                std::cout<<"[m37] scoped true local two-union protected cavity, separation="
                         <<separation<<" gap="<<gap
                         <<" strength="<<strength<<'\n';
                completed=true;
                break;
            }
        }
    }
    if(!completed)
        throw std::runtime_error(
            "M37 could not find supported TWO real scoped local physical unions with protected cavity: "+last);
}

// M38: use the actual two certified M37 same-local physical joins as the
// trusted predecessor of a new protected normal-driven stamp. Retain the
// sealed FIRST original, not a fabricated or recovered last-root reanchor.
struct M38ProtectedLocalFixture {
    DurableCheckpoint terminal;
    std::shared_ptr<const RegionTopologyIndex> firstOriginal;
    std::shared_ptr<const RegionTopologyIndex> afterFirstUnion;
    std::vector<FaceId> deliberate;
    std::vector<FaceId> protectedSources;
    std::size_t originalFaceCount{};
};
M38ProtectedLocalFixture makeM38ProtectedLocalFixture() {
    Mesh dummy;
    std::string last;
    for(const double separation:{5.0,5.5,6.0,6.5}) {
        for(const double gap:{.15,.25,.35}) {
            auto source=makeM11Towers(3,gap);
            const double shift=(-14+separation)-14;
            for(std::size_t i=16;i<32;++i)
                source.mesh.vertices[i].y+=shift;
            for(auto& area:source.input.at(1))
                area.center.y+=shift;
            addM37ProtectedMirroredCavity(source);
            const auto starting=checkpointM11Source(source);
            const auto ids=firstOriginalFaces(source.mesh);
            require(ids.size()==72,
                    "M38 keeps the authenticated 72-original-FaceId M37 source");
            const auto intended=m11RegionIntent(source,1);
            for(const double strength:{.35,.55,.8,1.0,1.3}) {
                VersionedSculptBrush physical(starting,strength,
                    {},{},{},256,{},m11PhysicalExactSettings());
                physical.setOriginalPatchIntent(intended);
                physical.stamp(dummy,source.input[0]);
                if(physical.completedUnions()!=1) {
                    last=physical.lastRejection();
                    continue;
                }
                const auto firstJoined=physical.snapshot();
                physical.setOriginalPatchIntent(intended);
                physical.stamp(dummy,source.input[1]);
                if(physical.completedUnions()!=2) {
                    last=physical.lastRejection();
                    continue;
                }
                const auto final=physical.checkpoint();
                require(final.index!=firstJoined&&
                        final.verifiedOriginalFidelity&&
                        final.verifiedOriginalFidelity->anchors.original==
                            starting.index&&
                        final.verifiedOriginalFidelity->lineage.terminal==
                            final.index&&
                        physical.historyStatistics().undoDepth==2&&
                        physical.rejectedStamps()==0,
                        "M38 predecessor contains TWO genuine M37 full physical exact unions");
                return {final,starting.index,firstJoined,intended,
                    {ids.begin()+32,ids.end()},ids.size()};
            }
        }
    }
    throw std::runtime_error(
        "M38 failed to reproduce two M37 source-certified local physical joins: "+last);
}

void testM38CompletePostD2WorkAndCumulativeProtectedDrift() {
    const auto joined=makeM38ProtectedLocalFixture();
    const auto joinedRoot=joined.terminal.index;
    const auto& lifetime=*joined.terminal.verifiedOriginalFidelity;
    const auto firstRoot=joined.firstOriginal;
    require(firstRoot==lifetime.anchors.original&&
            lifetime.lineage.original==firstRoot&&
            lifetime.lineage.terminal==joinedRoot&&
            joined.protectedSources.size()==40,
            "M38 cannot drift from any last-root-rebased or fabricated first source");
    const auto audited=auditOriginalPatchLifetime(lifetime,firstRoot);
    std::size_t liveChildren=0;
    for(const auto& [_,children]:lifetime.lineage.living)
        liveChildren+=children.size();
    require(audited.total.distanceTests==7*liveChildren&&
            audited.total.distanceTests>7*joined.originalFaceCount&&
            audited.originalSources==joined.originalFaceCount,
            "M38 real post-d2 TWO-union root audits seven immutable witnesses for EACH source-owned living descendant");
    std::size_t accounted=0;
    for(const auto& record:audited.perOriginal) {
        const auto face=std::find_if(lifetime.anchors.faces.begin(),
            lifetime.anchors.faces.end(),
            [&](const OriginalSourceFaceAnchor& fixed) {
                return fixed.face==record.original;
            });
        require(face!=lifetime.anchors.faces.end()&&
                face->originalEdgeLength>0&&
                record.sampled.witnessCount==7&&
                record.sampled.distanceTests==7*record.livingDescendants,
                "M38 full descendant work retains each own first-original seven XYZ samples and nonzero edge unit");
        accounted+=record.sampled.distanceTests;
    }
    require(accounted==audited.total.distanceTests,
            "M38 no unrelated XYZ or retired source is credited as preserved witness work");
    bool auditDenied=false;
    try {
        (void)auditOriginalPatchLifetime(
            lifetime,firstRoot,audited.total.distanceTests-1);
    }catch(const std::length_error&){auditDenied=true;}
    require(auditDenied,
            "M38 one-under post-d2 authenticated living-descendant source audit fails BEFORE partial witness classification");
    const auto exactly=auditOriginalPatchLifetime(
        lifetime,firstRoot,audited.total.distanceTests);
    require(exactly.total.distanceTests==audited.total.distanceTests,
            "M38 EXACT full original witness work cap succeeds");

    // Obtain both cumulative measurements from independently certified
    // private real Inflate/refine/collapse stages, as M34 did. These
    // measurements are NOT injected into the live publication path:
    // the normal-driven VersionedSculptBrush must enforce the selected
    // tolerance itself before its own root CAS and M29 history append.
    constexpr double driftStrength=.035;
    const std::array<BrushArea,2> wall{{
        {{8,80,0},.8,.3},{{-8,80,0},.8,.3}
    }};
    VersionedInflateBrush inflator(driftStrength,{});
    VersionedLocalRefiner refiner(joined.terminal.initialVertexCount,
                                  {},{},256);
    VersionedDirectionalCollapser collapser(
        joined.terminal.initialVertexCount,{}, {},256);
    const auto predict=[&](const OriginalPatchLifetime& previous) {
        const auto moved=inflator.stamp(previous.lineage.terminal,wall,256);
        require(moved.moved>0,
                "M38 drift proposal must physically move source-owned vertices");
        const auto refined=refiner.apply(moved.index,moved.baseline);
        const auto collapsed=collapser.apply(refined.index,moved.baseline);
        return advanceCertifiedPhysicalStampLifetime(
            previous,moved,refined,collapsed,nullptr,nullptr,
            16384,16384,16384,32768);
    };
    const auto predictedFirst=predict(lifetime);
    const auto predictedSecond=predict(predictedFirst);
    const auto protectedWorst=[&](const OriginalPatchLifetime& state) {
        const auto audit=auditOriginalPatchLifetime(state,firstRoot);
        double worst=0;
        for(const auto& record:audit.perOriginal)
            if(std::find(joined.protectedSources.begin(),
                         joined.protectedSources.end(),record.original)!=
                         joined.protectedSources.end())
                worst=std::max(worst,
                                record.sampled.maximumDistanceInEdges);
        return worst;
    };
    const auto firstDrift=protectedWorst(predictedFirst);
    const auto secondDrift=protectedWorst(predictedSecond);
    require(firstDrift>0&&secondDrift>firstDrift+1e-8,
            "M38 genuine second protected outer-wall physical movement increases cumulative FIRST-original edge-scaled drift");
    const double tolerance=.5*(firstDrift+secondDrift);
    const auto firstWork=auditOriginalPatchLifetime(
        predictedFirst,firstRoot).total.distanceTests;
    const auto secondWork=auditOriginalPatchLifetime(
        predictedSecond,firstRoot).total.distanceTests;
    require(secondWork<=firstWork,
            "M38 second refusal must not be caused by source work cap growth; measured complete second witness work fits the first accepted bound");
    require(firstWork>0&&firstWork<=131072&&
            firstWork==7*([&]{
                std::size_t count=0;
                for(const auto& [_,children]:predictedFirst.lineage.living)
                    count+=children.size();
                return count;
            }()),
            "M38 measured terminal proof cap includes every post-d2 descendant, not just old unsplit original triangles");

    auto policy=m11PhysicalExactSettings();
    policy.originalTerminal.maximumProtectedDeviationInEdges=tolerance;
    policy.originalTerminal.maxDistanceTests=firstWork;
    const auto sealedBefore=joined.terminal;
    const auto picker=joinedRoot->pickingRootIdentity();
    Mesh dummy;

    // Exact work-cap publication, against the genuine post-two-Boolean
    // first-root certificate rather than a standalone octahedron.
    VersionedSculptBrush permitted(sealedBefore,driftStrength,
                                   {},{},{},256,{},policy);
    permitted.setOriginalPatchIntent(joined.deliberate);
    permitted.stamp(dummy,wall);
    if(permitted.publishedRevisions()!=1)
        throw std::runtime_error(
            "M38 exact-cap protected physical successor refused: "+
            permitted.lastRejection());
    const auto accepted=permitted.checkpoint();
    require(accepted.index!=joinedRoot&&
            accepted.verifiedOriginalFidelity&&
            accepted.verifiedOriginalFidelity->anchors.original==firstRoot&&
            accepted.verifiedOriginalFidelity->lineage.terminal==
                accepted.index&&
            accepted.verifiedOriginalFidelity->acceptedTransitions==
                lifetime.acceptedTransitions+1&&
            permitted.historyStatistics().undoDepth==1&&
            permitted.rejectedStamps()==0&&
            !permitted.lastDirtyPartitions().empty()&&
            joinedRoot->pickingRootIdentity()==picker,
            "M38 EXACT measured post-d2 source work authorizes one real protected physical terminal CAS and M29 history");
    const auto liveFirstAudit=auditOriginalPatchLifetime(
        *accepted.verifiedOriginalFidelity,firstRoot,firstWork);
    require(liveFirstAudit.total.distanceTests==firstWork&&
            std::abs(protectedWorst(*accepted.verifiedOriginalFidelity)-
                     firstDrift)<1e-8,
            "M38 live accepted physically produced terminal matches measured drift and EXACT pre-CAS witness work");
    verifyOriginalPatchPublicationTransition(
        lifetime,*accepted.verifiedOriginalFidelity,
        joinedRoot,accepted.index);
    for(const auto& [a,b]:accepted.mirrorPairs) {
        const auto p=accepted.index->vertex(a).position;
        const auto q=accepted.index->vertex(b).position;
        require(p.x==-q.x&&p.y==q.y&&p.z==q.z,
                "M38 accepted physical source retains complete current reciprocal stable mirror identities");
    }

    auto tooLittle=policy;
    tooLittle.originalTerminal.maxDistanceTests=firstWork-1;
    VersionedSculptBrush overBudget(sealedBefore,driftStrength,
                                    {},{},{},256,{},tooLittle);
    overBudget.setOriginalPatchIntent(joined.deliberate);
    const auto beforeLow=overBudget.checkpoint();
    overBudget.stamp(dummy,wall);
    const auto afterLow=overBudget.checkpoint();
    require(overBudget.rejectedStamps()==1&&
            overBudget.publishedRevisions()==0&&
            overBudget.historyStatistics().undoDepth==0&&
            overBudget.lastDirtyPartitions().empty()&&
            afterLow.index==beforeLow.index&&
            afterLow.verifiedOriginalFidelity==
                beforeLow.verifiedOriginalFidelity&&
            afterLow.mirrorPairs==beforeLow.mirrorPairs&&
            overBudget.lastRejection().find(
                "First-original witness audit distance budget exhausted")!=
                    std::string::npos,
            "M38 ONE-UNDER same physical post-d2 witness work, not unrelated collision or geometry, refuses before CAS");

    const auto beforeSecond=accepted;
    const auto priorHistory=permitted.historyStatistics();
    permitted.setOriginalPatchIntent(joined.deliberate);
    permitted.stamp(dummy,wall);
    const auto refused=permitted.checkpoint();
    require(permitted.rejectedStamps()==1&&
            permitted.publishedRevisions()==1&&
            permitted.historyStatistics().undoDepth==priorHistory.undoDepth&&
            permitted.historyStatistics().redoDepth==priorHistory.redoDepth&&
            refused.index==beforeSecond.index&&
            refused.verifiedOriginalFidelity==
                beforeSecond.verifiedOriginalFidelity&&
            refused.mirrorPairs==beforeSecond.mirrorPairs&&
            permitted.lastDirtyPartitions().empty()&&
            permitted.lastRejection().find(
                "Unintended exposed/reversed-cavity first-original surface drift")!=
                    std::string::npos&&
            firstRoot->pickingRootIdentity()!=nullptr&&
            joinedRoot->pickingRootIdentity()==picker,
            "M38 SECOND individually small but cumulatively excessive protected original drift refuses without COW/source/history/mirrors/dirty change");

    // V13 disk marker is durable; opaque original provenance is not
    // reconstructed from terminal XYZ or from a coalesced latest root.
    const auto file=std::filesystem::temp_directory_path()/
        "mesher-v17-m38-protected-local-source.mshr";
    try {
        writeCheckpoint(file,refused);
        const auto disk=readCheckpoint(file);
        require(disk.originalFidelityRequired&&
                !disk.verifiedOriginalFidelity&&
                disk.index&&
                disk.index->retiredFaceIds()==beforeSecond.index->retiredFaceIds()&&
                disk.mirrorPairs==beforeSecond.mirrorPairs,
                "M38 terminal V13 checkpoint retains root/mirrors/retirements but never fabricates opaque first-source authority");
        bool rejectedDiskOnly=false;
        try {
            (void)VersionedSculptBrush(disk,driftStrength,
                                       {},{},{},256,{},policy);
        }catch(const std::logic_error&){rejectedDiskOnly=true;}
        require(rejectedDiskOnly,
                "M38 protected disk-only V13 restart fails closed without separately trusted original provenance");
    }catch(...) {
        std::error_code error;std::filesystem::remove(file,error);throw;
    }
    std::error_code error;std::filesystem::remove(file,error);
    require(permitted.undo()&&permitted.snapshot()==joinedRoot&&
            permitted.checkpoint().verifiedOriginalFidelity==
                sealedBefore.verifiedOriginalFidelity&&
            permitted.redo()&&permitted.snapshot()==accepted.index&&
            permitted.checkpoint().verifiedOriginalFidelity==
                accepted.verifiedOriginalFidelity,
            "M38 exact original-root post-d2 successor and COW source lifetime undo/redo as matching pointers");
    std::cout<<"[m38] 2 authentic local unions; post-d2 work="
             <<audited.total.distanceTests<<" first successor work="
             <<firstWork<<" first edge drift="<<firstDrift
             <<" second edge drift="<<secondDrift
             <<" tolerance="<<tolerance<<'\n';
}

// M39 independently selects a bounded, real 4D-equivalent stimulus on the
// TRUE M37 overlapping local source. Selection uses real normal-driven
// stamps, never manufactured Boolean contacts or tracker observations.
struct M39FourDimensionalSource {
    M11TowerSource source;
    DurableCheckpoint original;
    std::vector<FaceId> deliberate;
    double strength{};
    Vec3 exterior{};
};
Vec3 m39LiveExterior(const RegionTopologyIndex& index) {
    Vec3 exterior{};
    bool selected=false;
    index.revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& vertex:part.vertices) {
                const auto v=vertex.position;
                if(v.x<=0||v.y>=-12||std::abs(v.z)>2)continue;
                if(!selected||v.x<exterior.x) {
                    exterior=v;
                    selected=true;
                }
            }
        });
    require(selected,
            "M39 genuine joined root retains a live first-region exterior corner (not retired import XYZ)");
    return exterior;
}
M39FourDimensionalSource chooseM39FourDimensionalSource() {
    Mesh dummy;
    std::string last;
    // The 4D worker Begin supplies TWO reflected BrushAreas at .3 pressure
    // rather than M37's direct .95 test pressure. Search complete actual
    // physical Steps with this EXACT input before running the real FIFO.
    for(const double separation:{5.0,5.5,6.0,6.5}) {
        for(const double gap:{.15,.25,.35}) {
            auto source=makeM11Towers(3,gap);
            const double shift=(-14+separation)-14;
            for(std::size_t i=16;i<32;++i)
                source.mesh.vertices[i].y+=shift;
            for(auto& area:source.input.at(1))
                area.center.y+=shift;
            addM37ProtectedMirroredCavity(source);
            const auto original=checkpointM11Source(source);
            require(original.index->triangleIntersections({},{}).empty(),
                    "M39 M37 source has no initial interpenetrating triangles");
            const auto a=original.index->querySphere(source.input[0][0].center,6);
            const auto b=original.index->querySphere(source.input[1][0].center,6);
            bool common=false;
            for(const auto id:a)
                if(std::find(b.begin(),b.end(),id)!=b.end())common=true;
            require(common,
                    "M39 both proposed actual 4D contacts share original live brush-support IDs");
            const auto intended=m11RegionIntent(source,1);
            const auto originalFaces=firstOriginalFaces(source.mesh);
            require(originalFaces.size()==72&&intended.size()==32,
                    "M39 32 scoped sources leave 40 protected cavity, outer wall and unmodified tower source faces");
            const auto areaAt=[](Vec3 p,double radius) {
                return std::array<BrushArea,2>{{
                    {p,radius,.3},{{-p.x,p.y,p.z},radius,.3}
                }};
            };
            for(const double strength:{.35,.55,.8,1.0,1.3,1.6,2.0}) {
                VersionedSculptBrush trial(original,strength,{},{},{},256,{},
                                           m11PhysicalExactSettings());
                trial.setOriginalPatchIntent(intended);
                const auto firstArea=areaAt(source.input[0][0].center,6);
                trial.stamp(dummy,firstArea);
                if(trial.completedUnions()!=1) {
                    last="initial 4D-equivalent Contact: "+trial.lastRejection();
                    continue;
                }
                const auto contactRoot=trial.snapshot();
                const Vec3 exterior=m39LiveExterior(*contactRoot);
                const auto cleanArea=areaAt(exterior,.5);
                bool cleanAccepted=true;
                for(std::size_t i=0;i<5;++i) {
                    const auto old=trial.snapshot();
                    trial.setOriginalPatchIntent(intended);
                    trial.stamp(dummy,cleanArea);
                    if(trial.snapshot()==old||trial.rejectedStamps()!=0||
                       trial.contactStatistics().cleanChecks!=i+1||
                       trial.completedUnions()!=1) {
                        cleanAccepted=false;
                        last="intermediate 4D-equivalent Clean: "+
                            trial.lastRejection();
                        break;
                    }
                }
                if(!cleanAccepted)continue;
                if(trial.contactStatistics().closedEpisodes!=1||
                   trial.contactStatistics().episodes!=1) {
                    last="five direct clean classifications cannot close first episode";
                    continue;
                }
                trial.setOriginalPatchIntent(intended);
                const auto secondArea=areaAt(source.input[1][0].center,6);
                trial.stamp(dummy,secondArea);
                if(trial.completedUnions()!=2||
                   trial.contactStatistics().episodes!=2||
                   trial.contactStatistics().active!=true) {
                    last="later local 4D-equivalent recontact: "+
                        trial.lastRejection();
                    continue;
                }
                const auto afterSecond=trial.snapshot();
                // A later correctly scoped FIFO command must truly
                // publish one new source-certified clean successor.
                const auto afterExterior=m39LiveExterior(*afterSecond);
                trial.setOriginalPatchIntent(intended);
                trial.stamp(dummy,areaAt(afterExterior,.5));
                if(trial.snapshot()==afterSecond||
                   trial.rejectedStamps()!=0||
                   trial.contactStatistics().cleanChecks!=6||
                   trial.completedUnions()!=2) {
                    last="late scoped clean recovery: "+trial.lastRejection();
                    continue;
                }
                return {std::move(source),original,intended,strength,exterior};
            }
        }
    }
    throw std::runtime_error(
        "M39 no bounded REAL two-contact local source supports complete 4D Clean/closure/recontact/recovery: "+last);
}

void testM39ProtectedOverlappingPatchFourDContactEpisodesAndFIFO() {
    const auto selected=chooseM39FourDimensionalSource();
    const auto& source=selected.source;
    const auto starting=selected.original;
    const auto original=starting.index;
    const auto oldPicker=original->pickingRootIdentity();
    const auto sourceFaces=firstOriginalFaces(source.mesh);
    const auto settings=m11PhysicalExactSettings();
    AsyncSculptWorker worker(starting,.025,selected.strength,.2,
                             {},{},{},{},settings);
    std::vector<std::shared_ptr<const RegionTopologyIndex>> roots{original};
    const auto stroke=[&](Vec3 p,double radius,std::vector<FaceId> intended) {
        require(worker.canBegin(),
                "M39 producer never overlaps an incomplete 4D Begin/End reservation");
        worker.begin(p,radius,std::move(intended));
        worker.end(p,0);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
        while(!worker.canBegin()&&std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto status=worker.status();
        require(worker.canBegin()&&!status.active&&
                status.pendingSteps==0&&status.pendingSegments==0&&
                status.inputCommands==0,
                "M39 complete worker Begin/End drains all 4D input and publishes before producer reservation releases");
        return status;
    };
    const auto accept=[&](const AsyncSculptWorker::Status& state,
                          const char* label) {
        const auto published=worker.takeSnapshot();
        require(published&&published->index&&
                published->index!=roots.back()&&
                !published->dirtyPartitions.empty()&&
                state.history.undoDepth==roots.size()&&
                state.publishedRevisions==roots.size()&&
                state.completedStamps==
                    state.publishedRevisions+state.rejectedStamps,
                label);
        roots.push_back(published->index);
        require(original->pickingRootIdentity()==oldPicker,
                "M39 no accepted 4D stamp modifies retained first-original renderer/picker BVH");
        return published->index;
    };
    const auto inspectCheckpoint=[&]() {
        // A V13 disk snapshot is NOT a source certificate, but it must
        // preserve the exact mirror/retirement map under refused commands.
        const auto path=std::filesystem::temp_directory_path()/
            "mesher-v17-m39-fifo-refusal-probe.mshr";
        const auto previous=worker.checkpointStatistics();
        try {
            worker.saveCheckpoint(path);
            const auto stop=std::chrono::steady_clock::now()+
                            std::chrono::seconds(25);
            while(worker.checkpointStatistics().completed==previous.completed&&
                  worker.checkpointStatistics().failed==previous.failed&&
                  std::chrono::steady_clock::now()<stop)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const auto now=worker.checkpointStatistics();
            require(now.completed==previous.completed+1&&
                    now.failed==previous.failed,
                    "M39 FIFO durable provenance probe finishes after actual command queue drain");
            auto loaded=readCheckpoint(path);
            require(loaded.index&&loaded.originalFidelityRequired&&
                    !loaded.verifiedOriginalFidelity&&
                    loaded.mirrorPairs.size()>=starting.mirrorPairs.size(),
                    "M39 live protected 4D root saves proper V13 disk provenance without forged first root");
            std::error_code ec;std::filesystem::remove(path,ec);
            return loaded;
        }catch(...) {
            std::error_code ec;std::filesystem::remove(path,ec);throw;
        }
    };
    const auto initialContact=stroke(source.input[0][0].center,6,
                                     selected.deliberate);
    if(initialContact.completedUnions!=1)
        throw std::runtime_error("M39 FIRST overlapping physical FIFO union: "+
                                 initialContact.lastRejection);
    require(initialContact.contact.episodes==1&&
            initialContact.contact.contactChecks==1&&
            initialContact.contact.closedEpisodes==0&&
            initialContact.contact.active&&
            initialContact.history.undoDepth==1&&
            initialContact.rejectedStamps==0,
            "M39 genuine same-local 4D first certified c1/c2/c3/M03/d2 physical union opens Contact episode");
    const auto firstUnion=accept(initialContact,
                                "M39 initial Contact publishes only full first original-root terminal");
    require(firstUnion->retiredFaceCount()>original->retiredFaceCount(),
            "M39 first true local physical union permanently retires certified original FaceIds");
    const auto clean=[&](Vec3 p,std::size_t count,std::size_t run,
                         bool active) {
        const auto state=stroke(p,.5,selected.deliberate);
        if(state.contact.cleanChecks!=count||
           state.contact.quietRun!=run||
           state.contact.active!=active||
           state.contact.episodes!=(count>5?2u:1u)||
           state.contact.closedEpisodes!=(active&&count<=5?0u:1u)||
           state.completedUnions!=(count>5?2u:1u)||
           state.rejectedStamps!=(count>5?2u:(count>2?1u:0u)))
            throw std::runtime_error(
                "M39 real Clean misclassified: count="+std::to_string(count)+
                " quiet="+std::to_string(state.contact.quietRun)+
                " active="+std::to_string(state.contact.active)+
                " contact="+std::to_string(state.contact.contactChecks)+
                " rejected="+std::to_string(state.rejectedStamps)+
                " last="+state.lastRejection);
        (void)accept(state,
            "M39 independent V13 conclusive accepted Clean 4D stamp publishes one dirty terminal M29 root");
    };
    clean(selected.exterior,1,1,true);
    clean(selected.exterior,2,2,true);

    // Refuse an explicitly foreign FIRST-original FaceId: no speculative
    // contact or synthetic Clean may consume two quiet credits.
    const auto beforeInvalid=worker.status();
    const auto heldBeforeInvalid=roots.back();
    const auto storedBeforeInvalid=inspectCheckpoint();
    const auto invalid=stroke(selected.exterior,.28,{FaceId{99999999}});
    require(invalid.rejectedStamps==beforeInvalid.rejectedStamps+1&&
            invalid.contact.uncertainChecks==
                beforeInvalid.contact.uncertainChecks+1&&
            invalid.contact.quietRun==0&&invalid.contact.active&&
            invalid.contact.closedEpisodes==0&&
            invalid.contact.cleanChecks==beforeInvalid.contact.cleanChecks&&
            invalid.contact.episodes==1&&
            invalid.publishedRevisions==beforeInvalid.publishedRevisions&&
            invalid.history.undoDepth==beforeInvalid.history.undoDepth&&
            invalid.lastRejection.find("original")!=std::string::npos,
            "M39 REAL invalid caller FaceId Unknown interrupts two Clean without closing Contact or publishing");
    const auto noChange=worker.takeSnapshot();
    require(noChange&&noChange->index==heldBeforeInvalid&&
            noChange->dirtyPartitions.empty()&&
            worker.initialRevision()==heldBeforeInvalid&&
            original->pickingRootIdentity()==oldPicker,
            "M39 refused 4D source request cannot expose a partial dirty renderer or altered old root");
    const auto storedAfterInvalid=inspectCheckpoint();
    require(storedAfterInvalid.mirrorPairs==storedBeforeInvalid.mirrorPairs&&
            storedAfterInvalid.index->retiredFaceIds()==
                storedBeforeInvalid.index->retiredFaceIds()&&
            storedAfterInvalid.index->retiredVertexIds()==
                storedBeforeInvalid.index->retiredVertexIds()&&
            storedAfterInvalid.originalFidelityRequired==
                storedBeforeInvalid.originalFidelityRequired,
            "M39 first Unknown preserves EXACT durable reflected mirror, retired FaceId/VertexId and protected provenance marker");
    clean(selected.exterior,3,1,true);
    clean(selected.exterior,4,2,true);
    clean(selected.exterior,5,0,false);
    const auto closed=worker.status();
    require(closed.contact.closedEpisodes==1&&
            closed.contact.episodes==1&&
            closed.contact.uncertainChecks==1&&
            closed.contact.contactChecks==1,
            "M39 precisely THREE new accepted real Clean after Unknown end one complete Contact episode");

    // The second real physical source union must be IN THE SAME
    // originally overlapping brush patch after all five Clean stamps.
    const auto beforeSecond=roots.back();
    const auto second=stroke(source.input[1][0].center,6,selected.deliberate);
    if(second.completedUnions!=2)
        throw std::runtime_error(
            "M39 genuine overlapped later 4D recontact did not certify SECOND physical Boolean union: "+
            second.lastRejection);
    require(second.contact.contactChecks==2&&
            second.contact.episodes==2&&
            second.contact.closedEpisodes==1&&
            second.contact.active&&second.contact.quietRun==0&&
            second.history.undoDepth==roots.size()&&
            second.rejectedStamps==1,
            "M39 local actual 4D second independent certified source union reopens episode after real three-Clean closure");
    const auto afterSecond=accept(second,
        "M39 overlapping second union publishes exactly one new terminal with dirty partitions");
    require(afterSecond->retiredFaceCount()>beforeSecond->retiredFaceCount(),
            "M39 second actual physical contact owns NEW certified source retirement in same brush support");

    // Another refused asynchronous command after recontact must not
    // poison a LATER correctly scoped physical step in the FIFO.
    const auto preRecovery=worker.status();
    const auto storedBeforeRecoveryRefusal=inspectCheckpoint();
    const auto refused=stroke(m39LiveExterior(*afterSecond),.28,
                              {FaceId{99999998}});
    require(refused.rejectedStamps==preRecovery.rejectedStamps+1&&
            refused.contact.uncertainChecks==
                preRecovery.contact.uncertainChecks+1&&
            refused.contact.active&&refused.contact.episodes==2&&
            refused.publishedRevisions==preRecovery.publishedRevisions&&
            refused.history.undoDepth==preRecovery.history.undoDepth,
            "M39 late Unknown after second physical union is a recoverable FIFO refusal");
    const auto afterRefusal=worker.takeSnapshot();
    require(afterRefusal&&afterRefusal->index==afterSecond&&
            afterRefusal->dirtyPartitions.empty(),
            "M39 late refused FIFO input retains exact old terminal and zero new dirty owners");
    const auto storedAfterRecoveryRefusal=inspectCheckpoint();
    require(storedAfterRecoveryRefusal.mirrorPairs==
                storedBeforeRecoveryRefusal.mirrorPairs&&
            storedAfterRecoveryRefusal.index->retiredFaceIds()==
                storedBeforeRecoveryRefusal.index->retiredFaceIds()&&
            storedAfterRecoveryRefusal.index->retiredVertexIds()==
                storedBeforeRecoveryRefusal.index->retiredVertexIds()&&
            storedAfterRecoveryRefusal.originalFidelityRequired==
                storedBeforeRecoveryRefusal.originalFidelityRequired,
            "M39 later refused FIFO input also leaves EXACT durable mirror, retirement and V13 source-required identity unchanged");
    const auto finalExterior=m39LiveExterior(*afterSecond);
    clean(finalExterior,6,1,true);
    const auto final=roots.back();
    require(worker.status().completedUnions==2&&
            worker.status().publishedRevisions==roots.size()-1&&
            original->pickingRootIdentity()==oldPicker,
            "M39 valid late REAL 4D clean recovery advances one scoped source-certified physical successor");

    // The terminal is still part of the ORIGINAL M37 multi-shell root.
    // All far unscoped faces and original vertices remain unchanged.
    for(std::size_t i=32;i<sourceFaces.size();++i) {
        const auto a=original->maybeFace(sourceFaces[i]);
        const auto b=final->maybeFace(sourceFaces[i]);
        require(a&&b&&a->vertices==b->vertices,
                "M39 all forty protected outer/cavity/tower source FaceIds retain original owner and incidence");
        for(const auto id:a->vertices) {
            const auto p=original->vertex(id).position;
            const auto q=final->vertex(id).position;
            require(p.x==q.x&&p.y==q.y&&p.z==q.z,
                    "M39 all protected original outer and reversed cavity wall vertices retain first-root XYZ");
        }
    }
    for(const double x:{5.0,-5.0}) {
        const auto cavity=final->positiveWindingAt({x,80,0});
        const auto shell=final->positiveWindingAt({
            x+(x>0?1.5:-1.5),80,0});
        const auto outside=final->positiveWindingAt({
            x+(x>0?4.0:-4.0),80,0});
        require(cavity.reliable&&shell.reliable&&outside.reliable&&
                cavity.winding==0&&shell.winding==1&&outside.winding==0,
                "M39 both protected reversed-cavity original shells retain signed-winding void/material/exterior after 4D local source unions");
    }
    const auto checkpointFile=std::filesystem::temp_directory_path()/
        "mesher-v17-m39-local-4d-protected-fifo.mshr";
    try {
        const auto previousCheckpoint=worker.checkpointStatistics();
        worker.saveCheckpoint(checkpointFile);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(25);
        while(worker.checkpointStatistics().completed==
                  previousCheckpoint.completed&&
              worker.checkpointStatistics().failed==
                  previousCheckpoint.failed&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto finalCheckpoint=worker.checkpointStatistics();
        require(finalCheckpoint.completed==
                    previousCheckpoint.completed+1&&
                finalCheckpoint.failed==previousCheckpoint.failed,
                "M39 actual final 4D FIFO terminal completed one MORE independent durable checkpoint write");
        const auto loaded=readCheckpoint(checkpointFile);
        require(loaded.index&&loaded.originalFidelityRequired&&
                !loaded.verifiedOriginalFidelity&&
                loaded.index->retiredFaceIds()==final->retiredFaceIds()&&
                loaded.index->retiredVertexIds()==final->retiredVertexIds()&&
                loaded.mirrorPairs.size()>=starting.mirrorPairs.size(),
                "M39 protected V13 checkpoint records final-only geometry and honest non-recoverable first-source credential");
        for(const auto& [a,b]:loaded.mirrorPairs) {
            const auto p=loaded.index->vertex(a).position;
            const auto q=loaded.index->vertex(b).position;
            require(p.x==-q.x&&p.y==q.y&&p.z==q.z,
                    "M39 full durable final reflected mirror map has live reciprocal exact XYZ");
        }
        bool refusedMissingProof=false;
        try {
            (void)VersionedSculptBrush(loaded,selected.strength,
                                      {},{},{},256,{},settings);
        }catch(const std::logic_error&){refusedMissingProof=true;}
        require(refusedMissingProof,
                "M39 protected V13 disk-only restart refuses absent opaque first-original authority");
    }catch(...) {
        std::error_code ec;std::filesystem::remove(checkpointFile,ec);throw;
    }
    std::error_code ec;std::filesystem::remove(checkpointFile,ec);

    // Rejected Unknown commands add no history entries; all eight actual
    // accepted published roots and their real rendered states are exact
    // pointer identities over the whole FIFO undo/redo chain.
    for(std::size_t n=roots.size()-1;n>0;--n) {
        worker.undo();
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(worker.status().history.undoDepth!=n-1&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto snapshot=worker.takeSnapshot();
        require(snapshot&&snapshot->index==roots[n-1]&&
                worker.status().history.redoDepth==roots.size()-n,
                "M39 actual prior immutable renderer and picker roots survive exact worker FIFO undo");
    }
    for(std::size_t n=1;n<roots.size();++n) {
        worker.redo();
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(worker.status().history.undoDepth!=n&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto snapshot=worker.takeSnapshot();
        require(snapshot&&snapshot->index==roots[n]&&
                worker.status().history.redoDepth==roots.size()-1-n,
                "M39 actual terminal source-authorized Contact/Clean history roots redo exactly");
    }
    require(roots.back()==final&&worker.initialRevision()==final,
            "M39 final accepted COW terminal remains exact after complete eight-step history restoration");
    std::cout<<"[m39] real same-overlap 4D Contact, C,C,Unknown,C,C,C,"
             <<"recontact,Unknown,scoped recovery; strength="
             <<selected.strength<<" and "<<roots.size()-1
             <<" full immutable source-backed history Steps\n";
}

// ORIGINAL M36 final integrated gate: one real 4D FIFO owns the SAME
// original protected multi-shell source and complete history throughout
// contact, permitted drift, refused cumulative erosion, conclusive Clean,
// Unknown, closure, second local source union, and late recovery.
// Unlike separately accepted M38/M39, these occur IN ONE WORKER SESSION.
void testM36OriginalCombinedProtectedSamePatchPolicy() {
    const auto selected=chooseM39FourDimensionalSource();
    const auto& source=selected.source;
    const auto original=selected.original.index;
    const auto faces=firstOriginalFaces(source.mesh);
    require(faces.size()==72&&selected.deliberate.size()==32&&
            selected.original.verifiedOriginalFidelity&&
            selected.original.verifiedOriginalFidelity->anchors.original==original,
            "M36 same actual M37 source and immutable first-authenticated 32/72 source intent");
    const auto areaAt=[](Vec3 p,double radius) {
        return std::array<BrushArea,2>{{
            {p,radius,.3},{{-p.x,p.y,p.z},radius,.3}
        }};
    };
    auto base=m11PhysicalExactSettings();
    Mesh dummy;
    VersionedSculptBrush calibrator(selected.original,selected.strength,
                                    {},{},{},256,{},base);
    calibrator.setOriginalPatchIntent(selected.deliberate);
    calibrator.stamp(dummy,areaAt(source.input[0][0].center,6));
    const auto firstContact=calibrator.checkpoint();
    require(calibrator.completedUnions()==1&&
            firstContact.verifiedOriginalFidelity&&
            firstContact.index->retiredFaceCount()>original->retiredFaceCount(),
            "M36 calibration begins with actual source-certified original-root physical contact");
    const auto& firstProof=*firstContact.verifiedOriginalFidelity;
    constexpr Vec3 protectedOuter{8,80,0};
    constexpr double protectedRadius=.8;
    const auto wall=areaAt(protectedOuter,protectedRadius);

    // Measure the actual post-first-union protected tolerance using TWO
    // certified private normal-driven physical stages. The complete
    // default M29 source proof and first-reference history are untouched.
    VersionedInflateBrush inflator(selected.strength,{});
    VersionedLocalRefiner refiner(selected.original.initialVertexCount,
                                  {},{},256);
    VersionedDirectionalCollapser collapser(
        selected.original.initialVertexCount,{},{},256);
    const auto advance=[&](const OriginalPatchLifetime& earlier) {
        const auto inflated=inflator.stamp(earlier.lineage.terminal,wall,256);
        require(inflated.moved>0,
                "M36 protected outer-wall proposals truly move original geometry");
        const auto refined=refiner.apply(inflated.index,inflated.baseline);
        const auto collapsed=collapser.apply(refined.index,inflated.baseline);
        return advanceCertifiedPhysicalStampLifetime(
            earlier,inflated,refined,collapsed,nullptr,nullptr,
            16384,16384,16384,32768);
    };
    const auto predictedFirst=advance(firstProof);
    const auto predictedSecond=advance(predictedFirst);
    const auto protectedWorst=[&](const OriginalPatchLifetime& life) {
        const auto audited=auditOriginalPatchLifetime(life,original);
        double maximum=0;
        for(const auto& face:audited.perOriginal)
            if(std::find(faces.begin()+32,faces.end(),face.original)!=
                         faces.end())
                maximum=std::max(maximum,
                    face.sampled.maximumDistanceInEdges);
        return maximum;
    };
    const auto firstDeviation=protectedWorst(predictedFirst);
    const auto secondDeviation=protectedWorst(predictedSecond);
    require(firstDeviation>0&&secondDeviation>firstDeviation+1e-8,
            "M36 two separate real protected movements accumulate against immutable original edge scales");
    const auto exactWork=auditOriginalPatchLifetime(
        predictedFirst,original).total.distanceTests;
    const auto secondWork=auditOriginalPatchLifetime(
        predictedSecond,original).total.distanceTests;
    require(exactWork>0&&exactWork<=131072&&secondWork<=exactWork,
            "M36 exact first-original witness limit includes all cut descendants and second drift is NOT merely over budget");
    const double tolerance=.5*(firstDeviation+secondDeviation);
    auto policy=base;
    policy.originalTerminal.maximumProtectedDeviationInEdges=tolerance;

    // Separate exact/one-under verified source gates, on the SAME
    // genuine physical first contact, not a guessed ancestor or a
    // standalone easy source. This does not alter the live worker's
    // independent finite V13 and full-history M29 source limits.
    auto exact=policy;
    exact.originalTerminal.maxDistanceTests=exactWork;
    VersionedSculptBrush bounded(firstContact,selected.strength,
                                  {},{},{},256,{},exact);
    bounded.setOriginalPatchIntent(selected.deliberate);
    bounded.stamp(dummy,wall);
    if(bounded.publishedRevisions()!=1)
        throw std::runtime_error(
            "M36 exact full original-source witness budget unexpectedly refused: "+
            bounded.lastRejection());
    const auto boundedAccepted=bounded.checkpoint();
    require(boundedAccepted.verifiedOriginalFidelity&&
            boundedAccepted.verifiedOriginalFidelity->anchors.original==original&&
            std::abs(protectedWorst(*boundedAccepted.verifiedOriginalFidelity)-
                     firstDeviation)<1e-8&&
            auditOriginalPatchLifetime(
                *boundedAccepted.verifiedOriginalFidelity,original,exactWork)
                  .total.distanceTests==exactWork,
            "M36 exactly full seven-witness per living original child cap accepts genuine source-authenticated protected successor");
    verifyOriginalPatchPublicationTransition(
        firstProof,*boundedAccepted.verifiedOriginalFidelity,
        firstContact.index,boundedAccepted.index);
    auto oneUnder=exact;
    oneUnder.originalTerminal.maxDistanceTests=exactWork-1;
    VersionedSculptBrush denied(firstContact,selected.strength,
                                 {},{},{},256,{},oneUnder);
    denied.setOriginalPatchIntent(selected.deliberate);
    const auto denialBefore=denied.checkpoint();
    denied.stamp(dummy,wall);
    const auto denialAfter=denied.checkpoint();
    require(denied.rejectedStamps()==1&&
            denied.publishedRevisions()==0&&
            denied.historyStatistics().undoDepth==0&&
            denied.lastDirtyPartitions().empty()&&
            denialAfter.index==denialBefore.index&&
            denialAfter.verifiedOriginalFidelity==
                denialBefore.verifiedOriginalFidelity&&
            denialAfter.mirrorPairs==denialBefore.mirrorPairs&&
            denied.lastRejection().find(
                "First-original witness audit distance budget exhausted")!=
                std::string::npos,
            "M36 one-under complete original child source work rejects BEFORE CAS with exact root/proof/mirror/history/dirty rollback");

    // A single ACTUAL asynchronous 4D worker now carries the combined
    // Contact/drift/Unknown/closure/recontact/recovery history.
    AsyncSculptWorker worker(selected.original,.025,selected.strength,.2,
                             {},{},{},{},policy);
    const auto oldPicker=original->pickingRootIdentity();
    std::vector<std::shared_ptr<const RegionTopologyIndex>> roots{original};
    const auto stroke=[&](Vec3 center,double radius,
                          std::vector<FaceId> intent) {
        require(worker.canBegin(),
                "M36 previous FIFO reservation fully consumed before new Begin");
        worker.begin(center,radius,std::move(intent));
        worker.end(center,0);
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(30);
        while(!worker.canBegin()&&
              std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto state=worker.status();
        require(worker.canBegin()&&!state.active&&
                state.inputCommands==0&&state.pendingSteps==0&&
                state.pendingSegments==0,
                "M36 4D worker drains each reserved real Begin/End and publishes the full terminal");
        return state;
    };
    const auto accepted=[&](const AsyncSculptWorker::Status& state,
                            const char* label) {
        const auto snapshot=worker.takeSnapshot();
        require(snapshot&&snapshot->index&&
                snapshot->index!=roots.back()&&
                !snapshot->dirtyPartitions.empty()&&
                state.history.undoDepth==roots.size()&&
                state.publishedRevisions==roots.size()&&
                state.completedStamps==
                    state.publishedRevisions+state.rejectedStamps&&
                original->pickingRootIdentity()==oldPicker,
                label);
        roots.push_back(snapshot->index);
        return snapshot->index;
    };
    const auto refused=[&](const AsyncSculptWorker::Status& state,
                           const AsyncSculptWorker::Status& before,
                           const char* reason,const char* label) {
        const auto snapshot=worker.takeSnapshot();
        require(state.rejectedStamps==before.rejectedStamps+1&&
                state.publishedRevisions==before.publishedRevisions&&
                state.history.undoDepth==before.history.undoDepth&&
                state.history.redoDepth==before.history.redoDepth&&
                snapshot&&snapshot->index==roots.back()&&
                snapshot->dirtyPartitions.empty()&&
                worker.initialRevision()==roots.back()&&
                original->pickingRootIdentity()==oldPicker&&
                state.lastRejection.find(reason)!=std::string::npos,
                label);
    };
    const auto checkpoint=[&]() {
        const auto path=std::filesystem::temp_directory_path()/
            "mesher-v17-m36-integrated-fifo-protected.mshr";
        const auto prior=worker.checkpointStatistics();
        try {
            worker.saveCheckpoint(path);
            const auto until=std::chrono::steady_clock::now()+
                             std::chrono::seconds(25);
            while(worker.checkpointStatistics().completed==prior.completed&&
                  worker.checkpointStatistics().failed==prior.failed&&
                  std::chrono::steady_clock::now()<until)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            const auto next=worker.checkpointStatistics();
            require(next.completed==prior.completed+1&&
                    next.failed==prior.failed,
                    "M36 each requested durable checkpoint waits for actual completed protected FIFO serialization");
            auto snapshot=readCheckpoint(path);
            std::error_code ec;std::filesystem::remove(path,ec);
            require(snapshot.index&&snapshot.originalFidelityRequired&&
                    !snapshot.verifiedOriginalFidelity,
                    "M36 V13 disk provenance never invents a sealed M29 original lifetime");
            return snapshot;
        }catch(...) {
            std::error_code ec;std::filesystem::remove(path,ec);
            throw;
        }
    };
    const auto initial=stroke(source.input[0][0].center,6,
                              selected.deliberate);
    if(initial.completedUnions!=1)
        throw std::runtime_error(
            "M36 real original-root 4D local first Contact refused: "+
            initial.lastRejection);
    const auto first=accepted(initial,
        "M36 actual 4D first Contact publishes one authenticated terminal dirty/history root");
    require(initial.contact.contactChecks==1&&
            initial.contact.episodes==1&&
            initial.contact.active&&
            first->retiredFaceCount()>original->retiredFaceCount(),
            "M36 original overlapping patch genuine source-exact contact starts an episode");

    const auto firstMove=stroke(protectedOuter,protectedRadius,
                                selected.deliberate);
    if(firstMove.publishedRevisions!=2)
        throw std::runtime_error(
            "M36 genuine first remote protected outer-wall movement failed: "+
            firstMove.lastRejection);
    const auto firstMoved=accepted(firstMove,
        "M36 first actual 4D protected outer-wall motion is accepted and dirty as ONE M29 successor");
    require(firstMove.contact.cleanChecks==1&&
            firstMove.contact.quietRun==1&&firstMove.contact.active&&
            firstMove.completedUnions==1,
            "M36 permitted first small protected movement receives one conclusive Clean without caching weld authority");
    const auto priorAfterDrift=checkpoint();
    for(const auto& [u,v]:priorAfterDrift.mirrorPairs) {
        const auto p=priorAfterDrift.index->vertex(u).position;
        const auto q=priorAfterDrift.index->vertex(v).position;
        require(p.x==-q.x&&p.y==q.y&&p.z==q.z,
                "M36 actual first physical drift retains live exact mirrored source ID orbits");
    }

    const auto clean1=stroke(selected.exterior,.5,selected.deliberate);
    (void)accepted(clean1,
        "M36 real 4D exterior Clean after first protected drift publishes another source-authenticated terminal");
    require(clean1.contact.cleanChecks==2&&
            clean1.contact.quietRun==2&&clean1.contact.active,
            "M36 two consecutively accepted classified Clean observations do NOT prematurely end episode");

    const auto beforeInvalid=worker.status();
    const auto beforeInvalidDisk=checkpoint();
    const auto foreign=stroke(selected.exterior,.28,{FaceId{99999999}});
    refused(foreign,beforeInvalid,"original",
            "M36 foreign first-original FaceId refuses atomically in the same protected FIFO");
    require(foreign.contact.uncertainChecks==1&&
            foreign.contact.quietRun==0&&foreign.contact.active&&
            foreign.contact.closedEpisodes==0,
            "M36 invalid source owner is UNKNOWN, destroys both Clean credits and preserves active Contact");
    const auto afterInvalidDisk=checkpoint();
    require(beforeInvalidDisk.mirrorPairs==afterInvalidDisk.mirrorPairs&&
            beforeInvalidDisk.index->retiredFaceIds()==
                afterInvalidDisk.index->retiredFaceIds()&&
            beforeInvalidDisk.index->retiredVertexIds()==
                afterInvalidDisk.index->retiredVertexIds(),
            "M36 illegal source owner refusal preserves exact disk mirror/retirement identity");

    const auto beforeErosion=worker.status();
    const auto diskBeforeErosion=checkpoint();
    const auto erosion=stroke(protectedOuter,protectedRadius,
                              selected.deliberate);
    refused(erosion,beforeErosion,
        "Unintended exposed/reversed-cavity first-original surface drift",
        "M36 real second individually small 4D protected wall stroke exceeds CUMULATIVE original tolerance, not source work, without CAS");
    require(erosion.contact.uncertainChecks==2&&
            erosion.contact.quietRun==0&&
            erosion.contact.closedEpisodes==0&&
            erosion.contact.active,
            "M36 rejected would-be Clean cannot advance quiet streak after cumulative protected erosion");
    const auto diskAfterErosion=checkpoint();
    require(diskBeforeErosion.mirrorPairs==diskAfterErosion.mirrorPairs&&
            diskBeforeErosion.index->retiredFaceIds()==
                diskAfterErosion.index->retiredFaceIds()&&
            diskBeforeErosion.index->retiredVertexIds()==
                diskAfterErosion.index->retiredVertexIds(),
            "M36 cumulative drift refusal preserves exact mirrored checkpoint and stable retired source IDs");

    for(std::size_t i=0;i<3;++i) {
        const auto state=stroke(selected.exterior,.5,selected.deliberate);
        (void)accepted(state,
            "M36 each NEW real 4D Clean after both Unknown refusals publishes one full dirty history root");
        require(state.contact.cleanChecks==3+i&&
                state.contact.quietRun==(i==2?0:i+1)&&
                state.contact.active==(i!=2)&&
                state.contact.closedEpisodes==(i==2?1u:0u)&&
                state.contact.episodes==1,
                "M36 ONLY third CONSECUTIVE downstream-accepted Clean after Unknown closes same first episode");
    }
    const auto beforeSecond=roots.back();
    const auto secondContact=stroke(source.input[1][0].center,6,
                                    selected.deliberate);
    if(secondContact.completedUnions!=2)
        throw std::runtime_error(
            "M36 second real LOCAL source union after cumulative protected policy refused: "+
            secondContact.lastRejection);
    const auto twiceJoined=accepted(secondContact,
        "M36 second genuine original-root same-patch Boolean recontact publishes ONE new terminal");
    require(twiceJoined->retiredFaceCount()>beforeSecond->retiredFaceCount()&&
            secondContact.contact.contactChecks==2&&
            secondContact.contact.episodes==2&&
            secondContact.contact.closedEpisodes==1&&
            secondContact.contact.active,
            "M36 after protected drift and three accepted Clean, SECOND local physical source-certified union reopens independent episode");
    const auto beforeLateBad=worker.status();
    const auto lateBad=stroke(m39LiveExterior(*twiceJoined),.28,
                              {FaceId{99999998}});
    refused(lateBad,beforeLateBad,"original",
            "M36 late refused original source ownership cannot poison further actual 4D FIFO progress");
    require(lateBad.contact.uncertainChecks==3&&lateBad.contact.active,
            "M36 late illegal original scope is an interrupting Unknown after genuine recontact");
    const auto recovered=stroke(m39LiveExterior(*twiceJoined),.5,
                                selected.deliberate);
    const auto final=accepted(recovered,
        "M36 subsequent well-scoped real 4D Clean restores FIFO publication after THREE source-policy refusals");
    require(recovered.contact.cleanChecks==6&&
            recovered.contact.contactChecks==2&&
            recovered.contact.closedEpisodes==1&&
            recovered.contact.episodes==2&&
            recovered.contact.active&&
            recovered.history.undoDepth==roots.size()-1,
            "M36 final integrated protected 4D source-policy and diagnostic counters remain coherent");

    // The original geometry remains signed-winding valid even though
    // permitted FIRST protected outer displacement was not bitwise zero.
    // Source FaceId incidence and cavity vertices are never invented.
    for(std::size_t i=32;i<faces.size();++i) {
        const auto before=original->maybeFace(faces[i]);
        const auto after=final->maybeFace(faces[i]);
        require(before&&after&&before->vertices==after->vertices,
                "M36 all unscoped original outer/cavity/remote sources retain their exact FaceId and incident vertex identities");
    }
    for(const double x:{5.0,-5.0}) {
        const auto voidPoint=final->positiveWindingAt({x,80,0});
        const auto shell=final->positiveWindingAt({
            x+(x>0?1.5:-1.5),80,0});
        const auto outside=final->positiveWindingAt({
            x+(x>0?4.0:-4.0),80,0});
        require(voidPoint.reliable&&shell.reliable&&outside.reliable&&
                voidPoint.winding==0&&shell.winding==1&&
                outside.winding==0,
                "M36 actual protected post-drift mirrored outer and reversed-cavity walls retain 0/+1/0 signed occupancy");
    }
    const auto finalDisk=checkpoint();
    require(finalDisk.index->retiredFaceIds()==final->retiredFaceIds()&&
            finalDisk.index->retiredVertexIds()==final->retiredVertexIds()&&
            finalDisk.mirrorPairs.size()>=selected.original.mirrorPairs.size(),
            "M36 latest true FIFO protected terminal checkpoint retains complete source identities and mirror orbits");
    bool diskOnlyRejected=false;
    try {
        (void)VersionedSculptBrush(finalDisk,selected.strength,
                                   {},{},{},256,{},policy);
    }catch(const std::logic_error&){diskOnlyRejected=true;}
    require(diskOnlyRejected,
            "M36 disk-only protected terminal cannot reauthenticate first-original source from coincident XYZ");

    // The worker refreshes Status BEFORE it publishes the corresponding
    // immutable renderer frame (AsyncSculptWorker::run -> refreshStatus,
    // publish). A history-depth observation alone is NOT a publication
    // barrier. Poll for BOTH the target bounded-history position and the
    // EXACT expected root in the viewport mailbox. Drop older coalesced
    // frames but never accept a newer/wrong root as a successful restore.
    // This is synchronization, not a random-seed issue or a test timeout
    // increase: a missing correct publication still fails after 20s.
    const auto awaitHistoryFrame=[&](
        const std::shared_ptr<const RegionTopologyIndex>& expected,
        std::size_t undoDepth,std::size_t redoDepth) {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(std::chrono::steady_clock::now()<until) {
            const auto status=worker.status();
            if(status.history.undoDepth==undoDepth&&
               status.history.redoDepth==redoDepth) {
                const auto snapshot=worker.takeSnapshot();
                if(snapshot&&snapshot->index==expected&&
                   worker.initialRevision()==expected)
                    return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };
    for(std::size_t n=roots.size()-1;n>0;--n) {
        worker.undo();
        require(awaitHistoryFrame(roots[n-1],n-1,roots.size()-n),
                "M36 all accepted mixed Contact/Clean/allowed drift COW history pointers undo EXACTLY after protected refusals");
    }
    for(std::size_t n=1;n<roots.size();++n) {
        worker.redo();
        require(awaitHistoryFrame(roots[n],n,roots.size()-1-n),
                "M36 post-refusal 4D redo restores exact accepted original source roots one-by-one");
    }
    require(worker.initialRevision()==final&&
            firstMoved->pickingRootIdentity()!=nullptr&&
            original->pickingRootIdentity()==oldPicker,
            "M36 original and intermediate picker/BVH snapshots still immutable after entire combined FIFO replay");
    std::cout<<"[m36] original combined source-cavity policy: 2 true local "
             <<"4D unions; exact full/one-under first-origin work="
             <<exactWork<<"; cumulative protected edge drift "
             <<firstDeviation<<"/"<<secondDeviation<<"; "
             <<roots.size()-1<<" immutable accepted FIFO history roots\n";
}

// ORIGINAL V17-M12: final standalone source-reference acceptance. The
// M36 test above already executes the complete combined 4D policy in ONE
// worker history. This independently reconstructs the FIRST root's
// lifetime invariants from a second true M37 dual-physical-union final
// revision. It deliberately refuses last-root rebasing, fake certified
// counters, and even ONE missing source-descendant witness comparison.
void testM12OriginalFinalFirstReferenceAcceptance() {
    const auto fixture=makeM38ProtectedLocalFixture();
    const auto first=fixture.firstOriginal;
    const auto final=fixture.terminal.index;
    const auto proof=fixture.terminal.verifiedOriginalFidelity;
    require(first&&final&&proof&&first!=final&&
            first!=fixture.afterFirstUnion&&
            final!=fixture.afterFirstUnion&&
            proof->anchors.original==first&&
            proof->lineage.original==first&&
            proof->lineage.terminal==final&&
            proof->acceptedTransitions==2&&
            fixture.terminal.originalFidelityRequired&&
            fixture.originalFaceCount==72&&
            fixture.deliberate.size()==32&&
            fixture.protectedSources.size()==40&&
            proof->anchors.faces.size()==72&&
            proof->lineage.living.size()==72,
            "Original M12 accepts only the actual two-contact 72-source M37 final root with one fixed authenticated first-original lifetime");
    std::size_t living=0;
    for(std::size_t i=0;i<proof->anchors.faces.size();++i) {
        const auto& fixed=proof->anchors.faces[i];
        const auto& entry=proof->lineage.living[i];
        const auto originalFace=first->maybeFace(fixed.face);
        require(originalFace&&fixed.face==entry.first&&
                originalFace->vertices==fixed.originalVertices&&
                std::isfinite(fixed.originalEdgeLength)&&
                fixed.originalEdgeLength>0,
                "Original M12 all 72 first source FaceIds, vertex incidence and local shortest-edge units are immutable");
        const auto independent=captureOriginalSourceAnchors(
            first,{fixed.face},7);
        require(independent.original==first&&
                independent.faces.size()==1&&
                independent.faces[0].originalVertices==
                    fixed.originalVertices&&
                independent.faces[0].originalEdgeLength==
                    fixed.originalEdgeLength,
                "Original M12 immutable original 7-point reference recomputes from actual first COW root");
        for(std::size_t j=0;j<7;++j) {
            const auto a=independent.faces[0].witnesses[j];
            const auto b=fixed.witnesses[j];
            require(a.x==b.x&&a.y==b.y&&a.z==b.z,
                    "Original M12 every protected and intended witness XYZ belongs to the original FaceId, not a new terminal triangulation");
        }
        living+=entry.second.size();
        for(const auto child:entry.second)
            require(static_cast<bool>(final->maybeFace(child)),
                    "Original M12 every authenticated source living child really exists in exact terminal COW root");
    }
    const auto complete=auditOriginalPatchLifetime(
        *proof,first,7*living);
    require(living>72&&
            complete.originalSources==72&&
            complete.total.distanceTests==7*living&&
            complete.total.witnessCount==7*complete.livingOriginals,
            "Original M12 exact full audit traverses all genuine post-d2 descendants and seven original points PER living original source");
    std::size_t summed=0;
    for(const auto& record:complete.perOriginal) {
        require(record.sampled.witnessCount==7&&
                record.sampled.distanceTests==
                    7*record.livingDescendants,
                "Original M12 each living protected source samples all seven immutable original witnesses against every owned child");
        summed+=record.sampled.distanceTests;
    }
    require(summed==complete.total.distanceTests,
            "Original M12 all per-original work sums to exact full bounded terminal audit with no hidden partial success");

    bool staleRefused=false,oneUnderRefused=false,forgedRefused=false;
    try {
        (void)auditOriginalPatchLifetime(
            *proof,fixture.afterFirstUnion,7*living);
    }catch(const std::invalid_argument&){staleRefused=true;}
    try {
        (void)auditOriginalPatchLifetime(*proof,first,7*living-1);
    }catch(const std::length_error&){oneUnderRefused=true;}
    auto forged=*proof;
    ++forged.acceptedTransitions;
    try {
        (void)auditOriginalPatchLifetime(forged,first,7*living);
    }catch(const std::logic_error&){forgedRefused=true;}
    require(staleRefused&&oneUnderRefused&&forgedRefused,
            "Original M12 independently refuses a later-root reanchor, one-under full descendant budget, and unsealed lifetime counter forgery");

    for(const auto id:fixture.protectedSources) {
        const auto before=first->maybeFace(id);
        const auto after=final->maybeFace(id);
        require(before&&after&&before->vertices==after->vertices,
                "Original M12 all 40 unscoped source FaceIds retain exact original owner and ordered incidence");
        for(const auto vertex:before->vertices) {
            const auto p=first->vertex(vertex).position;
            const auto q=final->vertex(vertex).position;
            require(p.x==q.x&&p.y==q.y&&p.z==q.z,
                    "Original M12 signed-winding cavity and exposed walls retain untouched protected XYZ across both real local unions");
        }
    }
    for(const double x:{5.0,-5.0}) {
        const auto cavity=final->positiveWindingAt({x,80,0});
        const auto material=final->positiveWindingAt({
            x+(x>0?1.5:-1.5),80,0});
        const auto exterior=final->positiveWindingAt({
            x+(x>0?4.0:-4.0),80,0});
        require(cavity.reliable&&material.reliable&&
                exterior.reliable&&
                cavity.winding==0&&material.winding==1&&
                exterior.winding==0,
                "Original M12 both independently owned protected reflected reversed-cavity shells remain authentic 0/+1/0 signed solids");
    }
    require(fixture.terminal.mirrorPairs.size()>=
                fixture.terminal.verifiedFirstOriginalMirrorPairs.size()&&
            !fixture.terminal.mirrorPairs.empty(),
            "Original M12 final source retains its authenticated original reflected orbit records");
    for(const auto& [a,b]:fixture.terminal.mirrorPairs) {
        require(final->maybeVertex(a)&&final->maybeVertex(b),
                "Original M12 current reciprocal mirror pairs refer to real live permanent IDs");
        const auto p=final->vertex(a).position;
        const auto q=final->vertex(b).position;
        require(p.x==-q.x&&p.y==q.y&&p.z==q.z,
                "Original M12 actual mirrored terminal ID orbits remain exactly reflected without any coincident-XYZ deduplication");
    }
    require(first->pickingRootIdentity()!=nullptr&&
            fixture.afterFirstUnion->pickingRootIdentity()!=nullptr&&
            final->pickingRootIdentity()!=nullptr,
            "Original M12 all genuine first, intermediate and terminal reader/picker BVH roots survive immutable source history");

    std::cout<<"[m12] original final: 2 genuine overlapping-patch "
             <<"physical source unions, 72 first sources, "
             <<fixture.protectedSources.size()<<" untouched outer/cavity "
             <<"FaceIds, exact "<<complete.total.distanceTests
             <<" living-descendant witness tests, stale/forged/one-under "
             <<"refusal, independent mirror and cavity verification\n";
}

// M40 independently limits all private COW copies and final-root
// renderer/ID work for ONE genuine source-authenticated physical or
// queued exact operation. The first run MEASURES actual finite costs;
// the exact-bound rerun publishes; each one-under independently refuses
// BEFORE CAS with unchanged source authority, mirrors, history and dirty.
void testM40AggregateWholePrivateStepResourceBudgets() {
    const auto selected=chooseM39FourDimensionalSource();
    const auto input=std::array<BrushArea,2>{{
        {selected.source.input[0][0].center,6,.3},
        {{-selected.source.input[0][0].center.x,
          selected.source.input[0][0].center.y,
          selected.source.input[0][0].center.z},6,.3}
    }};
    const auto initial=selected.original;
    const auto first=initial.index;
    const auto picker=first->pickingRootIdentity();
    Mesh dummy;
    const auto settings=m11PhysicalExactSettings();
    const auto makeBrush=[&](SculptCollisionSettings policy) {
        return VersionedSculptBrush(initial,selected.strength,
                                     {},{},{},256,{},std::move(policy));
    };
    auto measured=makeBrush(settings);
    measured.setOriginalPatchIntent(selected.deliberate);
    measured.stamp(dummy,input);
    require(measured.completedUnions()==1&&
            measured.publishedRevisions()==1&&
            measured.historyStatistics().undoDepth==1,
            "M40 actual symmetric source-owned full private physical Step independently completes before calibration");
    const auto truth=measured.lastStepResourceWork();
    require(truth.path==SculptStepResourcePath::PhysicalStamp&&
            truth.evaluated&&truth.published&&!truth.refused&&
            truth.privateCopiedBytes>0&&
            truth.changedIdReferences>0&&
            truth.finalDirtyPartitions>0&&
            truth.finalDirtyPartitions==
                measured.lastDirtyPartitions().size()&&
            truth.elapsedMilliseconds>=0&&
            truth.refusal.empty(),
            "M40 all private Inflate/refine/collapse/Boolean COW copies, changed ID references and final dirty owners are measured honestly on successful original-root CAS");
    auto exact=settings;
    exact.stepResources.maxPrivateCopiedBytes=truth.privateCopiedBytes;
    exact.stepResources.maxChangedIdReferences=truth.changedIdReferences;
    exact.stepResources.maxFinalDirtyPartitions=truth.finalDirtyPartitions;
    auto within=makeBrush(exact);
    within.setOriginalPatchIntent(selected.deliberate);
    within.stamp(dummy,input);
    const auto accepted=within.checkpoint();
    require(within.completedUnions()==1&&
            accepted.verifiedOriginalFidelity&&
            accepted.verifiedOriginalFidelity->anchors.original==first&&
            within.lastStepResourceWork().privateCopiedBytes==
                truth.privateCopiedBytes&&
            within.lastStepResourceWork().changedIdReferences==
                truth.changedIdReferences&&
            within.lastStepResourceWork().finalDirtyPartitions==
                truth.finalDirtyPartitions,
            "M40 all three exact genuine physical Step aggregate resource caps accept, before any history append");
    for(std::size_t dimension=0;dimension<3;++dimension) {
        auto tooSmall=exact;
        const char* name="";
        if(dimension==0) {
            --tooSmall.stepResources.maxPrivateCopiedBytes;
            name="COW payload byte budget";
        }else if(dimension==1) {
            --tooSmall.stepResources.maxChangedIdReferences;
            name="changed ID reference budget";
        }else {
            --tooSmall.stepResources.maxFinalDirtyPartitions;
            name="final dirty partition budget";
        }
        auto denied=makeBrush(tooSmall);
        denied.setOriginalPatchIntent(selected.deliberate);
        const auto before=denied.checkpoint();
        denied.stamp(dummy,input);
        const auto after=denied.checkpoint();
        const auto telemetry=denied.lastStepResourceWork();
        require(denied.rejectedStamps()==1&&
                denied.completedUnions()==0&&
                denied.publishedRevisions()==0&&
                denied.historyStatistics().undoDepth==0&&
                denied.historyStatistics().redoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                after.index==before.index&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                after.mirrorPairs==before.mirrorPairs&&
                first->pickingRootIdentity()==picker&&
                telemetry.path==SculptStepResourcePath::PhysicalStamp&&
                telemetry.evaluated&&telemetry.refused&&
                !telemetry.published&&
                telemetry.privateCopiedBytes==truth.privateCopiedBytes&&
                telemetry.changedIdReferences==truth.changedIdReferences&&
                telemetry.finalDirtyPartitions==truth.finalDirtyPartitions&&
                telemetry.refusal.find(name)!=std::string::npos&&
                denied.lastRejection()==telemetry.refusal,
                "M40 physical Step ONE-under resource category refuses without CAS/source/mirror/dirty/history, with complete measured attempted work rather than zeroing refusal diagnostics");
    }
    auto invalid=makeBrush(settings);
    invalid.setOriginalPatchIntent({FaceId{99999999}});
    invalid.stamp(dummy,input);
    const auto early=invalid.lastStepResourceWork();
    require(early.path==SculptStepResourcePath::PhysicalStamp&&
            early.refused&&!early.evaluated&&!early.published&&
            early.privateCopiedBytes==0&&
            early.changedIdReferences==0&&
            early.finalDirtyPartitions==0&&
            !early.refusal.empty(),
            "M40 early refused input truthfully has NO measured candidate work and never reports fabricated memory or committed root");

    // Independent queued source-owned exact Boolean Step exercises the
    // same envelope without an Inflate stamp or guessed COW data.
    const auto mesh=twoIntersectingTetrahedra();
    const auto sourceFaces=firstOriginalFaces(mesh);
    const auto unionLimits=supportedUnionLimits(false);
    SculptCollisionSettings queued;
    queued.physicalExact=true;
    queued.originalTerminal.maximumProtectedDeviationInEdges=0;
    const auto makeQueued=[&](SculptCollisionSettings limits) {
        return VersionedSculptBrush(mesh,.006,{},{},{},false,
                                     256,{},std::move(limits));
    };
    auto positive=makeQueued(queued);
    require(positive.mergeExactContacts(
                positive.snapshot(),
                fullOriginalContacts(*positive.snapshot()),unionLimits,
                sourceFaces),
            "M40 real queued asymmetric original source c1/c2/c3/M03/d2 positive is separately complete");
    const auto queuedTruth=positive.lastStepResourceWork();
    require(queuedTruth.path==SculptStepResourcePath::QueuedExact&&
            queuedTruth.evaluated&&queuedTruth.published&&
            !queuedTruth.refused&&queuedTruth.refusal.empty()&&
            queuedTruth.privateCopiedBytes>0&&
            queuedTruth.changedIdReferences>0&&
            queuedTruth.finalDirtyPartitions>0&&
            queuedTruth.finalDirtyPartitions==
                positive.lastDirtyPartitions().size(),
            "M40 queued Step COW result reports all private cut/refine/join payload, net stable ID work and exact published owners");
    queued.stepResources.maxPrivateCopiedBytes=queuedTruth.privateCopiedBytes;
    queued.stepResources.maxChangedIdReferences=queuedTruth.changedIdReferences;
    queued.stepResources.maxFinalDirtyPartitions=
        queuedTruth.finalDirtyPartitions;
    auto queuedExact=makeQueued(queued);
    require(queuedExact.mergeExactContacts(
                queuedExact.snapshot(),
                fullOriginalContacts(*queuedExact.snapshot()),unionLimits,
                sourceFaces)&&
            queuedExact.lastStepResourceWork().published,
            "M40 exact all-category queued source union resource caps allow one history-authorized CAS");
    for(std::size_t dimension=0;dimension<3;++dimension) {
        auto oneUnder=queued;
        const char* name="";
        if(dimension==0) {
            --oneUnder.stepResources.maxPrivateCopiedBytes;
            name="COW payload byte budget";
        }else if(dimension==1) {
            --oneUnder.stepResources.maxChangedIdReferences;
            name="changed ID reference budget";
        }else {
            --oneUnder.stepResources.maxFinalDirtyPartitions;
            name="final dirty partition budget";
        }
        auto denied=makeQueued(oneUnder);
        const auto before=denied.checkpoint();
        require(!denied.mergeExactContacts(
                    before.index,
                    fullOriginalContacts(*before.index),unionLimits,
                    sourceFaces),
                "M40 queued one-under resource candidate MUST NOT publish");
        const auto after=denied.checkpoint();
        const auto status=denied.lastStepResourceWork();
        require(after.index==before.index&&
                after.mirrorPairs==before.mirrorPairs&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                denied.historyStatistics().undoDepth==0&&
                denied.publishedRevisions()==0&&
                denied.completedUnions()==0&&
                denied.rejectedUnions()==1&&
                denied.lastDirtyPartitions().empty()&&
                status.path==SculptStepResourcePath::QueuedExact&&
                status.refused&&status.evaluated&&!status.published&&
                status.privateCopiedBytes==queuedTruth.privateCopiedBytes&&
                status.changedIdReferences==queuedTruth.changedIdReferences&&
                status.finalDirtyPartitions==queuedTruth.finalDirtyPartitions&&
                status.refusal.find(name)!=std::string::npos,
                "M40 queued exact one-under per-private-Step COW/ID/dirty refusal leaves authentic root, source, mirrors and history untouched");
    }
    // The published 4D status must not report an attempted queued
    // resource workload as an accepted one, or omit successful costs.
    AsyncSculptWorker worker(mesh,.025,.006,.2,{},{},{},false,{},queued);
    const auto queueBase=worker.initialRevision();
    worker.mergeExactContacts(
        queueBase,fullOriginalContacts(*queueBase),unionLimits,sourceFaces);
    waitForUnion(worker,1);
    const auto stats=worker.status();
    require(stats.completedUnions==1&&stats.publishedRevisions==1&&
            stats.stepResources.path==SculptStepResourcePath::QueuedExact&&
            stats.stepResources.published&&
            stats.stepResources.privateCopiedBytes==
                queuedTruth.privateCopiedBytes&&
            stats.stepResources.changedIdReferences==
                queuedTruth.changedIdReferences&&
            stats.stepResources.finalDirtyPartitions==
                queuedTruth.finalDirtyPartitions,
            "M40 real FIFO worker status exports exact measured accepted queued Step resource statistics");
    std::cout<<"[m40] complete physical bytes="<<truth.privateCopiedBytes
             <<" changed refs="<<truth.changedIdReferences
             <<" final dirty="<<truth.finalDirtyPartitions
             <<"; queued bytes="<<queuedTruth.privateCopiedBytes
             <<" changed refs="<<queuedTruth.changedIdReferences
             <<" dirty="<<queuedTruth.finalDirtyPartitions
             <<" (exact and one-under all categories)\n";
}

// M41: independent query-operation aggregate limits cover V13 source
// discovery, repeated complete c1/c2/c3/M03 classification and M26
// source replay. Calibration is from real SUCCESSFUL final-root unions;
// one-under refusals are necessarily from the SAME supported candidate.
void testM41ExactWholeStepQueryAccounting() {
    const auto chosen=chooseM39FourDimensionalSource();
    const auto input=std::array<BrushArea,2>{{
        {chosen.source.input[0][0].center,6,.3},
        {{-chosen.source.input[0][0].center.x,
          chosen.source.input[0][0].center.y,
          chosen.source.input[0][0].center.z},6,.3}
    }};
    Mesh dummy;
    const auto original=chosen.original;
    const auto opts=m11PhysicalExactSettings();
    const auto makePhysical=[&](SculptCollisionSettings settings) {
        return VersionedSculptBrush(
            original,chosen.strength,{},{},{},256,{},std::move(settings));
    };
    auto baseline=makePhysical(opts);
    baseline.setOriginalPatchIntent(chosen.deliberate);
    baseline.stamp(dummy,input);
    require(baseline.completedUnions()==1,
            "M41 true physical 4D mirrored source union is positively accepted");
    const auto physical=baseline.lastStepResourceWork().exactStage;
    require(physical.collisionQueries>0&&
            physical.collisionNarrowPhase>0&&
            physical.windingQueries>0&&
            physical.windingTriangles>0&&
            physical.windingRays>0&&!physical.overflow,
            "M41 physical authenticated final source measured repeated V13 collision and actual winding query work");
    auto exact=opts;
    exact.exactStageLimits.maxCollisionQueries=physical.collisionQueries;
    exact.exactStageLimits.maxCollisionNarrowPhase=
        physical.collisionNarrowPhase;
    exact.exactStageLimits.maxWindingQueries=physical.windingQueries;
    exact.exactStageLimits.maxWindingTriangles=physical.windingTriangles;
    exact.exactStageLimits.maxWindingRays=physical.windingRays;
    auto onBoundary=makePhysical(exact);
    onBoundary.setOriginalPatchIntent(chosen.deliberate);
    onBoundary.stamp(dummy,input);
    const auto measured=onBoundary.lastStepResourceWork();
    require(onBoundary.completedUnions()==1&&
            measured.published&&measured.evaluated&&
            measured.exactStage.collisionQueries==physical.collisionQueries&&
            measured.exactStage.collisionNarrowPhase==
                physical.collisionNarrowPhase&&
            measured.exactStage.windingQueries==physical.windingQueries&&
            measured.exactStage.windingTriangles==physical.windingTriangles&&
            measured.exactStage.windingRays==physical.windingRays,
            "M41 every finite query-operation category accepts exactly at genuine complete physical original-root Step cost");
    for(std::size_t i=0;i<5;++i) {
        auto deniedSettings=exact;
        const char* category="";
        switch(i) {
        case 0:
            --deniedSettings.exactStageLimits.maxCollisionQueries;
            category="collision query budget";break;
        case 1:
            --deniedSettings.exactStageLimits.maxCollisionNarrowPhase;
            category="collision narrow-phase budget";break;
        case 2:
            --deniedSettings.exactStageLimits.maxWindingQueries;
            category="winding query budget";break;
        case 3:
            --deniedSettings.exactStageLimits.maxWindingTriangles;
            category="winding triangle budget";break;
        default:
            --deniedSettings.exactStageLimits.maxWindingRays;
            category="winding ray budget";break;
        }
        auto denied=makePhysical(deniedSettings);
        denied.setOriginalPatchIntent(chosen.deliberate);
        const auto old=denied.checkpoint();
        denied.stamp(dummy,input);
        const auto after=denied.checkpoint();
        const auto work=denied.lastStepResourceWork();
        require(denied.rejectedStamps()==1&&
                denied.completedUnions()==0&&
                denied.publishedRevisions()==0&&
                denied.lastDirtyPartitions().empty()&&
                denied.historyStatistics().undoDepth==0&&
                after.index==old.index&&
                after.mirrorPairs==old.mirrorPairs&&
                after.verifiedOriginalFidelity==
                    old.verifiedOriginalFidelity&&
                work.evaluated&&work.refused&&!work.published&&
                work.exactStage.collisionQueries==
                    physical.collisionQueries&&
                work.exactStage.windingQueries==
                    physical.windingQueries&&
                work.refusal.find(category)!=std::string::npos,
                "M41 category-specific physical one-under refuses BEFORE publication without source/mirror/history/dirty mutation");
    }
    // Separate BVH broad-phase accounting is independent of a
    // successful narrow-phase/winding witness. Guard each actual
    // box/partition/candidate bucket, including the signed ray work.
    require(physical.collisionBoxes>0&&
            physical.collisionPartitions>0&&
            physical.collisionCandidates>0&&
            physical.windingBoxes>0&&
            physical.windingPartitions>0,
            "M41 genuine original-root physical work exercises nonzero broad-phase BVH categories");
    auto exactBroad=exact;
    exactBroad.exactStageLimits.maxCollisionBoxes=physical.collisionBoxes;
    exactBroad.exactStageLimits.maxCollisionPartitions=
        physical.collisionPartitions;
    exactBroad.exactStageLimits.maxCollisionCandidates=
        physical.collisionCandidates;
    exactBroad.exactStageLimits.maxWindingBoxes=physical.windingBoxes;
    exactBroad.exactStageLimits.maxWindingPartitions=
        physical.windingPartitions;
    auto broadAccepted=makePhysical(exactBroad);
    broadAccepted.setOriginalPatchIntent(chosen.deliberate);
    broadAccepted.stamp(dummy,input);
    require(broadAccepted.completedUnions()==1,
            "M41 physical all true broad-phase per-category BVH caps accept at exact complete source cost");
    for(std::size_t i=0;i<5;++i) {
        auto oneUnder=exactBroad;
        const char* name="";
        switch(i) {
        case 0:
            --oneUnder.exactStageLimits.maxCollisionBoxes;
            name="collision box budget";break;
        case 1:
            --oneUnder.exactStageLimits.maxCollisionPartitions;
            name="collision partition budget";break;
        case 2:
            --oneUnder.exactStageLimits.maxCollisionCandidates;
            name="collision candidate budget";break;
        case 3:
            --oneUnder.exactStageLimits.maxWindingBoxes;
            name="winding box budget";break;
        default:
            --oneUnder.exactStageLimits.maxWindingPartitions;
            name="winding partition budget";break;
        }
        auto denied=makePhysical(oneUnder);
        denied.setOriginalPatchIntent(chosen.deliberate);
        const auto prev=denied.checkpoint();
        denied.stamp(dummy,input);
        const auto current=denied.checkpoint();
        const auto work=denied.lastStepResourceWork();
        require(denied.rejectedStamps()==1&&
                denied.completedUnions()==0&&
                denied.publishedRevisions()==0&&
                denied.historyStatistics().undoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                current.index==prev.index&&
                current.verifiedOriginalFidelity==
                    prev.verifiedOriginalFidelity&&
                current.mirrorPairs==prev.mirrorPairs&&
                work.evaluated&&work.refused&&!work.published&&
                work.refusal.find(name)!=std::string::npos,
                "M41 actual per-category broad-phase one-under physical work refuses atomically, independently of other limits");
    }
    // Independently stage a complete source-certified queued Boolean,
    // not a preconstructed collision or a fabricated winding certificate.
    const auto mesh=twoIntersectingTetrahedra();
    const auto sources=firstOriginalFaces(mesh);
    const auto limits=supportedUnionLimits(false);
    SculptCollisionSettings queuePolicy;
    queuePolicy.physicalExact=true;
    queuePolicy.originalTerminal.maximumProtectedDeviationInEdges=0;
    const auto queued=[&](SculptCollisionSettings policy) {
        return VersionedSculptBrush(
            mesh,.006,{},{},{},false,256,{},std::move(policy));
    };
    auto queuePositive=queued(queuePolicy);
    const auto firstQueue=queuePositive.snapshot();
    require(queuePositive.mergeExactContacts(
                firstQueue,fullOriginalContacts(*firstQueue),limits,
                sources),
            "M41 real source-authenticated queued original-root exact Boolean succeeds");
    const auto authentic=queuePositive.lastStepResourceWork().exactStage;
    require(authentic.collisionQueries>0&&
            authentic.collisionNarrowPhase>0&&
            authentic.windingQueries>0&&
            authentic.windingTriangles>0&&
            authentic.windingRays>0&&!authentic.overflow,
            "M41 queued source certificate measured independent repeated collision and positive-winding queries");
    queuePolicy.exactStageLimits.maxCollisionQueries=
        authentic.collisionQueries;
    queuePolicy.exactStageLimits.maxCollisionNarrowPhase=
        authentic.collisionNarrowPhase;
    queuePolicy.exactStageLimits.maxWindingQueries=
        authentic.windingQueries;
    queuePolicy.exactStageLimits.maxWindingTriangles=
        authentic.windingTriangles;
    queuePolicy.exactStageLimits.maxWindingRays=authentic.windingRays;
    auto queueExactly=queued(queuePolicy);
    const auto queueOld=queueExactly.snapshot();
    require(queueExactly.mergeExactContacts(
                queueOld,fullOriginalContacts(*queueOld),limits,sources)&&
            queueExactly.lastStepResourceWork().exactStage.windingQueries==
                authentic.windingQueries,
            "M41 real queued source exact Boolean accepts full measured winding and collision work at equality");
    for(std::size_t i=0;i<5;++i) {
        auto oneUnder=queuePolicy;
        const char* category="";
        switch(i) {
        case 0:
            --oneUnder.exactStageLimits.maxCollisionQueries;
            category="collision query budget";break;
        case 1:
            --oneUnder.exactStageLimits.maxCollisionNarrowPhase;
            category="collision narrow-phase budget";break;
        case 2:
            --oneUnder.exactStageLimits.maxWindingQueries;
            category="winding query budget";break;
        case 3:
            --oneUnder.exactStageLimits.maxWindingTriangles;
            category="winding triangle budget";break;
        default:
            --oneUnder.exactStageLimits.maxWindingRays;
            category="winding ray budget";break;
        }
        auto denied=queued(oneUnder);
        const auto before=denied.checkpoint();
        require(!denied.mergeExactContacts(
                    before.index,fullOriginalContacts(*before.index),
                    limits,sources),
                "M41 queued authentic one-under category must refuse");
        const auto after=denied.checkpoint();
        const auto work=denied.lastStepResourceWork();
        require(after.index==before.index&&
                after.mirrorPairs==before.mirrorPairs&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                denied.publishedRevisions()==0&&
                denied.rejectedUnions()==1&&
                denied.historyStatistics().undoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                work.refused&&work.evaluated&&!work.published&&
                work.exactStage.collisionQueries==
                    authentic.collisionQueries&&
                work.exactStage.windingQueries==
                    authentic.windingQueries&&
                work.refusal.find(category)!=std::string::npos,
                "M41 independently one-under queued collision/winding category leaves original-root CAS, first-source, history, mirrors and dirty owners unchanged");
    }
    require(authentic.collisionBoxes>0&&
            authentic.collisionPartitions>0&&
            authentic.collisionCandidates>0&&
            authentic.windingBoxes>0&&
            authentic.windingPartitions>0,
            "M41 queued genuinely source-revalidated exact Boolean uses measurable full BVH broad-phase");
    auto allQueue=queuePolicy;
    allQueue.exactStageLimits.maxCollisionBoxes=authentic.collisionBoxes;
    allQueue.exactStageLimits.maxCollisionPartitions=
        authentic.collisionPartitions;
    allQueue.exactStageLimits.maxCollisionCandidates=
        authentic.collisionCandidates;
    allQueue.exactStageLimits.maxWindingBoxes=authentic.windingBoxes;
    allQueue.exactStageLimits.maxWindingPartitions=
        authentic.windingPartitions;
    auto acceptedBroad=queued(allQueue);
    const auto acceptedRoot=acceptedBroad.snapshot();
    require(acceptedBroad.mergeExactContacts(
                acceptedRoot,fullOriginalContacts(*acceptedRoot),
                limits,sources),
            "M41 queued all true broad-phase caps accept independently at exactly completed full source work");
    for(std::size_t i=0;i<5;++i) {
        auto oneUnder=allQueue;
        const char* name="";
        switch(i) {
        case 0:
            --oneUnder.exactStageLimits.maxCollisionBoxes;
            name="collision box budget";break;
        case 1:
            --oneUnder.exactStageLimits.maxCollisionPartitions;
            name="collision partition budget";break;
        case 2:
            --oneUnder.exactStageLimits.maxCollisionCandidates;
            name="collision candidate budget";break;
        case 3:
            --oneUnder.exactStageLimits.maxWindingBoxes;
            name="winding box budget";break;
        default:
            --oneUnder.exactStageLimits.maxWindingPartitions;
            name="winding partition budget";break;
        }
        auto denied=queued(oneUnder);
        const auto old=denied.checkpoint();
        require(!denied.mergeExactContacts(
                    old.index,fullOriginalContacts(*old.index),limits,
                    sources),
                "M41 real queued one-under broad-phase source limit cannot publish");
        const auto after=denied.checkpoint();
        const auto work=denied.lastStepResourceWork();
        require(after.index==old.index&&
                after.verifiedOriginalFidelity==
                    old.verifiedOriginalFidelity&&
                after.mirrorPairs==old.mirrorPairs&&
                denied.completedUnions()==0&&
                denied.publishedRevisions()==0&&
                denied.rejectedUnions()==1&&
                denied.historyStatistics().undoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                work.refused&&work.evaluated&&!work.published&&
                work.refusal.find(name)!=std::string::npos,
                "M41 separately metered queued original-source broad-phase one-under refuses atomically");
    }
    std::cout<<"[m41] real physical queries="<<physical.collisionQueries
             <<" narrow="<<physical.collisionNarrowPhase
             <<" winding="<<physical.windingQueries
             <<" triangles="<<physical.windingTriangles
             <<" rays="<<physical.windingRays
             <<"; real queued queries="<<authentic.collisionQueries
             <<" narrow="<<authentic.collisionNarrowPhase
             <<" winding="<<authentic.windingQueries
             <<" triangles="<<authentic.windingTriangles
             <<" rays="<<authentic.windingRays<<"\n";
}

// M43: direct AND queued complete source-exact original-root admissions
// bind all actually nonzero contour/closure work to exact measured caps.
// One-under is category-specific and refuses BEFORE the only root CAS.
// A multi-ring unequal-density original closure is separately exercised
// by the topology_aware_closure_fitness suite for all 7 nonzero buckets.
void testM43WholeStepContourClosureAccounting() {
    struct Bucket {
        std::size_t ExactStageWork::*count;
        std::size_t ExactStageWorkLimits::*limit;
        const char* category;
    };
    constexpr std::array<Bucket,7> buckets{{
        {&ExactStageWork::contourEdgeProbes,
         &ExactStageWorkLimits::maxContourEdgeProbes,"contour edge-probe budget"},
        {&ExactStageWork::contourBoundaryEdges,
         &ExactStageWorkLimits::maxContourBoundaryEdges,"contour boundary-edge budget"},
        {&ExactStageWork::contourLoopVisits,
         &ExactStageWorkLimits::maxContourLoopVisits,"contour loop-visit budget"},
        {&ExactStageWork::closureRingTests,
         &ExactStageWorkLimits::maxClosureRingTests,"closure ring-test budget"},
        {&ExactStageWork::closurePairCandidates,
         &ExactStageWorkLimits::maxClosurePairCandidates,"closure pair-candidate budget"},
        {&ExactStageWork::closureSampleTests,
         &ExactStageWorkLimits::maxClosureSampleTests,"closure sample-test budget"},
        {&ExactStageWork::closureDpTransitions,
         &ExactStageWorkLimits::maxClosureDpTransitions,"closure DP-transition budget"}
    }};
    const auto fillLimits=[&](SculptCollisionSettings& settings,
                              const ExactStageWork& evidence)->std::size_t {
        std::size_t exercised=0;
        for(const auto& bucket:buckets) {
            const auto actual=evidence.*(bucket.count);
            settings.exactStageLimits.*(bucket.limit)=actual;
            if(actual)++exercised;
        }
        return exercised;
    };
    const auto chosen=chooseM39FourDimensionalSource();
    const auto input=std::array<BrushArea,2>{{
        {chosen.source.input[0][0].center,6,.3},
        {{-chosen.source.input[0][0].center.x,
          chosen.source.input[0][0].center.y,
          chosen.source.input[0][0].center.z},6,.3}
    }};
    Mesh dummy;
    const auto original=chosen.original;
    const auto physical=m11PhysicalExactSettings();
    const auto makePhysical=[&](SculptCollisionSettings settings) {
        return VersionedSculptBrush(
            original,chosen.strength,{},{},{},256,{},std::move(settings));
    };
    auto standard=makePhysical(physical);
    standard.setOriginalPatchIntent(chosen.deliberate);
    standard.stamp(dummy,input);
    require(standard.completedUnions()==1,
            "M43 real reflected original-source physical union must positively complete before contour calibration");
    const auto truth=standard.lastStepResourceWork().exactStage;
    auto exact=physical;
    const auto active=fillLimits(exact,truth);
    require(active>=3&&truth.contourEdgeProbes>0&&
            truth.contourBoundaryEdges>0&&
            truth.contourLoopVisits>0&&!truth.overflow,
            "M43 physical source union genuinely performs three kinds of private c3/d2 contour work");
    auto equal=makePhysical(exact);
    equal.setOriginalPatchIntent(chosen.deliberate);
    equal.stamp(dummy,input);
    require(equal.completedUnions()==1&&
            equal.lastStepResourceWork().published,
            "M43 all measured original-root physical contour/closure categories allow exact-boundary single CAS");
    for(const auto& bucket:buckets) {
        if(!(truth.*(bucket.count)))continue; // no fictional work
        auto tooFew=exact;
        --(tooFew.exactStageLimits.*(bucket.limit));
        auto denied=makePhysical(tooFew);
        denied.setOriginalPatchIntent(chosen.deliberate);
        const auto before=denied.checkpoint();
        denied.stamp(dummy,input);
        const auto after=denied.checkpoint();
        const auto& attempt=denied.lastStepResourceWork();
        require(denied.rejectedStamps()==1&&
                denied.completedUnions()==0&&
                denied.publishedRevisions()==0&&
                denied.historyStatistics().undoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                after.index==before.index&&
                after.mirrorPairs==before.mirrorPairs&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                attempt.evaluated&&attempt.refused&&!attempt.published&&
                attempt.exactStage.contourEdgeProbes==
                    truth.contourEdgeProbes&&
                attempt.exactStage.contourBoundaryEdges==
                    truth.contourBoundaryEdges&&
                attempt.refusal.find(bucket.category)!=std::string::npos,
                "M43 each actual physical contour/closure one-under refusal preserves source, mirrors, renderer, history and complete attempted work");
    }
    const auto mesh=twoIntersectingTetrahedra();
    const auto sourceFaces=firstOriginalFaces(mesh);
    const auto unionLimits=supportedUnionLimits(false);
    SculptCollisionSettings queuedPolicy;
    queuedPolicy.physicalExact=true;
    queuedPolicy.originalTerminal.maximumProtectedDeviationInEdges=0;
    const auto makeQueued=[&](SculptCollisionSettings settings) {
        return VersionedSculptBrush(
            mesh,.006,{},{},{},false,256,{},std::move(settings));
    };
    auto candidate=makeQueued(queuedPolicy);
    const auto candidateRoot=candidate.snapshot();
    require(candidate.mergeExactContacts(
                candidateRoot,fullOriginalContacts(*candidateRoot),
                unionLimits,sourceFaces),
            "M43 independently true queued c1/c2/c3/M03/d2 original-root Boolean completes");
    const auto queueTruth=candidate.lastStepResourceWork().exactStage;
    const auto queueActive=fillLimits(queuedPolicy,queueTruth);
    require(queueActive>=3&&queueTruth.contourEdgeProbes>0&&
            queueTruth.contourBoundaryEdges>0&&
            queueTruth.contourLoopVisits>0&&!queueTruth.overflow,
            "M43 real queued source union contains nonzero repeated contour extraction and loop work");
    auto queueExactly=makeQueued(queuedPolicy);
    const auto qBefore=queueExactly.snapshot();
    require(queueExactly.mergeExactContacts(
                qBefore,fullOriginalContacts(*qBefore),
                unionLimits,sourceFaces)&&
            queueExactly.lastStepResourceWork().published,
            "M43 queued original-root exact contour/closure boundaries accept real complete source work");
    for(const auto& bucket:buckets) {
        if(!(queueTruth.*(bucket.count)))continue;
        auto oneUnder=queuedPolicy;
        --(oneUnder.exactStageLimits.*(bucket.limit));
        auto denied=makeQueued(oneUnder);
        const auto before=denied.checkpoint();
        require(!denied.mergeExactContacts(
                    before.index,fullOriginalContacts(*before.index),
                    unionLimits,sourceFaces),
                "M43 queued one-under real original-root contour work must refuse");
        const auto after=denied.checkpoint();
        const auto& attempt=denied.lastStepResourceWork();
        require(after.index==before.index&&
                after.mirrorPairs==before.mirrorPairs&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                denied.completedUnions()==0&&
                denied.publishedRevisions()==0&&
                denied.rejectedUnions()==1&&
                denied.historyStatistics().undoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                attempt.evaluated&&attempt.refused&&!attempt.published&&
                attempt.exactStage.contourEdgeProbes==
                    queueTruth.contourEdgeProbes&&
                attempt.refusal.find(bucket.category)!=std::string::npos,
                "M43 queued independently named one-under contour/closure refuses without source, mirror, dirty or history side effects");
    }
    std::cout<<"[m43] original physical contour edge probes="
             <<truth.contourEdgeProbes<<" loop="<<truth.contourLoopVisits
             <<" ring="<<truth.closureRingTests
             <<" closure samples="<<truth.closureSampleTests
             <<"; queued probes="<<queueTruth.contourEdgeProbes
             <<" loop="<<queueTruth.contourLoopVisits
             <<" ring="<<queueTruth.closureRingTests<<"\n";
}

// M44: all actual joined original-root work, never just final seam size.
// The real physical mirrored and independent queued source-certified
// Booleans provide complete pre-CAS positive/one-under cost calibration.
// Unequal source subdivision refinement is separately tested in the
// genuine three-solid generic transverse graph collision suite.
void testM44OriginalSeamTraceJoinWork() {
    struct Bucket {
        std::size_t ExactStageWork::*actual;
        std::size_t ExactStageWorkLimits::*ceiling;
        const char* reason;
    };
    constexpr std::array<Bucket,7> categories{{
        {&ExactStageWork::seamTraceProbes,
         &ExactStageWorkLimits::maxSeamTraceProbes,
         "seam trace-probe budget"},
        {&ExactStageWork::seamMatchedIntervals,
         &ExactStageWorkLimits::maxSeamMatchedIntervals,
         "seam matched-interval budget"},
        {&ExactStageWork::seamWeldComparisons,
         &ExactStageWorkLimits::maxSeamWeldComparisons,
         "seam weld-comparison budget"},
        {&ExactStageWork::seamRefinePredicates,
         &ExactStageWorkLimits::maxSeamRefinePredicates,
         "seam refinement predicate budget"},
        {&ExactStageWork::seamRefineMatches,
         &ExactStageWorkLimits::maxSeamRefineMatches,
         "seam refinement match budget"},
        {&ExactStageWork::seamMirrorOrbitChecks,
         &ExactStageWorkLimits::maxSeamMirrorOrbitChecks,
         "seam mirror-orbit budget"},
        {&ExactStageWork::seamFaceRewires,
         &ExactStageWorkLimits::maxSeamFaceRewires,
         "seam face-rewire budget"}
    }};
    const auto setExact=[&](SculptCollisionSettings& limits,
                            const ExactStageWork& measured) {
        std::size_t nonzero=0;
        for(const auto& kind:categories) {
            const auto value=measured.*(kind.actual);
            limits.exactStageLimits.*(kind.ceiling)=value;
            if(value)++nonzero;
        }
        return nonzero;
    };
    const auto picked=chooseM39FourDimensionalSource();
    const auto first=picked.original;
    const auto input=std::array<BrushArea,2>{{
        {picked.source.input[0][0].center,6,.3},
        {{-picked.source.input[0][0].center.x,
          picked.source.input[0][0].center.y,
          picked.source.input[0][0].center.z},6,.3}
    }};
    const auto defaultPhysical=m11PhysicalExactSettings();
    const auto newPhysical=[&](SculptCollisionSettings configuration) {
        return VersionedSculptBrush(
            first,picked.strength,{},{},{},256,{},std::move(configuration));
    };
    Mesh dummy;
    auto reference=newPhysical(defaultPhysical);
    reference.setOriginalPatchIntent(picked.deliberate);
    reference.stamp(dummy,input);
    require(reference.completedUnions()==1,
            "M44 a genuine normal-driven 72-source mirrored final-root union completes before seam calibration");
    const auto truth=reference.lastStepResourceWork().exactStage;
    require(truth.seamTraceProbes>0&&truth.seamMatchedIntervals>0&&
            truth.seamWeldComparisons>0&&truth.seamMirrorOrbitChecks>0&&
            !truth.overflow,
            "M44 physical exact original cut/join executes real source-aligned seam traces, certified intervals, original-ID weld and mirrored-orbit validation");
    auto equal=defaultPhysical;
    const auto active=setExact(equal,truth);
    require(active>=4,"M44 positive mirrored source Step exercises 4+ seam categories");
    auto accepted=newPhysical(equal);
    accepted.setOriginalPatchIntent(picked.deliberate);
    accepted.stamp(dummy,input);
    require(accepted.completedUnions()==1&&
            accepted.lastStepResourceWork().published,
            "M44 true physical final original-root Step accepts every measured seam category at exact ceiling");
    for(const auto& kind:categories) {
        if(!(truth.*(kind.actual)))continue;
        auto under=equal;
        --(under.exactStageLimits.*(kind.ceiling));
        auto denied=newPhysical(under);
        denied.setOriginalPatchIntent(picked.deliberate);
        const auto before=denied.checkpoint();
        denied.stamp(dummy,input);
        const auto after=denied.checkpoint();
        const auto& attempted=denied.lastStepResourceWork();
        require(denied.rejectedStamps()==1&&denied.completedUnions()==0&&
                denied.publishedRevisions()==0&&
                denied.historyStatistics().undoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                after.index==before.index&&
                after.mirrorPairs==before.mirrorPairs&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                attempted.evaluated&&attempted.refused&&!attempted.published&&
                attempted.exactStage.seamTraceProbes==truth.seamTraceProbes&&
                attempted.refusal.find(kind.reason)!=std::string::npos,
                "M44 each truly exercised physical source-seam one-under category refuses atomically with accurate attempted work");
    }

    const auto mesh=twoIntersectingTetrahedra();
    const auto sources=firstOriginalFaces(mesh);
    const auto limits=supportedUnionLimits(false);
    SculptCollisionSettings queued;
    queued.physicalExact=true;
    queued.originalTerminal.maximumProtectedDeviationInEdges=0;
    const auto newQueued=[&](SculptCollisionSettings configuration) {
        return VersionedSculptBrush(
            mesh,.006,{},{},{},false,256,{},std::move(configuration));
    };
    auto source=newQueued(queued);
    const auto original=source.snapshot();
    require(source.mergeExactContacts(
                original,fullOriginalContacts(*original),limits,sources),
            "M44 independently genuine c1/c2/c3/M03/d2 queued exact source-owned Boolean union completes");
    const auto queueTruth=source.lastStepResourceWork().exactStage;
    require(queueTruth.seamTraceProbes>0&&
            queueTruth.seamMatchedIntervals>0&&
            queueTruth.seamWeldComparisons>0&&!queueTruth.overflow,
            "M44 source-certified queued exact union truly scans and joins the source-boundary intervals");
    const auto queueActive=setExact(queued,queueTruth);
    require(queueActive>=3,
            "M44 authenticated queued original-root join contains at least 3 measured positive seam categories");
    auto queueExact=newQueued(queued);
    const auto q0=queueExact.snapshot();
    require(queueExact.mergeExactContacts(
                q0,fullOriginalContacts(*q0),limits,sources)&&
            queueExact.lastStepResourceWork().published,
            "M44 queued original-root complete measured seam categories accept at exact boundary");
    for(const auto& kind:categories) {
        if(!(queueTruth.*(kind.actual)))continue;
        auto under=queued;
        --(under.exactStageLimits.*(kind.ceiling));
        auto denied=newQueued(under);
        const auto before=denied.checkpoint();
        require(!denied.mergeExactContacts(
                    before.index,fullOriginalContacts(*before.index),
                    limits,sources),
                "M44 real queued one-under original seam work must refuse");
        const auto after=denied.checkpoint();
        const auto& attempted=denied.lastStepResourceWork();
        require(after.index==before.index&&
                after.mirrorPairs==before.mirrorPairs&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                denied.publishedRevisions()==0&&
                denied.completedUnions()==0&&denied.rejectedUnions()==1&&
                denied.historyStatistics().undoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                attempted.evaluated&&attempted.refused&&!attempted.published&&
                attempted.exactStage.seamTraceProbes==
                    queueTruth.seamTraceProbes&&
                attempted.refusal.find(kind.reason)!=std::string::npos,
                "M44 every independently exercised queued seam source category one-under refuses before root CAS, preserves source/mirrors/history/dirty");
    }
    std::cout<<"[m44] physical source seam probes="<<truth.seamTraceProbes
             <<" intervals="<<truth.seamMatchedIntervals
             <<" weld comparisons="<<truth.seamWeldComparisons
             <<" mirrors="<<truth.seamMirrorOrbitChecks
             <<"; queued seam probes="<<queueTruth.seamTraceProbes
             <<" intervals="<<queueTruth.seamMatchedIntervals
             <<" comparisons="<<queueTruth.seamWeldComparisons<<"\n";
}

// M45: real repeated FIRST-root evidence, protected Exposed/ReversedBoundary
// feature decisions, verified source history and source-owned mirror orbits.
// Each supported complete original-root physical/queued candidate accepts
// at the exact measured per-category limit and one fewer refuses pre-CAS.
void testM45FirstOriginalFeatureHistoryWork() {
    struct Bucket {
        std::size_t ExactStageWork::*actual;
        std::size_t ExactStageWorkLimits::*ceiling;
        const char* reason;
    };
    constexpr std::array<Bucket,9> categories{{
        {&ExactStageWork::originalAnchorProofs,
         &ExactStageWorkLimits::maxOriginalAnchorProofs,
         "first-original anchor-proof budget"},
        {&ExactStageWork::originalDescendantProofs,
         &ExactStageWorkLimits::maxOriginalDescendantProofs,
         "first-original descendant-proof budget"},
        {&ExactStageWork::originalRetirementProofs,
         &ExactStageWorkLimits::maxOriginalRetirementProofs,
         "first-original retirement-proof budget"},
        {&ExactStageWork::originalWitnessTriangles,
         &ExactStageWorkLimits::maxOriginalWitnessTriangles,
         "first-original witness-triangle budget"},
        {&ExactStageWork::originalIntentChecks,
         &ExactStageWorkLimits::maxOriginalIntentChecks,
         "first-original intent-check budget"},
        {&ExactStageWork::originalProtectedFeatures,
         &ExactStageWorkLimits::maxOriginalProtectedFeatures,
         "first-original protected-feature budget"},
        {&ExactStageWork::originalMirrorOrbitChecks,
         &ExactStageWorkLimits::maxOriginalMirrorOrbitChecks,
         "first-original mirror-orbit budget"},
        {&ExactStageWork::originalHistoryRoots,
         &ExactStageWorkLimits::maxOriginalHistoryRoots,
         "first-original history-root budget"},
        {&ExactStageWork::originalHistoryTransitions,
         &ExactStageWorkLimits::maxOriginalHistoryTransitions,
         "first-original history-transition budget"}
    }};
    const auto setExact=[&](SculptCollisionSettings& limits,
                            const ExactStageWork& measured) {
        std::size_t nonzero=0;
        for(const auto& kind:categories) {
            const auto value=measured.*(kind.actual);
            limits.exactStageLimits.*(kind.ceiling)=value;
            if(value)++nonzero;
        }
        return nonzero;
    };
    const auto picked=chooseM39FourDimensionalSource();
    const auto original=picked.original;
    const auto input=std::array<BrushArea,2>{{
        {picked.source.input[0][0].center,6,.3},
        {{-picked.source.input[0][0].center.x,
          picked.source.input[0][0].center.y,
          picked.source.input[0][0].center.z},6,.3}
    }};
    const auto newPhysical=[&](SculptCollisionSettings settings) {
        return VersionedSculptBrush(
            original,picked.strength,{},{},{},256,{},std::move(settings));
    };
    Mesh dummy;
    auto physical=m11PhysicalExactSettings();
    auto reference=newPhysical(physical);
    reference.setOriginalPatchIntent(picked.deliberate);
    reference.stamp(dummy,input);
    require(reference.completedUnions()==1&&
            reference.lastStepResourceWork().published,
            "M45 real normal-driven protected reflected 72-original source union accepts");
    const auto actual=reference.lastStepResourceWork().exactStage;
    require(actual.originalAnchorProofs>0&&
            actual.originalDescendantProofs>0&&
            actual.originalWitnessTriangles>0&&
            actual.originalIntentChecks>0&&
            actual.originalProtectedFeatures>0&&
            actual.originalMirrorOrbitChecks>0&&
            actual.originalHistoryRoots>0&&
            actual.originalHistoryTransitions>=2&&
            !actual.overflow,
            "M45 physical first-original witnesses, intended/40 protected decisions, mirrored source orbits and repeated pre-CAS M29 proofs truly execute");
    const auto active=setExact(physical,actual);
    require(active>=8,
            "M45 reflected protected physical Step measures at least eight genuine source/feature/history categories");
    auto equal=newPhysical(physical);
    equal.setOriginalPatchIntent(picked.deliberate);
    equal.stamp(dummy,input);
    require(equal.completedUnions()==1&&
            equal.lastStepResourceWork().published&&
            equal.lastStepResourceWork().exactStage.originalWitnessTriangles==
                actual.originalWitnessTriangles,
            "M45 exact full-source work ceilings preserve true protected physical positive");
    for(const auto& kind:categories) {
        if(!(actual.*(kind.actual)))continue;
        auto under=physical;
        --(under.exactStageLimits.*(kind.ceiling));
        auto denied=newPhysical(under);
        denied.setOriginalPatchIntent(picked.deliberate);
        const auto before=denied.checkpoint();
        denied.stamp(dummy,input);
        const auto after=denied.checkpoint();
        const auto& attempted=denied.lastStepResourceWork();
        require(denied.rejectedStamps()==1&&
                denied.completedUnions()==0&&
                denied.publishedRevisions()==0&&
                denied.historyStatistics().undoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                after.index==before.index&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                after.mirrorPairs==before.mirrorPairs&&
                attempted.evaluated&&attempted.refused&&!attempted.published&&
                attempted.exactStage.originalWitnessTriangles==
                    actual.originalWitnessTriangles&&
                attempted.refusal.find(kind.reason)!=std::string::npos,
                "M45 each genuinely measured physical source/feature/history one-under refusal is named and atomic");
    }
    // Reexecuting the seven-first-original witness audit on an unchanged
    // authenticated root is REAL additional work; measuring a cached
    // lifetime pointer alone performs no closest-triangle comparison.
    const auto lifetime=reference.checkpoint().verifiedOriginalFidelity;
    require(lifetime&&lifetime->anchors.original==original.index,
            "M45 complete physical source keeps immutable FIRST reference");
    ExactStageWork repeated;
    {
        ScopedExactStageWork meter(repeated);
        const auto first=auditOriginalPatchLifetime(
            *lifetime,original.index);
        const auto second=auditOriginalPatchLifetime(
            *lifetime,original.index);
        require(first.total.distanceTests>0&&
                first.total.distanceTests==second.total.distanceTests&&
                repeated.originalWitnessTriangles==
                    first.total.distanceTests+second.total.distanceTests,
                "M45 two actual repeated immutable seven-point source audits charge BOTH complete closest-triangle passes");
        try {
            (void)auditOriginalPatchLifetime(
                *lifetime,original.index,first.total.distanceTests-1);
            throw std::runtime_error(
                "M45 one-under local audit should refuse before witness comparisons");
        }catch(const std::length_error&) {}
        require(repeated.originalWitnessTriangles==
                    first.total.distanceTests+second.total.distanceTests,
                "M45 local preflight refusal must not charge phantom witness triangles");
    }

    const auto mesh=twoIntersectingTetrahedra();
    const auto originals=firstOriginalFaces(mesh);
    const auto exactCutLimits=supportedUnionLimits(false);
    SculptCollisionSettings queued;
    queued.physicalExact=true;
    queued.originalTerminal.maximumProtectedDeviationInEdges=0;
    const auto newQueued=[&](SculptCollisionSettings settings) {
        return VersionedSculptBrush(
            mesh,.006,{},{},{},false,256,{},std::move(settings));
    };
    auto source=newQueued(queued);
    const auto root=source.snapshot();
    require(source.mergeExactContacts(
                root,fullOriginalContacts(*root),exactCutLimits,originals),
            "M45 independent authenticated asymmetric queued c1/c2/c3/M03/d2 positive completes");
    const auto queuedWork=source.lastStepResourceWork().exactStage;
    require(queuedWork.originalAnchorProofs>0&&
            queuedWork.originalDescendantProofs>0&&
            queuedWork.originalWitnessTriangles>0&&
            queuedWork.originalIntentChecks>0&&
            queuedWork.originalHistoryRoots>0&&
            queuedWork.originalHistoryTransitions>=2&&
            !queuedWork.overflow,
            "M45 independent queued Step performs authentic first-original/history source proof work");
    const auto queuedActive=setExact(queued,queuedWork);
    require(queuedActive>=6,
            "M45 asymmetric queued proof exercises independently measured source categories");
    auto accepted=newQueued(queued);
    const auto old=accepted.snapshot();
    require(accepted.mergeExactContacts(
                old,fullOriginalContacts(*old),exactCutLimits,originals)&&
            accepted.lastStepResourceWork().published,
            "M45 authenticated queued candidate accepts exact original/history ceilings");
    for(const auto& kind:categories) {
        if(!(queuedWork.*(kind.actual)))continue;
        auto under=queued;
        --(under.exactStageLimits.*(kind.ceiling));
        auto denied=newQueued(under);
        const auto before=denied.checkpoint();
        require(!denied.mergeExactContacts(
                    before.index,fullOriginalContacts(*before.index),
                    exactCutLimits,originals),
                "M45 queued exact one-under first-original work must refuse");
        const auto after=denied.checkpoint();
        const auto& attempted=denied.lastStepResourceWork();
        require(after.index==before.index&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                after.mirrorPairs==before.mirrorPairs&&
                denied.publishedRevisions()==0&&
                denied.completedUnions()==0&&denied.rejectedUnions()==1&&
                denied.historyStatistics().undoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                attempted.evaluated&&attempted.refused&&!attempted.published&&
                attempted.exactStage.originalWitnessTriangles==
                    queuedWork.originalWitnessTriangles&&
                attempted.refusal.find(kind.reason)!=std::string::npos,
                "M45 each actually executed queued source/history one-under category refuses before CAS without mutating source, mirror, dirty or history");
    }
    std::cout<<"[m45] protected physical witnesses="
             <<actual.originalWitnessTriangles
             <<" feature decisions="<<actual.originalProtectedFeatures
             <<" mirror comparisons="<<actual.originalMirrorOrbitChecks
             <<" history roots="<<actual.originalHistoryRoots
             <<"; queued witnesses="<<queuedWork.originalWitnessTriangles
             <<" history transitions="<<queuedWork.originalHistoryTransitions
             <<"; repeated standalone witnesses="
             <<repeated.originalWitnessTriangles<<"\n";
}

// M46: observe actual physical and queued source-certified operations through
// the same bounded FIFO/history/checkpoint interface used by the viewport.
// Attempted work is never equated with published work or with process RSS.
void testM46CumulativeSourceWorkerDiagnostics() {
    const auto chosen=chooseM39FourDimensionalSource();
    const auto physical=m11PhysicalExactSettings();
    const auto start=chosen.source.input[0][0].center;
    const auto newPhysical=[&](SculptCollisionSettings policy) {
        return VersionedSculptBrush(
            chosen.original,chosen.strength,{},{},{},256,{},policy);
    };
    Mesh dummy;
    auto direct=newPhysical(physical);
    require(direct.resourceTotals().attempts==0&&
            direct.resourceTotals().published==0,
            "M46 newly constructed protected physical controller starts telemetry at zero");
    direct.setOriginalPatchIntent(chosen.deliberate);
    const std::array<BrushArea,2> area{{
        {start,6,.3},{{-start.x,start.y,start.z},6,.3}
    }};
    direct.stamp(dummy,area);
    require(direct.completedUnions()==1,"M46 genuine direct mirrored source union succeeds");
    const auto dp=direct.lastStepResourceWork();
    const auto sum=direct.resourceTotals();
    require(dp.path==SculptStepResourcePath::PhysicalStamp&&
            dp.published&&dp.evaluated&&
            dp.stageWall.initialGeometryMs>=0&&
            dp.stageWall.sourceGeometryMs>=0&&
            dp.stageWall.originalProofMs>=0&&
            dp.stageWall.admissionMs>=0&&
            dp.stageWall.publicationAndHistoryMs>=0&&
            dp.exactStage.originalWitnessTriangles>0&&
            sum.attempts==1&&sum.physicalAttempts==1&&
            sum.published==1&&sum.refused==0&&
            sum.attemptedPrivateCopiedBytes==dp.privateCopiedBytes&&
            sum.publishedPrivateCopiedBytes==dp.privateCopiedBytes&&
            sum.attemptedExact.originalWitnessTriangles==
                dp.exactStage.originalWitnessTriangles&&
            sum.publishedExact.originalWitnessTriangles==
                dp.exactStage.originalWitnessTriangles&&
            sum.observedAttemptMilliseconds>=0,
            "M46 true direct physical source work and timed stages split attempted/published categories");
    auto physicalUnder=physical;
    physicalUnder.exactStageLimits.maxOriginalWitnessTriangles=
        dp.exactStage.originalWitnessTriangles-1;
    auto directDenied=newPhysical(physicalUnder);
    directDenied.setOriginalPatchIntent(chosen.deliberate);
    const auto physicalRoot=directDenied.snapshot();
    directDenied.stamp(dummy,area);
    const auto refusal=directDenied.lastStepResourceWork();
    const auto denied=directDenied.resourceTotals();
    require(directDenied.snapshot()==physicalRoot&&
            directDenied.historyStatistics().undoDepth==0&&
            refusal.refused&&!refusal.published&&refusal.evaluated&&
            refusal.refusalCategory==SculptRefusalCategory::FiniteWorkBudget&&
            denied.attempts==1&&denied.refused==1&&denied.budgetRefusals==1&&
            denied.published==0&&
            denied.attemptedExact.originalWitnessTriangles==
                dp.exactStage.originalWitnessTriangles&&
            denied.publishedExact.originalWitnessTriangles==0&&
            denied.publishedPrivateCopiedBytes==0&&
            denied.attemptedPrivateCopiedBytes>0&&
            directDenied.lastDirtyPartitions().empty(),
            "M46 physical true M45 source limit one-under preserves immutable root and counts only attempted finite work");

    const auto drain=[](AsyncSculptWorker& worker) {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(30);
        while(!worker.canBegin()&&std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.canBegin(),"M46 real source physical FIFO drains");
    };
    AsyncSculptWorker physicalWorker(chosen.original,.025,chosen.strength,.2,
                                    {},{},{},{},physical);
    physicalWorker.begin(start,6,chosen.deliberate);
    physicalWorker.end(start,0);
    drain(physicalWorker);
    const auto pw=physicalWorker.status();
    require(pw.completedUnions==1&&pw.resources.published==1&&
            pw.resources.physicalAttempts==1&&
            pw.resources.attemptedExact.originalWitnessTriangles>0&&
            pw.stepResources.stageWall.originalProofMs>=0&&
            pw.history.undoDepth==1&&pw.snapshotPublications>=1&&
            pw.lastSnapshotDirtyOwners>0&&
            pw.totalSnapshotDirtyOwners>=pw.lastSnapshotDirtyOwners&&
            pw.resources.publishedPrivateCopiedBytes>0,
            "M46 real 4D protected physical worker exports cumulative and last-Step phase/dirty/history facts");
    const auto prior=pw.resources;
    physicalWorker.undo();
    {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(physicalWorker.status().completedUndos==0&&
              std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto undone=physicalWorker.status();
    require(undone.completedUndos==1&&undone.history.redoDepth==1&&
            undone.resources.attempts==prior.attempts&&
            undone.resources.published==prior.published,
            "M46 undo changes published root/history but does not falsely charge a new sculpt attempt");
    physicalWorker.redo();
    {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(physicalWorker.status().completedRedos==0&&
              std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto redone=physicalWorker.status();
    require(redone.completedRedos==1&&redone.history.redoDepth==0&&
            redone.resources.attempts==prior.attempts&&
            redone.resources.published==prior.published,
            "M46 redo keeps work telemetry historical, never replaying previous work counts");

    const auto mesh=twoIntersectingTetrahedra();
    const auto originalSources=firstOriginalFaces(mesh);
    const auto unionLimits=supportedUnionLimits(false);
    SculptCollisionSettings queued;
    queued.physicalExact=true;
    queued.originalTerminal.maximumProtectedDeviationInEdges=0;
    auto reference=VersionedSculptBrush(
        mesh,.006,{},{},{},false,256,{},queued);
    const auto first=reference.snapshot();
    require(reference.mergeExactContacts(first,fullOriginalContacts(*first),
                                         unionLimits,originalSources),
            "M46 authenticated direct queued candidate accepted before FIFO calibration");
    const auto qWork=reference.lastStepResourceWork();
    const auto qTotals=reference.resourceTotals();
    require(qWork.published&&qWork.stageWall.initialGeometryMs>=0&&
            qWork.stageWall.sourceGeometryMs>=0&&
            qWork.stageWall.originalProofMs>=0&&
            qWork.stageWall.admissionMs>=0&&
            qWork.stageWall.publicationAndHistoryMs>=0&&
            qWork.exactStage.originalWitnessTriangles>0&&
            qTotals.queuedExactAttempts==1&&qTotals.published==1&&
            qTotals.publishedExact.originalWitnessTriangles==
                qWork.exactStage.originalWitnessTriangles,
            "M46 real queued exact direct path records separate measured proof phases and work");
    AsyncSculptWorker queuedWorker(mesh,.025,.006,.2,
                                   {},{},{},false,{},queued);
    const auto queuedRoot=queuedWorker.initialRevision();
    const auto contacts=fullOriginalContacts(*queuedRoot);
    queuedWorker.mergeExactContacts(queuedRoot,contacts,
                                    unionLimits,originalSources);
    waitForUnion(queuedWorker,1);
    const auto accepted=queuedWorker.status();
    require(accepted.completedUnions==1&&accepted.resources.attempts==1&&
            accepted.resources.published==1&&
            accepted.resources.queuedExactAttempts==1&&
            accepted.resources.publishedPrivateCopiedBytes>0&&
            accepted.resources.publishedExact.originalWitnessTriangles>0&&
            accepted.history.undoDepth==1&&
            accepted.snapshotPublications>=1,
            "M46 actual accepted queued original-root FIFO exports published exact work and bounded history");
    queuedWorker.mergeExactContacts(queuedRoot,contacts,
                                    unionLimits,originalSources);
    waitForUnion(queuedWorker,2);
    const auto stale=queuedWorker.status();
    require(stale.rejectedUnions==1&&stale.resources.attempts==2&&
            stale.resources.published==1&&stale.resources.refused==1&&
            stale.resources.staleRefusals==1&&
            stale.resources.attemptedPrivateCopiedBytes==
                accepted.resources.attemptedPrivateCopiedBytes&&
            stale.resources.publishedPrivateCopiedBytes==
                accepted.resources.publishedPrivateCopiedBytes&&
            stale.history.undoDepth==1,
            "M46 stale queued source-root refusal increments only attempted and stale-category counters");
    const auto checkpoint=std::filesystem::temp_directory_path()/
        "mesher-v17-m46-live-cumulative-diagnostics.mshr";
    try {
        queuedWorker.saveCheckpoint(checkpoint);
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(queuedWorker.checkpointStatistics().completed==0&&
              queuedWorker.checkpointStatistics().failed==0&&
              std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto saved=queuedWorker.status();
        require(saved.checkpoints.completed==1&&
                saved.checkpoints.failed==0,
                "M46 durable checkpoint writer completes after genuine queued union");
        const auto restored=readCheckpoint(checkpoint);
        require(restored.index&&restored.originalFidelityRequired&&
                !restored.verifiedOriginalFidelity&&
                saved.resources.attempts==2,
                "M46 disk restore preserves protected provenance marker but never invents M29 credentials or persisted work totals");
        bool refused=false;
        try {
            (void)AsyncSculptWorker(restored,.025,.006,.2,{},{},{},{},queued);
        }catch(const std::logic_error&) {refused=true;}
        require(refused,
                "M46 untrusted disk snapshot cannot restart a protected source worker with synthesized original lifetime");
    }catch(...) {
        std::error_code ec;std::filesystem::remove(checkpoint,ec);throw;
    }
    std::error_code ec;std::filesystem::remove(checkpoint,ec);
    auto queuedUnder=queued;
    queuedUnder.exactStageLimits.maxOriginalWitnessTriangles=
        qWork.exactStage.originalWitnessTriangles-1;
    AsyncSculptWorker refusedWorker(mesh,.025,.006,.2,
                                    {},{},{},false,{},queuedUnder);
    const auto oldRefused=refusedWorker.initialRevision();
    refusedWorker.mergeExactContacts(
        oldRefused,fullOriginalContacts(*oldRefused),
        unionLimits,originalSources);
    waitForUnion(refusedWorker,1);
    const auto bad=refusedWorker.status();
    require(bad.rejectedUnions==1&&bad.completedUnions==0&&
            bad.resources.attempts==1&&bad.resources.refused==1&&
            bad.resources.budgetRefusals==1&&
            bad.resources.published==0&&
            bad.resources.attemptedExact.originalWitnessTriangles==
                qWork.exactStage.originalWitnessTriangles&&
            bad.resources.publishedExact.originalWitnessTriangles==0&&
            bad.resources.attemptedPrivateCopiedBytes>0&&
            bad.resources.publishedPrivateCopiedBytes==0&&
            bad.history.undoDepth==0&&
            refusedWorker.initialRevision()==oldRefused&&
            bad.stepResources.refusalCategory==
                SculptRefusalCategory::FiniteWorkBudget,
            "M46 queued one-under true M45 work refuses atomically while diagnostics distinguish attempted cost from published revisions");
    std::cout<<"[m46] physical attempted="<<sum.attempts
             <<" protected witnesses="<<sum.attemptedExact.originalWitnessTriangles
             <<"; FIFO published="<<pw.resources.published
             <<" checkpoint histories="<<queuedWorker.checkpointStatistics().completed
             <<"; queued attempted="<<stale.resources.attempts
             <<" refused="<<stale.resources.refused<<"\n";
}

// Original M42 integrated acceptance. M40/41/43/44/45/46 operate
// SIMULTANEOUSLY on the same genuine, source-authenticated final-root
// physical and queued candidates. Independent peer tests remain the
// authority for optional dimensions absent from a supported fixture.
void testOriginalM42IntegratedBoundedSourceAndWorkerAcceptance() {
    struct Bucket {
        std::size_t ExactStageWork::*work;
        std::size_t ExactStageWorkLimits::*limit;
        const char* refusal;
    };
    constexpr std::array<Bucket,35> buckets{{
        {&ExactStageWork::collisionQueries,&ExactStageWorkLimits::maxCollisionQueries,"Aggregate source collision query budget exhausted"},
        {&ExactStageWork::collisionBoxes,&ExactStageWorkLimits::maxCollisionBoxes,"Aggregate source collision box budget exhausted"},
        {&ExactStageWork::collisionPartitions,&ExactStageWorkLimits::maxCollisionPartitions,"Aggregate source collision partition budget exhausted"},
        {&ExactStageWork::collisionCandidates,&ExactStageWorkLimits::maxCollisionCandidates,"Aggregate source collision candidate budget exhausted"},
        {&ExactStageWork::collisionNarrowPhase,&ExactStageWorkLimits::maxCollisionNarrowPhase,"Aggregate source collision narrow-phase budget exhausted"},
        {&ExactStageWork::windingQueries,&ExactStageWorkLimits::maxWindingQueries,"Aggregate source winding query budget exhausted"},
        {&ExactStageWork::windingBoxes,&ExactStageWorkLimits::maxWindingBoxes,"Aggregate source winding box budget exhausted"},
        {&ExactStageWork::windingPartitions,&ExactStageWorkLimits::maxWindingPartitions,"Aggregate source winding partition budget exhausted"},
        {&ExactStageWork::windingTriangles,&ExactStageWorkLimits::maxWindingTriangles,"Aggregate source winding triangle budget exhausted"},
        {&ExactStageWork::windingRays,&ExactStageWorkLimits::maxWindingRays,"Aggregate source winding ray budget exhausted"},
        {&ExactStageWork::arrangementPairTests,&ExactStageWorkLimits::maxArrangementPairTests,"Aggregate source arrangement pair-test budget exhausted"},
        {&ExactStageWork::arrangementPredicates,&ExactStageWorkLimits::maxArrangementPredicates,"Aggregate source arrangement predicate budget exhausted"},
        {&ExactStageWork::contourEdgeProbes,&ExactStageWorkLimits::maxContourEdgeProbes,"Aggregate source contour edge-probe budget exhausted"},
        {&ExactStageWork::contourBoundaryEdges,&ExactStageWorkLimits::maxContourBoundaryEdges,"Aggregate source contour boundary-edge budget exhausted"},
        {&ExactStageWork::contourLoopVisits,&ExactStageWorkLimits::maxContourLoopVisits,"Aggregate source contour loop-visit budget exhausted"},
        {&ExactStageWork::closureRingTests,&ExactStageWorkLimits::maxClosureRingTests,"Aggregate source closure ring-test budget exhausted"},
        {&ExactStageWork::closurePairCandidates,&ExactStageWorkLimits::maxClosurePairCandidates,"Aggregate source closure pair-candidate budget exhausted"},
        {&ExactStageWork::closureSampleTests,&ExactStageWorkLimits::maxClosureSampleTests,"Aggregate source closure sample-test budget exhausted"},
        {&ExactStageWork::closureDpTransitions,&ExactStageWorkLimits::maxClosureDpTransitions,"Aggregate source closure DP-transition budget exhausted"},
        {&ExactStageWork::seamTraceProbes,&ExactStageWorkLimits::maxSeamTraceProbes,"Aggregate source seam trace-probe budget exhausted"},
        {&ExactStageWork::seamMatchedIntervals,&ExactStageWorkLimits::maxSeamMatchedIntervals,"Aggregate source seam matched-interval budget exhausted"},
        {&ExactStageWork::seamWeldComparisons,&ExactStageWorkLimits::maxSeamWeldComparisons,"Aggregate source seam weld-comparison budget exhausted"},
        {&ExactStageWork::seamRefinePredicates,&ExactStageWorkLimits::maxSeamRefinePredicates,"Aggregate source seam refinement predicate budget exhausted"},
        {&ExactStageWork::seamRefineMatches,&ExactStageWorkLimits::maxSeamRefineMatches,"Aggregate source seam refinement match budget exhausted"},
        {&ExactStageWork::seamMirrorOrbitChecks,&ExactStageWorkLimits::maxSeamMirrorOrbitChecks,"Aggregate source seam mirror-orbit budget exhausted"},
        {&ExactStageWork::seamFaceRewires,&ExactStageWorkLimits::maxSeamFaceRewires,"Aggregate source seam face-rewire budget exhausted"},
        {&ExactStageWork::originalAnchorProofs,&ExactStageWorkLimits::maxOriginalAnchorProofs,"Aggregate first-original anchor-proof budget exhausted"},
        {&ExactStageWork::originalDescendantProofs,&ExactStageWorkLimits::maxOriginalDescendantProofs,"Aggregate first-original descendant-proof budget exhausted"},
        {&ExactStageWork::originalRetirementProofs,&ExactStageWorkLimits::maxOriginalRetirementProofs,"Aggregate first-original retirement-proof budget exhausted"},
        {&ExactStageWork::originalWitnessTriangles,&ExactStageWorkLimits::maxOriginalWitnessTriangles,"Aggregate first-original witness-triangle budget exhausted"},
        {&ExactStageWork::originalIntentChecks,&ExactStageWorkLimits::maxOriginalIntentChecks,"Aggregate first-original intent-check budget exhausted"},
        {&ExactStageWork::originalProtectedFeatures,&ExactStageWorkLimits::maxOriginalProtectedFeatures,"Aggregate first-original protected-feature budget exhausted"},
        {&ExactStageWork::originalMirrorOrbitChecks,&ExactStageWorkLimits::maxOriginalMirrorOrbitChecks,"Aggregate first-original mirror-orbit budget exhausted"},
        {&ExactStageWork::originalHistoryRoots,&ExactStageWorkLimits::maxOriginalHistoryRoots,"Aggregate first-original history-root budget exhausted"},
        {&ExactStageWork::originalHistoryTransitions,&ExactStageWorkLimits::maxOriginalHistoryTransitions,"Aggregate first-original history-transition budget exhausted"}
    }};
    const auto calibrate=[&](SculptCollisionSettings& policy,
                             const SculptStepResourceWork& actual) {
        policy.stepResources.maxPrivateCopiedBytes=actual.privateCopiedBytes;
        policy.stepResources.maxChangedIdReferences=actual.changedIdReferences;
        policy.stepResources.maxFinalDirtyPartitions=actual.finalDirtyPartitions;
        std::array<std::size_t,4> families{};
        for(std::size_t i=0;i<buckets.size();++i) {
            const auto& category=buckets[i];
            const auto n=actual.exactStage.*(category.work);
            policy.exactStageLimits.*(category.limit)=n;
            if(n)++families[i<12?0:i<19?1:i<26?2:3];
        }
        return families;
    };
    const auto checkStages=[](const SculptStepResourceWork& step) {
        return step.elapsedMilliseconds>=0&&
               step.stageWall.initialGeometryMs>=0&&
               step.stageWall.sourceGeometryMs>=0&&
               step.stageWall.originalProofMs>=0&&
               step.stageWall.admissionMs>=0&&
               step.stageWall.publicationAndHistoryMs>=0;
    };
    const auto checkAggregate=[&](const SculptStepResourceWork& actual,
                                  const SculptStepResourceWork& observed) {
        if(actual.privateCopiedBytes!=observed.privateCopiedBytes||
           actual.changedIdReferences!=observed.changedIdReferences||
           actual.finalDirtyPartitions!=observed.finalDirtyPartitions)
            return false;
        for(const auto& category:buckets)
            if(actual.exactStage.*(category.work)!=
               observed.exactStage.*(category.work))
                return false;
        return true;
    };
    const auto checkPublished=[&](const SculptStepResourceWork& attempt,
                                  const SculptResourceTotals& totals) {
        if(totals.attempts!=1||totals.published!=1||totals.refused!=0||
           totals.publishedPrivateCopiedBytes!=attempt.privateCopiedBytes||
           totals.attemptedPrivateCopiedBytes!=attempt.privateCopiedBytes||
           totals.publishedChangedIdReferences!=attempt.changedIdReferences||
           totals.attemptedChangedIdReferences!=attempt.changedIdReferences||
           totals.publishedDirtyPartitions!=attempt.finalDirtyPartitions||
           totals.attemptedDirtyPartitions!=attempt.finalDirtyPartitions)
            return false;
        for(const auto& category:buckets) {
            const auto measured=attempt.exactStage.*(category.work);
            if(totals.publishedExact.*(category.work)!=measured||
               totals.attemptedExact.*(category.work)!=measured)
                return false;
        }
        return true;
    };
    const auto checkRefused=[&](const SculptStepResourceWork& work,
                                const SculptResourceTotals& totals,
                                const SculptStepResourceWork& baseline) {
        if(totals.attempts!=1||totals.refused!=1||
           totals.budgetRefusals!=1||totals.published!=0||
           totals.publishedPrivateCopiedBytes!=0||
           totals.publishedChangedIdReferences!=0||
           totals.publishedDirtyPartitions!=0||
           work.refusalCategory!=SculptRefusalCategory::FiniteWorkBudget||
           work.published||!work.refused||!work.evaluated||
           totals.attemptedPrivateCopiedBytes!=work.privateCopiedBytes||
           totals.attemptedChangedIdReferences!=work.changedIdReferences||
           totals.attemptedDirtyPartitions!=work.finalDirtyPartitions||
           !checkAggregate(baseline,work))
            return false;
        for(const auto& category:buckets)
            if(totals.publishedExact.*(category.work)!=0||
               totals.attemptedExact.*(category.work)!=
                   baseline.exactStage.*(category.work))
                return false;
        return true;
    };
    const auto checkLimitFamilies=[&](const std::array<std::size_t,4>& count) {
        return count[0]>=8&&count[1]>=3&&count[2]>=3&&count[3]>=6;
    };
    const auto picked=chooseM39FourDimensionalSource();
    const auto initial=picked.original;
    const auto originalPicker=initial.index->pickingRootIdentity();
    const Vec3 at=picked.source.input[0][0].center;
    const auto areas=std::array<BrushArea,2>{{
        {at,6,.3},{{-at.x,at.y,at.z},6,.3}
    }};
    auto physical=m11PhysicalExactSettings();
    const auto createPhysical=[&](SculptCollisionSettings policy) {
        return VersionedSculptBrush(
            initial,picked.strength,{},{},{},256,{},std::move(policy));
    };
    Mesh dummy;
    auto physicalReference=createPhysical(physical);
    physicalReference.setOriginalPatchIntent(picked.deliberate);
    physicalReference.stamp(dummy,areas);
    const auto physicalWork=physicalReference.lastStepResourceWork();
    const auto physicalSource=physicalReference.checkpoint();
    require(physicalReference.completedUnions()==1&&
            physicalWork.evaluated&&physicalWork.published&&
            physicalWork.privateCopiedBytes>0&&
            physicalWork.changedIdReferences>0&&
            physicalWork.finalDirtyPartitions>0&&
            physicalWork.exactStage.collisionQueries>0&&
            physicalWork.exactStage.windingQueries>0&&
            physicalWork.exactStage.contourEdgeProbes>0&&
            physicalWork.exactStage.seamTraceProbes>0&&
            physicalWork.exactStage.originalWitnessTriangles>0&&
            physicalWork.exactStage.originalProtectedFeatures>0&&
            physicalWork.exactStage.originalMirrorOrbitChecks>0&&
            physicalWork.exactStage.originalHistoryTransitions>=2&&
            !physicalWork.exactStage.overflow&&
            physicalSource.verifiedOriginalFidelity&&
            physicalSource.verifiedOriginalFidelity->anchors.original==
                initial.index&&
            !physicalSource.mirrorPairs.empty()&&
            physicalReference.historyStatistics().undoDepth==1&&
            initial.index->pickingRootIdentity()==originalPicker&&
            checkStages(physicalWork),
            "original M42 real reflected protected 72-source physical union simultaneously executes M40/M41/M43/M44/M45/M46 and retains immutable first reference");
    const auto physicalFamilies=calibrate(physical,physicalWork);
    require(checkLimitFamilies(physicalFamilies),
            "original M42 genuine physical union executes the measured nonzero M41, contour, seam and first-original families");
    auto physicalEqual=createPhysical(physical);
    physicalEqual.setOriginalPatchIntent(picked.deliberate);
    physicalEqual.stamp(dummy,areas);
    require(physicalEqual.completedUnions()==1&&
            checkAggregate(physicalWork,physicalEqual.lastStepResourceWork())&&
            checkPublished(physicalEqual.lastStepResourceWork(),
                           physicalEqual.resourceTotals()),
            "original M42 ALL 38 simultaneous aggregate operation and COW caps at equality accept one original-root physical Step");
    std::size_t exercisedPhysical=0;
    const auto rejectPhysical=[&](SculptCollisionSettings limited,
                                  std::string_view failure) {
        auto candidate=createPhysical(std::move(limited));
        candidate.setOriginalPatchIntent(picked.deliberate);
        const auto before=candidate.checkpoint();
        candidate.stamp(dummy,areas);
        const auto after=candidate.checkpoint();
        const auto attempted=candidate.lastStepResourceWork();
        require(after.index==before.index&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                after.mirrorPairs==before.mirrorPairs&&
                before.index->pickingRootIdentity()==
                    after.index->pickingRootIdentity()&&
                candidate.historyStatistics().undoDepth==0&&
                candidate.historyStatistics().redoDepth==0&&
                candidate.lastDirtyPartitions().empty()&&
                candidate.completedUnions()==0&&
                candidate.publishedRevisions()==0&&
                candidate.rejectedStamps()==1&&
                attempted.refusal.find(failure)!=std::string::npos&&
                checkRefused(attempted,candidate.resourceTotals(),physicalWork),
                "original M42 one-under calibrated physical family veto refuses named category atomically before source/dirty/history/CAS");
    };
    for(const auto& category:buckets) {
        if(!(physicalWork.exactStage.*(category.work)))continue;
        auto under=physical;
        --(under.exactStageLimits.*(category.limit));
        rejectPhysical(under,category.refusal);
        ++exercisedPhysical;
    }
    const std::array<const char*,3> resourceReasons{{
        "COW payload byte budget",
        "changed ID reference budget",
        "final dirty partition budget"
    }};
    for(unsigned field=0;field<3;++field) {
        auto under=physical;
        if(field==0)--under.stepResources.maxPrivateCopiedBytes;
        else if(field==1)--under.stepResources.maxChangedIdReferences;
        else --under.stepResources.maxFinalDirtyPartitions;
        rejectPhysical(under,resourceReasons[field]);
    }
    // Same immutable FIRST root, now continued through two REAL clean
    // 4D Steps after certified Contact. Bound every dimension by the
    // greatest genuinely observed Step in this exact source history,
    // rather than trying to reuse the initial Contact's narrower cap
    // to judge a different Clean candidate.
    auto boundedHysteresis=physical;
    const auto cleanAreas=std::array<BrushArea,2>{{
        {picked.exterior,.5,.3},
        {{-picked.exterior.x,picked.exterior.y,picked.exterior.z},.5,.3}
    }};
    std::array<SculptStepResourceWork,2> cleanWork{};
    for(unsigned i=0;i<cleanWork.size();++i) {
        physicalReference.setOriginalPatchIntent(picked.deliberate);
        physicalReference.stamp(dummy,cleanAreas);
        cleanWork[i]=physicalReference.lastStepResourceWork();
        require(cleanWork[i].published&&cleanWork[i].evaluated&&
                !cleanWork[i].refused&&
                physicalReference.completedUnions()==1&&
                physicalReference.contactStatistics().cleanChecks==i+1&&
                physicalReference.contactStatistics().quietRun==i+1&&
                physicalReference.historyStatistics().undoDepth==i+2,
                "original M42 post-Contact original-source Clean phases are genuine accepted 4D history Steps");
        auto& cap=boundedHysteresis.stepResources;
        cap.maxPrivateCopiedBytes=std::max(
            cap.maxPrivateCopiedBytes,cleanWork[i].privateCopiedBytes);
        cap.maxChangedIdReferences=std::max(
            cap.maxChangedIdReferences,cleanWork[i].changedIdReferences);
        cap.maxFinalDirtyPartitions=std::max(
            cap.maxFinalDirtyPartitions,cleanWork[i].finalDirtyPartitions);
        for(const auto& category:buckets) {
            auto& limit=boundedHysteresis.exactStageLimits.*(category.limit);
            limit=std::max(limit,cleanWork[i].exactStage.*(category.work));
        }
    }

    const auto mesh=twoIntersectingTetrahedra();
    const auto sources=firstOriginalFaces(mesh);
    const auto limits=supportedUnionLimits(false);
    SculptCollisionSettings queued;
    queued.physicalExact=true;
    queued.originalTerminal.maximumProtectedDeviationInEdges=0;
    const auto createQueued=[&](SculptCollisionSettings policy) {
        return VersionedSculptBrush(
            mesh,.006,{},{},{},false,256,{},std::move(policy));
    };
    auto queuedReference=createQueued(queued);
    const auto queuedOriginal=queuedReference.snapshot();
    require(queuedReference.mergeExactContacts(
                queuedOriginal,fullOriginalContacts(*queuedOriginal),limits,sources),
            "original M42 genuine source-certified asymmetric queued two-tetra Boolean supports M40-M46 full integration");
    const auto queuedWork=queuedReference.lastStepResourceWork();
    const auto queuedSource=queuedReference.checkpoint();
    require(queuedWork.published&&queuedWork.evaluated&&
            queuedWork.privateCopiedBytes>0&&
            queuedWork.changedIdReferences>0&&
            queuedWork.finalDirtyPartitions>0&&
            queuedWork.exactStage.collisionQueries>0&&
            queuedWork.exactStage.windingQueries>0&&
            queuedWork.exactStage.contourEdgeProbes>0&&
            queuedWork.exactStage.seamTraceProbes>0&&
            queuedWork.exactStage.originalWitnessTriangles>0&&
            queuedWork.exactStage.originalHistoryTransitions>=2&&
            !queuedWork.exactStage.overflow&&
            queuedSource.verifiedOriginalFidelity&&
            queuedSource.verifiedOriginalFidelity->anchors.original==
                queuedOriginal&&
            queuedReference.historyStatistics().undoDepth==1&&
            checkStages(queuedWork),
            "original M42 source-authenticated queued union executes all applicable families and one M29 publication");
    const auto queuedFamilies=calibrate(queued,queuedWork);
    require(queuedFamilies[0]>=8&&queuedFamilies[1]>=3&&
            queuedFamilies[2]>=3&&queuedFamilies[3]>=5,
            "original M42 genuine queued source executes each M41/M43/M44/M45 family without inventing absent mirror or optional refinement work");
    auto queuedEqual=createQueued(queued);
    const auto beforeEqual=queuedEqual.snapshot();
    require(queuedEqual.mergeExactContacts(
                beforeEqual,fullOriginalContacts(*beforeEqual),limits,sources)&&
            checkAggregate(queuedWork,queuedEqual.lastStepResourceWork())&&
            checkPublished(queuedEqual.lastStepResourceWork(),
                           queuedEqual.resourceTotals()),
            "original M42 ALL 38 simultaneous exact resource and geometry ceilings accept one authenticated queued CAS");
    std::size_t exercisedQueued=0;
    const auto rejectQueued=[&](SculptCollisionSettings limited,
                                std::string_view failure) {
        auto denied=createQueued(std::move(limited));
        const auto before=denied.checkpoint();
        require(!denied.mergeExactContacts(
                    before.index,fullOriginalContacts(*before.index),
                    limits,sources),
                "original M42 reduced exact original-root queued budget MUST refuse");
        const auto after=denied.checkpoint();
        const auto work=denied.lastStepResourceWork();
        require(after.index==before.index&&
                after.verifiedOriginalFidelity==
                    before.verifiedOriginalFidelity&&
                after.mirrorPairs==before.mirrorPairs&&
                denied.historyStatistics().undoDepth==0&&
                denied.historyStatistics().redoDepth==0&&
                denied.lastDirtyPartitions().empty()&&
                denied.completedUnions()==0&&denied.rejectedUnions()==1&&
                denied.publishedRevisions()==0&&
                work.refusal.find(failure)!=std::string::npos&&
                checkRefused(work,denied.resourceTotals(),queuedWork),
                "original M42 exact one-under source-certified queued family limit is named and atomically refused without fabricated accepted work");
    };
    for(const auto& category:buckets) {
        if(!(queuedWork.exactStage.*(category.work)))continue;
        auto under=queued;
        --(under.exactStageLimits.*(category.limit));
        rejectQueued(under,category.refusal);
        ++exercisedQueued;
    }
    for(unsigned field=0;field<3;++field) {
        auto under=queued;
        if(field==0)--under.stepResources.maxPrivateCopiedBytes;
        else if(field==1)--under.stepResources.maxChangedIdReferences;
        else --under.stepResources.maxFinalDirtyPartitions;
        rejectQueued(under,resourceReasons[field]);
    }

    // Entire real asynchronous FIFO paths must expose the same exact
    // calibrated cost, source authentication, history and dirty owners.
    AsyncSculptWorker physicalWorker(initial,.025,picked.strength,.2,
                                    {},{},{},{},boundedHysteresis);
    physicalWorker.begin(at,6,picked.deliberate);
    physicalWorker.end(at,0);
    const auto until=std::chrono::steady_clock::now()+
                     std::chrono::seconds(30);
    while(!physicalWorker.canBegin()&&
          std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    require(physicalWorker.canBegin(),"original M42 physical FIFO must complete");
    const auto firstStatus=physicalWorker.status();
    require(firstStatus.completedUnions==1&&
            checkAggregate(physicalWork,firstStatus.stepResources)&&
            firstStatus.resources.attempts==1&&
            firstStatus.resources.published==1&&
            firstStatus.resources.physicalAttempts==1&&
            firstStatus.resources.publishedExact.originalWitnessTriangles==
                physicalWork.exactStage.originalWitnessTriangles&&
            firstStatus.history.undoDepth==1&&
            firstStatus.snapshotPublications>=1&&
            firstStatus.lastSnapshotDirtyOwners>0&&
            firstStatus.contact.contactChecks==1&&
            firstStatus.contact.active,
            "original M42 real bounded M40-M46 execution on worker publishes one Contact and one M29 history/dirty root");
    for(unsigned i=0;i<cleanWork.size();++i) {
        physicalWorker.begin(picked.exterior,.5,picked.deliberate);
        physicalWorker.end(picked.exterior,0);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
        while(!physicalWorker.canBegin()&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto clean=physicalWorker.status();
        require(physicalWorker.canBegin()&&
                checkAggregate(cleanWork[i],clean.stepResources)&&
                clean.completedUnions==1&&clean.rejectedStamps==0&&
                clean.contact.cleanChecks==i+1&&
                clean.contact.quietRun==i+1&&clean.contact.active&&
                clean.history.undoDepth==i+2&&
                clean.resources.attempts==i+2&&
                clean.resources.published==i+2,
                "original M42 SAME first-original source protected FIFO genuinely enters conclusive Clean hysteresis under simultaneously finite M40-M46 caps");
    }
    const auto liveRoot=physicalWorker.initialRevision();
    physicalWorker.begin(at,.28,{FaceId{99999999}});
    physicalWorker.end(at,0);
    const auto idle=std::chrono::steady_clock::now()+
                    std::chrono::seconds(30);
    while(!physicalWorker.canBegin()&&
          std::chrono::steady_clock::now()<idle)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto unknown=physicalWorker.status();
    require(physicalWorker.canBegin()&&
            unknown.rejectedStamps==1&&
            unknown.resources.attempts==4&&
            unknown.resources.published==3&&
            unknown.resources.refused==1&&
            unknown.resources.provenanceRefusals==1&&
            unknown.contact.uncertainChecks>=1&&
            unknown.contact.cleanChecks==2&&
            unknown.contact.active&&unknown.contact.quietRun==0&&
            unknown.history.undoDepth==3&&
            physicalWorker.initialRevision()==liveRoot,
            "original M42 invalid caller-provenance FIFO is UNKNOWN, not clean hysteresis credit or fabricated source/COW publication");
    physicalWorker.undo();
    {
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(physicalWorker.status().completedUndos==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(physicalWorker.status().completedUndos==1&&
            physicalWorker.status().history.redoDepth==1&&
            physicalWorker.status().resources.attempts==4,
            "original M42 protected worker undo restores historical original-root without rebilling source proof");
    physicalWorker.redo();
    {
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(physicalWorker.status().completedRedos==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(physicalWorker.status().completedRedos==1&&
            physicalWorker.status().history.redoDepth==0&&
            physicalWorker.initialRevision()==liveRoot&&
            physicalWorker.status().resources.published==3,
            "original M42 protected worker redo restores SAME accepted exact source without duplicated accepted work");

    AsyncSculptWorker queueWorker(mesh,.025,.006,.2,
                                 {},{},{},false,{},queued);
    const auto initialQueue=queueWorker.initialRevision();
    const auto contacts=fullOriginalContacts(*initialQueue);
    queueWorker.mergeExactContacts(initialQueue,contacts,limits,sources);
    waitForUnion(queueWorker,1);
    const auto firstQueue=queueWorker.status();
    require(firstQueue.completedUnions==1&&
            checkAggregate(queuedWork,firstQueue.stepResources)&&
            firstQueue.resources.queuedExactAttempts==1&&
            firstQueue.resources.published==1&&
            firstQueue.resources.publishedExact.originalWitnessTriangles==
                queuedWork.exactStage.originalWitnessTriangles&&
            firstQueue.history.undoDepth==1&&
            firstQueue.snapshotPublications>=1,
            "original M42 calibrated queued FIFO joins genuine source with all M40–M46 caps, published work and single M29 history");
    const auto currentQueue=queueWorker.initialRevision();
    queueWorker.mergeExactContacts(initialQueue,contacts,limits,sources);
    waitForUnion(queueWorker,2);
    const auto stale=queueWorker.status();
    require(stale.rejectedUnions==1&&stale.resources.attempts==2&&
            stale.resources.published==1&&
            stale.resources.staleRefusals==1&&
            stale.resources.publishedPrivateCopiedBytes==
                firstQueue.resources.publishedPrivateCopiedBytes&&
            stale.resources.attemptedPrivateCopiedBytes==
                firstQueue.resources.attemptedPrivateCopiedBytes&&
            stale.history.undoDepth==1&&
            queueWorker.initialRevision()==currentQueue,
            "original M42 stale queued root never consumes a previous successful M40-M46 certificate or publishes phantom COW work");

    const auto checkpointPath=std::filesystem::temp_directory_path()/
        "mesher-v17-m42-integrated-acceptance.mshr";
    try {
        queueWorker.saveCheckpoint(checkpointPath);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(queueWorker.checkpointStatistics().completed==0&&
              queueWorker.checkpointStatistics().failed==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto stored=queueWorker.status();
        require(stored.checkpoints.completed==1&&
                stored.checkpoints.failed==0&&
                stored.checkpointCommands==1&&
                stored.resources.attempts==2,
                "original M42 genuine FIFO checkpoint command retains diagnostic history without remaking private work");
        const auto disk=readCheckpoint(checkpointPath);
        require(disk.index&&disk.originalFidelityRequired&&
                !disk.verifiedOriginalFidelity,
                "original M42 V13 disk roundtrip NEVER forges original-source immutable M29 certificate or replayed resource work");
        bool refused=false;
        try {
            (void)AsyncSculptWorker(disk,.025,.006,.2,{},{},{},{},queued);
        }catch(const std::logic_error&) {refused=true;}
        require(refused,
                "original M42 disk-unverified protected checkpoint cannot silently revive authenticated union authority");
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(checkpointPath,ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(checkpointPath,ec);
    std::cout<<"[original m42] simultaneous 38 exact caps; true physical active "
             <<exercisedPhysical<<"/35, queued "<<exercisedQueued
             <<"/35; each active category and all 3 M40 dimensions "
             <<"independently one-under refused with full M46 source and worker diagnostics\n";
}

// Original M13 is an INDEPENDENT whole-session resource and diagnostic
// reconciliation, NOT a relabeling of M42's per-category one-under test.
// Here M40–M46 source geometry and COW facts are accumulated across real
// published, refused, stale, undo/redo and worker checkpoint transitions.
void testOriginalM13WholeStepResourceAndTelemetryReconciliation() {
    struct Bucket {
        std::size_t ExactStageWork::*count;
        std::size_t ExactStageWorkLimits::*ceiling;
    };
    constexpr std::array<Bucket,35> buckets{{
        {&ExactStageWork::collisionQueries,&ExactStageWorkLimits::maxCollisionQueries},
        {&ExactStageWork::collisionBoxes,&ExactStageWorkLimits::maxCollisionBoxes},
        {&ExactStageWork::collisionPartitions,&ExactStageWorkLimits::maxCollisionPartitions},
        {&ExactStageWork::collisionCandidates,&ExactStageWorkLimits::maxCollisionCandidates},
        {&ExactStageWork::collisionNarrowPhase,&ExactStageWorkLimits::maxCollisionNarrowPhase},
        {&ExactStageWork::windingQueries,&ExactStageWorkLimits::maxWindingQueries},
        {&ExactStageWork::windingBoxes,&ExactStageWorkLimits::maxWindingBoxes},
        {&ExactStageWork::windingPartitions,&ExactStageWorkLimits::maxWindingPartitions},
        {&ExactStageWork::windingTriangles,&ExactStageWorkLimits::maxWindingTriangles},
        {&ExactStageWork::windingRays,&ExactStageWorkLimits::maxWindingRays},
        {&ExactStageWork::arrangementPairTests,&ExactStageWorkLimits::maxArrangementPairTests},
        {&ExactStageWork::arrangementPredicates,&ExactStageWorkLimits::maxArrangementPredicates},
        {&ExactStageWork::contourEdgeProbes,&ExactStageWorkLimits::maxContourEdgeProbes},
        {&ExactStageWork::contourBoundaryEdges,&ExactStageWorkLimits::maxContourBoundaryEdges},
        {&ExactStageWork::contourLoopVisits,&ExactStageWorkLimits::maxContourLoopVisits},
        {&ExactStageWork::closureRingTests,&ExactStageWorkLimits::maxClosureRingTests},
        {&ExactStageWork::closurePairCandidates,&ExactStageWorkLimits::maxClosurePairCandidates},
        {&ExactStageWork::closureSampleTests,&ExactStageWorkLimits::maxClosureSampleTests},
        {&ExactStageWork::closureDpTransitions,&ExactStageWorkLimits::maxClosureDpTransitions},
        {&ExactStageWork::seamTraceProbes,&ExactStageWorkLimits::maxSeamTraceProbes},
        {&ExactStageWork::seamMatchedIntervals,&ExactStageWorkLimits::maxSeamMatchedIntervals},
        {&ExactStageWork::seamWeldComparisons,&ExactStageWorkLimits::maxSeamWeldComparisons},
        {&ExactStageWork::seamRefinePredicates,&ExactStageWorkLimits::maxSeamRefinePredicates},
        {&ExactStageWork::seamRefineMatches,&ExactStageWorkLimits::maxSeamRefineMatches},
        {&ExactStageWork::seamMirrorOrbitChecks,&ExactStageWorkLimits::maxSeamMirrorOrbitChecks},
        {&ExactStageWork::seamFaceRewires,&ExactStageWorkLimits::maxSeamFaceRewires},
        {&ExactStageWork::originalAnchorProofs,&ExactStageWorkLimits::maxOriginalAnchorProofs},
        {&ExactStageWork::originalDescendantProofs,&ExactStageWorkLimits::maxOriginalDescendantProofs},
        {&ExactStageWork::originalRetirementProofs,&ExactStageWorkLimits::maxOriginalRetirementProofs},
        {&ExactStageWork::originalWitnessTriangles,&ExactStageWorkLimits::maxOriginalWitnessTriangles},
        {&ExactStageWork::originalIntentChecks,&ExactStageWorkLimits::maxOriginalIntentChecks},
        {&ExactStageWork::originalProtectedFeatures,&ExactStageWorkLimits::maxOriginalProtectedFeatures},
        {&ExactStageWork::originalMirrorOrbitChecks,&ExactStageWorkLimits::maxOriginalMirrorOrbitChecks},
        {&ExactStageWork::originalHistoryRoots,&ExactStageWorkLimits::maxOriginalHistoryRoots},
        {&ExactStageWork::originalHistoryTransitions,&ExactStageWorkLimits::maxOriginalHistoryTransitions}
    }};
    const auto calibrate=[&](SculptCollisionSettings& setting,
                             const SculptStepResourceWork& step) {
        setting.stepResources.maxPrivateCopiedBytes=std::max(
            setting.stepResources.maxPrivateCopiedBytes,
            step.privateCopiedBytes);
        setting.stepResources.maxChangedIdReferences=std::max(
            setting.stepResources.maxChangedIdReferences,
            step.changedIdReferences);
        setting.stepResources.maxFinalDirtyPartitions=std::max(
            setting.stepResources.maxFinalDirtyPartitions,
            step.finalDirtyPartitions);
        for(const auto& bucket:buckets) {
            auto& max=setting.exactStageLimits.*(bucket.ceiling);
            max=std::max(max,step.exactStage.*(bucket.count));
        }
    };
    const auto atExactLimits=[&](SculptCollisionSettings& setting,
                                  const SculptStepResourceWork& step) {
        setting.stepResources.maxPrivateCopiedBytes=step.privateCopiedBytes;
        setting.stepResources.maxChangedIdReferences=step.changedIdReferences;
        setting.stepResources.maxFinalDirtyPartitions=step.finalDirtyPartitions;
        for(const auto& bucket:buckets)
            setting.exactStageLimits.*(bucket.ceiling)=
                step.exactStage.*(bucket.count);
    };
    const auto stageIsMeasured=[](const SculptStepResourceWork& step) {
        return std::isfinite(step.elapsedMilliseconds)&&
            step.elapsedMilliseconds>=0&&
            std::isfinite(step.stageWall.initialGeometryMs)&&
            step.stageWall.initialGeometryMs>=0&&
            std::isfinite(step.stageWall.sourceGeometryMs)&&
            step.stageWall.sourceGeometryMs>=0&&
            std::isfinite(step.stageWall.originalProofMs)&&
            step.stageWall.originalProofMs>=0&&
            std::isfinite(step.stageWall.admissionMs)&&
            step.stageWall.admissionMs>=0&&
            std::isfinite(step.stageWall.publicationAndHistoryMs)&&
            step.stageWall.publicationAndHistoryMs>=0;
    };
    const auto checkLedger=[&](const std::vector<SculptStepResourceWork>&
                                  witnessed,
                               const SculptResourceTotals& actual) {
        std::size_t published=0,refused=0;
        std::size_t copied=0,acceptedCopied=0;
        std::size_t changed=0,acceptedChanged=0;
        std::size_t dirty=0,acceptedDirty=0;
        std::size_t provenance=0,budget=0,stale=0,geometry=0;
        double elapsed=0,acceptedElapsed=0;
        for(const auto& step:witnessed) {
            require(stageIsMeasured(step),
                    "original M13 real attempted Step timings must be finite nonnegative observations, never estimated budget work");
            if(step.published) {
                ++published;
                acceptedCopied+=step.privateCopiedBytes;
                acceptedChanged+=step.changedIdReferences;
                acceptedDirty+=step.finalDirtyPartitions;
                acceptedElapsed+=step.elapsedMilliseconds;
            }else if(step.refused) {
                ++refused;
                switch(step.refusalCategory) {
                case SculptRefusalCategory::CallerOrProvenance:++provenance;break;
                case SculptRefusalCategory::FiniteWorkBudget:++budget;break;
                case SculptRefusalCategory::StaleExpectedRoot:++stale;break;
                case SculptRefusalCategory::GeometryOrTopology:++geometry;break;
                case SculptRefusalCategory::None:break;
                }
            }
            copied+=step.privateCopiedBytes;
            changed+=step.changedIdReferences;
            dirty+=step.finalDirtyPartitions;
            elapsed+=step.elapsedMilliseconds;
        }
        if(actual.attempts!=witnessed.size()||
           actual.published!=published||actual.refused!=refused||
           actual.unchanged!=witnessed.size()-published-refused||
           actual.provenanceRefusals!=provenance||
           actual.budgetRefusals!=budget||
           actual.staleRefusals!=stale||
           actual.geometryRefusals!=geometry||
           actual.attemptedPrivateCopiedBytes!=copied||
           actual.publishedPrivateCopiedBytes!=acceptedCopied||
           actual.attemptedChangedIdReferences!=changed||
           actual.publishedChangedIdReferences!=acceptedChanged||
           actual.attemptedDirtyPartitions!=dirty||
           actual.publishedDirtyPartitions!=acceptedDirty||
           actual.observedAttemptMilliseconds<0||
           actual.observedPublishedMilliseconds<0||
           !std::isfinite(actual.observedAttemptMilliseconds)||
           !std::isfinite(actual.observedPublishedMilliseconds))
            return false;
        // Time is observed independently on each execution. Compare only
        // with the ledger of THESE SAME calls; there is no timing threshold.
        const auto within=[](double lhs,double rhs) {
            return std::abs(lhs-rhs)<=std::max(1e-6,
                1e-10*std::max(std::abs(lhs),std::abs(rhs)));
        };
        if(!within(actual.observedAttemptMilliseconds,elapsed)||
           !within(actual.observedPublishedMilliseconds,acceptedElapsed))
            return false;
        for(const auto& bucket:buckets) {
            std::size_t attempts=0,publishes=0;
            for(const auto& step:witnessed) {
                const auto n=step.exactStage.*(bucket.count);
                attempts+=n;
                if(step.published)publishes+=n;
            }
            if(actual.attemptedExact.*(bucket.count)!=attempts||
               actual.publishedExact.*(bucket.count)!=publishes)
                return false;
        }
        return !actual.attemptedExact.overflow&&!actual.publishedExact.overflow;
    };
    const auto checkSameWork=[&](const SculptStepResourceWork& a,
                                 const SculptStepResourceWork& b) {
        if(a.privateCopiedBytes!=b.privateCopiedBytes||
           a.changedIdReferences!=b.changedIdReferences||
           a.finalDirtyPartitions!=b.finalDirtyPartitions)
            return false;
        for(const auto& bucket:buckets)
            if(a.exactStage.*(bucket.count)!=
               b.exactStage.*(bucket.count))return false;
        return true;
    };
    const auto chosen=chooseM39FourDimensionalSource();
    const auto first=chosen.original;
    const Vec3 origin=chosen.source.input[0][0].center;
    const std::array<BrushArea,2> physicalAreas{{
        {origin,6,.3},{{-origin.x,origin.y,origin.z},6,.3}
    }};
    const std::array<BrushArea,2> cleanAreas{{
        {chosen.exterior,.5,.3},
        {{-chosen.exterior.x,chosen.exterior.y,chosen.exterior.z},.5,.3}
    }};
    const auto makePhysical=[&](const SculptCollisionSettings& config) {
        return VersionedSculptBrush(
            first,chosen.strength,{},{},{},256,{},config);
    };
    auto physicalSettings=m11PhysicalExactSettings();
    Mesh dummy;
    auto calibration=makePhysical(physicalSettings);
    std::array<SculptStepResourceWork,3> trueSteps{};
    calibration.setOriginalPatchIntent(chosen.deliberate);
    calibration.stamp(dummy,physicalAreas);
    trueSteps[0]=calibration.lastStepResourceWork();
    require(calibration.completedUnions()==1&&trueSteps[0].published&&
            trueSteps[0].privateCopiedBytes>0&&
            trueSteps[0].exactStage.originalWitnessTriangles>0,
            "original M13 first genuine M39 4D protected source Contact is an accepted original-root COW Step");
    for(std::size_t i=1;i<trueSteps.size();++i) {
        calibration.setOriginalPatchIntent(chosen.deliberate);
        calibration.stamp(dummy,cleanAreas);
        trueSteps[i]=calibration.lastStepResourceWork();
        require(trueSteps[i].published&&!trueSteps[i].refused&&
                calibration.completedUnions()==1&&
                calibration.contactStatistics().quietRun==i&&
                calibration.historyStatistics().undoDepth==i+1,
                "original M13 second and third source-authorized protected 4D Steps are genuinely Clean and append real COW history");
    }
    atExactLimits(physicalSettings,trueSteps[0]);
    for(std::size_t i=1;i<trueSteps.size();++i)
        calibrate(physicalSettings,trueSteps[i]);

    auto physical=makePhysical(physicalSettings);
    std::vector<SculptStepResourceWork> witnessed;
    const auto stampPhysical=[&](const auto& area) {
        physical.setOriginalPatchIntent(chosen.deliberate);
        physical.stamp(dummy,area);
        const auto work=physical.lastStepResourceWork();
        witnessed.push_back(work);
        return work;
    };
    require(checkSameWork(trueSteps[0],stampPhysical(physicalAreas)),
            "original M13 simultaneously calibrated 38 finite Step caps accept one genuine first-root physical Contact");
    for(std::size_t i=1;i<trueSteps.size();++i)
        require(checkSameWork(trueSteps[i],stampPhysical(cleanAreas)),
                "original M13 actual subsequent protected Clean Step admits all 38 caps without previous-root reanchoring");
    const auto acceptedRoot=physical.snapshot();
    const auto acceptedHistory=physical.historyStatistics();
    const auto acceptedCertificate=physical.checkpoint();
    const auto beforeRefusal=physical.resourceTotals();
    physical.setOriginalPatchIntent({FaceId{99999999}});
    physical.stamp(dummy,cleanAreas);
    const auto invalid=physical.lastStepResourceWork();
    witnessed.push_back(invalid);
    const auto finalPhysical=physical.resourceTotals();
    require(invalid.refused&&!invalid.evaluated&&!invalid.published&&
            invalid.refusalCategory==SculptRefusalCategory::CallerOrProvenance&&
            invalid.privateCopiedBytes==0&&
            invalid.changedIdReferences==0&&
            invalid.finalDirtyPartitions==0&&
            physical.snapshot()==acceptedRoot&&
            physical.historyStatistics().undoDepth==
                acceptedHistory.undoDepth&&
            physical.historyStatistics().redoDepth==
                acceptedHistory.redoDepth&&
            physical.lastDirtyPartitions().empty()&&
            physical.checkpoint().verifiedOriginalFidelity==
                acceptedCertificate.verifiedOriginalFidelity&&
            physical.checkpoint().mirrorPairs==
                acceptedCertificate.mirrorPairs&&
            physical.contactStatistics().quietRun==0&&
            physical.contactStatistics().uncertainChecks>=1&&
            finalPhysical.physicalAttempts==4&&
            finalPhysical.published==3&&
            finalPhysical.provenanceRefusals==1&&
            finalPhysical.publishedPrivateCopiedBytes==
                beforeRefusal.publishedPrivateCopiedBytes&&
            checkLedger(witnessed,finalPhysical),
            "original M13 independently reconciles every real M40-M46 attempted/published category over 3 protected roots plus early Unknown without phantom COW/first-source work");
    for(const auto& bucket:buckets)
        require(invalid.exactStage.*(bucket.count)==0,
                "original M13 invalid-first-original caller request before private candidate charges ZERO unexecuted geometry work");
    require(physical.undo()&&physical.redo()&&
            physical.snapshot()==acceptedRoot&&
            checkLedger(witnessed,physical.resourceTotals()),
            "original M13 genuine direct history undo/redo changes the displayed root, NOT the cumulative private work already executed");

    // Exactly one lowered genuine work limit, with the remaining 37
    // at the first Step's measured true work, must record expensive
    // ATTEMPTED work but zero published work or root authority.
    auto physicalUnder=m11PhysicalExactSettings();
    atExactLimits(physicalUnder,trueSteps[0]);
    --physicalUnder.exactStageLimits.maxOriginalWitnessTriangles;
    auto denied=makePhysical(physicalUnder);
    denied.setOriginalPatchIntent(chosen.deliberate);
    const auto deniedOriginal=denied.checkpoint();
    denied.stamp(dummy,physicalAreas);
    const auto deniedWork=denied.lastStepResourceWork();
    require(deniedWork.refused&&deniedWork.evaluated&&!deniedWork.published&&
            deniedWork.refusalCategory==SculptRefusalCategory::FiniteWorkBudget&&
            deniedWork.refusal.find("first-original witness-triangle budget")!=
                std::string::npos&&
            checkSameWork(trueSteps[0],deniedWork)&&
            denied.checkpoint().index==deniedOriginal.index&&
            denied.checkpoint().mirrorPairs==deniedOriginal.mirrorPairs&&
            denied.historyStatistics().undoDepth==0&&
            denied.lastDirtyPartitions().empty()&&
            checkLedger({deniedWork},denied.resourceTotals())&&
            denied.resourceTotals().published==0&&
            denied.resourceTotals().attemptedPrivateCopiedBytes>0,
            "original M13 whole private physical M45 one-under budget charges genuine attempted work but cannot claim M29 published cost or source");

    const auto mesh=twoIntersectingTetrahedra();
    const auto originalSources=firstOriginalFaces(mesh);
    const auto fullLimits=supportedUnionLimits(false);
    SculptCollisionSettings queuedPolicy;
    queuedPolicy.physicalExact=true;
    queuedPolicy.originalTerminal.maximumProtectedDeviationInEdges=0;
    const auto newQueued=[&](const SculptCollisionSettings& setting) {
        return VersionedSculptBrush(
            mesh,.006,{},{},{},false,256,{},setting);
    };
    auto queueReference=newQueued(queuedPolicy);
    const auto originalQueue=queueReference.snapshot();
    require(queueReference.mergeExactContacts(
                originalQueue,fullOriginalContacts(*originalQueue),
                fullLimits,originalSources),
            "original M13 independent fully authenticated queued c1/c2/c3/M03/d2 Step is supported before 38-resource calibration");
    const auto queuedTrue=queueReference.lastStepResourceWork();
    atExactLimits(queuedPolicy,queuedTrue);
    auto boundedQueue=newQueued(queuedPolicy);
    const auto q0=boundedQueue.snapshot();
    require(boundedQueue.mergeExactContacts(
                q0,fullOriginalContacts(*q0),fullLimits,originalSources),
            "original M13 true queued Step accepts exact limits across ALL 38 resource categories");
    const auto qAccepted=boundedQueue.lastStepResourceWork();
    require(checkSameWork(queuedTrue,qAccepted)&&qAccepted.published,
            "original M13 queued genuine source payload and all 35 exact counters match independent prior physical-class calibration");
    const auto qPublished=boundedQueue.checkpoint();
    require(!boundedQueue.mergeExactContacts(
                q0,fullOriginalContacts(*q0),fullLimits,originalSources),
            "original M13 stale source expected-root request MUST fail before any speculative private work");
    const auto qStale=boundedQueue.lastStepResourceWork();
    require(qStale.refused&&!qStale.evaluated&&!qStale.published&&
            qStale.refusalCategory==SculptRefusalCategory::StaleExpectedRoot&&
            qStale.privateCopiedBytes==0&&
            qStale.changedIdReferences==0&&
            qStale.finalDirtyPartitions==0&&
            boundedQueue.checkpoint().index==qPublished.index&&
            boundedQueue.checkpoint().verifiedOriginalFidelity==
                qPublished.verifiedOriginalFidelity&&
            boundedQueue.checkpoint().mirrorPairs==qPublished.mirrorPairs&&
            boundedQueue.historyStatistics().undoDepth==1&&
            boundedQueue.resourceTotals().queuedExactAttempts==2&&
            checkLedger({qAccepted,qStale},boundedQueue.resourceTotals()),
            "original M13 cumulative queued telemetry excludes all phantom previous-root COW work on stale source request");
    for(const auto& bucket:buckets)
        require(qStale.exactStage.*(bucket.count)==0,
                "original M13 stale original-root queued proposal never charges unexecuted source proof");

    auto queueUnder=queuedPolicy;
    --queueUnder.exactStageLimits.maxOriginalWitnessTriangles;
    auto queuedDenied=newQueued(queueUnder);
    const auto qDeniedRoot=queuedDenied.checkpoint();
    require(!queuedDenied.mergeExactContacts(
                qDeniedRoot.index,
                fullOriginalContacts(*qDeniedRoot.index),
                fullLimits,originalSources),
            "original M13 genuine source queued exact one-under must refuse before one CAS");
    const auto qBad=queuedDenied.lastStepResourceWork();
    require(qBad.evaluated&&qBad.refused&&!qBad.published&&
            qBad.refusalCategory==SculptRefusalCategory::FiniteWorkBudget&&
            qBad.refusal.find("first-original witness-triangle budget")!=
                std::string::npos&&
            checkSameWork(queuedTrue,qBad)&&
            queuedDenied.checkpoint().index==qDeniedRoot.index&&
            queuedDenied.checkpoint().verifiedOriginalFidelity==
                qDeniedRoot.verifiedOriginalFidelity&&
            queuedDenied.historyStatistics().undoDepth==0&&
            queuedDenied.lastDirtyPartitions().empty()&&
            checkLedger({qBad},queuedDenied.resourceTotals())&&
            queuedDenied.resourceTotals().publishedExact.originalWitnessTriangles==0,
            "original M13 independent queued true one-under budget retains all attempted resource work with ZERO published original-source history");

    // The worker's FIFO view must be a real cumulative source of truth,
    // not a last-Step-only proxy. Use one genuine accepted queued root,
    // a stale source-root refusal, restore its history, and durable I/O.
    AsyncSculptWorker queueWorker(mesh,.025,.006,.2,
                                 {},{},{},false,{},queuedPolicy);
    const auto workerRoot=queueWorker.initialRevision();
    const auto workerContacts=fullOriginalContacts(*workerRoot);
    queueWorker.mergeExactContacts(workerRoot,workerContacts,
                                   fullLimits,originalSources);
    waitForUnion(queueWorker,1);
    const auto workerAccepted=queueWorker.status();
    require(workerAccepted.completedUnions==1&&
            checkSameWork(queuedTrue,workerAccepted.stepResources)&&
            checkLedger({workerAccepted.stepResources},
                        workerAccepted.resources)&&
            workerAccepted.history.undoDepth==1,
            "original M13 genuine queued FIFO accounting reconciles same actual source work in worker status");
    queueWorker.mergeExactContacts(workerRoot,workerContacts,
                                   fullLimits,originalSources);
    waitForUnion(queueWorker,2);
    const auto workerStale=queueWorker.status();
    require(workerStale.rejectedUnions==1&&
            workerStale.resources.staleRefusals==1&&
            workerStale.resources.published==1&&
            workerStale.resources.queuedExactAttempts==2&&
            checkLedger({workerAccepted.stepResources,workerStale.stepResources},
                        workerStale.resources)&&
            workerStale.resources.publishedPrivateCopiedBytes==
                workerAccepted.resources.publishedPrivateCopiedBytes,
            "original M13 FIFO current-session attempted and published totals exactly match independent source Step ledger");
    queueWorker.undo();
    {
        const auto stop=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(queueWorker.status().completedUndos==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto undoStatus=queueWorker.status();
    require(undoStatus.completedUndos==1&&
            undoStatus.history.redoDepth==1&&
            checkLedger({workerAccepted.stepResources,workerStale.stepResources},
                        undoStatus.resources),
            "original M13 worker actual undo restores history without pretending to rerun expensive protected-source Boolean work");
    queueWorker.redo();
    {
        const auto stop=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(queueWorker.status().completedRedos==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto redoStatus=queueWorker.status();
    require(redoStatus.completedRedos==1&&redoStatus.history.redoDepth==0&&
            redoStatus.resources.published==1&&
            checkLedger({workerAccepted.stepResources,workerStale.stepResources},
                        redoStatus.resources),
            "original M13 worker redo recovers accepted original-root history with no duplicate COW cost");

    const auto temp=std::filesystem::temp_directory_path()/
        "mesher-v17-m13-original-telemetry-acceptance.mshr";
    try {
        queueWorker.saveCheckpoint(temp);
        const auto stop=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(queueWorker.checkpointStatistics().completed==0&&
              queueWorker.checkpointStatistics().failed==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto saved=queueWorker.status();
        require(saved.checkpoints.completed==1&&saved.checkpoints.failed==0&&
                saved.checkpointCommands==1&&
                checkLedger({workerAccepted.stepResources,workerStale.stepResources},
                            saved.resources),
                "original M13 real asynchronous checkpoint command reports only actual disk I/O, never extra geometry or unreported work");
        const auto disk=readCheckpoint(temp);
        require(disk.index&&disk.originalFidelityRequired&&
                !disk.verifiedOriginalFidelity,
                "original M13 disk provenance does NOT serialize a forged M29 original-source credential or historic per-CPU-attempt measurements");
        bool refused=false;
        try {
            (void)AsyncSculptWorker(disk,.025,.006,.2,{},{},{},{},queuedPolicy);
        }catch(const std::logic_error&) {refused=true;}
        require(refused,
                "original M13 new controller cannot authenticate protected first original from an untrusted V13 disk snapshot");
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(temp,ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(temp,ec);
    std::cout<<"[original m13] 35 independently reconciled categories over "
             <<witnessed.size()<<" direct 4D attempts ("
             <<finalPhysical.published<<" published), genuine queued "
             <<"accepted/stale/budget refusals and FIFO history/checkpoint "
             <<"with strictly distinct attempted/published COW proxies\n";
}

// V17-M14: deliberately throw AFTER a real original-root CAS, at four
// auxiliary transaction boundaries. Such failure is neither a rejected
// private candidate nor a safely accepted history Step. Quarantine the
// owner instead of exposing an inconsistent indexed root/checkpoint.
void testM14PostCasFaultQuarantineAndCheckpointIsolation() {
    const std::array<SculptPostCasFaultPoint,4> points{{
        SculptPostCasFaultPoint::AfterRootCas,
        SculptPostCasFaultPoint::AfterMirrorState,
        SculptPostCasFaultPoint::AfterHistoryAppend,
        SculptPostCasFaultPoint::AfterAuxCommit
    }};
    const auto fault=[](SculptPostCasFaultPoint point) {
        return [point](SculptPostCasFaultPoint at) {
            if(at==point)
                throw std::runtime_error("M14 deterministic post-CAS fault");
        };
    };
    const auto source=chooseM39FourDimensionalSource();
    const auto center=source.source.input[0][0].center;
    const std::array<BrushArea,2> areas{{
        {center,6,.3},{{-center.x,center.y,center.z},6,.3}
    }};
    Mesh dummy;
    for(const auto point:points) {
        auto policy=m11PhysicalExactSettings();
        policy.postCasFaultInjection=fault(point);
        VersionedSculptBrush physical(
            source.original,source.strength,{},{},{},256,{},policy);
        const auto before=physical.checkpoint();
        physical.setOriginalPatchIntent(source.deliberate);
        bool thrown=false;
        try {physical.stamp(dummy,areas);}
        catch(const std::runtime_error& e) {
            thrown=std::string_view(e.what())=="M14 deterministic post-CAS fault";
        }
        const auto& work=physical.lastStepResourceWork();
        const auto totals=physical.resourceTotals();
        require(thrown&&physical.terminalFailure()&&
                physical.terminalFailureReason().find("post-CAS fault")!=
                    std::string::npos&&
                !physical.snapshot()&&
                work.path==SculptStepResourcePath::PhysicalStamp&&
                work.postCasFailed&&!work.refused&&!work.published&&
                work.privateCopiedBytes>0&&work.evaluated&&
                work.exactStage.originalWitnessTriangles>0&&
                totals.attempts==1&&totals.postCasFailures==1&&
                totals.published==0&&totals.refused==0&&
                totals.unchanged==0&&
                totals.attemptedPrivateCopiedBytes==work.privateCopiedBytes&&
                totals.attemptedExact.originalWitnessTriangles==
                    work.exactStage.originalWitnessTriangles&&
                totals.publishedPrivateCopiedBytes==0&&
                totals.publishedExact.originalWitnessTriangles==0&&
                physical.firstOriginalSourceRoot()==
                    before.verifiedOriginalFidelity->anchors.original&&
                physical.lastDirtyPartitions().empty(),
                "M14 injected protected physical post-CAS fault is terminal, quarantines uncertain root, and accounts only attempted COW and source work");
        unsigned blocked=0;
        try {(void)physical.checkpoint();}
        catch(const std::logic_error&) {++blocked;}
        try {(void)physical.undo();}
        catch(const std::logic_error&) {++blocked;}
        try {(void)physical.redo();}
        catch(const std::logic_error&) {++blocked;}
        try {physical.setOriginalPatchIntent(source.deliberate);}
        catch(const std::logic_error&) {++blocked;}
        try {physical.stamp(dummy,areas);}
        catch(const std::logic_error&) {++blocked;}
        require(blocked==5&&physical.resourceTotals().attempts==1,
                "M14 post-CAS quarantined physical controller forbids further original-source Step, undo/redo or forged checkpoint");
    }
    const auto mesh=twoIntersectingTetrahedra();
    const auto origins=firstOriginalFaces(mesh);
    const auto limits=supportedUnionLimits(false);
    SculptCollisionSettings unionPolicy;
    unionPolicy.physicalExact=true;
    unionPolicy.originalTerminal.maximumProtectedDeviationInEdges=0;
    for(const auto point:points) {
        auto policy=unionPolicy;
        policy.postCasFaultInjection=fault(point);
        VersionedSculptBrush queued(
            mesh,.006,{},{},{},false,256,{},policy);
        const auto before=queued.checkpoint();
        bool thrown=false;
        try {
            (void)queued.mergeExactContacts(
                before.index,fullOriginalContacts(*before.index),
                limits,origins);
        }catch(const std::runtime_error& e) {
            thrown=std::string_view(e.what())=="M14 deterministic post-CAS fault";
        }
        const auto& work=queued.lastStepResourceWork();
        const auto totals=queued.resourceTotals();
        require(thrown&&queued.terminalFailure()&&!queued.snapshot()&&
                work.path==SculptStepResourcePath::QueuedExact&&
                work.evaluated&&work.postCasFailed&&
                !work.refused&&!work.published&&
                work.privateCopiedBytes>0&&
                work.exactStage.originalWitnessTriangles>0&&
                totals.queuedExactAttempts==1&&
                totals.attempts==1&&totals.postCasFailures==1&&
                totals.published==0&&totals.refused==0&&
                totals.unchanged==0&&
                totals.attemptedPrivateCopiedBytes==work.privateCopiedBytes&&
                totals.attemptedExact.originalWitnessTriangles==
                    work.exactStage.originalWitnessTriangles&&
                totals.publishedPrivateCopiedBytes==0&&
                totals.publishedExact.originalWitnessTriangles==0&&
                queued.firstOriginalSourceRoot()==
                    before.verifiedOriginalFidelity->anchors.original&&
                queued.lastDirtyPartitions().empty(),
                "M14 injected original-source queued exact post-CAS failure cannot be treated as safe refusal or accepted union and quarantines source and history");
        unsigned blocked=0;
        try {(void)queued.checkpoint();}
        catch(const std::logic_error&) {++blocked;}
        try {(void)queued.undo();}
        catch(const std::logic_error&) {++blocked;}
        try {(void)queued.redo();}
        catch(const std::logic_error&) {++blocked;}
        try {(void)queued.mergeExactContacts(
                 before.index,fullOriginalContacts(*before.index),
                 limits,origins);}
        catch(const std::logic_error&) {++blocked;}
        require(blocked==4&&queued.resourceTotals().attempts==1,
                "M14 queued failure after original-root CAS forbids all follow-up geometry/history/durable-authority operations");
    }
    // Race the public snapshot reader against a deliberately held CAS.
    // An indexed root already changed, but mirror/history/dirty authority
    // has NOT completed: no reader may acquire the uncertain new root.
    std::atomic<bool> insideCas{false},allowFailure{false};
    auto heldPolicy=unionPolicy;
    heldPolicy.postCasFaultInjection=[&](SculptPostCasFaultPoint at) {
        if(at!=SculptPostCasFaultPoint::AfterRootCas)return;
        insideCas.store(true,std::memory_order_release);
        while(!allowFailure.load(std::memory_order_acquire))
            std::this_thread::yield();
        throw std::runtime_error("M14 deliberate held post-CAS reader race");
    };
    VersionedSculptBrush held(mesh,.006,{},{},{},false,256,{},heldPolicy);
    const auto heldFirst=held.checkpoint();
    std::exception_ptr heldException;
    std::thread producer([&] {
        try {
            (void)held.mergeExactContacts(
                heldFirst.index,fullOriginalContacts(*heldFirst.index),
                limits,origins);
        }catch(...) {heldException=std::current_exception();}
    });
    const auto enteredBy=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
    while(!insideCas.load(std::memory_order_acquire)&&
          std::chrono::steady_clock::now()<enteredBy)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const bool entered=insideCas.load(std::memory_order_acquire);
    const bool hidden=entered&&!held.snapshot();
    bool checkpointBlocked=false;
    if(entered) {
        try {(void)held.checkpoint();}
        catch(const std::logic_error&) {checkpointBlocked=true;}
    }
    allowFailure.store(true,std::memory_order_release);
    producer.join();
    require(entered&&hidden&&checkpointBlocked&&heldException&&
            held.terminalFailure()&&!held.snapshot()&&
            held.resourceTotals().postCasFailures==1,
            "M14 a concurrent reader cannot obtain half-committed CAS root or checkpoint while auxiliary mirror/history publication is held");

    // The worker must publish an explicit terminal-state diagnostic and
    // must NEVER release an uncertain renderer/picker snapshot.
    auto workerPolicy=unionPolicy;
    workerPolicy.postCasFaultInjection=fault(
        SculptPostCasFaultPoint::AfterHistoryAppend);
    AsyncSculptWorker worker(mesh,.025,.006,.2,
                             {},{},{},false,{},workerPolicy);
    const auto original=worker.initialRevision();
    worker.mergeExactContacts(original,fullOriginalContacts(*original),
                              limits,origins);
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
    AsyncSculptWorker::Status final{};
    do {
        final=worker.status();
        if(final.terminalFailure)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }while(std::chrono::steady_clock::now()<deadline);
    require(final.terminalFailure&&
            final.terminalFailureReason.find("post-CAS fault")!=
                std::string::npos&&
            final.stepResources.postCasFailed&&
            final.resources.postCasFailures==1&&
            final.resources.published==0&&
            final.resources.refused==0&&
            final.resources.attemptedPrivateCopiedBytes>0&&
            !worker.initialRevision()&&
            !worker.takeSnapshot().has_value()&&
            final.snapshotPublications==0&&
            final.history.undoDepth==0 /* untrusted on terminal failure */,
            "M14 actual FIFO worker reports fatal post-CAS source fault, charges attempted work and suppresses all uncertain frame publication");
    unsigned blocked=0;
    try {worker.saveCheckpoint(
        std::filesystem::temp_directory_path()/"mesher-m14-no-checkpoint.mshr");}
    catch(const std::runtime_error&) {++blocked;}
    try {worker.mergeExactContacts(original,fullOriginalContacts(*original),
                                  limits,origins);}
    catch(const std::runtime_error&) {++blocked;}
    require(blocked==2,
            "M14 fatal worker cannot enqueue a durable save or a new merge after ambiguous root CAS");

    // File-system I/O failure occurs OUTSIDE the indexed root CAS. The
    // asynchronous writer must report failure while retaining a previously
    // validated root and permitting a subsequent legitimate checkpoint.
    AsyncSculptWorker safe(mesh,.025,.006,.2,
                           {},{},{},false,{},unionPolicy);
    const auto initial=safe.initialRevision();
    safe.mergeExactContacts(initial,fullOriginalContacts(*initial),
                            limits,origins);
    waitForUnion(safe,1);
    const auto safeRoot=safe.initialRevision();
    require(safe.status().completedUnions==1&&safeRoot!=initial,
            "M14 independent worker has one valid exact original-root history before forcing checkpoint I/O error");
    const auto rootDir=std::filesystem::temp_directory_path()/
        "mesher-v17-m14-checkpoint-isolation";
    std::error_code ec;
    std::filesystem::remove_all(rootDir,ec);
    const auto impossible=rootDir/"missing-subdir"/"failure.mshr";
    safe.saveCheckpoint(impossible);
    {
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(safe.checkpointStatistics().failed==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(safe.checkpointStatistics().failed==1&&
            safe.status().resources.published==1&&
            safe.initialRevision()==safeRoot&&
            safe.status().history.undoDepth==1&&
            !safe.status().terminalFailure,
            "M14 failed filesystem checkpoint is explicitly reported while accepted indexed root, M29 source and history stay coherent");
    std::filesystem::create_directories(rootDir);
    const auto legitimate=rootDir/"valid.mshr";
    try {
        safe.saveCheckpoint(legitimate);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(safe.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto status=safe.status();
        require(status.checkpoints.completed==1&&
                status.checkpoints.failed==1&&
                safe.initialRevision()==safeRoot&&
                status.resources.published==1,
                "M14 independent successful post-error durable checkpoint does not double-publish or reexecute geometry");
        const auto saved=readCheckpoint(legitimate);
        require(saved.index&&saved.originalFidelityRequired&&
                !saved.verifiedOriginalFidelity,
                "M14 durable checkpoint after disk error is final source root without invented opaque M29 credentials");
    }catch(...) {
        std::filesystem::remove_all(rootDir,ec);
        throw;
    }
    std::filesystem::remove_all(rootDir,ec);
    std::cout<<"[m14] injected 4 physical and 4 queued true post-CAS failures, "
             <<"worker fatal quarantine and independent failed/recovered durable I/O\n";
}

// M47: a live Merge is never a render-thread all-pairs query or an
// anonymous XYZ weld. The one worker command must discover a COMPLETE
// bounded original-certified transverse contact set on the EXACT selected
// indexed root and submit it once, or refuse without any published owner.
void testM47BoundedAuthenticatedLiveMergeDiscovery() {
    const auto mesh=twoIntersectingTetrahedra();
    const auto originals=firstOriginalFaces(mesh);
    const auto limits=supportedUnionLimits(false);
    SculptCollisionSettings protectedPolicy;
    protectedPolicy.physicalExact=true;
    protectedPolicy.originalTerminal.maximumProtectedDeviationInEdges=0;
    const auto wait=[&](AsyncSculptWorker& worker,std::size_t attempts) {
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
        while(worker.status().liveMergeRequests<attempts&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.status().liveMergeRequests==attempts,
                "M47 deferred discovery command must drain on FIFO worker");
    };
    AsyncSculptWorker accepted(mesh,.025,.006,.2,
                               {},{},{},false,{},protectedPolicy);
    const auto before=accepted.initialRevision();
    const auto oldPicker=before->pickingRootIdentity();
    accepted.mergeDiscoveredContacts(before,originals,limits);
    wait(accepted,1);
    const auto good=accepted.status();
    require(good.liveMergeAccepted==1&&good.liveMergeRefusals==0&&
            good.liveMergeRefusal.empty()&&good.completedUnions==1&&
            good.publishedRevisions==1&&good.history.undoDepth==1&&
            good.resources.published==1&&
            good.stepResources.exactStage.originalWitnessTriangles>0&&
            before->pickingRootIdentity()==oldPicker&&
            before->retiredFaceCount()==0,
            "M47 certified original source, finite complete transverse discovery and original-root CAS merge once");
    const auto merged=accepted.takeSnapshot();
    require(merged&&merged->index&&merged->index!=before&&
            !merged->dirtyPartitions.empty()&&
            merged->index->retiredFaceCount()>0,
            "M47 renderer receives only one committed indexed root with correct COW dirty owner set");
    const auto rootAfter=accepted.initialRevision();
    accepted.mergeDiscoveredContacts(before,originals,limits);
    wait(accepted,2);
    const auto stale=accepted.status();
    require(stale.liveMergeAccepted==1&&stale.liveMergeRefusals==1&&
            stale.liveMergeRefusal.find("stale")!=std::string::npos&&
            stale.completedUnions==1&&stale.history.undoDepth==1&&
            stale.resources.published==1&&
            accepted.initialRevision()==rootAfter&&
            !accepted.takeSnapshot(),
            "M47 stale UI-selected root does not redirect source intent to new terminal or publish phantom dirties");
    accepted.undo();
    {
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(accepted.status().completedUndos==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto undone=accepted.takeSnapshot();
    require(accepted.status().completedUndos==1&&
            accepted.initialRevision()==before&&
            undone&&undone->index==before&&
            accepted.status().history.redoDepth==1,
            "M47 accepted live Merge undo restores original immutable picking root and history in FIFO");
    accepted.redo();
    {
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(accepted.status().completedRedos==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(accepted.status().completedRedos==1&&
            accepted.initialRevision()==rootAfter&&
            accepted.status().history.redoDepth==0&&
            accepted.status().resources.published==1,
            "M47 redo revives same certified union without a new work attempt");

    AsyncSculptWorker off(mesh,.025,.006,.2,
                          {},{},{},false);
    const auto offRoot=off.initialRevision();
    off.mergeDiscoveredContacts(offRoot,originals,limits);
    wait(off,1);
    const auto offStatus=off.status();
    require(offStatus.liveMergeRefusals==1&&offStatus.liveMergeAccepted==0&&
            offStatus.liveMergeRefusal.find("trusted initial")!=
                std::string::npos&&
            offStatus.completedUnions==0&&offStatus.history.undoDepth==0&&
            off.initialRevision()==offRoot&&
            !off.takeSnapshot(),
            "M47 legacy OFF-mode cannot be silently upgraded into authenticated original Boolean surgery");

    AsyncSculptWorker bounded(mesh,.025,.006,.2,
                              {},{},{},false,{},protectedPolicy);
    auto narrow=SculptSourceContactLimits{};
    narrow.maxVertices=3;
    const auto boundedRoot=bounded.initialRevision();
    bounded.mergeDiscoveredContacts(boundedRoot,originals,limits,narrow);
    wait(bounded,1);
    const auto budget=bounded.status();
    require(budget.liveMergeRefusals==1&&budget.liveMergeAccepted==0&&
            budget.liveMergeRefusal.find("budget")!=std::string::npos&&
            budget.completedUnions==0&&budget.resources.published==0&&
            budget.history.undoDepth==0&&
            bounded.initialRevision()==boundedRoot&&
            !bounded.takeSnapshot(),
            "M47 finite complete-root candidate work exhaustion refuses, never processes truncated contact subset");
    bounded.mergeDiscoveredContacts(boundedRoot,originals,limits);
    wait(bounded,2);
    require(bounded.status().liveMergeAccepted==1&&
            bounded.status().liveMergeRefusals==1&&
            bounded.status().completedUnions==1,
            "M47 a refused bounded discovery cannot poison a subsequent valid authenticated queued request");

    // Deliberate source selection must be an immutable FIRST-original
    // FaceId, never an arbitrary later/foreign XYZ-coincident face.
    AsyncSculptWorker invalid(mesh,.025,.006,.2,
                              {},{},{},false,{},protectedPolicy);
    const auto invalidRoot=invalid.initialRevision();
    invalid.mergeDiscoveredContacts(
        invalidRoot,{FaceId{99999999}},limits);
    wait(invalid,1);
    require(invalid.status().liveMergeRefusals==1&&
            invalid.status().liveMergeRefusal.find("selection invalid")!=
                std::string::npos&&
            invalid.status().history.undoDepth==0&&
            invalid.initialRevision()==invalidRoot&&
            !invalid.takeSnapshot(),
            "M47 foreign original FaceId cannot be selected or substituted by coincident current topology");
    bool emptySelectionRefused=false;
    try {invalid.mergeDiscoveredContacts(invalidRoot,{},limits);}
    catch(const std::invalid_argument&) {emptySelectionRefused=true;}
    require(emptySelectionRefused&&invalid.status().liveMergeRequests==1,
            "M47 no-source UI request must be rejected before enqueue");

    // A genuinely reciprocal reflected double section is source-owned
    // and can be assessed and joined ONLY with its certified involution.
    const auto mirrorMesh=twoIntersectingTetrahedra(true);
    const auto importedMirror=partitionFlatMesh(mirrorMesh,256);
    const auto mirroredRoot=RegionTopologyIndex::build(importedMirror.revision);
    std::vector<std::pair<VertexId,VertexId>> mirrorPairs;
    for(std::size_t i=0;i<8;++i)
        mirrorPairs.emplace_back(
            importedMirror.flatIndex.vertexIds[i],
            importedMirror.flatIndex.vertexIds[i+8]);
    auto mirroredProof=std::make_shared<const OriginalPatchLifetime>(
        beginOriginalPatchLifetime(captureOriginalSourceAnchors(
            mirroredRoot,importedMirror.flatIndex.faceIds,
            7*importedMirror.flatIndex.faceIds.size())));
    DurableCheckpoint trustedMirror(mirroredRoot,mirrorPairs,16,
                                    true,mirroredProof);
    trustedMirror.verifiedFirstOriginalMirrorPairs=mirrorPairs;
    AsyncSculptWorker reflected(
        std::move(trustedMirror),.025,.006,.2,{},{},{},{},
        protectedPolicy,importedMirror.flatIndex.faceIds);
    const auto mirroredOriginal=reflected.initialRevision();
    reflected.mergeDiscoveredContacts(
        mirroredOriginal,importedMirror.flatIndex.faceIds,
        supportedUnionLimits(true));
    wait(reflected,1);
    const auto mirroredResult=reflected.status();
    require(mirroredResult.liveMergeAccepted==1&&
            mirroredResult.completedUnions==1&&
            mirroredResult.history.undoDepth==1&&
            reflected.initialRevision()!=mirroredOriginal&&
            reflected.takeSnapshot().has_value(),
            "M47 reflected source-owned original pair graph with reciprocal mirror IDs positively publishes one joined Step");

    auto empty=twoIntersectingTetrahedra();
    for(std::size_t i=4;i<8;++i)
        empty.vertices[i].x+=50;
    AsyncSculptWorker clean(empty,.025,.006,.2,
                            {},{},{},false,{},protectedPolicy);
    const auto cleanRoot=clean.initialRevision();
    clean.mergeDiscoveredContacts(cleanRoot,firstOriginalFaces(empty),limits);
    wait(clean,1);
    const auto absent=clean.status();
    require(absent.liveMergeRefusals==1&&absent.liveMergeAccepted==0&&
            absent.liveMergeRefusal.find("no intersecting")!=
                std::string::npos&&
            absent.completedUnions==0&&absent.history.undoDepth==0&&
            clean.initialRevision()==cleanRoot&&
            !clean.takeSnapshot(),
            "M47 clean valid source is a reported no-contact refusal, not fabricated accepted union");

    // A missing checkpoint opaque credential must not be resurrected by
    // read-only discovery merely because its reconstructed XYZ matches.
    const auto file=std::filesystem::temp_directory_path()/
        "mesher-m47-bounded-untrusted-source.mshr";
    try {
        accepted.saveCheckpoint(file);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(accepted.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(accepted.checkpointStatistics().completed==1,
                "M47 complete merged root disk checkpoint must finish");
        const auto restored=readCheckpoint(file);
        require(restored.index&&restored.originalFidelityRequired&&
                !restored.verifiedOriginalFidelity,
                "M47 disk V13 persists source-authentication requirement but never creates trusted first-original credentials");
        bool denied=false;
        try {(void)AsyncSculptWorker(restored,.025,.006,.2,
                                     {},{},{},{},protectedPolicy);}
        catch(const std::logic_error&) {denied=true;}
        require(denied,
                "M47 untrusted V13 disk root cannot start protected live worker or mint new first-original source authority");
    }catch(...) {
        std::error_code ec;std::filesystem::remove(file,ec);
        throw;
    }
    std::error_code ec;std::filesystem::remove(file,ec);
    std::cout<<"[m47] bounded certified original full-contact FIFO; accepted,"
             <<" stale, OFF, zero contacts, one-under source work,"
             <<" undo/redo, dirty/picker and V13 checkpoint refusal\n";
}

// M49: the EXACT state machine used by VulkanViewport, with no shadow
// reimplementation in tests. Drive key repeat, mode switches, source
// selection, stale root, worker dispatch, renderer and history handoff.
void testM49DesktopModeSelectionAndBoundedWorkerIntegration() {
    DesktopMergeKeyEdges keyboard;
    const auto firstKey=keyboard.sample(true,false,false);
    const auto heldKey=keyboard.sample(true,false,false);
    const auto release=keyboard.sample(false,false,false);
    const auto secondKey=keyboard.sample(true,false,false);
    require(firstKey.toggle&&!heldKey.toggle&&!release.toggle&&
            secondKey.toggle&&!secondKey.submit&&!secondKey.clear,
            "M49 real viewport M-key edge detector toggles only once per key press");
    keyboard.sample(false,false,false);
    const auto enter=keyboard.sample(false,true,true);
    const auto heldEnter=keyboard.sample(false,true,true);
    keyboard.sample(false,false,false);
    const auto reenter=keyboard.sample(false,true,false);
    require(enter.submit&&enter.clear&&!heldEnter.submit&&
            !heldEnter.clear&&reenter.submit,
            "M49 actual Enter/Backspace edge state cannot duplicate a queued merge or clear on key-repeat");

    const auto mesh=twoIntersectingTetrahedra();
    const auto originals=firstOriginalFaces(mesh);
    SculptCollisionSettings policy;
    policy.physicalExact=true;
    policy.originalTerminal.maximumProtectedDeviationInEdges=0;
    AsyncSculptWorker worker(mesh,.025,.006,.2,
                             {},{},{},false,{},policy);
    const auto original=worker.initialRevision();
    DesktopMergeInput ui;
    require(!ui.enabled()&&ui.inflatesOnLeftClick()&&
            ui.selectedCount()==0&&
            ui.toggle(false,true,false)==DesktopMergeInput::Result::Unavailable&&
            !ui.enabled()&&ui.pick(original,originals.front(),true)==
                DesktopMergeInput::Result::Ignored&&
            ui.submit(original,false,true).result==
                DesktopMergeInput::Result::Ignored&&
            worker.status().liveMergeRequests==0,
            "M49 ordinary OFF launch has no selected source or hidden union command");
    require(ui.toggle(true,false,false)==DesktopMergeInput::Result::Busy&&
            ui.toggle(true,true,true)==DesktopMergeInput::Result::Ignored&&
            !ui.enabled()&&
            ui.toggle(true,true,false)==DesktopMergeInput::Result::ModeOn&&
            ui.enabled()&&!ui.inflatesOnLeftClick(),
            "M49 protected authorization is explicit, M starts OFF, and busy/sculpting cannot toggle into source selection");
    require(ui.pick(original,std::nullopt,true)==
                DesktopMergeInput::Result::NoHit&&
            ui.pick(original,FaceId{},true)==
                DesktopMergeInput::Result::InvalidFace&&
            ui.selectedCount()==0&&
            ui.submit(original,true,true).result==
                DesktopMergeInput::Result::NoSelection,
            "M49 no-hit, zero FaceId and empty Enter never create a Boolean command");
    require(ui.pick(original,originals.front(),false)==
                DesktopMergeInput::Result::Busy&&
            ui.pick(original,originals.front(),true)==
                DesktopMergeInput::Result::Selected&&
            ui.pick(original,originals.front(),true)==
                DesktopMergeInput::Result::Duplicate&&
            ui.selectedCount()==1,
            "M49 mouse-down selection uses stable original FaceId, rejects busy and suppresses duplicate click");
    require(ui.clear()==DesktopMergeInput::Result::Cleared&&
            ui.selectedCount()==0&&
            !ui.selectedRoot()&&
            ui.pick(original,originals.front(),true)==
                DesktopMergeInput::Result::Selected&&
            ui.toggle(true,true,false)==DesktopMergeInput::Result::ModeOff&&
            !ui.enabled()&&ui.selectedCount()==0&&
            ui.toggle(true,true,false)==DesktopMergeInput::Result::ModeOn,
            "M49 Backspace and M OFF clear the entire root-bound selection before a later ON session");

    // Selection cap uses the SAME pure UI logic, not a worker geometry
    // approximation. These synthetic FaceIds are never submitted.
    for(std::size_t n=1;n<=DesktopMergeInput::maximumSelection;++n)
        require(ui.pick(original,FaceId{static_cast<std::uint64_t>(n)},true)==
                    DesktopMergeInput::Result::Selected,
                "M49 strict 32 FaceId selection quota admits only unique clicks");
    require(ui.pick(original,FaceId{200},true)==
                DesktopMergeInput::Result::Full&&
            ui.selectedCount()==DesktopMergeInput::maximumSelection&&
            ui.clear()==DesktopMergeInput::Result::Cleared,
            "M49 33rd source click is bounded and cannot implicitly enter a second transaction");

    // Route a genuine multi-face nomination through the SAME UI
    // controller and M47 worker command used by VulkanViewport.
    for(const auto id:originals)
        require(ui.pick(original,id,true)==DesktopMergeInput::Result::Selected,
                "M49 real source selection captures each immutable original face once");
    auto submission=ui.submit(original,true,true);
    require(submission.result==DesktopMergeInput::Result::Queued&&
            submission.expected==original&&
            submission.originals==originals&&
            ui.selectedCount()==0&&!ui.selectedRoot(),
            "M49 Enter atomically packages exactly one root and nominated FaceIds then clears on-screen selection");
    worker.mergeDiscoveredContacts(std::move(submission.expected),
                                   std::move(submission.originals),
                                   desktopMergeExactLimits(false));
    const auto stop=std::chrono::steady_clock::now()+
                    std::chrono::seconds(30);
    while((worker.status().liveMergeRequests<1||
           worker.status().pendingLiveMergeRequests>0)&&
          std::chrono::steady_clock::now()<stop)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto mergedStatus=worker.status();
    auto mergedFrame=worker.takeSnapshot();
    const auto mergedRoot=worker.initialRevision();
    require(mergedStatus.liveMergeRequests==1&&
            mergedStatus.liveMergeAccepted==1&&
            mergedStatus.liveMergeRefusals==0&&
            mergedStatus.pendingLiveMergeRequests==0&&
            mergedStatus.completedUnions==1&&
            mergedStatus.history.undoDepth==1&&
            mergedStatus.resources.published==1&&
            mergedStatus.snapshotPublications>=1&&
            mergedFrame&&mergedFrame->index==mergedRoot&&
            mergedFrame->index!=original&&
            !mergedFrame->dirtyPartitions.empty()&&
            mergedRoot->retiredFaceCount()>0,
            "M49 explicit ON selection queues one genuine bounded union and publishes correct indexed picker and dirty renderer owners");

    // A new committed displayed root invalidates a previous immutable
    // token BEFORE any request can reach the worker.
    require(ui.pick(original,originals.front(),true)==
                DesktopMergeInput::Result::Selected&&
            ui.submit(mergedRoot,true,true).result==
                DesktopMergeInput::Result::Stale&&
            ui.selectedCount()==0&&
            worker.status().liveMergeRequests==1,
            "M49 Enter with obsolete displayed revision refuses stale without enqueueing a replacement-root union");
    require(ui.pick(original,originals.front(),true)==
                DesktopMergeInput::Result::Selected,
            "M49 a fresh selection remains tied to the original revision token");
    ui.revisionChanged(mergedRoot);
    require(!ui.selectedRoot()&&ui.selectedCount()==0&&
            ui.submit(mergedRoot,true,true).result==
                DesktopMergeInput::Result::NoSelection,
            "M49 published dirty revision immediately clears previous source nominations");
    require(ui.pick(mergedRoot,originals.front(),true)==
                DesktopMergeInput::Result::Selected&&
            ui.submit(mergedRoot,true,false).result==
                DesktopMergeInput::Result::Busy&&
            !ui.selectedRoot()&&ui.selectedCount()==0,
            "M49 pending worker command refuses and clears selection without accidental queued sculpt request");
    require(ui.toggle(true,true,false)==DesktopMergeInput::Result::ModeOff&&
            ui.inflatesOnLeftClick()&&
            ui.pick(mergedRoot,originals.front(),true)==
                DesktopMergeInput::Result::Ignored&&
            worker.status().liveMergeRequests==1,
            "M49 OFF mode restores legacy LMB Inflate semantics with no implicit new Merge");

    worker.undo();
    {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(worker.status().completedUndos==0&&
              std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto undoFrame=worker.takeSnapshot();
    require(worker.status().completedUndos==1&&
            worker.status().history.redoDepth==1&&
            worker.initialRevision()==original&&
            undoFrame&&undoFrame->index==original&&
            worker.status().resources.published==1,
            "M49 viewport/indexed picker handoff after Undo restores exact original without charging more geometry");
    worker.redo();
    {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(worker.status().completedRedos==0&&
              std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    auto redoFrame=worker.takeSnapshot();
    require(worker.status().completedRedos==1&&
            worker.status().history.redoDepth==0&&
            worker.initialRevision()==mergedRoot&&
            redoFrame&&redoFrame->index==mergedRoot&&
            worker.status().resources.published==1,
            "M49 desktop redo publishes same certified joined root/dirty owner without rerunning Boolean");

    // Rejection flows through the SAME UI submission path, not a
    // direct test-only invocation of the worker's Boolean API.
    AsyncSculptWorker refused(mesh,.025,.006,.2,
                              {},{},{},false,{},policy);
    DesktopMergeInput refusalUi;
    const auto refusalRoot=refused.initialRevision();
    require(refusalUi.toggle(true,true,false)==DesktopMergeInput::Result::ModeOn&&
            refusalUi.pick(refusalRoot,FaceId{99999999},true)==
                DesktopMergeInput::Result::Selected,
            "M49 displayed stable picker IDs are not silently reinterpreted as original credentials on UI thread");
    auto bad=refusalUi.submit(refusalRoot,true,true);
    require(bad.result==DesktopMergeInput::Result::Queued,
            "M49 source selection transport is distinct from M47 source authorization");
    refused.mergeDiscoveredContacts(std::move(bad.expected),
                                    std::move(bad.originals),
                                    desktopMergeExactLimits(false));
    {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(refused.status().liveMergeRequests<1&&
              std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(refused.status().liveMergeRefusals==1&&
            refused.status().liveMergeAccepted==0&&
            refused.status().liveMergeRefusal.find("selection invalid")!=
                std::string::npos&&
            refused.initialRevision()==refusalRoot&&
            refused.status().history.undoDepth==0&&
            !refused.takeSnapshot(),
            "M49 invalid source FaceId refuses in authenticated worker with zero dirty/history/picker mutation");

    // The same UI controller must also preserve source nomination for
    // a true reflected involution; the sphere-only vertex order cannot
    // be substituted for this non-spherical source.
    const auto mirrorMesh=twoIntersectingTetrahedra(true);
    const auto partitioned=partitionFlatMesh(mirrorMesh,256);
    const auto initialMirror=RegionTopologyIndex::build(partitioned.revision);
    std::vector<std::pair<VertexId,VertexId>> mirrorPairs;
    for(std::size_t i=0;i<8;++i)
        mirrorPairs.emplace_back(partitioned.flatIndex.vertexIds[i],
                                 partitioned.flatIndex.vertexIds[i+8]);
    auto lifetime=std::make_shared<const OriginalPatchLifetime>(
        beginOriginalPatchLifetime(captureOriginalSourceAnchors(
            initialMirror,partitioned.flatIndex.faceIds,
            7*partitioned.flatIndex.faceIds.size())));
    DurableCheckpoint trustedMirror(initialMirror,mirrorPairs,16,true,lifetime);
    trustedMirror.verifiedFirstOriginalMirrorPairs=mirrorPairs;
    AsyncSculptWorker reflected(std::move(trustedMirror),.025,.006,.2,
                                {},{},{},{},policy,
                                partitioned.flatIndex.faceIds);
    DesktopMergeInput reflectedUi;
    const auto reflectedRoot=reflected.initialRevision();
    require(!reflectedUi.enabled()&&
            reflectedUi.toggle(true,true,false)==DesktopMergeInput::Result::ModeOn,
            "M49 even explicitly sealed reflected worker starts desktop selection OFF");
    for(const auto id:partitioned.flatIndex.faceIds)
        require(reflectedUi.pick(reflectedRoot,id,true)==
                    DesktopMergeInput::Result::Selected,
                "M49 reflected UI nominations retain first-original stable IDs from reciprocal mirror source");
    auto mirrorSubmission=reflectedUi.submit(reflectedRoot,true,true);
    require(mirrorSubmission.result==DesktopMergeInput::Result::Queued&&
            mirrorSubmission.originals==partitioned.flatIndex.faceIds,
            "M49 reflected Enter transports the complete original mirror-owned FaceId set unmodified");
    reflected.mergeDiscoveredContacts(std::move(mirrorSubmission.expected),
                                      std::move(mirrorSubmission.originals),
                                      desktopMergeExactLimits(true));
    {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(30);
        while((reflected.status().liveMergeRequests<1||
               reflected.status().pendingLiveMergeRequests>0)&&
              std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto mirroredStatus=reflected.status();
    auto mirroredFrame=reflected.takeSnapshot();
    require(mirroredStatus.liveMergeAccepted==1&&
            mirroredStatus.liveMergeRefusals==0&&
            mirroredStatus.history.undoDepth==1&&
            mirroredStatus.resources.published==1&&
            mirroredFrame&&mirroredFrame->index!=reflectedRoot&&
            !mirroredFrame->dirtyPartitions.empty(),
            "M49 genuine explicit reflected Vulkan-mode submission accepts reciprocal mirror source and publishes one renderer/picking root");

    // V13 storage cannot recreate a sealed M29 first-root certificate.
    const auto storage=std::filesystem::temp_directory_path()/
        "mesher-v17-m49-desktop-recovery.mshr";
    try {
        worker.saveCheckpoint(storage);
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(worker.checkpointStatistics().completed==0&&
              worker.checkpointStatistics().failed==0&&
              std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto durable=readCheckpoint(storage);
        require(durable.index&&durable.originalFidelityRequired&&
                !durable.verifiedOriginalFidelity,
                "M49 displayed joined root persists only protected requirement, not forged first-original auth");
        bool rejected=false;
        try {
            (void)AsyncSculptWorker(durable,.025,.006,.2,
                                    {},{},{},{},policy);
        }catch(const std::logic_error&) {rejected=true;}
        require(rejected,
                "M49 desktop recovery cannot silently re-enable protected Merge with disk-only V13 credentials");
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(storage,ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(storage,ec);
    std::cout<<"[m49] actual viewport state default OFF; M/Enter/Backspace edges;"
             <<" bounded original FaceId selection; genuine queued merge,"
             <<" stale and worker source refusal; dirty/picker history and V13 storage\n";
}

void testM11SixteenAlternatingPhysicalTowerUnions() {
    constexpr double halfGap=.15;
    const double strength=chooseM11TowerStrength();
    Mesh dummy;
    const auto source=makeM11Towers(16,halfGap);
    const auto starting=checkpointM11Source(source);
    const auto original=starting.index;
    const auto originalPicker=original->pickingRootIdentity();
    std::vector<VertexId> allIds;
    original->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& v:part.vertices)allIds.push_back(v.id);
        });
    require(original->triangleIntersections(allIds,{}).empty(),
            "All sixteen symmetric tower inputs start with distinct clean shells");
    VersionedSculptBrush brush(starting,strength,
        {},{},{},256,{},m11PhysicalExactSettings());
    std::vector<std::shared_ptr<const RegionTopologyIndex>> immutable{
        original};
    std::size_t unionCount=0;
    for(std::size_t step=0;step<16;++step) {
        const auto before=brush.snapshot();
        const auto previous=brush.historyStatistics();
        const auto previousMirror=brush.checkpoint().mirrorPairs;
        const auto oldPicking=before->pickingRootIdentity();
        brush.setOriginalPatchIntent(m11RegionIntent(source,step));
        brush.stamp(dummy,source.input[step]);
        if(brush.completedUnions()!=unionCount+1)
            throw std::runtime_error(
                "M11 physical tower "+std::to_string(step)+
                " did not produce required supported Boolean union: "+
                brush.lastRejection());
        ++unionCount;
        const auto after=brush.snapshot();
        require(after!=before&&
                before->pickingRootIdentity()==oldPicking&&
                brush.publishedRevisions()==unionCount&&
                brush.completedStamps()==step+1&&
                brush.historyStatistics().undoDepth==previous.undoDepth+1&&
                brush.rejectedStamps()==0&&
                brush.collisionRepairedStamps()==unionCount&&
                !brush.lastDirtyPartitions().empty()&&
                brush.checkpoint().index==after,
                "M11 every real alternate tower stamp has one final immutable CAS/history");
        std::vector<VertexId> vertices;
        after->revision().forEachPartition(
            [&](PartitionId,const MeshPartition& part) {
                for(const auto& v:part.vertices)vertices.push_back(v.id);
            });
        require(after->triangleIntersections(vertices,{}).empty()&&
                after->retiredFaceCount()>before->retiredFaceCount()&&
                brush.checkpoint().mirrorPairs.size()>=
                    previousMirror.size(),
                "M11 each accepted tower union has no residual original contacts and valid net mirrored IDs");
        for(const auto& [a,b]:brush.checkpoint().mirrorPairs) {
            require(after->maybeVertex(a)&&after->maybeVertex(b),
                "M11 historical mirror map has no deleted original source ID");
            const auto x=after->vertex(a).position;
            const auto y=after->vertex(b).position;
            require(x.x==-y.x&&x.y==y.y&&x.z==y.z,
                "M11 every terminal mirror orbit has exact reflected 3D positions");
        }
        immutable.push_back(after);
    }
    require(unionCount==16&&brush.historyStatistics().undoDepth==16&&
            brush.publishedRevisions()==16&&
            original->pickingRootIdentity()==originalPicker&&
            original->retiredFaceCount()==0,
            "Sixteen genuinely physical alternating symmetric welds retain immutable old readers");
    for(std::size_t n=16;n>0;--n) {
        require(brush.undo()&&brush.snapshot()==immutable[n-1],
                "All sixteen accepted physical Boolean roots are independently undoable");
    }
    require(brush.checkpoint().mirrorPairs.size()==starting.mirrorPairs.size(),
            "Sixteen undo operations restore all original reciprocal source vertex IDs");
    for(std::size_t n=1;n<=16;++n)
        require(brush.redo()&&brush.snapshot()==immutable[n],
                "All sixteen physical symmetric terminal roots are independently redoable");
    std::cout<<"[m11] sixteen genuinely joined symmetric two-tower inputs, strength="
             <<strength<<" and complete original-root history\n";
}

void testM11OversizedPhysicalContactRefusesThenValidDrains() {
    // Unlike an injected terminal validator refusal, this is an ACTUAL
    // unsupported source candidate. A broad symmetrical brush moves many
    // initially separate closed tetrahedron pairs at once, exceeding the
    // bounded certified finite-source V13 section budget. The entire
    // physical remesh candidate must be rejected without even one join.
    const auto source=makeM11Towers(16,.15);
    const auto initial=checkpointM11Source(source);
    auto opts=m11PhysicalExactSettings();
    opts.physicalSourceLimits.maxSectionTests=12; // one section group=6
    AsyncSculptWorker worker(initial,.025,chooseM11TowerStrength(),.2,
                             {},{},{},{},opts);
    const auto old=worker.initialRevision();
    const auto wait=[&] {
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
        while(!worker.canBegin()&&std::chrono::steady_clock::now()<stop) {
            (void)worker.status();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(worker.canBegin(),"M11 finite-source over-budget worker event drained");
        return worker.status();
    };
    worker.begin({5,0,70},160,firstOriginalFaces(source.mesh));
    worker.end({5,0,70},0);
    const auto rejected=wait();
    const auto refusedSnapshot=worker.takeSnapshot();
    if(rejected.rejectedStamps!=1)
        throw std::runtime_error(
            "M11 broad physically induced >12 original source contacts did not refuse: "+
            rejected.lastRejection);
    require(rejected.completedStamps==1&&rejected.completedUnions==0&&
            rejected.publishedRevisions==0&&
            rejected.history.undoDepth==0&&
            refusedSnapshot&&refusedSnapshot->index==old&&
            refusedSnapshot->dirtyPartitions.empty()&&
            !rejected.lastRejection.empty()&&
            rejected.contact.uncertainChecks==1&&
            rejected.contact.cleanChecks==0&&
            rejected.contact.contactChecks==0&&
            rejected.contact.episodes==0,
            "M23 actual source V13 section-budget exhaustion is UNKNOWN, never Clean, Contact or a published root");
    // Identical original root and same worker now accept the ordinary
    // six-contact one-region physically triggered mirrored union.
    worker.begin(source.input.front()[0].center,6,
                 m11RegionIntent(source,0));
    worker.end(source.input.front()[0].center,0);
    const auto accepted=wait();
    const auto result=worker.takeSnapshot();
    if(accepted.completedUnions!=1)
        throw std::runtime_error(
            "M11 valid queued stamp failed to recover after source-work refusal: "+
            accepted.lastRejection);
    require(accepted.completedStamps==2&&accepted.rejectedStamps==1&&
            accepted.publishedRevisions==1&&accepted.history.undoDepth==1&&
            result&&result->index!=old&&!result->dirtyPartitions.empty()&&
            old->retiredFaceCount()==0&&result->index->retiredFaceCount()>0&&
            accepted.contact.uncertainChecks==1&&
            accepted.contact.contactChecks==1&&
            accepted.contact.episodes==1&&
            accepted.contact.active,
            "M23 actual work-exhausted physical FIFO recovers with a NEW fully proved Contact episode and valid source weld");
}

void testM11QueuedPhysicalTowersRefusalCleanAndRecovery() {
    const auto source=makeM11Towers(16,.15);
    const auto restored=checkpointM11Source(source);
    const auto first=restored.index;
    const auto strength=chooseM11TowerStrength();
    std::size_t validationCount=0;
    SculptStepValidator rejectFifth=[&](const ProposedSculptStep& step) {
        require(step.before&&step.candidate&&step.before!=step.candidate,
                "M11 queued worker validates only immutable original and full candidate roots");
        if(++validationCount==5)
            return std::string("M11 deliberately invalid terminal stamp: recoverable refusal");
        return std::string{};
    };
    AsyncSculptWorker worker(restored,.025,strength,.2,{},{},{},
                             rejectFifth,m11PhysicalExactSettings());
    std::vector<std::shared_ptr<const RegionTopologyIndex>> roots{first};
    const auto waitForDrain=[&] {
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
        while(!worker.canBegin()&&std::chrono::steady_clock::now()<stop) {
            (void)worker.status();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto current=worker.status();
        require(worker.canBegin()&&current.pendingSteps==0&&
                current.pendingSegments==0,
                "M11 4D worker consumes each queued input exactly once before next stroke");
        return current;
    };
    std::size_t nextRegion=0;
    const auto stampAt=[&](Vec3 center,double radius) {
        // A no-contact command is unscoped; a genuine tower command
        // carries all caller-approved earlier original bands plus this one.
        const bool isNoContact=center.z>9000;
        worker.begin(center,radius,isNoContact?std::vector<FaceId>{}:
                     m11RegionIntent(source,nextRegion));
        worker.end(center,0);
        return waitForDrain();
    };
    for(std::size_t i=0;i<16;++i) {
        const auto center=source.input[i][0].center;
        // An explicit, physically no-contact 4D stroke in the middle
        // must drain without producing a new root or a synthetic union.
        if(i==4) {
            const auto before=roots.back();
            const auto noContact=stampAt({5,-14,10000},6);
            const auto unchangedSnapshot=worker.takeSnapshot();
            require(noContact.completedUnions==4&&
                    noContact.publishedRevisions==4&&
                    noContact.history.undoDepth==4&&
                    noContact.rejectedStamps==0&&
                    unchangedSnapshot&&unchangedSnapshot->index==before&&
                    unchangedSnapshot->dirtyPartitions.empty()&&
                    validationCount==4,
                    "M11 no-contact queued input neither publishes nor consumes a success");
            // Next real positive candidate is VALID geometrically, but
            // the caller vetoes its terminal Step after exact staging.
            // This checks the entire speculative map/history rollback.
            nextRegion=i;
            const auto refused=stampAt(center,6);
            const auto refusedSnapshot=worker.takeSnapshot();
            require(refused.rejectedStamps==1&&
                    refused.completedUnions==4&&
                    refused.publishedRevisions==4&&
                    refused.history.undoDepth==4&&
                    refusedSnapshot&&refusedSnapshot->index==before&&
                    refusedSnapshot->dirtyPartitions.empty()&&
                    validationCount==5&&
                    !refused.lastRejection.empty(),
                    "M11 post-Boolean refused input preserves queued root and full original history");
            require(worker.initialRevision()!=nullptr&&
                    before->pickingRootIdentity()!=nullptr,
                    "Rejected physical Step preserves held immutable BVH readers");
        }
        nextRegion=i;
        const auto current=stampAt(center,6);
        if(current.completedUnions!=i+1)
            throw std::runtime_error(
                "M11 queued alternate tower "+std::to_string(i)+
                " failed physical source union: "+current.lastRejection);
        const auto snapshot=worker.takeSnapshot();
        require(snapshot&&snapshot->index&&snapshot->index!=roots.back()&&
                !snapshot->dirtyPartitions.empty()&&
                current.publishedRevisions==i+1&&
                current.history.undoDepth==i+1&&
                current.completedStamps==i+1+(i>=4?2:0)&&
                current.rejectedStamps==(i>=4?1u:0u),
                "Each M11 real 4D physical input produces exactly one final snapshot and history entry");
        require(roots.back()->pickingRootIdentity()!=nullptr&&
                snapshot->index->retiredFaceCount()>
                    roots.back()->retiredFaceCount(),
                "M11 queued physical terminal source union preserves prior picker roots");
        roots.push_back(snapshot->index);
    }
    const auto checkpointFile=std::filesystem::temp_directory_path()/
        "mesher-v17-m11-16-real-physical-tower-unions.mshr";
    try {
        worker.saveCheckpoint(checkpointFile);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
        while(worker.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.checkpointStatistics().completed==1,
                "M11 4D queued final root has a durable complete checkpoint");
        const auto checkpoint=readCheckpoint(checkpointFile);
        require(checkpoint.index&&
                checkpoint.index->retiredFaceIds()==
                    roots.back()->retiredFaceIds()&&
                checkpoint.mirrorPairs.size()>restored.mirrorPairs.size(),
                "M11 final durable recovery contains all net mirrored terminal source identities");
        for(const auto& [a,b]:checkpoint.mirrorPairs) {
            require(checkpoint.index->maybeVertex(a)&&
                    checkpoint.index->maybeVertex(b),
                    "No retired or transient speculative ID leaks into M11 durable mirror checkpoint");
        }
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(checkpointFile,ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(checkpointFile,ec);
    // M65 (observed on independently merged-main Windows CI): history
    // depth can be published before the coalesced renderer Snapshot.
    // Status-only polling followed by a single takeSnapshot() is an
    // observer race, NOT evidence of a lost physical undo. Do not drop
    // any of the 16 exact immutable-root identity assertions.
    const auto waitForExactHistoryFrame=[&](
            const std::shared_ptr<const RegionTopologyIndex>& expected,
            std::size_t undoDepth,std::size_t redoDepth,
            const char* action,std::size_t step) {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(30);
        std::optional<AsyncSculptWorker::Snapshot> observed;
        while(std::chrono::steady_clock::now()<until) {
            if(auto candidate=worker.takeSnapshot()) {
                if(candidate->index==expected)
                    observed=std::move(candidate);
            }
            const auto status=worker.status();
            if(observed&&observed->index==expected&&
               worker.initialRevision()==expected&&
               status.history.undoDepth==undoDepth&&
               status.history.redoDepth==redoDepth)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const auto status=worker.status();
        throw std::runtime_error(
            std::string("M11 4D FIFO ")+action+
            " exact root/history frame timed out at step "+
            std::to_string(step)+" (expected depths "+
            std::to_string(undoDepth)+"/"+std::to_string(redoDepth)+
            ", observed "+std::to_string(status.history.undoDepth)+
            "/"+std::to_string(status.history.redoDepth)+
            ", expected frame "+(observed?"seen":"not seen")+")");
    };
    for(std::size_t n=16;n>0;--n) {
        worker.undo();
        waitForExactHistoryFrame(roots[n-1],n-1,17-n,"undo",n);
    }
    for(std::size_t n=1;n<=16;++n) {
        worker.redo();
        waitForExactHistoryFrame(roots[n],n,16-n,"redo",n);
    }
}

void testM28FullPrivatePhysicalStampLifetime() {
    // Two independently complete *private* source chains; the worker's
    // final original-root CAS is simulated only AFTER M21's decision.
    Mesh shape={{{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
                 {0,0,1},{0,0,-1}},
                {{4,0,1},{4,1,2},{4,2,3},{4,3,0},
                 {5,1,0},{5,2,1},{5,3,2},{5,0,3}}};
    const auto indexed=partitionFlatMesh(shape,256);
    const auto original=RegionTopologyIndex::build(indexed.revision);
    const auto firstRoot=original->pickingRootIdentity();
    const auto before=beginOriginalPatchLifetime(
        captureOriginalSourceAnchors(original,indexed.flatIndex.faceIds,56));
    VersionedInflateBrush inflate(.035,{});
    VersionedLocalRefiner refine(6,{},{},256);
    VersionedDirectionalCollapser collapse(6,{},{},256);
    const std::array<BrushArea,1> patch{{{{1,0,0},.8,.9}}};
    const auto firstInflate=inflate.stamp(original,patch,256);
    require(firstInflate.moved>0,
            "M28 first real Inflate stamp moves permanent source vertices");
    const auto firstRefine=refine.apply(
        firstInflate.index,firstInflate.baseline);
    const auto firstCollapse=collapse.apply(
        firstRefine.index,firstInflate.baseline);
    const auto once=advanceCertifiedPhysicalStampLifetime(
        before,firstInflate,firstRefine,firstCollapse,
        nullptr,nullptr,128,128,6,8);
    require(once.acceptedTransitions==1&&
            once.anchors.original==original&&
            once.lineage.terminal==firstCollapse.index,
            "M28 full private Inflate/refine/collapse stages form ONE externally certified original Step");
    const auto secondInflate=inflate.stamp(firstCollapse.index,patch,256);
    const auto secondRefine=refine.apply(
        secondInflate.index,secondInflate.baseline);
    const auto secondCollapse=collapse.apply(
        secondRefine.index,secondInflate.baseline);
    const auto twice=advanceCertifiedPhysicalStampLifetime(
        once,secondInflate,secondRefine,secondCollapse,
        nullptr,nullptr,128,128,6,8);
    require(twice.acceptedTransitions==2&&
            twice.anchors.original==original&&
            twice.lineage.terminal==secondCollapse.index,
            "M28 second real private stamp retains FIRST original references and one external successor");
    const auto firstDistance=auditOriginalPatchLifetime(once,original,56,128);
    const auto secondDistance=auditOriginalPatchLifetime(twice,original,56,128);
    require(secondDistance.total.maximumDistanceInEdges>
                firstDistance.total.maximumDistanceInEdges+1e-6,
            "M28 second stamp accumulates measured protected erosion against FIRST root");
    const double tolerance=(firstDistance.total.maximumDistanceInEdges+
                            secondDistance.total.maximumDistanceInEdges)*.5;
    const auto firstIntent=declareOriginalPatchIntent(before,{});
    const auto secondIntent=declareOriginalPatchIntent(once,{});
    const auto accepted=evaluateOriginalPatchIntent(
        before,once,firstIntent,tolerance,56,128);
    require(accepted.protectedOriginals==8&&
            accepted.totalDistanceTests==56,
            "M28 complete first physical displacement is below explicit cumulative policy limit");
    IndexedMeshRepository repository(original);
    require(repository.publishIfCurrent(original,once.lineage.terminal),
            "M28 first verified candidate is eligible for atomic CAS");
    bool refused=false;
    try {(void)evaluateOriginalPatchIntent(
        once,twice,secondIntent,tolerance,56,128);}
    catch(const std::logic_error&){refused=true;}
    require(refused&&repository.snapshot()==once.lineage.terminal&&
            original->pickingRootIdentity()==firstRoot,
            "M28 second cumulative erosion refuses before private candidate can touch published root");
    refused=false;
    try {(void)advanceCertifiedPhysicalStampLifetime(
        once,secondInflate,secondRefine,secondCollapse,
        nullptr,nullptr,128,128,5,8);}
    catch(const std::length_error&){refused=true;}
    require(refused,"M28 complete Inflate vertex evidence bound accepts six, refuses five");
    auto forged=secondInflate;
    forged.baseline.movedVertices.clear();
    refused=false;
    try {(void)advanceCertifiedPhysicalStampLifetime(
        once,forged,secondRefine,secondCollapse,
        nullptr,nullptr,128,128,6,8);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"M28 unjournaled actual stamp displacement refuses");

    // Real M24 source split in the SAME privately prepared physical
    // stamp, not a synthetic standalone child ancestry record.
    VersionedInflateBrush growing(.3,{});
    const auto grown=growing.stamp(original,patch,256);
    RefineSettings splitOptions;
    splitOptions.enabled=true;
    splitOptions.maximumEdgeRatio=.1;
    splitOptions.maximumSplitPairsPerStamp=1;
    splitOptions.optimizeFlips=false;
    VersionedLocalRefiner splitRefiner(6,splitOptions,{},256);
    const auto split=splitRefiner.apply(grown.index,grown.baseline);
    require(split.statistics.splitPairs==1&&split.splitSources.size()==2,
            "M28 actual physical Inflate moved an original edge enough to require one committed M24 split");
    VersionedDirectionalCollapser noCollapse(6,{},{},256);
    const auto collapsedSplit=noCollapse.apply(split.index,grown.baseline);
    const auto splitLifetime=advanceCertifiedPhysicalStampLifetime(
        before,grown,split,collapsedSplit,nullptr,nullptr,2,10,6,8);
    const auto splitIntent=declareOriginalPatchIntent(
        before,original->vertex(indexed.flatIndex.vertexIds[0]).incidentFaces);
    const auto splitDecision=evaluateOriginalPatchIntent(
        before,splitLifetime,splitIntent,1e-9,70,10);
    require(splitLifetime.acceptedTransitions==1&&
            splitLifetime.sourceFaceEvidence==2&&
            splitLifetime.lineage.terminal==collapsedSplit.index&&
            splitDecision.totalDistanceTests==70&&
            splitDecision.protectedOriginals==4,
            "M28 one external first-source successor spans actual Inflate and two-child M24 refinement");
    refused=false;
    try {(void)advanceCertifiedPhysicalStampLifetime(
        before,grown,split,collapsedSplit,nullptr,nullptr,1,10,6,8);}
    catch(const std::length_error&){refused=true;}
    require(refused,
            "M28 one-under complete physical split journal refuses before publication");

    // Genuine source-complete c1/c2/c3/M03/d2 union following an Inflate
    // stamp, with NO intermediate stage masquerading as a published root.
    Mesh tetra;
    for(const auto p:std::array<Vec3,8>{{
        {-1,0,0},{1,0,0},{0,1.2,0},{0,.2,-1.3},
        {-.65,.1,.35},{.65,.1,.35},{0,.9,.35},{0,.3125,-.65}
    }})tetra.vertices.push_back({p.x+5,p.y,p.z});
    for(unsigned body=0;body<2;++body)
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})tetra.triangles.push_back({
            4*body+t[0],4*body+t[1],4*body+t[2]});
    const auto supplied=partitionFlatMesh(tetra,256);
    const auto source=RegionTopologyIndex::build(supplied.revision);
    const auto sourceBefore=beginOriginalPatchLifetime(
        captureOriginalSourceAnchors(source,supplied.flatIndex.faceIds,56));
    const std::array<BrushArea,1> touching{{{{4,0,0},.5,.9}}};
    VersionedInflateBrush physicalInflate(.0005,{});
    const auto moved=physicalInflate.stamp(source,touching,256);
    VersionedLocalRefiner physicalRefine(8,{},{},256);
    const auto refined=physicalRefine.apply(moved.index,moved.baseline);
    VersionedDirectionalCollapser physicalCollapse(8,{},{},256);
    const auto collapsed=physicalCollapse.apply(refined.index,moved.baseline);
    const auto contacts=assessPostSculptOriginalContacts(
        {source,collapsed.index,moved.baseline.movedVertices,
         moved.baseline.affectedFaces},false);
    require(contacts.ready(),
            "M28 genuine post-physical original V13 contacts are completely certified");
    ExactCutStepLimits quality;
    auto& c=quality.join.removal.contours;
    c.maximumPlanarityInRadii=4.0;
    c.maximumSingleCapPlanarityInRadii=4.0;
    c.maximumIsoperimetricRatio=20.0;
    c.maximumSingleCapIsoperimetricRatio=20.0;
    c.maximumPerimeterRatio=20.0;
    c.maximumGapInEdges=2.5;
    c.minimumNormalAlignment=0;
    const auto exact=prepareExactCutStep(
        collapsed.index,contacts.contacts,quality);
    require(exact.complete,"M28 genuine physically generated exact terminal source join accepted");
    const auto joined=advanceCertifiedPhysicalStampLifetime(
        sourceBefore,moved,refined,collapsed,&exact,
        nullptr,8192,8192,8,8);
    require(joined.acceptedTransitions==1&&
            joined.lineage.original==source&&
            joined.lineage.terminal==exact.final&&
            joined.sourceFaceEvidence>0&&
            exact.final->retiredFaceCount()>0,
            "M28 real Inflate/refine/collapse/c3-M03-d2 cascade exposes ONE sealed original successor");
    const auto allow=declareOriginalPatchIntent(
        sourceBefore,supplied.flatIndex.faceIds);
    std::size_t comparisons=0;
    for(const auto& [_,faces]:joined.lineage.living)
        comparisons+=7*faces.size();
    const auto verified=evaluateOriginalPatchIntent(
        sourceBefore,joined,allow,0,comparisons,8192);
    require(verified.totalDistanceTests==comparisons&&
            verified.certifiedRetiredOriginals==
                joined.certifiedRetiredOriginals.size(),
            "M28 physical union composes exact source retirement with original intent");
    refused=false;
    try {(void)advanceCertifiedPhysicalStampLifetime(
        sourceBefore,moved,refined,collapsed,&exact,
        nullptr,joined.sourceFaceEvidence-1,8192,8,8);}
    catch(const std::length_error&){refused=true;}
    require(refused,"M28 real exact union source evidence one-under limit refuses");
}

void testFirstPhysicalExactBooleanOneOriginalRootStep() {
    const auto config=[] {
        SculptCollisionSettings c;
        c.physicalExact=true; // deliberately never set by the desktop UI
        auto& p=c.physicalExactLimits.join.removal.contours;
        p.maximumPlanarityInRadii=4.0;
        p.maximumSingleCapPlanarityInRadii=4.0;
        p.maximumIsoperimetricRatio=20.0;
        p.maximumSingleCapIsoperimetricRatio=20.0;
        p.maximumPerimeterRatio=20.0;
        p.maximumGapInEdges=2.5;
        p.minimumNormalAlignment=0;
        return c;
    };
    const auto geometry=[] {
        Mesh mesh;
        for(const auto p:std::array<Vec3,8>{{
            {-1,0,0},{1,0,0},{0,1.2,0},{0,.2,-1.3},
            {-.65,.1,.35},{.65,.1,.35},{0,.9,.35},{0,.3125,-.65}
        }})mesh.vertices.push_back({p.x+5,p.y,p.z});
        for(unsigned tetra=0;tetra<2;++tetra)
            for(const auto t:std::array<std::array<unsigned,3>,4>{{
                {0,1,2},{1,0,3},{2,1,3},{0,2,3}
            }})mesh.triangles.push_back({
                4*tetra+t[0],4*tetra+t[1],4*tetra+t[2]});
        return mesh;
    };
    const auto initial=geometry();
    auto configExact=config();
    Mesh dummy;
    const std::array<BrushArea,1> stroke{{{{4,0,0},.5,.9}}};
    VersionedSculptBrush physical(
        initial,.0005,{},{},{},false,256,{},configExact);
    const auto original=physical.snapshot();
    const auto oldPicker=original->pickingRootIdentity();
    physical.setOriginalPatchIntent(firstOriginalFaces(initial));
    physical.stamp(dummy,stroke);
    if(physical.completedUnions()!=1)
        throw std::runtime_error(
            "Physically moved certified off-plane union did not publish: "+
            physical.lastRejection());
    require(physical.completedStamps()==1&&
            physical.publishedRevisions()==1&&
            physical.historyStatistics().undoDepth==1&&
            physical.collisionRepairedStamps()==1&&
            !physical.lastDirtyPartitions().empty()&&
            physical.snapshot()!=original&&
            original->pickingRootIdentity()==oldPicker&&
            original->retiredFaceCount()==0,
            "M18 first physically generated candidate publishes ONE exact original-root Step");
    const auto terminal=physical.snapshot();
    std::vector<VertexId> all;
    terminal->revision().forEachPartition([&](PartitionId,const MeshPartition& p){
        for(const auto& v:p.vertices)all.push_back(v.id);
    });
    require(terminal->triangleIntersections(all,{}).empty()&&
            terminal->retiredFaceCount()>0,
            "Real physically triggered source weld has tombstones and no residual V13 contacts");
    require(physical.undo()&&physical.snapshot()==original&&
            physical.historyStatistics().redoDepth==1&&
            physical.redo()&&physical.snapshot()==terminal,
            "M18 original-root physical union occupies exactly one undo/redo history transition");
    const auto journal=physical.checkpoint();
    require(journal.index==terminal&&journal.mirrorPairs.empty()&&
            journal.index->retiredFaceIds()==terminal->retiredFaceIds(),
            "M18 physical final checkpoint retains ONLY original-root final result");

    // The identical policy also travels through the actual asynchronous
    // 4D input queue, not through an explicit contact/union command.
    AsyncSculptWorker queued(initial,.025,.0005,.2,{},{},{},false,
                             {},config());
    const auto queuedOriginal=queued.initialRevision();
    queued.begin({4,0,0},.5,firstOriginalFaces(initial));
    queued.end({4,0,0},0);
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
    while(!queued.canBegin()&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto state=queued.status();
    if(state.completedUnions!=1)
        throw std::runtime_error(
            "M18 real queued physical sculpt input refused: "+state.lastRejection);
    require(queued.canBegin()&&state.completedStamps==1&&
            state.publishedRevisions==1&&state.history.undoDepth==1&&
            state.collisionRepairedStamps==1,
            "M18 physically queued stroke completes one union/CAS/history, no Merge command");
    const auto queuedFinal=queued.takeSnapshot();
    require(queuedFinal&&queuedFinal->index!=queuedOriginal&&
            !queuedFinal->dirtyPartitions.empty()&&
            queuedFinal->index->retiredFaceCount()>0,
            "M18 renderer mailbox sees only full terminal original-root physical Boolean");
    queued.undo();
    while(queued.status().history.redoDepth==0&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(queued.status().history.redoDepth==1,
            "M18 queued physical union has one undo/redo transition");
    queued.redo();
    while(queued.status().history.redoDepth!=0&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    require(queued.status().history.redoDepth==0&&
            queued.status().history.undoDepth==1,
            "M18 worker redo restores single terminal Boolean history entry");
    const auto durableFile=std::filesystem::temp_directory_path()/
        "mesher-v17-m18-physical-worker-checkpoint.mshr";
    try {
        queued.saveCheckpoint(durableFile);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        while(queued.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(queued.checkpointStatistics().completed==1,
                "M18 real queued physical terminal Step is durably saved");
        const auto reloaded=readCheckpoint(durableFile);
        require(reloaded.index&&
                reloaded.index->retiredFaceIds()==
                    queuedFinal->index->retiredFaceIds()&&
                reloaded.index->retiredVertexIds()==
                    queuedFinal->index->retiredVertexIds(),
                "M18 queued recovered checkpoint preserves final-only permanent ID history");
    }catch(...){
        std::error_code ec;std::filesystem::remove(durableFile,ec);
        throw;
    }
    std::error_code removeError;std::filesystem::remove(durableFile,removeError);

    // A deliberately exhausted source query refuses before any private
    // conformance root or history is exposed. A later identical valid
    // physical stamp must work with the original immutable root.
    auto invalid=config();
    invalid.physicalSourceLimits.maxSectionTests=1;
    VersionedSculptBrush refused(initial,.0005,{},{},{},false,256,{},invalid);
    const auto preserved=refused.snapshot();
    refused.stamp(dummy,stroke);
    require(refused.snapshot()==preserved&&
            refused.rejectedStamps()==1&&
            refused.historyStatistics().undoDepth==0&&
            refused.lastDirtyPartitions().empty()&&
            refused.checkpoint().index==preserved,
            "M18 incomplete physical source work cannot publish an original-root Step");

    // A real symmetric paired stamp is checked on BOTH reflected sheets.
    Mesh reflected=initial;
    const auto n=initial.vertices.size();
    for(const auto p:initial.vertices)
        reflected.vertices.push_back({-p.x,p.y,p.z});
    const auto nf=initial.triangles.size();
    for(std::size_t i=0;i<nf;++i) {
        const auto t=initial.triangles[i];
        const auto off=static_cast<std::uint32_t>(n);
        reflected.triangles.push_back({t.a+off,t.c+off,t.b+off});
    }
    const auto imported=partitionFlatMesh(reflected,256);
    DurableCheckpoint source;
    source.index=RegionTopologyIndex::build(imported.revision);
    source.initialVertexCount=reflected.vertices.size();
    for(std::size_t i=0;i<n;++i)
        source.mirrorPairs.emplace_back(
            imported.flatIndex.vertexIds[i],
            imported.flatIndex.vertexIds[n+i]);
    source.verifiedOriginalFidelity=std::make_shared<const OriginalPatchLifetime>(
        beginOriginalPatchLifetime(captureOriginalSourceAnchors(
            source.index,imported.flatIndex.faceIds,
            7*imported.flatIndex.faceIds.size())));
    source.verifiedFirstOriginalMirrorPairs=source.mirrorPairs;
    source.originalFidelityRequired=true;
    std::size_t validationCount=0;
    SculptStepValidator deliberateRefusal=[&](const ProposedSculptStep& candidate) {
        require(candidate.before&&candidate.candidate&&
                candidate.before!=candidate.candidate,
                "M18 validator sees only the OLD and FULL terminal Boolean root");
        return ++validationCount==1?
            std::string("M18 deliberately refused after speculative exact mirror staging"):
            std::string{};
    };
    VersionedSculptBrush symmetric(source,.0005,{},{},{},256,
                                   deliberateRefusal,config());
    const auto mirroredInitial=symmetric.snapshot();
    auto expectedOriginalPairs=source.mirrorPairs;
    for(auto& [a,b]:expectedOriginalPairs)
        if(b<a)std::swap(a,b);
    std::sort(expectedOriginalPairs.begin(),expectedOriginalPairs.end());
    const std::array<BrushArea,2> both{{
        {{4,0,0},.5,.9},{{-4,0,0},.5,.9}}};
    symmetric.setOriginalPatchIntent(firstOriginalFaces(reflected));
    symmetric.stamp(dummy,both);
    require(symmetric.snapshot()==mirroredInitial&&
            symmetric.rejectedStamps()==1&&
            symmetric.historyStatistics().undoDepth==0&&
            symmetric.checkpoint().mirrorPairs==expectedOriginalPairs&&
            symmetric.publishedRevisions()==0&&
            symmetric.lastDirtyPartitions().empty()&&
            !symmetric.lastRejection().empty(),
            "M18 speculative exact source join rejection must leave original root/history");
    symmetric.stamp(dummy,both);
    if(symmetric.completedUnions()!=1)
        throw std::runtime_error(
            "M18 real mirrored physical exact Step refused: "+
            symmetric.lastRejection());
    const auto final=symmetric.snapshot();
    require(final!=mirroredInitial&&
            symmetric.publishedRevisions()==1&&
            symmetric.historyStatistics().undoDepth==1&&
            symmetric.checkpoint().mirrorPairs.size()>source.mirrorPairs.size(),
            "Physically stamped reflected union publishes one root with net new mirror orbits");
    for(const auto& [a,b]:symmetric.checkpoint().mirrorPairs) {
        require(final->maybeVertex(a)&&final->maybeVertex(b),
                "Physical reflected checkpoint contains only live reciprocal mirror IDs");
        const auto pa=final->vertex(a).position,pb=final->vertex(b).position;
        require(pa.x==-pb.x&&pa.y==pb.y&&pa.z==pb.z,
                "Physical reflected exact union retains source-correct 3D mirrors");
    }
    require(symmetric.undo()&&symmetric.snapshot()==mirroredInitial&&
            symmetric.checkpoint().mirrorPairs==expectedOriginalPairs&&
            symmetric.redo()&&symmetric.snapshot()==final,
            "Physical exact symmetric history reverses full original and terminal mirrors");
    const auto file=std::filesystem::temp_directory_path()/
        "mesher-v17-m18-one-physical-exact-boolean.mshr";
    try {
        writeCheckpoint(file,symmetric.checkpoint());
        const auto reloaded=readCheckpoint(file);
        require(reloaded.index&&
                reloaded.index->retiredFaceIds()==final->retiredFaceIds()&&
                reloaded.mirrorPairs==symmetric.checkpoint().mirrorPairs,
                "Physical exact Boolean survives one durable mirrored checkpoint read-back");
    }catch(...){
        std::error_code ec;std::filesystem::remove(file,ec);
        throw;
    }
    std::error_code ec;std::filesystem::remove(file,ec);
}
void testWorkerInvariantX0OneOriginalRootHistoryAndCheckpoint() {
    Mesh mesh;
    for(const auto p:std::array<Vec3,8>{{
        {-1,0,0},{1,0,0},{0,1.2,0},{0,.2,-1.3},
        {-.65,.1,.35},{.65,.1,.35},{0,.9,.35},{0,.3125,-.65}
    }})mesh.vertices.push_back(p);
    for(unsigned tetra=0;tetra<2;++tetra)
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})
            mesh.triangles.push_back({
                tetra*4+t[0],tetra*4+t[1],tetra*4+t[2]});
    const auto imported=partitionFlatMesh(mesh,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> originalPairs;
    for(unsigned tetra=0;tetra<2;++tetra) {
        const auto base=tetra*4u;
        const auto a=imported.flatIndex.vertexIds[base],
                   b=imported.flatIndex.vertexIds[base+1],
                   c=imported.flatIndex.vertexIds[base+2],
                   d=imported.flatIndex.vertexIds[base+3];
        originalPairs.emplace_back(a,b);
        originalPairs.emplace_back(c,c);
        originalPairs.emplace_back(d,d);
    }
    AsyncSculptWorker worker({root,originalPairs,8},
                             .025,.006,.2,{},{},{});
    const auto contacts=fullOriginalContacts(*root);
    require(contacts.size()==3,
            "X=0 worker requires the exact complete THREE-source-contact input");
    auto limits=supportedUnionLimits(true);
    limits.join.removal.contours.maximumGapInEdges=2.5;
    const auto firstContacts=contacts;
    auto partial=firstContacts;
    partial.pop_back();
    worker.mergeExactContacts(root,partial,limits);
    waitForUnion(worker,1);
    require(worker.status().rejectedUnions==1&&
            worker.status().completedUnions==0&&
            worker.status().history.undoDepth==0&&
            !worker.takeSnapshot(),
            "Incomplete X=0 original pair refuses without a cut root or partial history");
    worker.mergeExactContacts(root,contacts,limits);
    waitForUnion(worker,2);
    if(worker.status().completedUnions!=1)
        throw std::runtime_error(
            "Queued source-certified X=0 original-root Step refused: "+
            worker.status().lastRejection);
    require(worker.status().publishedRevisions==1&&
            worker.status().history.undoDepth==1&&
            worker.status().completedStamps==0,
            "One accepted X=0 union input publishes exactly one original-root Step");
    const auto accepted=worker.takeSnapshot();
    require(accepted&&accepted->index&&accepted->index!=root&&
            !accepted->dirtyPartitions.empty()&&
            root->retiredFaceCount()==0&&
            accepted->index->retiredFaceCount()>0,
            "Renderer obtains final X=0 joined root only, preserving old picking readers");
    const auto final=accepted->index;
    std::size_t saved=0;
    const auto checkpoint=[&](const char* stage) {
        const auto file=std::filesystem::temp_directory_path()/
            (std::string("mesher_v17_m09_worker_")+stage+".mshr");
        worker.saveCheckpoint(file);
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
        ++saved;
        while(worker.checkpointStatistics().completed<saved&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.checkpointStatistics().completed==saved,
                "Queued X=0 mirror snapshot finished its durable write");
        auto loaded=readCheckpoint(file);
        std::filesystem::remove(file);
        return loaded;
    };
    const auto after=checkpoint("accepted");
    require(after.index->retiredFaceIds()==final->retiredFaceIds(),
            "Final worker checkpoint stores ONLY completed X=0 terminal root");
    const auto mirrors=after.mirrorPairs;
    std::size_t fixed=0,paired=0;
    for(const auto& [a,b]:mirrors) {
        require(final->maybeVertex(a)&&final->maybeVertex(b),
                "All three X=0 worker controller mirror maps retain only live IDs");
        const auto p=final->vertex(a).position,
                   q=final->vertex(b).position;
        require(p.x==-q.x&&p.y==q.y&&p.z==q.z,
                "X=0 worker mirror map must remain exact after original CAS");
        if(a==b){++fixed;require(p.x==0,
                "One worker self-mirror entry represents one permanent X=0 ID");}
        else ++paired;
    }
    require(fixed>0&&paired>0&&mirrors.size()>originalPairs.size(),
            "Worker accepted newly added SINGLETON and reciprocal mirror orbits");
    worker.mergeExactContacts(root,contacts,limits);
    waitForUnion(worker,3);
    require(worker.status().rejectedUnions==2&&
            worker.status().history.undoDepth==1&&
            !worker.takeSnapshot(),
            "Stale queued X=0 Step never publishes a second union or mirror delta");
    worker.undo();
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(20);
    while(worker.status().history.redoDepth==0&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto undone=worker.takeSnapshot();
    require(undone&&undone->index==root,
            "One worker undo restores SAME original immutable root");
    const auto undoneMirrors=checkpoint("undone").mirrorPairs;
    auto expectedOriginalMirrors=originalPairs;
    for(auto& [a,b]:expectedOriginalMirrors)
        if(b<a)std::swap(a,b);
    std::sort(expectedOriginalMirrors.begin(),expectedOriginalMirrors.end());
    if(undoneMirrors!=expectedOriginalMirrors) {
        std::cout<<"[x0-m09-undo] original="<<expectedOriginalMirrors.size()
                 <<" restored="<<undoneMirrors.size()<<'\n';
        for(const auto& [a,b]:expectedOriginalMirrors)
            std::cout<<"[x0-m09-undo] expected "<<a<<","<<b<<'\n';
        for(const auto& [a,b]:undoneMirrors)
            std::cout<<"[x0-m09-undo] actual "<<a<<","<<b<<'\n';
    }
    require(undoneMirrors==expectedOriginalMirrors,
            "One X=0 worker undo restores all historical fixed and reciprocal mirror orbits");
    worker.redo();
    while(worker.status().history.redoDepth!=0&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto redone=worker.takeSnapshot();
    require(redone&&redone->index==final&&
            checkpoint("redone").mirrorPairs==mirrors,
            "One worker redo restores exact terminal root and singleton/pair mirror map");
    const auto loaded=checkpoint("terminal");
    require(loaded.index&&loaded.mirrorPairs==mirrors&&
            loaded.index->retiredFaceIds()==final->retiredFaceIds()&&
            loaded.index->retiredVertexIds()==final->retiredVertexIds(),
            "X=0 durable reload has exact live fixed/paired orbits and tombstones");
    AsyncSculptWorker recovered(loaded,.025,.006,.2,{},{},{});
    require(recovered.initialRevision()==loaded.index&&
            recovered.status().history.undoDepth==0&&
            recovered.status().publishedRevisions==0,
            "Recovered X=0 worker begins from ONLY the committed terminal revision");
    // A restored symmetric worker must have three mutually consistent
    // mirror-controller states or this explicit command refuses before
    // attempting source V13 planning with the wrong root.
    recovered.mergeExactContacts(loaded.index,contacts,limits);
    waitForUnion(recovered,1);
    require(recovered.status().completedUnions==0&&
            recovered.status().rejectedUnions==1&&
            recovered.status().lastRejection.find("controllers disagree")==
                std::string::npos&&
            !recovered.takeSnapshot(),
            "Recovered worker keeps synchronized fixed/pair maps while refusing stale source contacts");
}
void testWorkerMirroredUnionMirrorHistoryAndCheckpoint() {
    auto mesh=twoIntersectingTetrahedra(true);
    const auto imported=partitionFlatMesh(mesh,256);
    auto original=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> originalPairs;
    for(std::size_t i=0;i<8;++i)
        originalPairs.emplace_back(imported.flatIndex.vertexIds[i],
                                   imported.flatIndex.vertexIds[i+8]);
    AsyncSculptWorker worker({original,originalPairs,16},
                             .025,.006,.2,{},{},{});
    const auto contacts=fullOriginalContacts(*original);
    const auto limits=supportedUnionLimits(true);
    worker.mergeExactContacts(original,contacts,limits);
    waitForUnion(worker,1);
    if(worker.status().completedUnions!=1)
        throw std::runtime_error(
            "Mirrored worker refused complete exact union: "+
            worker.status().lastRejection);
    // The command's completed-union counter is published separately
    // from its coalesced renderer frame and history snapshot. Never
    // assume the first takeSnapshot() immediately after status has
    // switched must contain the new root (observed on merged main GCC).
    std::optional<AsyncSculptWorker::Snapshot> joined;
    const auto joinDeadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(15);
    while(std::chrono::steady_clock::now()<joinDeadline) {
        if(auto frame=worker.takeSnapshot()) {
            if(frame->index&&frame->index!=original)joined=std::move(frame);
        }
        const auto current=worker.status();
        if(joined&&joined->index==worker.initialRevision()&&
           current.history.undoDepth==1)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(joined&&joined->index&&joined->index!=original&&
            joined->index==worker.initialRevision()&&
            worker.status().history.undoDepth==1,
            "Mirrored union publishes one immutable root and one history transition");
    // M57 CI exposed the old test's status-only observer race on MSVC:
    // history.redoDepth can advance BEFORE the worker publishes its
    // coalesced immutable renderer Snapshot. Never mistake the absence
    // of that still-pending frame for a worker undo/redo defect.
    const auto awaitHistoryFrame=[&](
            const std::shared_ptr<const RegionTopologyIndex>& expected,
            std::size_t undoDepth,std::size_t redoDepth) {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(15);
        while(std::chrono::steady_clock::now()<until) {
            const auto frame=worker.takeSnapshot();
            const auto status=worker.status();
            if(frame&&frame->index==expected&&
               worker.initialRevision()==expected&&
               status.history.undoDepth==undoDepth&&
               status.history.redoDepth==redoDepth)return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };
    worker.undo();
    require(awaitHistoryFrame(original,0,1),
            "Mirrored worker undo restores original root and reciprocal old mirrors");
    worker.redo();
    require(awaitHistoryFrame(joined->index,1,0),
            "Mirrored worker redo restores both final welded components");
    const auto file=std::filesystem::temp_directory_path()/
                    "mesher-worker-v17-reflected-union.mshr";
    try {
        worker.saveCheckpoint(file);
        const auto saveDeadline=std::chrono::steady_clock::now()+
                                std::chrono::seconds(15);
        while(worker.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<saveDeadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.checkpointStatistics().completed==1,
                "Mirrored one-Step checkpoint persisted");
        const auto loaded=readCheckpoint(file);
        require(loaded.index->retiredVertexIds()==joined->index->retiredVertexIds()&&
                loaded.mirrorPairs.size()>0,
                "Mirrored union checkpoint has only surviving mirror correspondences");
        for(const auto& [a,b]:loaded.mirrorPairs)
            require(loaded.index->maybeVertex(a)&&loaded.index->maybeVertex(b),
                    "Mirrored checkpoint never refers to a deleted seam vertex");
    }catch(...) {
        std::error_code ec;std::filesystem::remove(file,ec);
        throw;
    }
    std::error_code ec;std::filesystem::remove(file,ec);
}

void testM32ActivePhysicalOriginalTerminalPolicy() {
    // The same actual two normal-driven stages are independently certified
    // with M28 solely to choose a tolerance between genuine measurements.
    // The production brush below must enforce it itself BEFORE publishing.
    Mesh shape={{{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
                 {0,0,1},{0,0,-1}},
                {{4,0,1},{4,1,2},{4,2,3},{4,3,0},
                 {5,1,0},{5,2,1},{5,3,2},{5,0,3}}};
    const auto imported=partitionFlatMesh(shape,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    auto original=beginOriginalPatchLifetime(captureOriginalSourceAnchors(
        root,imported.flatIndex.faceIds,56));
    VersionedInflateBrush physical(.035,{});
    VersionedLocalRefiner refiner(6,{},{},256);
    VersionedDirectionalCollapser collapser(6,{},{},256);
    const std::array<BrushArea,1> patch{{{{1,0,0},.8,.9}}};
    const auto inflated1=physical.stamp(root,patch,256);
    const auto refined1=refiner.apply(inflated1.index,inflated1.baseline);
    const auto collapsed1=collapser.apply(refined1.index,inflated1.baseline);
    const auto first=advanceCertifiedPhysicalStampLifetime(
        original,inflated1,refined1,collapsed1,nullptr,nullptr,128,128,6,8);
    const auto inflated2=physical.stamp(first.lineage.terminal,patch,256);
    const auto refined2=refiner.apply(inflated2.index,inflated2.baseline);
    const auto collapsed2=collapser.apply(refined2.index,inflated2.baseline);
    const auto second=advanceCertifiedPhysicalStampLifetime(
        first,inflated2,refined2,collapsed2,nullptr,nullptr,128,128,6,8);
    const auto one=auditOriginalPatchLifetime(first,root,56,128);
    const auto two=auditOriginalPatchLifetime(second,root,56,128);
    const double tolerance=(one.total.maximumDistanceInEdges+
                            two.total.maximumDistanceInEdges)*.5;
    require(two.total.maximumDistanceInEdges>
            one.total.maximumDistanceInEdges+1e-6,
            "M32 real second source displacement is cumulatively larger");

    SculptCollisionSettings exact;
    exact.physicalExact=true;
    exact.originalTerminal.maximumProtectedDeviationInEdges=tolerance;
    exact.originalTerminal.maxDistanceTests=56;
    // A valid caller may permit more descendants than the immutable
    // M29 history bound, but the physical gate MUST ALSO check M29's
    // default before CAS (not discover a mismatch after publication).
    exact.originalTerminal.maxLivingDescendants=32768;
    exact.originalTerminal.maxCumulativeFaceEvidence=128;
    exact.originalTerminal.maxInflateVertices=6;
    exact.originalTerminal.maxInflateFaces=8;
    VersionedSculptBrush brush(shape,.035,{},{},{},false,256,{},exact);
    const auto firstRoot=brush.snapshot();
    const auto oldPicker=firstRoot->pickingRootIdentity();
    const auto initialProof=brush.checkpoint();
    require(initialProof.verifiedOriginalFidelity&&
            initialProof.verifiedOriginalFidelity->lineage.terminal==firstRoot&&
            initialProof.originalFidelityRequired&&
            initialProof.verifiedOriginalFidelity->acceptedTransitions==0,
            "M32 physicalExact history starts sealed at actual FIRST root");
    Mesh dummy;
    brush.stamp(dummy,patch);
    if(brush.publishedRevisions()!=1)
        throw std::runtime_error(
            "M32 first small first-original physical stamp refused: "+
            brush.lastRejection());
    const auto accepted=brush.checkpoint();
    require(accepted.index==brush.snapshot()&&accepted.index!=firstRoot&&
            accepted.verifiedOriginalFidelity&&
            accepted.verifiedOriginalFidelity->anchors.original==firstRoot&&
            accepted.verifiedOriginalFidelity->acceptedTransitions==1&&
            brush.historyStatistics().undoDepth==1&&
            !brush.lastDirtyPartitions().empty()&&
            firstRoot->pickingRootIdentity()==oldPicker,
            "M32 one pre-CAS-certified physical Step publishes matching root, lifetime and dirty history");
    // Independently apply the actual default M29 append contract to
    // this REAL protected physical successor, even though the caller's
    // configured M21 descendant budget is more permissive.
    verifyOriginalPatchPublicationTransition(
        *initialProof.verifiedOriginalFidelity,
        *accepted.verifiedOriginalFidelity,firstRoot,accepted.index);
    brush.stamp(dummy,patch);
    const auto refused=brush.checkpoint();
    require(brush.rejectedStamps()==1&&
            brush.publishedRevisions()==1&&
            brush.historyStatistics().undoDepth==1&&
            brush.lastDirtyPartitions().empty()&&
            refused.index==accepted.index&&
            refused.verifiedOriginalFidelity==accepted.verifiedOriginalFidelity&&
            refused.verifiedOriginalFidelity->sourceFaceEvidence==
                accepted.verifiedOriginalFidelity->sourceFaceEvidence&&
            refused.verifiedOriginalFidelity->anchors.original==firstRoot,
            "M32 individually small but cumulative second first-root erosion refuses atomically");
    require(brush.undo()&&brush.snapshot()==firstRoot&&
            brush.checkpoint().verifiedOriginalFidelity==
                initialProof.verifiedOriginalFidelity&&
            brush.redo()&&brush.snapshot()==accepted.index&&
            brush.checkpoint().verifiedOriginalFidelity==
                accepted.verifiedOriginalFidelity,
            "M32 undo/redo reuses exact matching immutable original-source credentials");

    // A one-under COMPLETE seven-witness cap rejects before source CAS.
    auto low=exact;
    low.originalTerminal.maxDistanceTests=55;
    VersionedSculptBrush bounded(shape,.035,{},{},{},false,256,{},low);
    const auto stable=bounded.snapshot();
    bounded.stamp(dummy,patch);
    require(bounded.rejectedStamps()==1&&bounded.snapshot()==stable&&
            bounded.historyStatistics().undoDepth==0&&
            bounded.checkpoint().verifiedOriginalFidelity->lineage.terminal==
                stable&&bounded.lastDirtyPartitions().empty(),
            "M32 one-below-complete 56 original-child witnesses refuses before CAS");
    low=exact;
    low.originalTerminal.maxInflateVertices=5;
    VersionedSculptBrush vertexLimited(shape,.035,{},{},{},false,256,{},low);
    const auto untouched=vertexLimited.snapshot();
    vertexLimited.stamp(dummy,patch);
    require(vertexLimited.rejectedStamps()==1&&
            vertexLimited.snapshot()==untouched&&
            vertexLimited.historyStatistics().undoDepth==0,
            "M32 complete six-ID moved-source Inflate cap refuses at five");

    // Explicit authenticated deliberate scope permits a real physical
    // source deformation even when unscoped tolerance is zero.
    auto intended=exact;
    intended.originalTerminal.maximumProtectedDeviationInEdges=0;
    VersionedSculptBrush scoped(shape,.035,{},{},{},false,256,{},intended);
    scoped.setOriginalPatchIntent(imported.flatIndex.faceIds);
    scoped.stamp(dummy,patch);
    require(scoped.publishedRevisions()==1&&
            scoped.checkpoint().verifiedOriginalFidelity->acceptedTransitions==1,
            "M32 authenticated pre-edit caller scope exempts only geometric threshold");
    const auto prior=scoped.checkpoint();
    scoped.setOriginalPatchIntent({});
    scoped.stamp(dummy,patch);
    require(scoped.rejectedStamps()==1&&
            scoped.checkpoint().index==prior.index&&
            scoped.checkpoint().verifiedOriginalFidelity==
                prior.verifiedOriginalFidelity,
            "M32 unscoped later stamp does not inherit previous intended exemption");
}



void testM33ProtectedX0FixedPointQueuedUnion() {
    // THREE source-contact original X=0 fixed-point case, not a mirrored
    // off-plane approximation or a coincidence-based source mapping.
    Mesh mesh;
    for(const auto p:std::array<Vec3,8>{{
        {-1,0,0},{1,0,0},{0,1.2,0},{0,.2,-1.3},
        {-.65,.1,.35},{.65,.1,.35},{0,.9,.35},{0,.3125,-.65}
    }})mesh.vertices.push_back(p);
    for(unsigned tetra=0;tetra<2;++tetra)
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})
            mesh.triangles.push_back({
                tetra*4+t[0],tetra*4+t[1],tetra*4+t[2]});
    const auto imported=partitionFlatMesh(mesh,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    std::vector<MirrorPair> mirrorPairs;
    std::unordered_map<VertexId,VertexId> mirrored;
    for(unsigned body=0;body<2;++body) {
        const auto k=body*4u;
        const auto a=imported.flatIndex.vertexIds[k],
                   b=imported.flatIndex.vertexIds[k+1],
                   c=imported.flatIndex.vertexIds[k+2],
                   d=imported.flatIndex.vertexIds[k+3];
        for(const auto& pair:std::array<MirrorPair,3>{{
            {a,b},{c,c},{d,d}
        }}) {
            mirrorPairs.push_back(pair);
            mirrored[pair.first]=pair.second;
            mirrored[pair.second]=pair.first;
        }
    }
    const auto rootCertificate=std::make_shared<const OriginalPatchLifetime>(
        beginOriginalPatchLifetime(captureOriginalSourceAnchors(
            root,imported.flatIndex.faceIds,
            7*imported.flatIndex.faceIds.size())));
    DurableCheckpoint trusted(root,mirrorPairs,8,true,rootCertificate);
    trusted.verifiedFirstOriginalMirrorPairs=mirrorPairs;
    SculptCollisionSettings config;
    config.physicalExact=true;
    config.originalTerminal.maximumProtectedDeviationInEdges=0;
    auto limits=supportedUnionLimits(true);
    limits.join.removal.contours.maximumGapInEdges=2.5;
    const auto contacts=fullOriginalContacts(*root);
    require(contacts.size()==3,"M33 X=0 verified original contains all three contacts");
    const auto exact=prepareExactCutStep(root,contacts,limits,&mirrored);
    require(exact.complete&&exact.final&&exact.sourceEvidence,
            "M33 X=0 real reflected c1/c2/c3/M03/d2 source evidence is complete");
    const auto proof=advanceOriginalPatchLifetime(
        *rootCertificate,exact,&mirrored,16384,16384);
    require(proof.acceptedTransitions==1&&proof.sourceFaceEvidence>0,
            "M33 X=0 fixed-point original source disposition has an authenticated direct successor");

    AsyncSculptWorker worker(trusted,.025,.006,.2,{},{},{},{},config);
    auto incomplete=contacts;
    incomplete.pop_back();
    worker.mergeExactContacts(root,incomplete,limits,imported.flatIndex.faceIds);
    waitForUnion(worker,1);
    require(worker.status().rejectedUnions==1&&
            worker.status().completedUnions==0&&
            worker.status().history.undoDepth==0&&
            !worker.takeSnapshot(),
            "M33 incomplete original X=0 contact set fails without a mirror or history mutation");
    worker.mergeExactContacts(root,contacts,limits,
                              {imported.flatIndex.faceIds.front()});
    waitForUnion(worker,2);
    require(worker.status().rejectedUnions==2&&
            worker.status().history.undoDepth==0&&
            !worker.takeSnapshot(),
            "M33 half-original reflected FaceId intent fails the source-proven orbit test");
    worker.mergeExactContacts(root,contacts,limits,imported.flatIndex.faceIds);
    waitForUnion(worker,3);
    if(worker.status().completedUnions!=1)
        throw std::runtime_error(
            "M33 fully authenticated protected X=0 exact union refused: "+
            worker.status().lastRejection);
    const auto final=worker.takeSnapshot();
    require(final&&final->index&&final->index!=root&&
            !final->dirtyPartitions.empty()&&
            worker.status().history.undoDepth==1&&
            worker.status().publishedRevisions==1,
            "M33 X=0 protected union commits one final root after two refused inputs");
    worker.undo();
    const auto stop=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(worker.status().history.redoDepth==0&&
          std::chrono::steady_clock::now()<stop)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto back=worker.takeSnapshot();
    require(back&&back->index==root,
            "M33 X=0 protected original COW root remains exact under undo");
    worker.redo();
    while(worker.status().history.redoDepth!=0&&
          std::chrono::steady_clock::now()<stop)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto restored=worker.takeSnapshot();
    require(restored&&restored->index==final->index,
            "M33 X=0 tracked redo restores final reflected fixed-point root");

    // M34: the exact reflected three-contact terminal itself becomes
    // the predecessor of a REAL symmetric 4D physical Begin/End. The
    // caller names original FaceIds, including M26-certified retired
    // origins, rather than substituting coincident or reflected XYZ.
    worker.begin({1,0,0},.3,imported.flatIndex.faceIds);
    worker.end({1,0,0},0);
    const auto physicalDeadline=std::chrono::steady_clock::now()+
                                std::chrono::seconds(20);
    while(!worker.canBegin()&&
          std::chrono::steady_clock::now()<physicalDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    require(worker.canBegin(),
            "M34 protected X=0 physical stamp drains after queued exact union");
    const auto crossMode=worker.status();
    if(crossMode.history.undoDepth!=2||crossMode.rejectedStamps!=0)
        throw std::runtime_error(
            "M34 protected X=0 post-exact physical stamp refused: "+
            crossMode.lastRejection);
    const auto crossSnapshot=worker.takeSnapshot();
    require(crossSnapshot&&crossSnapshot->index!=restored->index&&
            !crossSnapshot->dirtyPartitions.empty()&&
            crossMode.completedStamps==1&&crossMode.completedUnions==1&&
            crossMode.history.undoDepth==2,
            "M34 reflected fixed-point queued exact terminal supports next 4D source-authenticated physical root");
    const auto afterPhysical=crossSnapshot->index;
    worker.undo();
    const auto undoPhysicalDeadline=std::chrono::steady_clock::now()+
                                    std::chrono::seconds(20);
    while(worker.status().history.redoDepth!=1&&
          std::chrono::steady_clock::now()<undoPhysicalDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto crossUndone=worker.takeSnapshot();
    require(crossUndone&&crossUndone->index==restored->index,
            "M34 mixed protected X=0 physical undo restores exact mirrored union root");
    worker.redo();
    while(worker.status().history.redoDepth!=0&&
          std::chrono::steady_clock::now()<undoPhysicalDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto crossRedone=worker.takeSnapshot();
    require(crossRedone&&crossRedone->index==afterPhysical,
            "M34 mixed protected X=0 physical redo restores the terminal mirrored COW root");

    VersionedSculptBrush direct(trusted,.006,{},{},{},256,{},config);
    require(direct.mergeExactContacts(root,contacts,limits,
                                      imported.flatIndex.faceIds)&&
            direct.checkpoint().verifiedOriginalFidelity&&
            direct.checkpoint().verifiedOriginalFidelity->anchors.original==root&&
            direct.checkpoint().verifiedOriginalFidelity->lineage.terminal==
                direct.snapshot()&&
            direct.checkpoint().verifiedOriginalFidelity->sourceFaceEvidence==
                proof.sourceFaceEvidence,
            "M33 X=0 mirror singleton/paired source history carries exact matched immutable first root");
    const auto originalMirrorCount=direct.checkpoint().mirrorPairs.size();
    require(originalMirrorCount>mirrorPairs.size(),
            "M33 X=0 protected union records net added fixed-point and reciprocal orbits");

    // M22 independent final gate: after a REAL authenticated reflected
    // d2 terminal, a later physical Begin scope containing only one
    // original face of a mirrored orbit MUST be refused before CAS.
    // Neither the live fixed-point/reciprocal mirrors nor the sealed
    // first-original certificate may be mutated, evicted or rebased.
    const auto protectedTerminal=direct.checkpoint();
    const auto protectedHistory=direct.historyStatistics();
    const auto rejectionCount=direct.rejectedStamps();
    direct.setOriginalPatchIntent({imported.flatIndex.faceIds.front()});
    Mesh dummy;
    const std::array<BrushArea,1> patch{{{{1,0,0},.3,.3}}};
    direct.stamp(dummy,patch);
    const auto rejectedTerminal=direct.checkpoint();
    require(direct.rejectedStamps()==rejectionCount+1&&
            rejectedTerminal.index==protectedTerminal.index&&
            rejectedTerminal.verifiedOriginalFidelity==
                protectedTerminal.verifiedOriginalFidelity&&
            rejectedTerminal.verifiedOriginalFidelity->anchors.original==root&&
            rejectedTerminal.mirrorPairs==protectedTerminal.mirrorPairs&&
            direct.lastDirtyPartitions().empty()&&
            direct.historyStatistics().undoDepth==protectedHistory.undoDepth&&
            direct.historyStatistics().redoDepth==protectedHistory.redoDepth&&
            direct.lastRejection().find("original")!=std::string::npos,
            "M22 half-orbit physical request after certified X=0 exact union refuses atomically without mirror, source, root, dirty or history changes");

    require(direct.undo()&&direct.checkpoint().mirrorPairs.size()==
                mirrorPairs.size()&&
            direct.redo()&&direct.checkpoint().mirrorPairs.size()==
                originalMirrorCount,
            "M33 X=0 original singleton/reciprocal mirror journals survive protected undo/redo");
}

void testM33AuthenticatedQueuedOriginalExactUnion() {
    const auto mesh=twoIntersectingTetrahedra();
    const auto sources=firstOriginalFaces(mesh);
    const auto indexed=partitionFlatMesh(mesh,256);
    const auto original=RegionTopologyIndex::build(indexed.revision);
    const auto rootProof=beginOriginalPatchLifetime(
        captureOriginalSourceAnchors(original,indexed.flatIndex.faceIds,
                                     7*indexed.flatIndex.faceIds.size()));
    const auto contacts=fullOriginalContacts(*original);
    const auto limits=supportedUnionLimits(false);
    const auto privatelyPrepared=prepareExactCutStep(original,contacts,limits);
    require(privatelyPrepared.complete&&privatelyPrepared.final&&
            privatelyPrepared.sourceEvidence,
            "M33 asymmetric positive is a genuine fully source-certified c1/c2/c3/M03/d2 union");
    const auto checked=advanceOriginalPatchLifetime(
        rootProof,privatelyPrepared,nullptr,16384,16384);
    const auto audit=auditOriginalPatchLifetime(checked,original,131072,16384);
    require(audit.total.distanceTests>=7&&checked.sourceFaceEvidence>0,
            "M33 private expected work includes actual exact-cut source lineage and original witnesses");

    SculptCollisionSettings policy;
    policy.physicalExact=true;
    policy.originalTerminal.maximumProtectedDeviationInEdges=0;
    policy.originalTerminal.maxDistanceTests=audit.total.distanceTests;
    policy.originalTerminal.maxCumulativeFaceEvidence=
        checked.sourceFaceEvidence;
    // Guarantee the exact work limit, then exercise a one-under negative.
    auto limited=policy;
    limited.originalTerminal.maxDistanceTests=audit.total.distanceTests-1;
    AsyncSculptWorker denied(mesh,.025,.006,.2,{},{},{},false,{},limited);
    const auto refusedOld=denied.initialRevision();
    const auto refusedPicking=refusedOld->pickingRootIdentity();
    denied.mergeExactContacts(refusedOld,fullOriginalContacts(*refusedOld),
                             limits,sources);
    waitForUnion(denied,1);
    auto denial=denied.status();
    require(denial.rejectedUnions==1&&denial.completedUnions==0&&
            denial.history.undoDepth==0&&denial.publishedRevisions==0&&
            !denied.takeSnapshot()&&refusedOld->pickingRootIdentity()==refusedPicking,
            "M33 one-under COMPLETE original witness work fails before any terminal CAS");
    require(denial.lastRejection.find("original")!=std::string::npos,
            "M33 one-under original-witness rejection is diagnosable");

    // The independent M26 complete child/source-face verification is
    // cumulative: its exact demonstrated work succeeds above, but one
    // fewer unit must refuse BEFORE any publication or undo cursor edit.
    auto oneUnderSource=policy;
    oneUnderSource.originalTerminal.maxCumulativeFaceEvidence=
        checked.sourceFaceEvidence-1;
    AsyncSculptWorker evidenceLimited(
        mesh,.025,.006,.2,{},{},{},false,{},oneUnderSource);
    const auto evidenceOld=evidenceLimited.initialRevision();
    evidenceLimited.mergeExactContacts(
        evidenceOld,fullOriginalContacts(*evidenceOld),limits,sources);
    waitForUnion(evidenceLimited,1);
    require(evidenceLimited.status().rejectedUnions==1&&
            evidenceLimited.status().publishedRevisions==0&&
            evidenceLimited.status().history.undoDepth==0&&
            !evidenceLimited.takeSnapshot(),
            "M33 one-under complete M26 cut source-face evidence refuses before CAS");

    auto inputBound=policy;
    inputBound.originalIntentInput.maxDeclaredSources=sources.size()-1;
    AsyncSculptWorker tooManySources(
        mesh,.025,.006,.2,{},{},{},false,{},inputBound);
    const auto sourceCapRoot=tooManySources.initialRevision();
    tooManySources.mergeExactContacts(
        sourceCapRoot,fullOriginalContacts(*sourceCapRoot),limits,sources);
    waitForUnion(tooManySources,1);
    require(tooManySources.status().rejectedUnions==1&&
            tooManySources.status().history.undoDepth==0&&
            tooManySources.status().publishedRevisions==0,
            "M33 raw caller FaceId scope has a finite bound BEFORE retired-source filtering");

    // A plain V12 terminal-only checkpoint has no first-original
    // certificate. Selecting physicalExact MUST NOT let its explicit
    // queued union become a legacy untracked path.
    const auto raw=partitionFlatMesh(mesh,256);
    VersionedSculptBrush sourceLess(
        {RegionTopologyIndex::build(raw.revision),{},mesh.vertices.size()},
        .006,{},{},{},256,{},policy);
    const auto rawRoot=sourceLess.snapshot();
    require(!sourceLess.mergeExactContacts(
            rawRoot,fullOriginalContacts(*rawRoot),limits,
            raw.flatIndex.faceIds)&&
            sourceLess.snapshot()==rawRoot&&
            sourceLess.historyStatistics().undoDepth==0,
            "M33 opt-in physicalExact cannot bypass missing original proof on unprotected V12 recovery");

    AsyncSculptWorker worker(mesh,.025,.006,.2,{},{},{},false,{},policy);
    const auto before=worker.initialRevision();
    const auto picking=before->pickingRootIdentity();
    const auto complete=fullOriginalContacts(*before);
    worker.mergeExactContacts(before,complete,limits,{FaceId{9999999}});
    waitForUnion(worker,1);
    require(worker.status().rejectedUnions==1&&
            worker.status().publishedRevisions==0&&
            worker.status().history.undoDepth==0&&
            !worker.takeSnapshot(),
            "M33 forged original FaceId refuses privately before exact-cut CAS");
    worker.mergeExactContacts(before,complete,limits,sources);
    waitForUnion(worker,2);
    const auto acceptedStatus=worker.status();
    if(acceptedStatus.completedUnions!=1)
        throw std::runtime_error(
            "M33 real authenticated queued first-original union refused: "+
            acceptedStatus.lastRejection);
    auto accepted=worker.takeSnapshot();
    require(accepted&&accepted->index!=before&&
            acceptedStatus.rejectedUnions==1&&
            acceptedStatus.publishedRevisions==1&&
            acceptedStatus.history.undoDepth==1&&
            acceptedStatus.completedStamps==0&&
            !accepted->dirtyPartitions.empty()&&
            before->pickingRootIdentity()==picking,
            "M33 one queued exact final root, dirty partitions and source history after refusal");

    // A stale queued source root is NEVER permitted to masquerade as the
    // new predecessor, even if original contacts are geometrically identical.
    worker.mergeExactContacts(before,complete,limits,sources);
    waitForUnion(worker,3);
    require(worker.status().rejectedUnions==2&&
            worker.status().completedUnions==1&&
            worker.status().history.undoDepth==1&&
            !worker.takeSnapshot(),
            "M33 stale queued original root refused with no second snapshot or history");

    const auto file=std::filesystem::temp_directory_path()/
        "mesher-v17-m33-protected-explicit-source-exact.mshr";
    try {
        worker.saveCheckpoint(file);
        const auto deadline=std::chrono::steady_clock::now()+
            std::chrono::seconds(20);
        while(worker.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.checkpointStatistics().completed==1,
                "M33 protected exact final root writes one durable checkpoint");
        const auto loaded=readCheckpoint(file);
        require(loaded.originalFidelityRequired&&
                !loaded.verifiedOriginalFidelity&&loaded.index&&
                loaded.index->retiredFaceIds()==accepted->index->retiredFaceIds(),
                "M33 protected V13 disk checkpoint requires authentic original proof; never reanchors");
        bool refused=false;
        try {(void)VersionedSculptBrush(loaded,.006,{},{},{},
                                         256,{},policy);}
        catch(const std::logic_error&){refused=true;}
        require(refused,"M33 protected disk-recovered root refuses source-less physicalExact restart");
    }catch(...) {
        std::error_code ec;std::filesystem::remove(file,ec);throw;
    }
    std::error_code ec;std::filesystem::remove(file,ec);
    worker.undo();
    const auto undoDeadline=std::chrono::steady_clock::now()+
                             std::chrono::seconds(20);
    while(worker.status().history.redoDepth==0&&
          std::chrono::steady_clock::now()<undoDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    require(worker.status().history.undoDepth==0&&
            worker.takeSnapshot()->index==before,
            "M33 one undo restores actual FIRST-original source root");
    worker.redo();
    while(worker.status().history.redoDepth!=0&&
          std::chrono::steady_clock::now()<undoDeadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto restored=worker.takeSnapshot();
    require(restored&&restored->index==accepted->index&&
            worker.status().history.undoDepth==1,
            "M33 one redo restores exact authenticated queued terminal root");

    // Direct brush exposes the matching sealed history credential and
    // proves a refused budget preserves an existing redo branch.
    VersionedSculptBrush direct(mesh,.006,{},{},{},false,256,{},policy);
    const auto firstRoot=direct.snapshot();
    require(direct.mergeExactContacts(firstRoot,
            fullOriginalContacts(*firstRoot),limits,sources),
            "M33 direct worker-owned protected exact step publishes certified union");
    const auto after=direct.checkpoint();
    require(after.verifiedOriginalFidelity&&
            after.verifiedOriginalFidelity->anchors.original==firstRoot&&
            after.verifiedOriginalFidelity->lineage.terminal==after.index&&
            after.verifiedOriginalFidelity->acceptedTransitions==1&&
            after.verifiedOriginalFidelity->sourceFaceEvidence>0,
            "M33 one accepted queued exact terminal carries one authenticated first-original history successor");
    require(direct.undo()&&direct.snapshot()==firstRoot&&
            direct.checkpoint().verifiedOriginalFidelity->acceptedTransitions==0&&
            direct.historyStatistics().redoDepth==1,
            "M33 direct undo moves authenticated M29 source cursor to first root");
    auto oneUnder=limits;
    oneUnder.maxNetChangedIds=1;
    require(!direct.mergeExactContacts(firstRoot,
                fullOriginalContacts(*firstRoot),oneUnder,sources)&&
            direct.snapshot()==firstRoot&&
            direct.historyStatistics().redoDepth==1&&
            direct.checkpoint().verifiedOriginalFidelity->acceptedTransitions==0,
            "M33 full-transaction budget refusal preserves original root and redo branch");
    require(direct.redo()&&direct.snapshot()==after.index&&
            direct.checkpoint().verifiedOriginalFidelity==
                after.verifiedOriginalFidelity,
            "M33 redo after refused alternate branch reuses exact original-source certificate");
}


void testM34MixedProtectedPhysicalAndQueuedExactFIFO() {
    // Two genuinely crossing closed tetrahedra, plus an entirely separate
    // closed octahedron. First publish the explicitly intended exact union:
    // until it is joined, the original tetra contact is NOT a clean state
    // for the otherwise distant normal-driven physical stamp.
    auto mesh=twoIntersectingTetrahedra();
    const std::size_t offset=mesh.vertices.size();
    for(const auto p:std::array<Vec3,6>{{
        {1,0,0},{0,1,0},{-1,0},{0,-1,0},
        {0,0,1},{0,0,-1}
    }})mesh.vertices.push_back({p.x+21,p.y,p.z});
    for(const auto t:std::array<std::array<unsigned,3>,8>{{
        {4,0,1},{4,1,2},{4,2,3},{4,3,0},
        {5,1,0},{5,2,1},{5,3,2},{5,0,3}
    }})mesh.triangles.push_back({
        static_cast<unsigned>(offset)+t[0],
        static_cast<unsigned>(offset)+t[1],
        static_cast<unsigned>(offset)+t[2]});
    const auto imported=partitionFlatMesh(mesh,256);
    const auto firstRoot=RegionTopologyIndex::build(imported.revision);
    const auto originalSources=imported.flatIndex.faceIds;
    require(originalSources.size()==16,
            "M34 composite contains exactly sixteen independently owned original faces");
    const std::vector<FaceId> tetraSources(
        originalSources.begin(),originalSources.begin()+8);
    const std::set<FaceId> sculptSources(
        originalSources.begin()+8,originalSources.end());
    const auto contacts=fullOriginalContacts(*firstRoot);
    require(contacts.size()>=8,
            "M34 baseline tetrahedra have genuine complete independent original contacts");
    auto limits=supportedUnionLimits(false);
    // Compute a first-original protected tolerance against the SAME
    // M26-certified joined root as the real worker, never against an
    // unjoined, inherently colliding interim candidate.
    const auto firstOriginal=beginOriginalPatchLifetime(
        captureOriginalSourceAnchors(firstRoot,originalSources,112));
    const auto exact=prepareExactCutStep(firstRoot,contacts,limits);
    require(exact.complete&&exact.final&&exact.sourceEvidence,
            "M34 composite queued exact Step has complete c1/c2/c3/M03/d2 source evidence");
    const auto joinedLifetime=advanceOriginalPatchLifetime(
        firstOriginal,exact,nullptr,32768,32768);
    const std::array<BrushArea,1> patch{{{{22,0,0},.8,.3}}};
    VersionedInflateBrush inflator(.035,{});
    VersionedLocalRefiner refiner(mesh.vertices.size(),{},{},256);
    VersionedDirectionalCollapser collapser(mesh.vertices.size(),{},{},256);
    const auto inflated1=inflator.stamp(exact.final,patch,256);
    const auto refined1=refiner.apply(inflated1.index,inflated1.baseline);
    const auto collapsed1=collapser.apply(refined1.index,inflated1.baseline);
    const auto firstProof=advanceCertifiedPhysicalStampLifetime(
        joinedLifetime,inflated1,refined1,collapsed1,
        nullptr,nullptr,32768,32768,32768,32768);
    const auto inflated2=inflator.stamp(
        firstProof.lineage.terminal,patch,256);
    const auto refined2=refiner.apply(inflated2.index,inflated2.baseline);
    const auto collapsed2=collapser.apply(refined2.index,inflated2.baseline);
    const auto secondProof=advanceCertifiedPhysicalStampLifetime(
        firstProof,inflated2,refined2,collapsed2,
        nullptr,nullptr,32768,32768,32768,32768);
    const auto protectedDeviation=[&](const OriginalPatchLifetime& lifetime) {
        // Post-cut original FaceIds can each have multiple certified
        // living children. The full witness bound is 7 per CHILD, not
        // 7 per original triangle; never underbudget the reference audit.
        const auto audit=auditOriginalPatchLifetime(
            lifetime,firstRoot,131072,32768);
        double worst=0;
        for(const auto& record:audit.perOriginal)
            if(sculptSources.contains(record.original))
                worst=std::max(worst,record.sampled.maximumDistanceInEdges);
        return worst;
    };
    const auto firstDrift=protectedDeviation(firstProof);
    const auto secondDrift=protectedDeviation(secondProof);
    require(secondDrift>firstDrift+1e-6,
            "M34 real second octahedral stamp accumulates additional first-original drift");
    SculptCollisionSettings protectedMode;
    protectedMode.physicalExact=true;
    protectedMode.originalTerminal.maximumProtectedDeviationInEdges=
        .5*(firstDrift+secondDrift);
    AsyncSculptWorker worker(mesh,.025,.035,.2,{},{},{},
                             false,{},protectedMode);
    const auto original=worker.initialRevision();
    const auto picker=original->pickingRootIdentity();

    // Two commands queued together: a finite-work refusal must not
    // poison the FIFO successor's expected root or consume its intent.
    auto oneUnder=limits;
    oneUnder.maxNetChangedIds=1;
    worker.mergeExactContacts(original,fullOriginalContacts(*original),
                              oneUnder,tetraSources);
    worker.mergeExactContacts(original,fullOriginalContacts(*original),
                              limits,tetraSources);
    waitForUnion(worker,2);
    const auto joined=worker.status();
    if(joined.completedUnions!=1)
        throw std::runtime_error(
            "M34 complete original-root FIFO exact union refused: "+
            joined.lastRejection);
    const auto joinedSnapshot=worker.takeSnapshot();
    require(joined.rejectedUnions==1&&
            joined.publishedRevisions==1&&joined.history.undoDepth==1&&
            joinedSnapshot&&joinedSnapshot->index!=original&&
            !joinedSnapshot->dirtyPartitions.empty()&&
            original->pickingRootIdentity()==picker,
            "M34 denied exact command then valid queued cut has precisely one final renderer root");
    const auto welded=joinedSnapshot->index;
    require(welded->retiredFaceCount()>original->retiredFaceCount(),
            "M34 accepted c3/M03 source-owner retirement is real");

    const auto drain=[&]() {
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(25);
        while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(worker.canBegin(),"M34 physical 4D Begin/End drained");
        const auto status=worker.status();
        require(!status.active&&status.pendingSteps==0&&
                status.pendingSegments==0&&status.inputCommands==0,
                "M34 serialized 4D worker has no outstanding partial samples");
        return status;
    };
    // Continue to DECLARE the tetra sources intentionally edited by
    // the prior exact union; all eight octa faces remain unscoped and
    // protected by first-original edge-relative finite witness evidence.
    worker.begin({22,0,0},.8,tetraSources);
    worker.end({22,0,0},0);
    const auto first=drain();
    if(first.publishedRevisions!=2)
        throw std::runtime_error(
            "M34 allowed first unscoped octa 4D stamp refused: "+
            first.lastRejection);
    const auto moved=worker.takeSnapshot();
    require(moved&&moved->index!=welded&&
            !moved->dirtyPartitions.empty()&&
            first.history.undoDepth==2&&first.rejectedStamps==0&&
            welded->pickingRootIdentity()!=nullptr,
            "M34 permitted small first-original octa drift follows authentic queued exact predecessor");
    const auto allowed=moved->index;
    worker.begin({22,0,0},.8,tetraSources);
    worker.end({22,0,0},0);
    const auto refused=drain();
    const auto noChange=worker.takeSnapshot();
    require(refused.rejectedStamps==1&&refused.completedStamps==2&&
            refused.publishedRevisions==2&&refused.history.undoDepth==2&&
            noChange&&noChange->index==allowed&&
            noChange->dirtyPartitions.empty()&&
            !refused.lastRejection.empty(),
            "M34 second individually small but cumulative protected original drift refuses atomically");
    // A refused physicalExact stamp can also increment rejectedUnions.
    // Consequently an absolute union total is NOT a completion fence
    // after mixing physical and explicit queue operations: under MSVC
    // it could already equal three before this Merge even ran.
    // Require a NEW union outcome relative to the post-physical status.
    const auto beforeStale=worker.status();
    const auto previousUnionOutcomes=
        beforeStale.completedUnions+beforeStale.rejectedUnions;
    worker.mergeExactContacts(welded,fullOriginalContacts(*original),
                              limits,tetraSources);
    waitForUnion(worker,previousUnionOutcomes+1);
    const auto stale=worker.status();
    require(stale.rejectedUnions==beforeStale.rejectedUnions+1&&
            stale.completedUnions==beforeStale.completedUnions&&
            stale.history.undoDepth==2&&
            stale.publishedRevisions==beforeStale.publishedRevisions&&
            stale.lastRejection.find("stale")!=std::string::npos&&
            !worker.takeSnapshot(),
            "M34 actual completed stale expected-root Merge refuses atomically after physical stamp");

    // Later correctly scoped physical input must still drain against the
    // accepted post-union, post-first-stamp direct predecessor certificate.
    worker.begin({22,0,0},.8,originalSources);
    worker.end({22,0,0},0);
    const auto resumed=drain();
    if(resumed.publishedRevisions!=3)
        throw std::runtime_error(
            "M34 valid later scoped 4D FIFO recovery refused: "+
            resumed.lastRejection);
    const auto recovered=worker.takeSnapshot();
    if(!(recovered&&recovered->index!=allowed&&
         !recovered->dirtyPartitions.empty()&&
         resumed.history.undoDepth==3&&resumed.rejectedStamps==1&&
         resumed.completedUnions==1))
        throw std::runtime_error(
            "M34 subsequent 4D scope: snap="+std::to_string(bool(recovered))+
            " dirty="+std::to_string(recovered?recovered->dirtyPartitions.size():0)+
            " rootChanged="+std::to_string(recovered&&recovered->index!=allowed)+
            " liveChanged="+std::to_string(worker.initialRevision()!=allowed)+
            " snapMatchesLive="+std::to_string(recovered&&recovered->index==worker.initialRevision())+
            " snapStamps="+std::to_string(recovered?recovered->completedStamps:0)+
            " completedStamps="+std::to_string(resumed.completedStamps)+
            " undo="+std::to_string(resumed.history.undoDepth)+
            " stampsRefused="+std::to_string(resumed.rejectedStamps)+
            " unionsCompleted="+std::to_string(resumed.completedUnions)+
            " lastRefusal="+resumed.lastRejection);
    const auto final=recovered->index;

    const auto file=std::filesystem::temp_directory_path()/
        "mesher-v17-m34-mixed-original-fifo.mshr";
    try {
        worker.saveCheckpoint(file);
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(20);
        while(worker.checkpointStatistics().completed==0&&
              std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.checkpointStatistics().completed==1,
                "M34 mixed physical/union terminal durable checkpoint completed");
        const auto loaded=readCheckpoint(file);
        require(loaded.index&&loaded.originalFidelityRequired&&
                !loaded.verifiedOriginalFidelity&&
                loaded.index->retiredFaceIds()==final->retiredFaceIds(),
                "M34 checkpoint contains only protected final root and no forged first-source proof");
        bool missingProofRefused=false;
        try {(void)VersionedSculptBrush(loaded,.035,{},{},{},
                                         256,{},protectedMode);}
        catch(const std::logic_error&){missingProofRefused=true;}
        require(missingProofRefused,
                "M34 disk-only V13 terminal refuses original-proof-less restart");
    }catch(...) {
        std::error_code ec;std::filesystem::remove(file,ec);throw;
    }
    std::error_code ec;std::filesystem::remove(file,ec);

    const std::array<std::shared_ptr<const RegionTopologyIndex>,4>
        roots{{original,welded,allowed,final}};
    // M43 acceptance exposed a pre-existing M34 synchronization race:
    // worker refreshStatus() runs BEFORE publish() installs the immutable
    // UI snapshot. Seeing the new undoDepth does NOT fence the snapshot
    // mailbox under MSVC (or Linux). Wait for the ACTUAL expected root
    // AND history depths instead; do not weaken exact-ID undo/redo proof.
    const auto waitForHistorySnapshot=[&](
        std::size_t undoDepth,std::size_t redoDepth,
        const std::shared_ptr<const RegionTopologyIndex>& target) {
        std::optional<AsyncSculptWorker::Snapshot> found;
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(20);
        while(std::chrono::steady_clock::now()<deadline) {
            const auto progress=worker.status();
            if(progress.history.undoDepth==undoDepth&&
               progress.history.redoDepth==redoDepth) {
                auto candidate=worker.takeSnapshot();
                if(candidate&&candidate->index==target) {
                    found=std::move(candidate);
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return found;
    };
    for(std::size_t n=3;n>0;--n) {
        worker.undo();
        const auto snapshot=waitForHistorySnapshot(
            n-1,4-n,roots[n-1]);
        require(snapshot&&snapshot->index==roots[n-1]&&
                worker.status().history.undoDepth==n-1&&
                worker.status().history.redoDepth==4-n,
                "M34 each mixed source-exact/physical COW root is individually undoable");
    }
    for(std::size_t n=1;n<=3;++n) {
        worker.redo();
        const auto snapshot=waitForHistorySnapshot(
            n,3-n,roots[n]);
        require(snapshot&&snapshot->index==roots[n]&&
                worker.status().history.undoDepth==n&&
                worker.status().history.redoDepth==3-n,
                "M34 redo across exact and physical entries restores exact immutable history roots");
    }
}

void testM31ExplicitFirstOriginalIntentTransport() {
    // Input scope is the CALLER's permanent original FaceId set, captured
    // before the physical candidate exists; it is not proximity-discovered.
    Mesh octa={{{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
                {0,0,1},{0,0,-1}},
               {{4,0,1},{4,1,2},{4,2,3},{4,3,0},
                {5,1,0},{5,2,1},{5,3,2},{5,0,3}}};
    const auto ids=partitionFlatMesh(octa,256).flatIndex.faceIds;
    SculptCollisionSettings selected;
    selected.physicalExact=true;
    VersionedSculptBrush brush(octa,.035,{},{},{},false,256,{},selected);
    const auto first=brush.snapshot();
    const auto oldPicking=first->pickingRootIdentity();
    require(brush.firstOriginalSourceRoot()==first,
            "M31 fresh physical-exact root seals genuine first-original source authority");
    Mesh dummy;
    const std::array<BrushArea,1> area{{{{1,0,0},.8,.9}}};
    brush.setOriginalPatchIntent({FaceId{99999999}});
    brush.stamp(dummy,area);
    require(brush.snapshot()==first&&brush.rejectedStamps()==1&&
            brush.publishedRevisions()==0&&
            brush.historyStatistics().undoDepth==0&&
            !brush.lastRejection().empty(),
            "M31 foreign intended original refuses without modifying first root, picker or history");
    brush.setOriginalPatchIntent({ids.front()});
    require(brush.originalPatchIntent()==std::vector<FaceId>{ids.front()},
            "M31 caller's exact FaceId selection remains intact");
    brush.setOriginalPatchIntent(ids);
    brush.stamp(dummy,area);
    require(brush.completedStamps()==2&&brush.rejectedStamps()==1&&
            brush.publishedRevisions()==1&&brush.snapshot()!=first&&
            brush.firstOriginalSourceRoot()==first&&
            first->pickingRootIdentity()==oldPicking,
            "M31 valid new physical scope drains after refused input without rebasing original root");

    SphereOptions options;
    options.vertexCount=42;
    options.relaxationIterations=3;
    const auto sphere=generateSphere(options);
    const auto faces=partitionFlatMesh(sphere,256).flatIndex.faceIds;
    VersionedSculptBrush reflected(sphere,.006,{},{},{},true,256,{},selected);
    const auto symmetricRoot=reflected.snapshot();
    const std::array<BrushArea,2> bilateral{{
        {{.25,.65,.70},.3,.3},{{-.25,.65,.70},.3,.3}}};
    reflected.setOriginalPatchIntent({faces.front()});
    reflected.stamp(dummy,bilateral);
    require(reflected.rejectedStamps()==1&&
            reflected.snapshot()==symmetricRoot&&
            reflected.checkpoint().mirrorPairs.size()>0,
            "M31 original reflection refuses a half-face source orbit without corrupting mirrors");
    reflected.setOriginalPatchIntent(faces);
    reflected.stamp(dummy,bilateral);
    require(reflected.rejectedStamps()==1&&
            reflected.publishedRevisions()==1&&
            reflected.firstOriginalSourceRoot()==symmetricRoot,
            "M31 complete caller-selected original mirror source orbits are permitted");

    // The 4D FIFO transports each Begin scope separately; later requests
    // cannot accidentally inherit the previous stroke's bad source input.
    AsyncSculptWorker worker(octa,.025,.035,.2,{},{},{},false,{},selected);
    const auto before=worker.initialRevision();
    const auto wait=[&]() {
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(15);
        while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        require(worker.canBegin(),"M31 queued original-source Begin/End drained");
    };
    worker.begin({1,0,0},.8,{FaceId{99999999}});
    worker.end({1,0,0},0);
    wait();
    require(worker.status().rejectedStamps==1&&
            worker.status().publishedRevisions==0&&
            worker.initialRevision()==before,
            "M31 invalid queued original scope refuses without a geometric Step");
    worker.begin({1,0,0},.8,ids);
    worker.end({1,0,0},0);
    wait();
    require(worker.status().rejectedStamps==1&&
            worker.status().publishedRevisions==1&&
            worker.status().pendingSteps==0&&worker.status().pendingSegments==0,
            "M31 subsequent genuinely valid queued physical request drains");

    // Explicit source-exact union has a SEPARATE captured original scope
    // on its own queued command, not a borrow from the prior stroke.
    AsyncSculptWorker merger(twoIntersectingTetrahedra(),.025,.006,.2,
                             {},{},{},false,{},selected);
    const auto initial=merger.initialRevision();
    const auto contacts=fullOriginalContacts(*initial);
    const auto limits=supportedUnionLimits(false);
    merger.mergeExactContacts(initial,contacts,limits,{FaceId{99999999}});
    waitForUnion(merger,1);
    require(merger.status().rejectedUnions==1&&
            merger.status().publishedRevisions==0&&
            merger.status().history.undoDepth==0,
            "M31 foreign explicit-union source scope refuses before cut or CAS");
    merger.mergeExactContacts(initial,contacts,limits,
                              firstOriginalFaces(twoIntersectingTetrahedra()));
    waitForUnion(merger,2);
    if(merger.status().completedUnions!=1)
        throw std::runtime_error(
            "M33 supported caller-scoped queued original union refused: "+
            merger.status().lastRejection);
    require(merger.status().rejectedUnions==1&&
            merger.status().history.undoDepth==1&&
            merger.status().publishedRevisions==1,
            "M33 valid queued source-exact union succeeds after invalid original request");
}
void testM50DesktopAsymmetricFixtureAndProtectedWorker() {
    const Mesh source=desktopMergeAsymmetricTetraFixture();
    require(source.vertices.size()==8&&source.triangles.size()==8,
            "M50 live asymmetric test source is two four-face tetrahedra");
    require(source.vertices[0].x==0&&source.vertices[7].x==3&&
            source.triangles[0].a==0&&source.triangles[7].a==3,
            "M50 desktop source matches accepted M47 two-tetra geometry");
    SculptCollisionSettings policy;
    policy.physicalExact=true;
    policy.originalTerminal.maximumProtectedDeviationInEdges=0;
    AsyncSculptWorker worker(source,.025,.006,.2,
                             {},{},{},false,{},policy);
    const auto first=worker.initialRevision();
    const auto oldPicking=first->pickingRootIdentity();
    require(worker.status().liveMergeRequests==0&&
            worker.status().publishedRevisions==0&&
            worker.status().history.undoDepth==0&&
            !worker.takeSnapshot(),
            "M50 fixture construction never secretly selects or joins sheets");
    worker.mergeDiscoveredContacts(first,firstOriginalFaces(source),
                                   desktopMergeExactLimits(false));
    // M36/#103: history status can precede the actual renderer mailbox.
    // Require both exact terminal immutable frame AND matching history;
    // do not sample the mailbox once after only status has advanced.
    const auto end=std::chrono::steady_clock::now()+
                   std::chrono::seconds(30);
    std::shared_ptr<const RegionTopologyIndex> observedFinal;
    bool observedDirty=false;
    while(std::chrono::steady_clock::now()<end) {
        const auto frame=worker.takeSnapshot();
        if(frame&&frame->index&&frame->index!=first&&
           frame->index==worker.initialRevision()) {
            observedFinal=frame->index;
            observedDirty=!frame->dirtyPartitions.empty();
        }
        const auto progress=worker.status();
        if(observedFinal&&observedDirty&&
           observedFinal==worker.initialRevision()&&
           progress.liveMergeRequests==1&&
           progress.pendingLiveMergeRequests==0&&
           progress.liveMergeAccepted==1&&
           progress.history.undoDepth==1)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto status=worker.status();
    require(status.liveMergeRequests==1&&status.liveMergeAccepted==1&&
            status.liveMergeRefusals==0&&status.history.undoDepth==1&&
            status.completedUnions==1&&status.publishedRevisions==1&&
            observedFinal&&observedFinal==worker.initialRevision()&&
            observedDirty&&first->pickingRootIdentity()==oldPicking,
            "M50 shared desktop source succeeds only on explicit worker Merge with exact renderer frame");
}
void testM51DeliberateFixtureNominationAndReflectedWorker() {
    const auto mirrorMesh=desktopMergeReflectedTetraFixture();
    require(mirrorMesh.vertices.size()==16&&mirrorMesh.triangles.size()==16,
            "M51 reflected fixture has 16 independently owned original faces and vertices");
    for(std::size_t i=0;i<8;++i) {
        const auto a=mirrorMesh.vertices[i],b=mirrorMesh.vertices[i+8];
        require(a.x==-b.x&&a.y==b.y&&a.z==b.z,
                "M51 mirrored source coordinates have separate exact reciprocal vertices");
        const auto f=mirrorMesh.triangles[i],g=mirrorMesh.triangles[i+8];
        require(g.a==f.a+8&&g.b==f.c+8&&g.c==f.b+8,
                "M51 mirrored face winding is reversed under physical reflection");
    }
    auto source=desktopMergeReflectedFirstOriginal();
    const auto originalFaces=source.firstFaces;
    const auto originalRoot=source.checkpoint.index;
    require(source.checkpoint.originalFidelityRequired&&
            source.checkpoint.verifiedOriginalFidelity&&
            source.checkpoint.verifiedFirstOriginalMirrorPairs.size()==8&&
            originalFaces.size()==16,
            "M51 reflected fixture carries a first-original sealed history and eight actual stable mirror orbits");
    SculptCollisionSettings protectedPolicy;
    protectedPolicy.physicalExact=true;
    protectedPolicy.originalTerminal.maximumProtectedDeviationInEdges=0;
    AsyncSculptWorker worker(std::move(source.checkpoint),.025,.006,.2,
                             {},{},{},{},protectedPolicy,originalFaces);
    const auto before=worker.initialRevision();
    require(before==originalRoot&&
            worker.status().liveMergeRequests==0&&
            worker.status().history.undoDepth==0,
            "M51 explicitly authorized source still starts WITHOUT automatic surgery");
    DesktopMergeInput ui;
    DesktopMergeKeyEdges keys;
    require(ui.nominateFixtureOriginals(before,before,originalFaces,true)==
                DesktopMergeInput::Result::Ignored&&
            !ui.enabled()&&ui.selectedCount()==0,
            "M51 A key is inert while Merge mode remains default OFF");
    auto first=keys.sample(false,false,false,true);
    auto held=keys.sample(false,false,false,true);
    keys.sample(false,false,false,false);
    auto repeated=keys.sample(false,false,false,true);
    require(first.nominate&&!held.nominate&&repeated.nominate&&
            !first.toggle&&!first.submit&&!first.clear,
            "M51 actual GLFW A-key signal uses a rising edge only");
    require(ui.toggle(true,true,false)==DesktopMergeInput::Result::ModeOn,
            "M51 M-mode remains required before the deliberate A selection");
    require(ui.nominateFixtureOriginals(before,{},originalFaces,true)==
                DesktopMergeInput::Result::Stale&&
            ui.nominateFixtureOriginals(before,before,{},true)==
                DesktopMergeInput::Result::NoSelection&&
            ui.nominateFixtureOriginals(before,before,originalFaces,false)==
                DesktopMergeInput::Result::Busy&&
            ui.selectedCount()==0,
            "M51 invalid authority, empty candidate and busy action cannot nominate");
    auto duplicated=originalFaces;
    duplicated.push_back(originalFaces.front());
    require(ui.nominateFixtureOriginals(before,before,duplicated,true)==
                DesktopMergeInput::Result::InvalidFace&&
            ui.selectedCount()==0,
            "M51 duplicated fixture authorization refuses as a whole");
    auto forged=originalFaces;
    forged.back()=FaceId{99999999};
    require(ui.nominateFixtureOriginals(before,before,forged,true)==
                DesktopMergeInput::Result::InvalidFace&&
            ui.selectedCount()==0,
            "M51 a foreign source ID cannot be coerced into an apparent root member");
    auto tooMany=originalFaces;
    for(std::size_t i=0;i<17;++i)
        tooMany.push_back(FaceId{static_cast<std::uint64_t>(100+i)});
    require(ui.nominateFixtureOriginals(before,before,tooMany,true)==
                DesktopMergeInput::Result::Full&&
            ui.selectedCount()==0,
            "M51 a 33-face nomination refuses before modifying any selected original");
    require(ui.pick(before,originalFaces.front(),true)==
                DesktopMergeInput::Result::Selected&&
            ui.nominateFixtureOriginals(before,before,originalFaces,true)==
                DesktopMergeInput::Result::Selected&&
            ui.selectedCount()==16&&
            ui.nominateFixtureOriginals(before,before,originalFaces,true)==
                DesktopMergeInput::Result::Duplicate&&
            ui.selectedCount()==16,
            "M51 explicit full-source key combines with already clicked face without duplicate IDs");
    auto submitted=ui.submit(before,true,true);
    require(submitted.result==DesktopMergeInput::Result::Queued&&
            submitted.expected==before&&
            submitted.originals.size()==originalFaces.size()&&
            ui.selectedCount()==0,
            "M51 Enter submits all and only the deliberate first-original IDs with same immutable root");
    worker.mergeDiscoveredContacts(std::move(submitted.expected),
                                   std::move(submitted.originals),
                                   desktopMergeExactLimits(true));
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
    std::shared_ptr<const RegionTopologyIndex> merged;
    bool dirty=false;
    while(std::chrono::steady_clock::now()<deadline) {
        if(auto snapshot=worker.takeSnapshot()) {
            if(snapshot->index&&snapshot->index!=before) {
                merged=snapshot->index;
                dirty=!snapshot->dirtyPartitions.empty();
            }
        }
        const auto status=worker.status();
        if(merged&&dirty&&merged==worker.initialRevision()&&
           status.liveMergeAccepted==1&&status.history.undoDepth==1&&
           status.pendingLiveMergeRequests==0)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto success=worker.status();
    require(success.liveMergeRequests==1&&success.liveMergeAccepted==1&&
            success.liveMergeRefusals==0&&success.history.undoDepth==1&&
            success.publishedRevisions==1&&
            success.resources.published==1&&dirty&&merged&&
            merged==worker.initialRevision()&&merged!=before,
            "M51 reflected A/Enter through actual controller performs one authenticated Boolean and terminal dirty frame");
    require(ui.nominateFixtureOriginals(merged,before,originalFaces,true)==
                DesktopMergeInput::Result::Stale&&
            ui.selectedCount()==0,
            "M51 an old fixture A-key authority cannot rebind to a subsequent rendered COW root");
    const auto awaitFrame=[&](const std::shared_ptr<const RegionTopologyIndex>& root,
                              std::size_t undo,std::size_t redo) {
        const auto until=std::chrono::steady_clock::now()+
                         std::chrono::seconds(20);
        while(std::chrono::steady_clock::now()<until) {
            const auto snapshot=worker.takeSnapshot();
            const auto status=worker.status();
            if(snapshot&&snapshot->index==root&&
               worker.initialRevision()==root&&
               status.history.undoDepth==undo&&status.history.redoDepth==redo)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return false;
    };
    worker.undo();
    require(awaitFrame(before,0,1),
            "M51 reflected one-step undo restores exact original indexed rendered root");
    worker.redo();
    require(awaitFrame(merged,1,0),
            "M51 reflected one-step redo restores exactly the same accepted indexed root");

    // A separate supported asymmetric fixture uses precisely the same
    // explicit input controller, no symmetry-only surrogate.
    const auto asym=desktopMergeAsymmetricTetraFixture();
    SculptCollisionSettings policy;
    policy.physicalExact=true;
    policy.originalTerminal.maximumProtectedDeviationInEdges=0;
    AsyncSculptWorker asymmetric(asym,.025,.006,.2,
                                  {},{},{},false,{},policy);
    const auto asymRoot=asymmetric.initialRevision();
    const auto asymFaces=firstOriginalFaces(asym);
    DesktopMergeInput asymUi;
    require(asymUi.toggle(true,true,false)==DesktopMergeInput::Result::ModeOn&&
            asymUi.nominateFixtureOriginals(asymRoot,asymRoot,asymFaces,true)==
                DesktopMergeInput::Result::Selected&&
            asymUi.selectedCount()==8,
            "M51 explicit A nominates eight genuine asymmetric FIRST-original faces");
    const auto asymSubmission=asymUi.submit(asymRoot,true,true);
    require(asymSubmission.result==DesktopMergeInput::Result::Queued&&
            asymSubmission.originals==asymFaces,
            "M51 asymmetric A/Enter passes unaltered original source IDs");
    asymmetric.mergeDiscoveredContacts(asymSubmission.expected,
                                        asymSubmission.originals,
                                        desktopMergeExactLimits(false));
    const auto asymEnd=std::chrono::steady_clock::now()+
                       std::chrono::seconds(30);
    std::shared_ptr<const RegionTopologyIndex> asymFinal;
    while(std::chrono::steady_clock::now()<asymEnd) {
        if(auto frame=asymmetric.takeSnapshot())
            if(frame->index&&frame->index!=asymRoot&&
               !frame->dirtyPartitions.empty())asymFinal=frame->index;
        const auto s=asymmetric.status();
        if(asymFinal&&asymFinal==asymmetric.initialRevision()&&
           s.liveMergeAccepted==1&&s.history.undoDepth==1)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(asymFinal&&asymFinal==asymmetric.initialRevision()&&
            asymmetric.status().liveMergeAccepted==1,
            "M51 asymmetric named fixture A/Enter produces real source-positive merged dirty root");

    DesktopMergeInput refusalUi;
    require(refusalUi.toggle(true,true,false)==DesktopMergeInput::Result::ModeOn,
            "M51 refusal setup permits only deliberate ON selection");
    require(refusalUi.pick(asymRoot,asymFaces.front(),true)==
                DesktopMergeInput::Result::Selected,
            "M51 refusal tests establish a single exact initial-root pick");
    auto foreignRoot=std::shared_ptr<const RegionTopologyIndex>{};
    require(refusalUi.nominateFixtureOriginals(asymRoot,foreignRoot,asymFaces,true)==
                DesktopMergeInput::Result::Stale&&
            refusalUi.selectedCount()==1,
            "M51 invalid batch does not discard a valid independently picked face");
    require(refusalUi.clear()==DesktopMergeInput::Result::Cleared&&
            refusalUi.toggle(true,true,false)==DesktopMergeInput::Result::ModeOff&&
            refusalUi.nominateFixtureOriginals(asymRoot,asymRoot,asymFaces,true)==
                DesktopMergeInput::Result::Ignored,
            "M51 Backspace/M-OFF clear selection and explicitly disable hidden nomination");
    std::cout<<"M51 complete explicit asymmetric and reflected original-source selection, "
                "refusal, CAS, exact render root, undo/redo\n";
}
}
int main() {
    try {
        testThreadedMatchesExactSculpt();
        testWorkerCanRestartAndCancel();
        testOneShotStampIsExactlyOneVisibleWorkerRevision();
        testCoalescedRootsAndCompatibilityBridge();
        testViewportAdapterAfterStableIdSplit();
        testWorkerHistoryAndRecoveredCheckpoint();
        testReversibleMirrorDeltaJournals();
        testRejectedSymmetricStepRollsBackMirrorsAndHistory();
        testWorkerRejectsOneStepAndDrainsRemainingInput();
        testOptInCollisionAssessmentOnCleanAsyncStrokes();
        testVersionedWorkerWithRemeshingEnabled();
        testQueuedExactUnionOneStepHistoryAndRefusal();
        testWorkerMultipleCompleteSectionsOneHistoryStep();
        testWorkerMirroredUnionMirrorHistoryAndCheckpoint();
        testWorkerMirroredThreeBodyOneHistoryStep();
        testWorkerInvariantX0OneOriginalRootHistoryAndCheckpoint();
        testM28FullPrivatePhysicalStampLifetime();
        testM31ExplicitFirstOriginalIntentTransport();
        testM32ActivePhysicalOriginalTerminalPolicy();
        testM33AuthenticatedQueuedOriginalExactUnion();
        testM33ProtectedX0FixedPointQueuedUnion();
        testM34MixedProtectedPhysicalAndQueuedExactFIFO();
        testFirstPhysicalExactBooleanOneOriginalRootStep();
        testM23ConclusiveEpisodesAndReal4DFIFO();
        testM35RepeatedPhysicalUnionsInOneOverlappingPatch();
        testM37ScopedSamePatchUnionsWithProtectedCavity();
        testM38CompletePostD2WorkAndCumulativeProtectedDrift();
        testM39ProtectedOverlappingPatchFourDContactEpisodesAndFIFO();
        testM36OriginalCombinedProtectedSamePatchPolicy();
        testM12OriginalFinalFirstReferenceAcceptance();
        testM40AggregateWholePrivateStepResourceBudgets();
        testM41ExactWholeStepQueryAccounting();
        testM43WholeStepContourClosureAccounting();
        testM44OriginalSeamTraceJoinWork();
        testM45FirstOriginalFeatureHistoryWork();
        testM46CumulativeSourceWorkerDiagnostics();
        testOriginalM42IntegratedBoundedSourceAndWorkerAcceptance();
        testOriginalM13WholeStepResourceAndTelemetryReconciliation();
        testM14PostCasFaultQuarantineAndCheckpointIsolation();
        testM47BoundedAuthenticatedLiveMergeDiscovery();
        testM49DesktopModeSelectionAndBoundedWorkerIntegration();
        testM50DesktopAsymmetricFixtureAndProtectedWorker();
        testM51DeliberateFixtureNominationAndReflectedWorker();
        testM11SixteenAlternatingPhysicalTowerUnions();
        testM11QueuedPhysicalTowersRefusalCleanAndRecovery();
        testM11OversizedPhysicalContactRefusesThenValidDrains();
        testExactPhysicalRemeshIsOneSpeculativeTransaction();
        testPhysicallyCreatedContactCanPassExactSourcePlanner();
        testWorkerUnionRefusalsDoNotBlockLaterInput();
        std::cout<<"Asynchronous sculpt worker tests passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Async worker test failed: "<<e.what()<<'\n';
        return 1;
    }
}

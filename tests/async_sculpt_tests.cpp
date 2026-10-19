#include "mesher/async_sculpt.hpp"
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
    Mesh mesh;
    const std::array<Vec3,8> coords{{
        {0,0,0},{2,0,0},{0,2,0},
        {.75,-.5,-1},{.75,1.75,1},{.75,.25,-1},
        {.5,.5,-3},{3,.5,0}
    }};
    const std::array<std::array<std::uint32_t,3>,8> faces{{
        {0,1,2},{1,0,6},{2,1,6},{0,2,6},
        {3,4,5},{4,3,7},{5,4,7},{3,5,7}
    }};
    for(const auto p:coords)
        mesh.vertices.push_back(reflected?Vec3{p.x+5,p.y,p.z}:p);
    if(reflected)for(const auto p:coords)
        mesh.vertices.push_back({-p.x-5,p.y,p.z});
    for(const auto face:faces)
        mesh.triangles.push_back({face[0],face[1],face[2]});
    if(reflected)for(const auto face:faces)
        mesh.triangles.push_back({face[0]+8,face[2]+8,face[1]+8});
    return mesh;
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
            !rejected.lastRejection.empty(),
            "Oversize contact query rolls back ALL physically moved sheets without a partial published union");
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
            old->retiredFaceCount()==0&&result->index->retiredFaceCount()>0,
            "Queued worker consumes refused large contact then correct physical symmetric union once");
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
    for(std::size_t n=16;n>0;--n) {
        worker.undo();
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
        while(worker.status().history.redoDepth!=17-n&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto snapshot=worker.takeSnapshot();
        require(snapshot&&snapshot->index==roots[n-1]&&
                worker.status().history.undoDepth==n-1,
                "M11 each real FIFO physical union is independently undoable");
    }
    for(std::size_t n=1;n<=16;++n) {
        worker.redo();
        const auto stop=std::chrono::steady_clock::now()+
                        std::chrono::seconds(30);
        while(worker.status().history.undoDepth!=n&&
              std::chrono::steady_clock::now()<stop)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto snapshot=worker.takeSnapshot();
        require(snapshot&&snapshot->index==roots[n],
                "M11 4D FIFO redo restores exact immutable terminal root identity");
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
    const auto joined=worker.takeSnapshot();
    require(joined&&joined->index&&joined->index!=original&&
            worker.status().history.undoDepth==1,
            "Mirrored union publishes one immutable root and one history transition");
    worker.undo();
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(15);
    while(worker.status().history.redoDepth==0&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto undone=worker.takeSnapshot();
    require(undone&&undone->index==original,
            "Mirrored worker undo restores original root and reciprocal old mirrors");
    worker.redo();
    while(worker.status().history.redoDepth!=0&&
          std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto redone=worker.takeSnapshot();
    require(redone&&redone->index==joined->index,
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
    for(std::size_t n=3;n>0;--n) {
        worker.undo();
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(20);
        while(worker.status().history.undoDepth!=n-1&&
              std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto snapshot=worker.takeSnapshot();
        require(snapshot&&snapshot->index==roots[n-1]&&
                worker.status().history.redoDepth==4-n,
                "M34 each mixed source-exact/physical COW root is individually undoable");
    }
    for(std::size_t n=1;n<=3;++n) {
        worker.redo();
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(20);
        while(worker.status().history.undoDepth!=n&&
              std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const auto snapshot=worker.takeSnapshot();
        require(snapshot&&snapshot->index==roots[n]&&
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
}
int main() {
    try {
        testThreadedMatchesExactSculpt();
        testWorkerCanRestartAndCancel();
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

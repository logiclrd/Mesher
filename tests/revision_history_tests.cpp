#include "mesher/revision_history.hpp"
#include "mesher/repair_surgery.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/versioned_refine.hpp"
#include "mesher/versioned_collapse.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace mesher;
namespace {
// No RNG is used by this suite. Its varying temporary directory name
// only avoids collisions. Thread scheduling and filesystem I/O do vary.
// Flushed progress stays visible with CTest --verbose during a hang.
void phase(std::string_view name) {
    std::cerr<<"[persistent_history_checkpoints] "<<name<<std::endl;
}
void check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
Mesh octahedron() {
    return {{{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
             {0,0,1},{0,0,-1}},
            {{4,0,1},{4,1,2},{4,2,3},{4,3,0},
             {5,1,0},{5,2,1},{5,3,2},{5,0,3}}};
}
std::shared_ptr<const RegionTopologyIndex> moved(
    std::shared_ptr<const RegionTopologyIndex> previous,VertexId id,double scale) {
    RegionEdit edit(previous,{id},{},1);
    edit.moveVertex(id,previous->vertex(id).position*scale);
    (void)edit.commit();
    return edit.updatedIndex();
}
void testBoundedHistoryAndBranching() {
    auto initial=partitionFlatMesh(octahedron(),1);
    auto before=RegionTopologyIndex::build(initial.revision);
    RevisionRetirementQueue retired(3);
    RevisionHistory history(before,4,256,&retired);
    const auto target=initial.flatIndex.vertexIds.front();
    std::vector<std::shared_ptr<const RegionTopologyIndex>> readers{before};
    for(int i=0;i<12;++i) {
        std::cerr<<"[persistent_history_checkpoints] retired root append "
                 <<i<<"/12"<<std::endl;
        auto prior=history.current();
        auto next=moved(prior,target,1.0+0.001);
        history.append(next,{0,1},{},{},128);
        if(i==4)readers.push_back(next);
        const auto stats=history.statistics();
        check(stats.retainedRevisions<=4&&stats.retainedChangeBytes<=256,
              "Root count and cloned-payload estimate remain bounded");
    }
    auto stats=history.statistics();
    check(stats.droppedRevisions>0&&stats.undoDepth>=1,
          "History evicts oldest roots under an explicit budget");
    const auto latest=history.current();
    const auto backward=history.undo();
    check(backward.index!=latest&&history.canRedo(),
          "Undo restores an earlier complete immutable revision");
    const auto forward=history.redo();
    check(forward.index==latest&&!history.canRedo(),
          "Redo restores the original latest root");
    (void)history.undo();
    auto fork=moved(history.current(),target,1.02);
    history.append(fork,{1},{},{},64);
    check(!history.canRedo()&&history.current()==fork,
          "New edit after undo drops only the abandoned redo branch");
    check(history.statistics().discardedRedoRevisions>0,
          "History records discarded redo roots");
    phase("retirement queue drain BEGIN");
    retired.drain();
    phase("retirement queue drain END");
    for(const auto& held:readers) {
        check(bool(held->firstRayHit(viewportRay(640,360,1280,720))),
              "External snapshots survive bounded history eviction");
    }
}
void testUndoMirrorDelta() {
    auto initial=partitionFlatMesh(octahedron(),1);
    auto before=RegionTopologyIndex::build(initial.revision);
    RevisionHistory history(before,5,1024);
    auto changed=moved(before,initial.flatIndex.vertexIds[0],1.01);
    const auto inserted=std::pair{initial.flatIndex.vertexIds[0],
                                  initial.flatIndex.vertexIds[2]};
    const auto removed=std::pair{initial.flatIndex.vertexIds[1],
                                 initial.flatIndex.vertexIds[1]};
    history.append(changed,{0,4,4},{inserted},{removed},64);
    auto undo=history.undo();
    check(undo.index==before&&undo.dirtyPartitions.size()==2,
          "Undo includes the complete deduplicated dirty partition delta");
    const MirrorPair canonical{std::min(inserted.first,inserted.second),
                               std::max(inserted.first,inserted.second)};
    check(undo.removeMirrors.size()==1&&undo.removeMirrors.front()==canonical&&
          undo.addMirrors.size()==1&&undo.addMirrors.front()==removed,
          "Undo reverses added/deleted symmetry correspondence");
    auto redo=history.redo();
    check(redo.index==changed&&redo.addMirrors.front()==canonical&&
          redo.removeMirrors.front()==removed,
          "Redo reapplies forward symmetry delta");
}
void testCheckpointRoundTripAndRecovery() {
    const auto dir=std::filesystem::temp_directory_path()/
        ("mesher-v12-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    phase("checkpoint roundtrip temporary directory setup");
    std::filesystem::create_directories(dir);
    const auto file=dir/"model.mshr";
    try {
        auto imported=partitionFlatMesh(octahedron(),1);
        auto before=RegionTopologyIndex::build(imported.revision);
        const auto a=imported.flatIndex.vertexIds[0];
        const auto b=imported.flatIndex.vertexIds[1];
        RegionEdit shortEdge(before,{b},{},1);
        shortEdge.moveVertex(b,{.6,.4,0});
        (void)shortEdge.commit();
        CollapseSettings settings;
        settings.enabled=true;
        settings.minimumEdgeRatio=2;
        settings.maximumPairsPerStamp=1;
        VersionedDirectionalCollapser collapse(6,settings,{},1);
        LocalStampBaseline baseline{before,{b},before->vertex(b).incidentFaces};
        const auto collapsed=collapse.tryEdge(shortEdge.updatedIndex(),
                                              baseline,a,b);
        check(collapsed.statistics.accepted==1,
              "Checkpoint fixture removes real stable IDs");
        const auto snapshot=DurableCheckpoint{collapsed.index,{},6};
        phase("first checkpoint write BEGIN");
        writeCheckpoint(file,snapshot);
        phase("first checkpoint write END; reload BEGIN");
        auto restored=readCheckpoint(file);
        phase("first checkpoint reload END");
        check(restored.initialVertexCount==6,
              "Checkpoint preserves original remeshing reference density");
        check(restored.index->retiredVertex(b)&&
              restored.index->retiredFaceCount()==2,
              "Checkpoint recovers persistent vertex and face tombstones");
        check(restored.index->revision().identityWatermarks().nextVertex==
              collapsed.index->revision().identityWatermarks().nextVertex,
              "Restored identity allocator retains consumed/abandoned high water");
        check(restored.index->revision().partitionCount()==
              collapsed.index->revision().partitionCount(),
              "Checkpoint preserves exact partition topology and owners");
        for(int k=0;k<20;++k) {
            const auto eye=Vec3{3.,2.*std::cos(k*.2),2.*std::sin(k*.2)};
            const auto ray=Ray{eye,normalized(Vec3{}-eye)};
            const auto p=restored.index->firstRayHit(ray);
            const auto q=collapsed.index->firstRayHit(ray);
            check(bool(p)==bool(q) &&
                  (!p||length(*p-*q)<1e-10),
                  "Restored BVH reproduces nearest intersections exactly");
        }
        const auto marks=restored.index->revision().identityWatermarks();
        // Even if a malicious file's checksum is recomputed, tombstones
        // beyond the saved allocation frontier must never be accepted:
        // the next allocation would otherwise reuse a retired identity.
        const auto savedRevision=std::shared_ptr<const MeshRevision>(
            restored.index,&restored.index->revision());
        bool rejectedVertex=false,rejectedFace=false;
        try {
            (void)RegionTopologyIndex::restore(
                savedRevision,{marks.nextVertex},{});
        }catch(const std::invalid_argument&) {rejectedVertex=true;}
        try {
            (void)RegionTopologyIndex::restore(
                savedRevision,{}, {marks.nextFace});
        }catch(const std::invalid_argument&) {rejectedFace=true;}
        check(rejectedVertex&&rejectedFace,
              "Recovered tombstones cannot reach an unallocated stable ID");
        auto view=restored.index->revision().createMutableView();
        const auto fresh=view.addVertex(123,{2,0,0});
        check(fresh==marks.nextVertex&&fresh>b,
              "Fresh vertex IDs after reload cannot reuse tombstoned IDs");

        auto second=DurableCheckpoint{before,{}};
        phase("replacement checkpoint write BEGIN");
        writeCheckpoint(file,second);
        phase("replacement checkpoint write END");
        check(readCheckpoint(file).index->retiredFaceCount()==0,
              "Subsequent complete checkpoint atomically replaces old version");
        // Force a failure BEFORE replace by obstructing the temporary path.
        // The last valid destination must still be readable afterwards.
        auto obstructed=file;
        obstructed+=".tmp";
        std::filesystem::create_directory(obstructed);
        bool saveFailed=false;
        phase("deliberately obstructed write BEGIN");
        try {writeCheckpoint(file,snapshot);}
        catch(const std::exception&) {saveFailed=true;}
        phase("deliberately obstructed write END");
        check(saveFailed&&readCheckpoint(file).index->retiredFaceCount()==0,
              "Failed temp write preserves the preceding complete checkpoint");
        std::error_code cleanupError;
        std::filesystem::remove(obstructed,cleanupError);
        // Corruption/truncation must never yield partial recovered geometry.
        {
            std::ofstream damaged(file,std::ios::binary|std::ios::app);
            damaged.put('X');
        }
        try {
            (void)readCheckpoint(file);
            throw std::runtime_error("Trailing garbage was accepted");
        }catch(const std::runtime_error& e) {
            check(std::string(e.what())!="Trailing garbage was accepted",
                  "Checksum rejects appended bytes");
        }
        phase("pre-truncation checkpoint write BEGIN");
        writeCheckpoint(file,snapshot);
        phase("pre-truncation checkpoint write END");
        std::filesystem::resize_file(file,20);
        try {
            (void)readCheckpoint(file);
            throw std::runtime_error("Truncated file was accepted");
        }catch(const std::runtime_error& e) {
            check(std::string(e.what())!="Truncated file was accepted",
                  "Checkpoint reader refuses truncated geometry");
        }
    }catch(...) {
        std::filesystem::remove_all(dir);
        throw;
    }
    std::filesystem::remove_all(dir);
}
void testM29BoundedAuthenticatedLifetimeHistoryAndRecovery() {
    auto imported=partitionFlatMesh(octahedron(),1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto fixedRoot=original->pickingRootIdentity();
    auto first=std::make_shared<OriginalPatchLifetime>(
        beginOriginalPatchLifetime(
            captureOriginalSourceAnchors(original,imported.flatIndex.faceIds,56)));
    RevisionHistory tracked(original,3,512,nullptr,6,8,first);
    VersionedInflateBrush inflate(.035,{});
    VersionedLocalRefiner refiner(6,{},{},1);
    VersionedDirectionalCollapser collapse(6,{},{},1);
    const std::array<BrushArea,1> patch{{{{1,0,0},.8,.9}}};
    const auto makeNext=[&](const OriginalPatchLifetime& previous) {
        const auto post=inflate.stamp(previous.lineage.terminal,patch,1);
        const auto refined=refiner.apply(post.index,post.baseline);
        const auto collapsed=collapse.apply(refined.index,post.baseline);
        check(post.moved>0,"M29 real source lifetime history step changes geometry");
        return std::make_shared<OriginalPatchLifetime>(
            advanceCertifiedPhysicalStampLifetime(
                previous,post,refined,collapsed,
                nullptr,nullptr,256,256,6,8));
    };
    auto one=makeNext(*first);
    tracked.append(one->lineage.terminal,{1},{},{},128,6,8,one);
    check(tracked.currentFidelity()==one&&
          tracked.statistics().undoDepth==1&&
          one->anchors.original==original,
          "M29 first worker-style history append stores exact authenticated lifetime root");
    auto two=makeNext(*one);
    tracked.append(two->lineage.terminal,{2},{},{},128,6,8,two);
    const auto prior=tracked.statistics();
    auto forged=std::make_shared<OriginalPatchLifetime>(*two);
    forged->sourceFaceEvidence=999;
    bool refused=false;
    auto three=makeNext(*two);
    try {tracked.append(three->lineage.terminal,{3},{},{},128,6,8,forged);}
    catch(const std::logic_error&){refused=true;}
    check(refused&&tracked.current()==two->lineage.terminal&&
          tracked.currentFidelity()==two&&
          tracked.statistics().undoDepth==prior.undoDepth&&
          tracked.statistics().discardedRedoRevisions==prior.discardedRedoRevisions,
          "M29 unsealed original evidence cannot mutate history, cursor, redo or budget");
    refused=false;
    try {tracked.append(three->lineage.terminal,{3},{},{},128,6,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused&&tracked.currentFidelity()==two,
          "M29 missing tracked lifetime cannot silently disable fidelity after two Steps");
    refused=false;
    try {tracked.append(three->lineage.terminal,{3},{},{},128,6,8,one);}
    catch(const std::logic_error&){refused=true;}
    check(refused&&tracked.current()==two->lineage.terminal,
          "M29 stale historical certificate cannot be submitted as next published source owner");
    const auto changedBefore=tracked.currentFidelity();
    tracked.append(three->lineage.terminal,{3},{},{},128,6,8,three);
    const auto backward=tracked.undo();
    check(backward.index==two->lineage.terminal&&
          backward.fidelity==two&&tracked.currentFidelity()==changedBefore&&
          tracked.statistics().redoDepth==1,
          "M29 undo restores exact previous root AND matching immutable source certificate");
    const auto forward=tracked.redo();
    check(forward.index==three->lineage.terminal&&
          forward.fidelity==three&&tracked.currentFidelity()==three,
          "M29 redo restores original-parent provenance without reanchoring source witnesses");

    // True history eviction must not be defeated by a strong chain of
    // opaque proof parents pinning all historical COW root directories.
    std::weak_ptr<const RegionTopologyIndex> evicted=one->lineage.terminal;
    auto four=makeNext(*three);
    tracked.append(four->lineage.terminal,{4},{},{},128,6,8,four);
    check(tracked.statistics().droppedRevisions>0&&
          tracked.statistics().retainedRevisions<=3&&
          four->anchors.original==original&&
          four->sourceFaceEvidence>=three->sourceFaceEvidence&&
          four->acceptedTransitions==4,
          "M29 bounded history evicts revisions but preserves original source and cumulative counters");
    one.reset();
    check(evicted.expired(),
          "M29 weak opaque certificate parents do not retain evicted intermediate COW roots");

    // Undo + accepted alternative edit discards only redo, while a
    // rejected stale alternative leaves it intact.
    const auto restore=tracked.undo();
    auto alternate=makeNext(*restore.fidelity);
    const auto stats=tracked.statistics();
    refused=false;
    try {tracked.append(alternate->lineage.terminal,{5},{},{},64,6,8,four);}
    catch(const std::logic_error&){refused=true;}
    check(refused&&tracked.statistics().redoDepth==stats.redoDepth,
          "M29 stale branch source proof does not discard undo redo or change history");
    tracked.append(alternate->lineage.terminal,{5},{},{},64,6,8,alternate);
    check(!tracked.canRedo()&&tracked.currentFidelity()==alternate&&
          tracked.statistics().discardedRedoRevisions>0&&
          original->pickingRootIdentity()==fixedRoot,
          "M29 genuinely authenticated new branch discards redo but leaves original picker fixed");

    const auto file=std::filesystem::temp_directory_path()/
        "mesher-v17-m29-protected-original-checkpoint.mshr";
    DurableCheckpoint protectedSnapshot;
    protectedSnapshot.index=tracked.current();
    protectedSnapshot.initialVertexCount=6;
    protectedSnapshot.verifiedOriginalFidelity=tracked.currentFidelity();
    protectedSnapshot.originalFidelityRequired=true;
    try {
        phase("M29 protected checkpoint write BEGIN");
        writeCheckpoint(file,protectedSnapshot);
        phase("M29 protected checkpoint write END; reload BEGIN");
        const auto loaded=readCheckpoint(file);
        phase("M29 protected checkpoint reload END");
        check(loaded.index&&loaded.originalFidelityRequired&&
              !loaded.verifiedOriginalFidelity&&
              loaded.index->pickingRootIdentity()!=nullptr,
              "M29 V13 disk checkpoint stores protected provenance requirement but cannot fabricate a sealed root");
        SculptCollisionSettings physical;
        physical.physicalExact=true;
        refused=false;
        phase("M29 untrusted V13 constructor (expected refusal) BEGIN");
        try {(void)VersionedSculptBrush(
            loaded,.01,{},{},{},256,{},physical);}
        catch(const std::logic_error&){refused=true;}
        phase("M29 untrusted V13 constructor (expected refusal) END");
        check(refused,
              "M29 protected physicalExact cannot reset original witnesses at recovered terminal root");
        // An explicitly trusted in-memory handoff DOES retain exactly
        // the original opaque first-root source certificate.
        phase("M29 trusted in-memory constructor BEGIN");
        {
            VersionedSculptBrush restored(
                protectedSnapshot,.01,{},{},{},256,{},physical);
            phase("M29 trusted in-memory constructor END");
            check(restored.snapshot()==tracked.current()&&
                  restored.checkpoint().originalFidelityRequired&&
                  restored.checkpoint().verifiedOriginalFidelity==
                      tracked.currentFidelity(),
                  "M29 in-memory authenticated handoff preserves source identity and protected requirement");
            phase("M29 trusted brush destructor BEGIN");
        }
        phase("M29 trusted brush destructor END");
    }catch(...) {
        std::error_code ec;std::filesystem::remove(file,ec);
        throw;
    }
    std::error_code ec;std::filesystem::remove(file,ec);
}

void testAsyncCheckpointCoalescing() {
    const auto dir=std::filesystem::temp_directory_path()/
        ("mesher-v12-async-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    const auto file=dir/"latest.mshr";
    try {
        auto imported=partitionFlatMesh(octahedron(),1);
        auto index=RegionTopologyIndex::build(imported.revision);
        const auto first=index;
        auto next=moved(index,imported.flatIndex.vertexIds.front(),1.2);
        {
            phase("async writer constructor BEGIN");
            AsyncCheckpointWriter writer;
            phase("async writer constructor END");
            for(int i=0;i<6;++i) {
                std::cerr<<"[persistent_history_checkpoints] checkpoint enqueue "
                         <<i<<"/6"<<std::endl;
                writer.request(file,{i==5?next:first,{}});
            }
            phase("async writer drain BEGIN");
            writer.drain();
            phase("async writer drain END");
            const auto stats=writer.statistics();
            check(stats.requested==6&&stats.completed>=1&&
                  stats.completed+stats.coalesced+stats.failed==6,
                  "Latest-only checkpoint worker accounts for every request");
            check(stats.failed==0&&stats.lastError.empty()&&stats.lastBytes>0,
                  "Background writes complete without hidden exceptions");
            phase("async writer destructor BEGIN");
        }
        phase("async writer destructor END; reload BEGIN");
        auto loaded=readCheckpoint(file);
        phase("async writer reload END");
        check(length(loaded.index->vertex(imported.flatIndex.vertexIds.front()).position-
                     next->vertex(imported.flatIndex.vertexIds.front()).position)==0,
              "Latest writer result wins over any superseded snapshots");
    }catch(...) {
        std::filesystem::remove_all(dir);
        throw;
    }
    std::filesystem::remove_all(dir);
}
}
int main() {
    try {
        phase("START RNG=none; fixed octahedron and test inputs; "
              "clock names temporary paths; thread scheduling varies");
        const auto run=[](std::string_view name,auto&& operation) {
            phase(std::string("BEGIN ")+std::string(name));
            operation();
            phase(std::string("END ")+std::string(name));
        };
        run("bounded history and retirement",testBoundedHistoryAndBranching);
        run("undo mirror delta",testUndoMirrorDelta);
        run("checkpoint roundtrip",testCheckpointRoundTripAndRecovery);
        run("M29 authenticated lifetime",testM29BoundedAuthenticatedLifetimeHistoryAndRecovery);
        run("async checkpoint coalescing",testAsyncCheckpointCoalescing);
        std::cout<<"V12 history and checkpoint regressions passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"V12 history/checkpoint failure: "<<e.what()<<'\n';
        return 1;
    }
}

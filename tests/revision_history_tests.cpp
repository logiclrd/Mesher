#include "mesher/revision_history.hpp"
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
#include <vector>

using namespace mesher;
namespace {
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
    retired.drain();
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
    const auto canonical=std::minmax(inserted.first,inserted.second);
    check(undo.removeMirrors.size()==1&&undo.removeMirrors.front()==canonical&&
          undo.addMirrors.size()==1&&undo.addMirrors.front()==removed,
          "Undo reverses added/deleted symmetry correspondence");
    auto redo=history.redo();
    check(redo.index==changed&&redo.addMirrors.front()==inserted&&
          redo.removeMirrors.front()==removed,
          "Redo reapplies forward symmetry delta");
}
void testCheckpointRoundTripAndRecovery() {
    const auto dir=std::filesystem::temp_directory_path()/
        ("mesher-v12-"+std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
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
        const auto snapshot=DurableCheckpoint{collapsed.index,{}};
        writeCheckpoint(file,snapshot);
        auto restored=readCheckpoint(file);
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
        auto view=restored.index->revision().createMutableView();
        const auto fresh=view.addVertex(123,{2,0,0});
        check(fresh==marks.nextVertex&&fresh>b,
              "Fresh vertex IDs after reload cannot reuse tombstoned IDs");

        auto second=DurableCheckpoint{before,{}};
        writeCheckpoint(file,second);
        check(readCheckpoint(file).index->retiredFaceCount()==0,
              "Subsequent complete checkpoint atomically replaces old version");
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
        writeCheckpoint(file,snapshot);
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
            AsyncCheckpointWriter writer;
            for(int i=0;i<6;++i)
                writer.request(file,{i==5?next:first,{}});
            writer.drain();
            const auto stats=writer.statistics();
            check(stats.requested==6&&stats.completed>=1&&
                  stats.completed+stats.coalesced+stats.failed==6,
                  "Latest-only checkpoint worker accounts for every request");
            check(stats.failed==0&&stats.lastError.empty()&&stats.lastBytes>0,
                  "Background writes complete without hidden exceptions");
        }
        auto loaded=readCheckpoint(file);
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
        testBoundedHistoryAndBranching();
        testUndoMirrorDelta();
        testCheckpointRoundTripAndRecovery();
        testAsyncCheckpointCoalescing();
        std::cout<<"V12 history and checkpoint regressions passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"V12 history/checkpoint failure: "<<e.what()<<'\n';
        return 1;
    }
}

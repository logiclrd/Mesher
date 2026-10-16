#include "mesher/revision_retirement.hpp"
#include "mesher/async_sculpt.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/partition_render.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace mesher;
namespace {
void check(bool valid,const char* message) {
    if(!valid)throw std::runtime_error(message);
}
void testBoundedRetirementWithSlowReaders() {
    SphereOptions options;
    options.vertexCount=550;
    options.relaxationIterations=6;
    const auto import=partitionFlatMesh(generateSphere(options),16);
    const auto original=RegionTopologyIndex::build(import.revision);
    auto current=original;
    PartitionRenderCache rendered;
    rendered.initialize(current);
    RevisionRetirementQueue retirement(3);
    std::vector<std::shared_ptr<const RegionTopologyIndex>> readers{original};
    const auto selected=original->querySphere({.25,-.65,.65},.45);
    check(!selected.empty(),"Fixture provides a local brush candidate");
    const auto id=selected.front();
    const auto oldPosition=original->vertex(id).position;
    std::size_t totalCopied=0;
    std::size_t totalPaths=0;
    for(int i=0;i<160;++i) {
        std::shared_ptr<const RegionTopologyIndex> next;
        {
            RegionEdit edit(current,{id},{},16);
            edit.moveVertex(id,oldPosition*(1.0+0.00002*(i+1)));
            const auto committed=edit.commit();
            next=edit.updatedIndex();
            check(committed.statistics.clonedPartitionBytes>0,
                  "Editing an existing partition reports copied actual bytes");
            check(committed.statistics.copiedDirectoryNodes<=
                  9*committed.statistics.modifiedPartitions,
                  "COW directory copies remain bounded by dirty partitions");
            totalCopied+=committed.statistics.clonedPartitionBytes;
            totalPaths+=committed.statistics.copiedDirectoryNodes;
        }
        auto previous=std::move(current);
        current=std::move(next);
        const std::vector<VertexId> changed{id};
        const auto dirty=dirtyRenderPartitions(*previous,*current,changed,{});
        rendered.advance(current,dirty,&retirement);
        retirement.retire(std::move(previous));
        if(i%25==0)readers.push_back(current);
        const auto stats=retirement.statistics();
        check(stats.queuedRoots<=3&&stats.peakQueuedRoots<=3,
              "Async retired-root queue never exceeds its configured bound");
        check(current->updateStatistics().fullMeshTraversals==0,
              "160 local revisions avoid full-mesh index traversal");
        RegionTopologyIndex::PickStatistics hits;
        (void)readers.front()->firstRayHit(
            viewportRay(640,360,1280,720),&hits);
    }
    retirement.drain();
    const auto stats=retirement.statistics();
    check(stats.queuedRoots==0&&stats.submittedRoots>=160,
          "All enqueued root releases complete off-thread");
    check(stats.releasedReferences==stats.submittedRoots,
          "Every retired root reference is released by retirement worker");
    check(totalCopied>0&&totalPaths>=160,
          "Cloned bytes and directory node metrics capture real work");
    check(length(original->vertex(id).position-oldPosition)==0,
          "Earliest retained root remains unchanged through 160 commits");
    for(const auto& held:readers) {
        const auto hit=held->firstRayHit(viewportRay(640,360,1280,720));
        check(bool(hit),"Slow reader retains usable exact acceleration");
    }
    const auto rootsHeld=RegionTopologyIndex::liveRevisionCount();
    check(rootsHeld>=readers.size(),
          "Live-root counter includes externally held historic snapshots");
    readers.clear();
    rendered.initialize(current); // releases only its identical current root
    check(current->vertex(id).position.x!=original->vertex(id).position.x,
          "Revisions retain their own geometric coordinates");
}
void testConcurrentReaderAndPublication() {
    auto mesh=generateSphere(SphereOptions{500,true,4,6});
    auto imported=partitionFlatMesh(mesh,16);
    auto original=RegionTopologyIndex::build(imported.revision);
    IndexedMeshRepository repo(original);
    RevisionRetirementQueue retirement(8);
    auto ids=original->querySphere({.25,-.65,.65},.40);
    check(!ids.empty(),"Concurrent fixture has local seed");
    const auto id=ids.front();
    std::atomic<bool> completed{false};
    std::atomic<int> readCount{0};
    std::atomic<int> misses{0};
    std::jthread observer([&] {
        while(!completed.load()) {
            auto state=repo.snapshot();
            auto hit=state->firstRayHit(viewportRay(640,360,1280,720));
            if(!hit)++misses;
            ++readCount;
        }
    });
    for(int i=0;i<60;++i) {
        auto prior=repo.snapshot();
        std::shared_ptr<const RegionTopologyIndex> next;
        {
            RegionEdit edit(prior,{id},{},16);
            edit.moveVertex(id,original->vertex(id).position*
                            (1.0+0.00003*(i+1)));
            (void)edit.commit();
            next=edit.updatedIndex();
        }
        check(repo.publishIfCurrent(prior,next),
              "Single-writer indexed CAS does not lose concurrent reads");
        retirement.retire(std::move(prior));
    }
    completed=true;
    observer.join();
    retirement.drain();
    check(readCount>0&&misses==0,
          "Concurrent reader retains coherent ray hits throughout publication");
    check(repo.snapshot()->updateStatistics().fullMeshTraversals==0,
          "No per-publication global traversal during concurrent edits");
}
void testBacklogAndCancellation() {
    auto initial=generateSphere(SphereOptions{500,true,5,6});
    const auto before=RegionTopologyIndex::liveRevisionCount();
    std::shared_ptr<const RegionTopologyIndex> retained;
    {
        AsyncSculptWorker worker(initial,.012,.005,.2,{true,.25},
                                CollapseSettings{},RefineSettings{},true);
        retained=worker.initialRevision();
        worker.begin({-.2,-.6,.75},.2);
        worker.update({-.3,-.7,.61},.22);
        worker.end({-.35,-.72,.58},.4);
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(20);
        while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline) {
            (void)worker.status();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(worker.canBegin(),"Worker drains queued exact 4D stamps");
        const auto status=worker.status();
        check(status.completedStamps>=2 &&
              status.pendingSteps==0&&status.pendingSegments==0,
              "Slow consumer does not drop queued Step stamps");
        check(status.clonedPartitionBytes>0&&
              status.lastPublicationMs>=0&&status.liveRevisions>=1,
              "Worker reports clone bytes, root counts and publication latency");
        auto snapshot=worker.takeSnapshot();
        check(snapshot&&snapshot->index,
              "Slow reader can take latest coalesced immutable root");
        retained=snapshot->index;
        // Cancel/shutdown with another stroke queued. jthread must finish
        // cleanly while a separate reader retains its independent old root.
        worker.begin({-.2,-.6,.75},.1);
        worker.update({-.35,-.72,.58},.3);
        worker.end({-.35,-.72,.58},.3);
    }
    check(bool(retained->firstRayHit(viewportRay(640,360,1280,720))),
          "Reader-held snapshot survives worker cancellation and destruction");
    retained.reset();
    check(RegionTopologyIndex::liveRevisionCount()<=before+2,
          "No leaked indexed worker roots after shutdown and final release");
}
}
int main() {
    try {
        testBoundedRetirementWithSlowReaders();
        testConcurrentReaderAndPublication();
        testBacklogAndCancellation();
        std::cout<<"Bounded retirement and concurrent reader stress passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"V11 retirement stress failure: "<<e.what()<<'\n';
        return 1;
    }
}

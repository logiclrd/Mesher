#include "mesher/versioned_mesh.hpp"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace mesher;
namespace {
void require(bool condition, const char* message) {
    if(!condition)throw std::runtime_error(message);
}

void testCopyOnWriteAndCheapCommit() {
    auto base=MeshRevision::empty();
    auto seed=base->createMutableView();
    for(std::uint32_t i=0;i<1024;++i) {
        auto& partition=seed.editPartition(i*1048577u);
        partition.vertices.push_back({i,{double(i),0,0}});
    }
    auto seeded=seed.commit();
    require(seeded.revision->partitionCount()==1024,"All partitions inserted");
    auto old=seeded.revision;
    const auto shared=old->partition(0);
    const auto changed=old->partition(1048577u);
    auto edit=old->createMutableView();
    auto& first=edit.editPartition(1048577u);
    first.vertices[0].position.x=987;
    // The second edit must reuse the same writable partition, not clone again.
    auto& second=edit.editPartition(1048577u);
    second.vertices[0].position.y=456;
    auto committed=edit.commit();
    require(committed.statistics.modifiedPartitions==1,"One dirty partition");
    require(committed.statistics.clonedPartitions==1,"Exactly one partition clone");
    require(committed.statistics.copiedDirectoryNodes==9,"Constant-depth path copy");
    require(committed.revision->partitionCount()==1024,"Partition count stable");
    require(committed.revision->partition(0)==shared,"Unmodified pointer identity retained");
    require(committed.revision->partition(1048577u)!=changed,"Edited partition cloned");
    require(changed->vertices[0].position.x==1 &&
            changed->vertices[0].position.y==0,"Old revision unchanged");
    require(committed.revision->partition(1048577u)->vertices[0].position.x==987 &&
            committed.revision->partition(1048577u)->vertices[0].position.y==456,
            "New revision contains both edits");
    bool rejected=false;
    try { (void)edit.editPartition(0); }
    catch(const std::logic_error&) {rejected=true;}
    require(rejected,"Committed view cannot mutate published partitions");
    rejected=false;
    try { (void)edit.commit(); }
    catch(const std::logic_error&) {rejected=true;}
    require(rejected,"A transaction can commit only once");
}

void testRemovalAndBranching() {
    auto empty=MeshRevision::empty();
    auto a=empty->createMutableView();
    a.editPartition(42).faces.push_back({88,{1,2,3}});
    auto first=a.commit().revision;
    auto b=first->createMutableView();
    b.erasePartition(42);
    b.editPartition(99).vertices.push_back({7,{1,2,3}});
    auto second=b.commit().revision;
    require(second->partitionCount()==1,"Partition removal and creation counted");
    require(!second->partition(42),"Removed partition absent in newer revision");
    require(first->partition(42)->faces[0].id==88,"Older revision survives deletion");
    auto c=first->createMutableView();
    c.editPartition(42).faces[0].id=77;
    auto branch=c.commit().revision;
    require(branch->partition(42)->faces[0].id==77,"Independent branch edit");
    require(first->partition(42)->faces[0].id==88,"Base branch unchanged");
    require(second->partition(99)->vertices.size()==1,"Other branch unchanged");
    auto noop=second->createMutableView().commit();
    require(noop.statistics.copiedDirectoryNodes==0,"No-op commit has no path copies");
    require(noop.revision->partition(99)==second->partition(99),
            "No-op revision retains exact partition identity");
}

void testConcurrentPublication() {
    auto initial=MeshRevision::empty();
    auto working=initial->createMutableView();
    working.editPartition(7).vertices.push_back({100,{0,0,0}});
    MeshRepository repository(working.commit().revision);
    const auto old=repository.snapshot();
    std::atomic<bool> failed{false};
    std::thread reader([&] {
        for(int i=0;i<2000;++i) {
            auto snapshot=repository.snapshot();
            auto partition=snapshot->partition(7);
            if(!partition || partition->vertices.size()!=1 ||
               partition->vertices[0].position.x<0)failed=true;
        }
    });
    for(int i=1;i<=200;++i) {
        auto transaction=repository.snapshot()->createMutableView();
        transaction.editPartition(7).vertices[0].position.x=i;
        repository.publish(transaction.commit().revision);
    }
    reader.join();
    require(!failed,"Readers see complete valid published partitions");
    require(old->partition(7)->vertices[0].position.x==0,
            "An old revision stays unchanged through 200 publications");
    require(repository.snapshot()->partition(7)->vertices[0].position.x==200,
            "Reader can acquire newest published revision");
}
}
int main() {
    try {
        testCopyOnWriteAndCheapCommit();
        testRemovalAndBranching();
        testConcurrentPublication();
        std::cout<<"Versioned mesh storage tests passed\n";
    } catch(const std::exception& e) {
        std::cerr<<"Versioned mesh test failed: "<<e.what()<<'\n';
        return 1;
    }
    return 0;
}

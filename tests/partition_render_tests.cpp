#include "mesher/partition_render.hpp"
#include "mesher/versioned_inflate.hpp"
#include "mesher/versioned_refine.hpp"
#include "mesher/versioned_collapse.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace mesher;
namespace {
void check(bool condition,const char* why) {
    if(!condition)throw std::runtime_error(why);
}
bool same(PartitionRenderVertex a,PartitionRenderVertex b) {
    return a.px==b.px&&a.py==b.py&&a.pz==b.pz&&
           a.nx==b.nx&&a.ny==b.ny&&a.nz==b.nz&&
           a.material==b.material;
}
bool same(const PartitionRenderGeometry& a,const PartitionRenderGeometry& b) {
    if(a.solid.size()!=b.solid.size()||a.wire.size()!=b.wire.size())return false;
    for(std::size_t i=0;i<a.solid.size();++i)
        if(!same(a.solid[i],b.solid[i]))return false;
    for(std::size_t i=0;i<a.wire.size();++i)
        if(!same(a.wire[i],b.wire[i]))return false;
    return true;
}
void equalFull(const PartitionRenderCache& cache,
               std::shared_ptr<const RegionTopologyIndex> index) {
    PartitionRenderCache reference;
    reference.initialize(std::move(index));
    const auto& expected=reference.partitions();
    const auto& actual=cache.partitions();
    check(expected.size()==actual.size(),
          "Incremental render partition set must match independent rebuild");
    for(const auto& [id,geometry]:expected) {
        const auto found=actual.find(id);
        check(found!=actual.end()&&same(*found->second,*geometry),
              "Incrementally regenerated triangles and edges match full rebuild");
    }
}
std::size_t edgeCount(const PartitionRenderCache& cache) {
    std::size_t count=0;
    for(const auto& [id,partition]:cache.partitions()) {
        (void)id;
        check(partition->solid.size()%3==0 &&
              partition->wire.size()%2==0,
              "Triangles and edge lines are complete primitives");
        count+=partition->wire.size()/2;
    }
    return count;
}
Mesh octahedron() {
    return {{{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
             {0,0,1},{0,0,-1}},
            {{4,0,1},{4,1,2},{4,2,3},{4,3,0},
             {5,1,0},{5,2,1},{5,3,2},{5,0,3}}};
}
void testLocalMoveAndSharedBoundaries() {
    SphereOptions options;
    options.vertexCount=900;
    options.relaxationIterations=8;
    auto imported=partitionFlatMesh(generateSphere(options),16);
    auto before=RegionTopologyIndex::build(imported.revision);
    PartitionRenderCache cache;
    cache.initialize(before);
    equalFull(cache,before);
    // For a closed genus-zero triangle mesh E=3F/2, with no seam duplication
    // even when its adjacent faces reside in distinct physical partitions.
    const auto edges=edgeCount(cache);
    check(edges==3*imported.flatIndex.faceIds.size()/2,
          "Every shared edge drawn exactly once across partition borders");
    const auto candidate=before->querySphere({.25,-.65,.65},.15);
    check(!candidate.empty(),"Fixture supplies local moved vertices");
    const auto target=candidate.front();
    std::unordered_map<PartitionId,const PartitionRenderGeometry*> prior;
    for(const auto& [id,p]:cache.partitions())prior.emplace(id,p.get());
    RegionEdit edit(before,{target});
    edit.moveVertex(target,before->vertex(target).position*1.001);
    (void)edit.commit();
    const auto after=edit.updatedIndex();
    std::vector<VertexId> changed{target};
    auto dirty=dirtyRenderPartitions(*before,*after,changed,{});
    check(!dirty.empty()&&dirty.size()<cache.partitions().size()/2,
          "Local motion updates bounded face-owner halo, not whole mesh");
    const auto stats=cache.advance(after,dirty);
    check(stats.rebuiltPartitions==dirty.size()&&
          stats.rebuiltFaces>0&&stats.uploadedBytes>0,
          "Render telemetry measures only dirty partition geometry");
    const std::set<PartitionId> affected(dirty.begin(),dirty.end());
    for(const auto& [id,p]:cache.partitions())
        if(!affected.contains(id))
            check(p.get()==prior.at(id),
                  "Untouched CPU render buffers retain exact object pointer");
    equalFull(cache,after);
    check(edgeCount(cache)==edges,
          "Cross-partition line ownership remains unique after local move");
    check(cache.revision()==after&&
          before->vertex(target).position.x!=after->vertex(target).position.x,
          "Old revision remains untouched while cache uses new revision");
}
void testWireOwnershipIndependentOfFaceStorageOrder() {
    const auto imported=partitionFlatMesh(octahedron(),64);
    auto revision=imported.revision;
    auto view=revision->createMutableView();
    auto& partition=view.editPartition(0);
    std::reverse(partition.faces.begin(),partition.faces.end());
    const auto reversed=view.commit().revision;
    const auto index=RegionTopologyIndex::build(reversed);
    PartitionRenderCache cache;
    cache.initialize(index);
    check(edgeCount(cache)==12,
          "Canonical edge owner must win even if faces are stored backwards");
}
void testSplitAndCollapseTopology() {
    auto imported=partitionFlatMesh(octahedron(),1);
    auto before=RegionTopologyIndex::build(imported.revision);
    PartitionRenderCache cache;
    cache.initialize(before);
    check(edgeCount(cache)==12,
          "Original octahedron has twelve distinct globally owned wires");
    auto target=imported.flatIndex.vertexIds[0];
    RegionEdit deformation(before,{target},{},1);
    deformation.moveVertex(target,{1.25,0,0});
    (void)deformation.commit();
    const auto deformed=deformation.updatedIndex();
    LocalStampBaseline baseline{before,{target},
                                before->vertex(target).incidentFaces};
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=1;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refiner(6,settings,{},1);
    const auto refined=refiner.apply(deformed,baseline);
    check(refined.statistics.splitPairs==1,
          "Cross-partition fixture inserts one midpoint and two faces");
    std::vector<VertexId> affected{target};
    affected.insert(affected.end(),refined.insertedVertices.begin(),
                    refined.insertedVertices.end());
    std::vector<FaceId> changed=baseline.affectedFaces;
    changed.insert(changed.end(),refined.changedFaces.begin(),
                   refined.changedFaces.end());
    auto dirty=dirtyRenderPartitions(*before,*refined.index,affected,changed);
    cache.advance(refined.index,dirty);
    equalFull(cache,refined.index);
    check(edgeCount(cache)==15,
          "Two triangles per split add exactly three unique wire edges");
    // A collapsed edge removes its two incident faces and one surviving
    // vertex ID; the pre-stamp baseline retains the old edge length.
    const auto original=partitionFlatMesh(octahedron(),1);
    auto prior=RegionTopologyIndex::build(original.revision);
    PartitionRenderCache collapseCache;
    collapseCache.initialize(prior);
    const auto a=original.flatIndex.vertexIds[0];
    const auto b=original.flatIndex.vertexIds[1];
    RegionEdit shortEdge(prior,{b},{},1);
    shortEdge.moveVertex(b,{.6,.4,0});
    (void)shortEdge.commit();
    const auto newBefore=shortEdge.updatedIndex();
    CollapseSettings collapse;
    collapse.enabled=true;
    collapse.minimumEdgeRatio=2;
    collapse.maximumPairsPerStamp=1;
    VersionedDirectionalCollapser collapser(6,collapse,{},1);
    LocalStampBaseline shrink{prior,{b},prior->vertex(b).incidentFaces};
    const auto result=collapser.tryEdge(newBefore,shrink,a,b);
    check(result.statistics.accepted==1,
          "Fixture accepts one validated directional edge collapse");
    auto dirtyCollapse=dirtyRenderPartitions(
        *prior,*result.index,result.removedVertices,result.removedFaces);
    collapseCache.advance(result.index,dirtyCollapse);
    equalFull(collapseCache,result.index);
    check(edgeCount(collapseCache)==9,
          "Removing an octahedron edge reduces unique wire edge count to nine");
    check(result.index->retiredVertex(b),
          "Obsolete vertex ID is tombstoned while render partition is updated");
}
void testIndependentOpenRootRebuild() {
    // UX06 accepts a new source with unrelated stable IDs and different
    // partition densities. The old owners must disappear, shared-number
    // owners must be regenerated, and the new root must match a full rebuild.
    auto oldImport=partitionFlatMesh(octahedron(),1);
    auto old=RegionTopologyIndex::build(oldImport.revision);
    PartitionRenderCache cache;
    cache.initialize(old);
    const auto oldCount=cache.partitions().size();
    Mesh replacement=octahedron();
    for(auto& p:replacement.vertices) {
        p.x=p.x*0.5+2.0;
        p.z=p.z*1.75;
    }
    auto nextImport=partitionFlatMesh(replacement,4);
    auto next=RegionTopologyIndex::build(nextImport.revision);
    std::set<PartitionId> owners;
    old->revision().forEachPartition([&](PartitionId id,const MeshPartition&) {
        owners.insert(id);
    });
    next->revision().forEachPartition([&](PartitionId id,const MeshPartition&) {
        owners.insert(id);
    });
    cache.advance(next,{owners.begin(),owners.end()});
    equalFull(cache,next);
    check(cache.revision()==next&&cache.partitions().size()<oldCount,
          "Independent open root retires removed owners and repaints shared IDs");
    check(old->revision().partition(6)!=nullptr,
          "Importing another mesh must not edit the prior immutable root");
    cache.advance(old,{owners.begin(),owners.end()});
    equalFull(cache,old);
    check(cache.revision()==old&&cache.partitions().size()==oldCount,
          "Rebinding an unrelated old root must also rebuild correct geometry");
}
void testCoalescedDirtyUnion() {
    SphereOptions options;
    options.vertexCount=800;
    options.relaxationIterations=8;
    auto imported=partitionFlatMesh(generateSphere(options),16);
    auto original=RegionTopologyIndex::build(imported.revision);
    PartitionRenderCache cache;
    cache.initialize(original);
    const auto selected=original->querySphere({.25,-.65,.65},.35);
    const auto other=original->querySphere({-.25,-.65,.65},.35);
    check(!selected.empty()&&!other.empty(),
          "Distinct stroke locations select at least one vertex");
    const auto a=selected.front(),b=other.front();
    RegionEdit first(original,{a});
    first.moveVertex(a,original->vertex(a).position*1.001);
    (void)first.commit();
    const auto middle=first.updatedIndex();
    RegionEdit second(middle,{b});
    second.moveVertex(b,middle->vertex(b).position*1.002);
    (void)second.commit();
    const auto final=second.updatedIndex();
    auto firstDirty=dirtyRenderPartitions(*original,*middle,
                                         std::vector<VertexId>{a},{});
    auto secondDirty=dirtyRenderPartitions(*middle,*final,
                                          std::vector<VertexId>{b},{});
    // Worker coalescing retains the union since the last *consumed* frame.
    firstDirty.insert(firstDirty.end(),secondDirty.begin(),secondDirty.end());
    std::sort(firstDirty.begin(),firstDirty.end());
    firstDirty.erase(std::unique(firstDirty.begin(),firstDirty.end()),
                     firstDirty.end());
    cache.advance(final,firstDirty);
    equalFull(cache,final);
    check(firstDirty.size()<cache.partitions().size(),
          "Coalesced distant changes do not require a full partition rebuild");
}
}
int main() {
    try {
        testLocalMoveAndSharedBoundaries();
        testWireOwnershipIndependentOfFaceStorageOrder();
        testSplitAndCollapseTopology();
        testIndependentOpenRootRebuild();
        testCoalescedDirtyUnion();
        std::cout<<"Dirty partition CPU rendering regression passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Dirty partition render failure: "<<e.what()<<'\n';
        return 1;
    }
}

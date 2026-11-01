#include "mesher/mesh_partitioning.hpp"
#include "mesher/region_edit.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace mesher;
namespace {
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
template<class F>
void rejects(F&& fn,const char* message) {
    bool failed=false;
    try{fn();}catch(const std::exception&){failed=true;}
    require(failed,message);
}
Mesh octahedron() {
    return {{{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},{0,0,1},{0,0,-1}},
            {{4,0,1},{4,1,2},{4,2,3},{4,3,0},
             {5,1,0},{5,2,1},{5,3,2},{5,0,3}}};
}
std::vector<VertexId> bruteSphere(const MeshRevision& revision,
                                  Vec3 center,double radius) {
    std::vector<VertexId> result;
    revision.forEachPartition([&](PartitionId,const MeshPartition& partition){
        for(const auto& vertex:partition.vertices) {
            const auto d=vertex.position-center;
            if(length(d)<radius)result.push_back(vertex.id);
        }
    });
    std::sort(result.begin(),result.end());
    return result;
}
void testPersistentMap() {
    PersistentIndexMap<int,int> tree;
    std::map<int,int> reference;
    std::uint32_t state=42;
    auto next=[&] {
        state^=state<<13;state^=state>>17;state^=state<<5;
        return state;
    };
    for(int i=0;i<9000;++i) {
        const auto key=static_cast<int>(next()%1900);
        auto previous=tree;
        const auto old=previous.find(key);
        const bool remove=(next()%4)==0;
        std::size_t copies=0;
        if(remove) {
            tree.erase(key,&copies);
            reference.erase(key);
        }else {
            tree.set(key,i,&copies);
            reference[key]=i;
        }
        if(old)require(previous.at(key)==*old,"Persistent old root changed");
        if(i%191==0) {
            require(tree.size()==reference.size(),"Persistent map size diverged");
            auto it=reference.begin();
            tree.forEach([&](int k,int v) {
                require(it!=reference.end() && it->first==k && it->second==v,
                        "Persistent AVL sort/lookup error");
                ++it;
            });
            require(it==reference.end(),"Persistent AVL lost keys");
        }
        require(copies<150,"Persistent AVL update cloned too many nodes");
    }
}
void testExactSpatialAndPicking() {
    SphereOptions options;
    options.vertexCount=800;
    options.relaxationIterations=8;
    auto flat=generateSphere(options);
    auto imported=partitionFlatMesh(flat,32);
    auto index=RegionTopologyIndex::build(imported.revision,.12);
    require(index->updateStatistics().fullMeshTraversals==1,
            "Initial index must report its one full traversal");
    for(int i=0;i<35;++i) {
        const Vec3 p{std::sin(i*.7),std::cos(i*.6),std::sin(i*.4)};
        const double radius=.05+.015*(i%16);
        require(index->querySphere(p,radius)==bruteSphere(*imported.revision,p,radius),
                "Indexed spatial query must exactly match brute-force geometry");
    }
    const auto ray=viewportRay(400,300,800,600);
    auto expected=firstMeshHit(flat,ray);
    auto actual=index->firstRayHit(ray);
    require(bool(expected)==bool(actual),"Indexed picking hit state differs");
    if(expected)require(length(*expected-*actual)<1e-9,
                        "Indexed picking must return the same nearest triangle");
    rejects([&] {(void)index->querySphere({0,0,0},-1);},
            "Negative radius must be rejected");
    rejects([&] {(void)index->querySphere({0,0,0},INFINITY);},
            "Infinite radius must be rejected");
}

void testLocalRevisionChain() {
    auto imported=partitionFlatMesh(octahedron(),1);
    auto current=RegionTopologyIndex::build(imported.revision,.15);
    const auto base=current;
    const auto id=imported.flatIndex.vertexIds[0];
    const auto unaffected=imported.flatIndex.vertexIds[2];
    const auto baseVertex=base->maybeVertex(unaffected);
    require(baseVertex!=nullptr,"Missing base element");
    IndexedMeshRepository published(current);
    for(int step=1;step<=180;++step) {
        RegionEdit edit(current,{id});
        const double x=1.0+step*.0001;
        edit.moveVertex(id,{x,0,0});
        auto committed=edit.commit();
        auto next=edit.updatedIndex();
        require(next && next->revision().partitionCount()==
                    committed.revision->partitionCount(),
                "Edited index and new mesh must pair up");
        require(&next->revision()==committed.revision.get(),
                "Incremental index references exactly committed revision");
        const auto stats=next->updateStatistics();
        require(stats.fullMeshTraversals==0,
                "No full topology traversal allowed per committed edit");
        require(stats.changedVertexRecords==1 &&
                stats.changedFaceRecords==0 && stats.changedEdgeRecords==0 &&
                stats.changedCellRecords<=2,"Only localized index records updated");
        require(stats.copiedTreeNodes<200,
                "Single vertex edit should clone bounded persistent index paths");
        require(next->maybeVertex(unaffected)==baseVertex,
                "Unmodified vertex index record identity must persist");
        require(current->vertex(id).position.x==1.0+(step-1)*.0001,
                "Previous index snapshot changed");
        const auto neighbors=next->querySphere({x,0,0},.00001);
        require(std::find(neighbors.begin(),neighbors.end(),id)!=neighbors.end(),
                "Edited vertex must be in its new spatial bin");
        require(published.publishIfCurrent(current,next),
                "Matching prior indexed state must publish atomically");
        require(!published.publishIfCurrent(current,base),
                "Stale indexed revision must not overwrite newer state");
        current=std::move(next);
    }
    require(base->vertex(id).position.x==1,
            "Long revision chain cannot modify the original index");
    require(current->vertex(id).position.x>base->vertex(id).position.x,
            "Final revision retains all edits");
}
void testReindexedTopology() {
    auto imported=partitionFlatMesh(octahedron(),1);
    const auto& v=imported.flatIndex.vertexIds;
    const auto& f=imported.flatIndex.faceIds;
    auto root=RegionTopologyIndex::build(imported.revision);
    RegionEdit flip(root,{v[0]});
    flip.replaceFace(f[0],{v[4],v[3],v[1]});
    flip.replaceFace(f[3],{v[0],v[1],v[3]});
    auto result=flip.commit();
    auto updated=flip.updatedIndex();
    require(updated && &updated->revision()==result.revision.get(),
            "Valid topology change must produce paired incremental index");
    require(updated->updateStatistics().fullMeshTraversals==0,
            "Edge flipping must not rebuild global index");
    require(updated->updateStatistics().changedFaceRecords==2 &&
            updated->updateStatistics().changedEdgeRecords<=6,
            "Only flipped faces and their edges may change");
    require(updated->face(f[0]).vertices==std::array<VertexId,3>{v[4],v[3],v[1]},
            "Updated face index must match committed triangle");
    const auto removed=updated->maybeEdgeSides({std::min(v[0],v[4]),
                                                 std::max(v[0],v[4])});
    require(!removed,"Flipped-away diagonal is removed from edge index");
    const auto newDiagonal=updated->maybeEdgeSides({std::min(v[1],v[3]),
                                                     std::max(v[1],v[3])});
    require(newDiagonal && newDiagonal->size()==2,
            "New cross-partition edge has both oriented incident faces");
    RegionEdit following(updated,{v[0]});
    following.moveVertex(v[0],{1.001,0,0});
    (void)following.commit();
    require(following.updatedIndex()->updateStatistics().fullMeshTraversals==0,
            "An edit following a topology change reuses the incremental index");
    const auto rebuild=RegionTopologyIndex::build(result.revision);
    for(const auto id:v) {
        require(updated->vertex(id).incidentFaces==
                rebuild->vertex(id).incidentFaces,
                "Incremental adjacency must match independent complete rebuild");
    }
    for(const auto id:f)
        require(updated->face(id).vertices==rebuild->face(id).vertices,
                "Incremental face map differs from independent rebuild");
    const auto owner=root->vertex(v[0]).owner;
    RegionEdit split(root,{v[0]});
    const auto newId=split.addVertex(owner,{1.0/3,1.0/3,1.0/3});
    split.replaceFace(f[0],{v[4],v[0],newId});
    split.addFace(owner,{v[0],v[1],newId});
    split.addFace(owner,{v[1],v[4],newId});
    const auto subdivided=split.commit();
    auto indexed=split.updatedIndex();
    require(indexed->vertex(newId).incidentFaces.size()==3,
            "New vertex must have correct local incident faces");
    require(indexed->querySphere({1.0/3,1.0/3,1.0/3},.01)==
            std::vector<VertexId>{newId},
            "New vertex must immediately be spatially discoverable");
    require(indexed->updateStatistics().fullMeshTraversals==0,
            "Subdivision index update cannot scan all partitions");
    const auto rebuilt=RegionTopologyIndex::build(subdivided.revision);
    require(rebuilt->vertex(newId).incidentFaces==
            indexed->vertex(newId).incidentFaces,
            "Subdivided index must match rebuilt connectivity");
}
void testReadersCanKeepOldIndex() {
    auto imported=partitionFlatMesh(octahedron(),1);
    auto old=RegionTopologyIndex::build(imported.revision);
    IndexedMeshRepository repository(old);
    const auto id=imported.flatIndex.vertexIds[0];
    std::atomic<bool> failed{false};
    std::thread reader([&]{
        for(int i=0;i<1000;++i) {
            const auto state=repository.snapshot();
            const auto& revision=state->revision();
            const auto& vertex=state->vertex(id);
            if(!revision.partition(vertex.owner) ||
               !(vertex.position.x>=1&&vertex.position.x<2))
                failed.store(true);
        }
    });
    auto current=old;
    for(int i=1;i<=75;++i) {
        RegionEdit edit(current,{id});
        edit.moveVertex(id,{1+i*.001,0,0});
        (void)edit.commit();
        auto next=edit.updatedIndex();
        if(!repository.publishIfCurrent(current,next))failed.store(true);
        current=std::move(next);
    }
    reader.join();
    require(!failed,"Readers always observe a complete geometry/index pair");
    require(old->vertex(id).position.x==1,
            "Concurrent reader's old revision remains unchanged");
}
}
int main() {
    try {
        testPersistentMap();
        testExactSpatialAndPicking();
        testLocalRevisionChain();
        testReindexedTopology();
        testReadersCanKeepOldIndex();
        std::cout<<"Incremental versioned index tests passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Incremental index failure: "<<e.what()<<'\n';
        return 1;
    }
    return 0;
}

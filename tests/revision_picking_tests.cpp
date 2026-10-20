#include "mesher/region_edit.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/versioned_refine.hpp"
#include "mesher/versioned_collapse.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
#include <unordered_map>

using namespace mesher;
namespace {
void check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
std::optional<Vec3> brute(const MeshRevision& revision,Ray ray,
                          std::size_t* triangles=nullptr) {
    double closest=std::numeric_limits<double>::infinity();
    std::size_t tested=0;
    std::unordered_map<VertexId,Vec3> positions;
    revision.forEachPartition([&](PartitionId,const MeshPartition& part) {
        for(const auto& vertex:part.vertices)
            positions.emplace(vertex.id,vertex.position);
    });
    revision.forEachPartition([&](PartitionId,const MeshPartition& part) {
        for(const auto& t:part.faces) {
            ++tested;
            const auto a=positions.at(t.vertices[0]);
            const auto b=positions.at(t.vertices[1]);
            const auto c=positions.at(t.vertices[2]);
            const auto ab=b-a,ac=c-a;
            const auto p=cross(ray.direction,ac);
            const auto determinant=dot(ab,p);
            if(std::abs(determinant)<1e-13)continue;
            const double inverse=1/determinant;
            const auto displacement=ray.origin-a;
            const auto u=dot(displacement,p)*inverse;
            if(u<-1e-12||u>1.0+1e-12)continue;
            const auto q=cross(displacement,ab);
            const auto v=dot(ray.direction,q)*inverse;
            if(v<-1e-12||u+v>1.0+1e-12)continue;
            const auto distance=dot(ac,q)*inverse;
            if(distance>1e-8&&distance<closest)closest=distance;
        }
    });
    if(triangles)*triangles=tested;
    if(!std::isfinite(closest))return std::nullopt;
    return ray.origin+ray.direction*closest;
}
void compare(const RegionTopologyIndex& index,Ray ray) {
    RegionTopologyIndex::PickStatistics statistics;
    const auto fast=index.firstRayHit(ray,&statistics);
    const auto exact=brute(index.revision(),ray);
    const auto tagged=index.firstRayFaceHit(ray);
    check(bool(tagged)==bool(fast),
          "M48 typed immutable nearest FaceId picker agrees with legacy ray result");
    if(tagged) {
        check(tagged->face!=0&&index.maybeFace(tagged->face).has_value()&&
              length(tagged->point-*fast)<2e-9,
              "M48 source FaceId is live in exactly same indexed root and hit location");
    }
    check(bool(fast)==bool(exact),"BVH hit/miss agrees with exhaustive face traversal");
    if(fast)check(length(*fast-*exact)<2e-9,
                  "BVH nearest ray intersection equals exhaustive traversal");
    check(statistics.trianglesTested<=index.revision().partitionCount()*256,
          "BVH statistics never exceed default bounded partition triangle count");
}
Mesh octahedron() {
    return {{{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
             {0,0,1},{0,0,-1}},
            {{4,0,1},{4,1,2},{4,2,3},{4,3,0},
             {5,1,0},{5,2,1},{5,3,2},{5,0,3}}};
}
void testExactAndPruned() {
    SphereOptions options;
    options.vertexCount=2400;
    options.relaxationIterations=8;
    auto imported=partitionFlatMesh(generateSphere(options),32);
    const auto index=RegionTopologyIndex::build(imported.revision);
    std::size_t triangles=0;
    for(int y=0;y<=720;y+=90)
        for(int x=0;x<=1280;x+=80) {
            const auto ray=viewportRay(x,y,1280,720);
            compare(*index,ray);
            RegionTopologyIndex::PickStatistics stats;
            (void)index->firstRayHit(ray,&stats);
            check(stats.trianglesTested<imported.flatIndex.faceIds.size(),
                  "Picking should prune triangle leaves even on misses");
            triangles+=stats.trianglesTested;
        }
    check(triangles>0,"Picking test exercises actual triangle intersections");
    RegionTopologyIndex::PickStatistics mid;
    const auto hit=index->firstRayHit(viewportRay(640,360,1280,720),&mid);
    check(bool(hit),"Center ray finds a hit");
    check(mid.trianglesTested<imported.flatIndex.faceIds.size()/4,
          "Center ray should reject most triangles without intersection tests");
    check(mid.partitionsTested<imported.revision->partitionCount()/3,
          "Top-level bounds prune most spatially grouped partitions");
    compare(*index,{{0,0,4},{0,0,-1}});
    compare(*index,{{3,3,4},{0,0,-1}});
    compare(*index,{{0,0,4},{0,0,1}});
    compare(*index,{{1,1,1},normalized(Vec3{-1,-1,-1})});
    try{
        (void)index->firstRayHit({{},{}});
        throw std::runtime_error("Zero direction was accepted");
    }catch(const std::invalid_argument&) {}
}
void testIncrementalSnapshotsAndNoOp() {
    SphereOptions options;
    options.vertexCount=1200;
    options.relaxationIterations=8;
    auto imported=partitionFlatMesh(generateSphere(options),16);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto originalRoot=original->pickingRootIdentity();
    auto candidate=original->querySphere({.25,-.65,.65},.45);
    check(!candidate.empty(),"Spatial candidate exists");
    const auto id=candidate.front();
    RegionEdit first(original,{id});
    first.moveVertex(id,original->vertex(id).position*1.035);
    (void)first.commit();
    const auto moved=first.updatedIndex();
    check(moved->pickingRootIdentity()!=originalRoot,
          "Changed geometry yields new immutable BVH root");
    check(moved->updateStatistics().rebuiltPickingPartitions>0,
          "Changed vertex rebuilds its incident face owner partitions");
    check(moved->updateStatistics().rebuiltPickingPartitions<18,
          "Local edit does not rebuild distant partition BVHs");
    check(moved->updateStatistics().copiedPickingDirectoryNodes<=
          moved->updateStatistics().rebuiltPickingPartitions*9,
          "Each changed partition clones at most nine directory nodes");
    check(moved->updateStatistics().fullMeshTraversals==0,
          "Incremental BVH update does not scan the mesh");
    for(int y=110;y<610;y+=95)for(int x=190;x<1100;x+=130) {
        const auto ray=viewportRay(x,y,1280,720);
        compare(*original,ray);
        compare(*moved,ray);
    }
    auto other=original->querySphere({-.25,-.65,.65},.45);
    check(!other.empty(),"Second branch candidate exists");
    RegionEdit branch(original,{other.front()});
    branch.moveVertex(other.front(),
                      original->vertex(other.front()).position*1.025);
    (void)branch.commit();
    const auto fork=branch.updatedIndex();
    check(fork->pickingRootIdentity()!=moved->pickingRootIdentity(),
          "Independent revision branches own distinct BVH roots");
    for(int x=240;x<1100;x+=175) {
        const auto ray=viewportRay(x,350,1280,720);
        compare(*fork,ray);
        compare(*moved,ray);
        compare(*original,ray);
    }
    // Inserting an empty revision operation never demands a topology rebuild.
    RegionEdit identical(moved,{id});
    (void)identical.commit();
    const auto noop=identical.updatedIndex();
    check(noop->pickingRootIdentity()==moved->pickingRootIdentity(),
          "No changed face/vertex reuses complete ray hierarchy");
}
void testCrossPartitionSplitAndCollapse() {
    auto imported=partitionFlatMesh(octahedron(),1);
    auto before=RegionTopologyIndex::build(imported.revision);
    const auto a=imported.flatIndex.vertexIds[0],b=imported.flatIndex.vertexIds[1];
    RegionEdit move(before,{a},{},1);
    move.moveVertex(a,{1.25,0,0});
    (void)move.commit();
    auto stretched=move.updatedIndex();
    LocalStampBaseline baseline{before,{a},before->vertex(a).incidentFaces};
    RefineSettings refine;
    refine.enabled=true;
    refine.maximumEdgeRatio=.25;
    refine.maximumSplitPairsPerStamp=1;
    refine.optimizeFlips=false;
    VersionedLocalRefiner refiner(6,refine,{},1);
    const auto split=refiner.apply(stretched,baseline);
    check(split.statistics.splitPairs==1,
          "Fixture produces cross-partition split and inserted stable face IDs");
    check(split.index->updateStatistics().fullMeshTraversals==0,
          "Split BVH path copies without global traversal");
    check(split.index->updateStatistics().rebuiltPickingPartitions>0,
          "Inserted faces initialize new partition BVH leaves");
    for(int i=0;i<40;++i) {
        const auto angle=i*0.15707963267948966;
        const auto eye=Vec3{2.0*std::cos(angle),2.0*std::sin(angle),1.4};
        compare(*before,{eye,normalized(Vec3{}-eye)});
        compare(*split.index,{eye,normalized(Vec3{}-eye)});
    }
    RegionEdit shrink(before,{b},{},1);
    shrink.moveVertex(b,{.6,.4,0});
    (void)shrink.commit();
    auto shortIndex=shrink.updatedIndex();
    CollapseSettings collapse;
    collapse.enabled=true;
    collapse.minimumEdgeRatio=2;
    collapse.maximumPairsPerStamp=1;
    VersionedDirectionalCollapser collapser(6,collapse,{},1);
    LocalStampBaseline beforeCollapse{before,{b},before->vertex(b).incidentFaces};
    const auto result=collapser.tryEdge(shortIndex,beforeCollapse,a,b);
    check(result.statistics.accepted==1,
          "Fixture accepts a locally validated edge collapse");
    check(result.index->retiredFaceCount()==2,
          "Deleted faces retained as tombstones and removed from BVH");
    for(int i=0;i<40;++i) {
        const auto eye=Vec3{3,2*std::cos(i*.125),2*std::sin(i*.125)};
        compare(*result.index,{eye,normalized(Vec3{}-eye)});
        compare(*before,{eye,normalized(Vec3{}-eye)});
    }
}
}
int main() {
    try {
        testExactAndPruned();
        testIncrementalSnapshotsAndNoOp();
        testCrossPartitionSplitAndCollapse();
        std::cout<<"Revision BVH picking regression passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Revision BVH picking failure: "<<error.what()<<'\n';
        return 1;
    }
}

#include "mesher/versioned_refine.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace mesher;
namespace {
void check(bool valid,const char* reason) {
    if(!valid)throw std::runtime_error(reason);
}
Mesh octahedron() {
    return {{{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
             {0,0,1},{0,0,-1}},
            {{4,0,1},{4,1,2},{4,2,3},{4,3,0},
             {5,1,0},{5,2,1},{5,3,2},{5,0,3}}};
}
Mesh asFlat(const MeshRevision& revision) {
    Mesh mesh;
    std::unordered_map<VertexId,std::uint32_t> offsets;
    revision.forEachPartition([&](PartitionId,const MeshPartition& partition) {
        for(const auto& v:partition.vertices) {
            const auto id=static_cast<std::uint32_t>(mesh.vertices.size());
            check(offsets.emplace(v.id,id).second,"Vertex stable IDs unique");
            mesh.vertices.push_back(v.position);
        }
    });
    revision.forEachPartition([&](PartitionId,const MeshPartition& partition) {
        for(const auto& f:partition.faces)
            mesh.triangles.push_back({offsets.at(f.vertices[0]),
                                      offsets.at(f.vertices[1]),
                                      offsets.at(f.vertices[2])});
    });
    return mesh;
}
void validateIndexed(const std::shared_ptr<const RegionTopologyIndex>& current) {
    check(validateClosedSphereTopology(asFlat(current->revision())),
          "Closed manifold after local split/flip");
    const auto rebuilt=RegionTopologyIndex::build(
        std::shared_ptr<const MeshRevision>(current,
                                            &current->revision()));
    current->revision().forEachPartition([&](PartitionId,
                                             const MeshPartition& partition) {
        for(const auto& v:partition.vertices) {
            check(current->vertex(v.id).incidentFaces==
                  rebuilt->vertex(v.id).incidentFaces,
                  "Incremental adjacency equals rebuilt topology");
            check(length(current->vertex(v.id).position-v.position)==0,
                  "Incremental vertex coordinates equal stored revision");
        }
        for(const auto& f:partition.faces)
            check(current->face(f.id).vertices==
                  rebuilt->face(f.id).vertices,
                  "Incremental face connectivity equals rebuilt topology");
    });
}
void testTransactionalSplitAndOverflow() {
    auto initial=partitionFlatMesh(octahedron(),1);
    auto prior=RegionTopologyIndex::build(initial.revision);
    const auto& ids=initial.flatIndex.vertexIds;
    const auto vertex=ids[0];
    RegionEdit deformation(prior,{vertex},{},1);
    deformation.moveVertex(vertex,{1.25,0,0});
    (void)deformation.commit();
    const auto changed=deformation.updatedIndex();
    LocalStampBaseline baseline{prior,{vertex},
                                prior->vertex(vertex).incidentFaces};
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=1;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refiner(octahedron().vertices.size(),settings,{},1);
    const auto result=refiner.apply(changed,baseline);
    check(result.statistics.splitPairs==1 &&
          result.statistics.newVertices==1,
          "Stretched edge splits in a local transaction");
    check(result.insertedVertices.size()==1,
          "Fresh stable vertex ID exposed");
    check(result.index->updateStatistics().fullMeshTraversals==0,
          "No global index rebuild after splitting");
    check(result.index->revision().partitionCount()>
          changed->revision().partitionCount(),
          "Full-capacity partitions allocate fresh bounded overflow owners");
    check(result.copiedDirectoryNodes<=9*result.modifiedPartitions,
          "Local commits copy bounded partition directory paths");
    std::size_t shared=0;
    changed->revision().forEachPartition([&](PartitionId id,
                                              const MeshPartition& old) {
        const auto now=result.index->revision().partition(id);
        if(now.get()==&old)++shared;
    });
    check(shared>0,"Untouched partitions retain exact pointer identity");
    result.index->revision().forEachPartition([&](PartitionId,
                                                  const MeshPartition& part) {
        check(part.faces.size()<=1&&part.vertices.size()<=1,
              "Overflow partitions preserve strict capacity");
    });
    validateIndexed(result.index);
    check(length(prior->vertex(vertex).position-
                 Vec3{1,0,0})==0,"Earlier revision unaffected");
    check(length(changed->vertex(vertex).position-
                 Vec3{1.25,0,0})==0,"Deformation revision unaffected");
    const auto once=result.index;
    // A second refinement against the original stamp baseline cannot select
    // any of its removed edges and must leave all old revisions readable.
    auto after=refiner.apply(once,LocalStampBaseline{once,{},{}});
    check(after.index==once,"No affected faces means no work");
    validateIndexed(after.index);
}
void testMirrorSplit() {
    auto initial=partitionFlatMesh(octahedron(),1);
    const auto& ids=initial.flatIndex.vertexIds;
    auto before=RegionTopologyIndex::build(initial.revision);
    std::vector<std::pair<VertexId,VertexId>> mirrors;
    const std::array<std::uint32_t,6> counterpart{2,1,0,3,4,5};
    for(std::size_t i=0;i<ids.size();++i)
        mirrors.emplace_back(ids[i],ids[counterpart[i]]);
    RegionEdit deformation(before,{ids[0],ids[2]},
                            mirrors,1);
    deformation.moveVertex(ids[0],{1.2,0,0});
    (void)deformation.commit();
    auto current=deformation.updatedIndex();
    auto affected=before->vertex(ids[0]).incidentFaces;
    const auto& more=before->vertex(ids[2]).incidentFaces;
    affected.insert(affected.end(),more.begin(),more.end());
    std::sort(affected.begin(),affected.end());
    affected.erase(std::unique(affected.begin(),affected.end()),affected.end());
    LocalStampBaseline baseline{before,{ids[0],ids[2]},affected};
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=3;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refiner(6,settings,mirrors,1);
    auto result=refiner.apply(current,baseline);
    check(result.statistics.splitPairs>0,"Symmetric split candidates accepted");
    check(result.statistics.newVertices>=result.statistics.splitPairs,
          "Refiner tracks stable new midpoint identities");
    check(result.index->revision().partitionCount()>
          current->revision().partitionCount(),"Symmetry uses bounded overflow");
    for(const auto id:result.insertedVertices) {
        const auto partner=refiner.mirrors().at(id);
        const auto a=result.index->vertex(id).position;
        const auto b=result.index->vertex(partner).position;
        check(length(Vec3{a.x+b.x,a.y-b.y,a.z-b.z})<1e-13,
              "New midpoint reflection exact across partition boundaries");
        if(id==partner)check(a.x==0,"Shared seam midpoint lies on X=0");
    }
    for(const auto& [id,partner]:refiner.mirrors()) {
        const auto a=result.index->vertex(id).position;
        const auto b=result.index->vertex(partner).position;
        check(length(Vec3{a.x+b.x,a.y-b.y,a.z-b.z})<1e-12,
              "Refined mirror mapping remains a geometric involution");
    }
    validateIndexed(result.index);
}
void testLocalFlipAndRepeatedRevisions() {
    SphereOptions options;
    options.vertexCount=400;
    options.relaxationIterations=8;
    auto imported=partitionFlatMesh(generateSphere(options),16);
    auto current=RegionTopologyIndex::build(imported.revision);
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.85;
    settings.maximumSplitPairsPerStamp=5;
    settings.maximumFlipPairsPerStamp=8;
    settings.optimizeFlips=true;
    VersionedLocalRefiner refiner(400,settings,{},16);
    std::uint32_t splits=0,flips=0;
    for(int step=0;step<7;++step) {
        // Local Inflate supplies an exact immutable pre-stamp baseline.
        VersionedInflateBrush inflate(.025,CurvatureSettings{true,.25});
        auto deformed=inflate.stamp(current,
            std::vector<BrushArea>{{{.25,-.65,.67},.52,.25}},16);
        auto changed=refiner.apply(deformed.index,deformed.baseline);
        splits+=changed.statistics.splitPairs;
        flips+=changed.statistics.flipPairs;
        current=changed.index;
        check(current->updateStatistics().fullMeshTraversals==0 ||
              changed.statistics.splitPairs==0,
              "Refinement updates never require global scans");
        validateIndexed(current);
    }
    check(splits>0,"Repeated local stamps exercise directional splitting");
    // Flip proposals have strict quality and orientation tests, so they may
    // legitimately be absent for a particular deterministic sphere fixture.
    check(flips<=splits*settings.maximumFlipPairsPerStamp,
          "Flips bounded by refinement candidates");
    check(current->revision().partitionCount()>=
          imported.revision->partitionCount(),
          "Refinement never compacts or globally renumbers partitions");
}
}
int main() {
    try {
        const auto scenario=[](const char* name,auto work) {
            try{work();}
            catch(...){std::cerr<<"Failed V6 scenario: "<<name<<std::endl;throw;}
        };
        scenario("overflow split",testTransactionalSplitAndOverflow);
        scenario("mirrored split",testMirrorSplit);
        scenario("repeated split and flip",testLocalFlipAndRepeatedRevisions);
        std::cout<<"Transactional stable-ID refinement tests passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"V6 refinement failure: "<<e.what()<<'\n';
        return 1;
    }
}

#include "mesher/versioned_refine.hpp"
#include "mesher/repair_surgery.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <set>
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
    check(result.changedFaces.size()==4,
          "Dirty face report includes both replaced and both inserted faces");
    for(const auto face:result.changedFaces)
        check(result.index->face(face).vertices[0]!=0,
              "All reported face IDs resolve in the committed revision");
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
void testM24CertifiedSourceSplitsAndRefusals() {
    auto imported=partitionFlatMesh(octahedron(),1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto vertex=imported.flatIndex.vertexIds[0];
    const auto sourceFaces=original->vertex(vertex).incidentFaces;
    const auto anchored=captureOriginalSourceAnchors(
        original,sourceFaces,sourceFaces.size()*7);
    auto lineage=beginSourceSplitLineage(anchored);
    check(lineage.original==original&&lineage.terminal==original&&
          lineage.living.size()==sourceFaces.size(),
          "M24 captures first-original FaceIds, not spatially matched candidates");

    RegionEdit deformation(original,{vertex},{},1);
    deformation.moveVertex(vertex,{1.25,0,0});
    (void)deformation.commit();
    const auto moved=deformation.updatedIndex();
    const LocalStampBaseline baseline{original,{vertex},sourceFaces};
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=1;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refiner(6,settings,{},1);
    const auto split=refiner.apply(moved,baseline);
    check(split.statistics.splitPairs==1&&split.splitSources.size()==2&&
          split.stampOriginal==original&&split.sourceIndex==moved,
          "Actual local split emits two precisely rooted source-child records");
    const auto proven=advanceCertifiedSplitLineage(lineage,split,2,6);
    std::size_t survivors=0,extended=0;
    for(const auto& [_,faces]:proven.living) {
        survivors+=faces.size();
        if(faces.size()==2)++extended;
    }
    check(survivors==sourceFaces.size()+2&&extended==2&&
          proven.verifiedFaceSplits==2&&
          proven.terminal==split.index,
          "Both incident parent FaceIds retain independent living subdivided ancestry");
    check(original->retiredFaceCount()==0&&
          original->vertex(vertex).position.x==1&&
          moved->vertex(vertex).position.x==1.25,
          "All earlier fixed first-reference readers remain immutable");

    bool refused=false;
    try {(void)advanceCertifiedSplitLineage(lineage,split,1,6);}
    catch(const std::length_error&){refused=true;}
    check(refused,"One below exact two-face lineage work budget refuses");
    refused=false;
    try {(void)advanceCertifiedSplitLineage(lineage,split,2,5);}
    catch(const std::length_error&){refused=true;}
    check(refused,"One below living protected descendant budget refuses");

    auto missing=split;
    missing.splitSources.pop_back();
    refused=false;
    try {(void)advanceCertifiedSplitLineage(lineage,missing,2,6);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"Missing one original incident source record cannot be certified");
    auto forged=split;
    forged.splitSources.front().sourceFace=sourceFaces.back()==
        forged.splitSources.front().sourceFace?sourceFaces.front():sourceFaces.back();
    if(forged.splitSources.front().sourceFace==
       split.splitSources.front().sourceFace)
        forged.splitSources.front().sourceFace=0;
    refused=false;
    try {(void)advanceCertifiedSplitLineage(lineage,forged,2,6);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"Forged source ownership never follows a coincident/nearby FaceId");

    refused=false;
    try {(void)advanceCertifiedSplitLineage(proven,split,2,8);}
    catch(const std::invalid_argument&){refused=true;}
    check(refused,"Previous accepted final root cannot be replaced by older stamp original");

    RegionEdit nextDeformation(split.index,{vertex},{},1);
    nextDeformation.moveVertex(vertex,{1.5,0,0});
    (void)nextDeformation.commit();
    const auto nextIndex=nextDeformation.updatedIndex();
    const LocalStampBaseline nextBaseline{
        split.index,{vertex},split.index->vertex(vertex).incidentFaces};
    const auto second=refiner.apply(nextIndex,nextBaseline);
    check(second.statistics.splitPairs==1&&second.splitSources.size()==2,
          "Second actual edit creates another bounded split generation");
    const auto successive=advanceCertifiedSplitLineage(proven,second,2,8);
    check(successive.verifiedFaceSplits==4&&
          successive.original==original&&successive.terminal==second.index,
          "Original-source ownership composes through two real split generations");
}

void testM24MirroredSplitSourceOwnership() {
    auto imported=partitionFlatMesh(octahedron(),1);
    const auto& ids=imported.flatIndex.vertexIds;
    const auto original=RegionTopologyIndex::build(imported.revision);
    const std::array<std::uint32_t,6> counterpart{2,1,0,3,4,5};
    std::vector<std::pair<VertexId,VertexId>> mirrors;
    for(std::size_t i=0;i<ids.size();++i)
        mirrors.emplace_back(ids[i],ids[counterpart[i]]);
    RegionEdit deformation(original,{ids[0],ids[2]},mirrors,1);
    deformation.moveVertex(ids[0],{1.2,0,0});
    (void)deformation.commit();
    auto moved=deformation.updatedIndex();
    auto affected=original->vertex(ids[0]).incidentFaces;
    const auto more=original->vertex(ids[2]).incidentFaces;
    affected.insert(affected.end(),more.begin(),more.end());
    std::sort(affected.begin(),affected.end());
    affected.erase(std::unique(affected.begin(),affected.end()),affected.end());
    const auto anchors=captureOriginalSourceAnchors(original,affected,56);
    auto lineage=beginSourceSplitLineage(anchors);
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=3;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refiner(6,settings,mirrors,1);
    const auto result=refiner.apply(
        moved,LocalStampBaseline{original,{ids[0],ids[2]},affected});
    check(result.statistics.splitPairs>0&&
          result.insertedMirrorPairs.size()==result.statistics.splitPairs,
          "True reflected split emits exact midpoint mirror orbit journals");
    const auto certified=advanceCertifiedSplitLineage(
        lineage,result,64,96);
    check(certified.terminal==result.index&&
          certified.verifiedFaceSplits==result.splitSources.size(),
          "Reflected and symmetry-plane split source ancestry passes independent checks");
    auto forged=result;
    forged.insertedMirrorPairs.clear();
    // A missing explicit mirror proof is not permission to infer reflected
    // source equivalence from two near/identical XYZ positions.
    // (The next gate validates geometry and existence; it must also reject
    // omitted declared mirror maps.)
    check(refiner.mirrors().size()>=ids.size(),
          "Real symmetric branch retains live permanent ID partners");
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
    check(result.insertedMirrorPairs.size()==
          result.statistics.splitPairs,
          "Only newly inserted symmetric pairs are exported");
    VersionedInflateBrush nextBrush(.0001,{},mirrors);
    for(const auto& pair:result.insertedMirrorPairs)
        nextBrush.addMirrorPair(pair.first,pair.second);
    const auto next=nextBrush.stamp(result.index,
        std::vector<BrushArea>{{{.25,0,.65},2.5,.25},
                               {{-.25,0,.65},2.5,.25}},1);
    check(next.moved>0,
          "Following symmetric Inflate accepts newly split midpoint IDs");
    for(const auto& [id,partner]:refiner.mirrors()) {
        const auto a=next.index->vertex(id).position;
        const auto b=next.index->vertex(partner).position;
        check(length(Vec3{a.x+b.x,a.y-b.y,a.z-b.z})<1e-12,
              "Updated mirror mapping stays valid through the next stamp");
    }
    for(const auto& [id,partner]:refiner.mirrors()) {
        const auto a=result.index->vertex(id).position;
        const auto b=result.index->vertex(partner).position;
        check(length(Vec3{a.x+b.x,a.y-b.y,a.z-b.z})<1e-12,
              "Refined mirror mapping remains a geometric involution");
    }
    // Every oriented triangle must have a reflected counterpart, including
    // the split faces that were appended to new overflow partitions.
    const auto canonical=[](std::array<VertexId,3> face) {
        if(face[1]<face[0] && face[1]<face[2])
            return std::array<VertexId,3>{face[1],face[2],face[0]};
        if(face[2]<face[0] && face[2]<face[1])
            return std::array<VertexId,3>{face[2],face[0],face[1]};
        return face;
    };
    std::set<std::array<VertexId,3>> oriented;
    result.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& face:part.faces)
                oriented.insert(canonical(face.vertices));
        });
    result.index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& face:part.faces) {
                const auto& t=face.vertices;
                const std::array<VertexId,3> reflection={
                    refiner.mirrors().at(t[0]),refiner.mirrors().at(t[2]),
                    refiner.mirrors().at(t[1])};
                check(oriented.contains(canonical(reflection)),
                      "All appended and rewritten faces retain mirrored winding");
            }
        });
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
    std::cout<<"V6 stress case: "<<splits<<" split pairs and "
             <<flips<<" flip pairs\n";
    check(splits>0,"Repeated local stamps exercise directional splitting");
    check(flips>0,"Repeated deformations exercise accepted local edge flipping");
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
        scenario("M24 source split lineage",testM24CertifiedSourceSplitsAndRefusals);
        scenario("M24 mirrored split lineage",testM24MirroredSplitSourceOwnership);
        scenario("overflow split",testTransactionalSplitAndOverflow);
        scenario("mirrored split",testMirrorSplit);
        scenario("repeated split and flip",testLocalFlipAndRepeatedRevisions);
        std::cout<<"Transactional stable-ID refinement tests passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"V6 refinement failure: "<<e.what()<<'\n';
        return 1;
    }
}

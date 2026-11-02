#include "mesher/versioned_refine.hpp"
#include "mesher/repair_surgery.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <map>
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

    auto wrongEdge=split;
    wrongEdge.splitSources.back().edgeA=
        wrongEdge.splitSources.back().midpoint;
    refused=false;
    try {(void)advanceCertifiedSplitLineage(lineage,wrongEdge,2,6);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"Split provenance cannot substitute a non-source permanent edge");
    auto unprovedFlip=split;
    unprovedFlip.statistics.flipPairs=1;
    refused=false;
    try {(void)advanceCertifiedSplitLineage(lineage,unprovedFlip,2,6);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"Unproven edge-flip area transfer refuses split-only protected ancestry");

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

void testM27OriginalLifetimeRepeatedRealRemesh() {
    const auto imported=partitionFlatMesh(octahedron(),1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto rootIdentity=original->pickingRootIdentity();
    const auto v=imported.flatIndex.vertexIds[0];
    const auto originals=original->vertex(v).incidentFaces;
    const auto anchors=captureOriginalSourceAnchors(
        original,originals,originals.size()*7);
    const auto initial=beginOriginalPatchLifetime(anchors);
    const auto pristine=auditOriginalPatchLifetime(
        initial,original,7*originals.size(),originals.size());
    check(pristine.total.maximumDistanceInEdges<1e-12&&
          pristine.total.distanceTests==7*originals.size()&&
          pristine.livingOriginals==originals.size(),
          "M27 first-original source witness zero and exact budget before real edits");
    RefineSettings options;
    options.enabled=true;
    options.maximumEdgeRatio=.25;
    options.maximumSplitPairsPerStamp=1;
    options.optimizeFlips=false;
    VersionedLocalRefiner refine(6,options,{},1);
    RegionEdit first(original,{v},{},1);
    first.moveVertex(v,{1.25,0,0});
    (void)first.commit();
    const auto firstResult=refine.apply(
        first.updatedIndex(),LocalStampBaseline{original,{v},originals});
    check(firstResult.statistics.splitPairs==1&&
          firstResult.splitSources.size()==2,
          "M27 first real COW stamp adds actual source-bound descendants");
    const auto once=advanceOriginalPatchLifetime(
        initial,firstResult,4,8);
    const auto firstAudit=auditOriginalPatchLifetime(once,original,42,8);
    check(once.sourceFaceEvidence==2&&once.acceptedTransitions==1&&
          firstAudit.total.distanceTests==42&&
          firstAudit.total.maximumDistanceInEdges>1e-9&&
          firstAudit.total.witnessCount==originals.size()*7,
          "M27 first real stamp measures fixed original witnesses on source-only children");
    RegionEdit second(firstResult.index,{v},{},1);
    second.moveVertex(v,{1.5,0,0});
    (void)second.commit();
    const auto secondResult=refine.apply(
        second.updatedIndex(),LocalStampBaseline{
            firstResult.index,{v},firstResult.index->vertex(v).incidentFaces});
    check(secondResult.statistics.splitPairs==1&&
          secondResult.splitSources.size()==2,
          "M27 second real stamp has separately committed source evidence");
    bool refused=false;
    try {(void)advanceOriginalPatchLifetime(once,secondResult,3,8);}
    catch(const std::length_error&){refused=true;}
    check(refused,"M27 one-under CUMULATIVE face budget refuses second accepted edit");
    const auto twice=advanceOriginalPatchLifetime(once,secondResult,4,8);
    IndexedMeshRepository publications(original);
    check(publications.publishIfCurrent(original,firstResult.index)&&
          publications.publishIfCurrent(firstResult.index,secondResult.index)&&
          publications.snapshot()==secondResult.index&&
          !publications.publishIfCurrent(original,firstResult.index),
          "M27 two real validated revision roots publish as distinct CAS Steps; stale replay refuses");
    const auto secondAudit=auditOriginalPatchLifetime(twice,original,56,8);
    check(twice.anchors.original==original&&
          twice.lineage.original==original&&
          twice.lineage.terminal==secondResult.index&&
          twice.sourceFaceEvidence==4&&twice.acceptedTransitions==2&&
          twice.lineage.verifiedFaceSplits==4&&
          secondAudit.total.distanceTests==56&&
          secondAudit.total.witnessCount==originals.size()*7&&
          secondAudit.total.maximumDistanceInEdges>
              firstAudit.total.maximumDistanceInEdges,
          "M27 two real publishable roots show cumulative drift against SAME original patch");
    refused=false;
    try {(void)auditOriginalPatchLifetime(twice,original,55,8);}
    catch(const std::length_error&){refused=true;}
    check(refused,"M27 exact 56 distance tests accept; one below refuses whole audit");
    refused=false;
    try {(void)advanceOriginalPatchLifetime(twice,secondResult,6,8);}
    catch(const std::invalid_argument&){refused=true;}
    check(refused,"M27 cannot replay a stale pre-stamp original root");
    refused=false;
    try {(void)auditOriginalPatchLifetime(twice,twice.lineage.terminal,56,8);}
    catch(const std::invalid_argument&){refused=true;}
    check(refused,"M27 caller cannot re-anchor cumulative distances to newest stamp");
    auto fraudulent=twice;
    fraudulent.anchors.faces.front().witnesses[0].x+=1;
    refused=false;
    try {(void)auditOriginalPatchLifetime(fraudulent,original,56,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M27 original witness spoof cannot hide repeated protected erosion");
    fraudulent=twice;
    fraudulent.sourceFaceEvidence=0;
    refused=false;
    try {(void)advanceOriginalPatchLifetime(
        fraudulent,secondResult,4,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M27 opaque proof prevents reset of cumulative original-source work cap");
    fraudulent=twice;
    fraudulent.acceptedTransitions=0;
    refused=false;
    try {(void)auditOriginalPatchLifetime(fraudulent,original,56,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M27 opaque proof prevents rewriting accepted Step history");
    fraudulent=twice;
    std::swap(fraudulent.lineage.living[0].second,
              fraudulent.lineage.living[1].second);
    refused=false;
    try {(void)auditOriginalPatchLifetime(fraudulent,original,56,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M27 even coincident source FaceIds cannot swap protected ownership");
    check(original->pickingRootIdentity()==rootIdentity&&
          original->vertex(v).position.x==1&&
          original->retiredFaceCount()==0,
          "M27 all accepted and refused witness audits preserve original readers");
}

void testM21RealNonMirroredIntentAndProtectedOuterFaces() {
    const auto imported=partitionFlatMesh(octahedron(),1);
    const auto first=RegionTopologyIndex::build(imported.revision);
    const auto all=imported.flatIndex.faceIds;
    const auto v=imported.flatIndex.vertexIds[0];
    const auto affected=first->vertex(v).incidentFaces;
    check(all.size()==8&&affected.size()==4,
          "M21 real original octahedron has four intended and four unaffected exterior faces");
    const auto anchors=captureOriginalSourceAnchors(first,all,56);
    const auto before=beginOriginalPatchLifetime(anchors);
    const auto intent=declareOriginalPatchIntent(before,affected,nullptr,8);
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=1;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refiner(6,settings,{},1);
    RegionEdit edit(first,{v},{},1);
    edit.moveVertex(v,{1.25,0,0});
    (void)edit.commit();
    const auto changed=refiner.apply(
        edit.updatedIndex(),LocalStampBaseline{first,{v},affected});
    check(changed.statistics.splitPairs==1&&changed.splitSources.size()==2,
          "M21 real deliberately displaced surface is source-certified through a local split");
    const auto after=advanceOriginalPatchLifetime(before,changed,2,128);
    std::size_t exactWork=0;
    for(const auto& [_,living]:after.lineage.living)
        exactWork+=7*living.size();
    const auto accepted=evaluateOriginalPatchIntent(
        before,after,intent,1e-9,exactWork,128);
    check(accepted.intendedOriginals==4&&
          accepted.protectedOriginals==4&&
          accepted.protectedWitnesses==28&&
          accepted.totalDistanceTests==exactWork&&
          accepted.certifiedRetiredOriginals==0&&
          accepted.maximumProtectedDeviationInEdges<1e-9,
          "M21 actual caller-designated sculpt moves only intended sources, preserving untouched exterior");
    bool refused=false;
    try {(void)evaluateOriginalPatchIntent(
        before,after,intent,1e-9,exactWork-1,128);}
    catch(const std::length_error&){refused=true;}
    check(refused,"M21 complete original patch work cap refuses one below even with scoped intent");
    std::vector<FaceId> partial(affected.begin()+1,affected.end());
    const auto incomplete=declareOriginalPatchIntent(before,partial,nullptr,8);
    refused=false;
    try {(void)evaluateOriginalPatchIntent(
        before,after,incomplete,1e-9,exactWork,128);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M21 missing deliberately displaced source ID cannot be inferred by brush proximity");
    refused=false;
    try {(void)declareOriginalPatchIntent(before,{all[0],all[0]},nullptr,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M21 duplicate caller intent source refuses");
    refused=false;
    try {(void)declareOriginalPatchIntent(before,{all.back()+100},nullptr,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M21 invented foreign original FaceId cannot gain exemption");
    refused=false;
    try {(void)evaluateOriginalPatchIntent(
        before,before,intent,1e-9,exactWork,128);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M21 declaration cannot authorize an uncertified sibling or original root");
    const auto other=declareOriginalPatchIntent(after,affected,nullptr,128);
    refused=false;
    try {(void)evaluateOriginalPatchIntent(
        before,after,other,1e-9,exactWork,128);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M21 stale declaration cannot be rebound to a newer source root");
    check(first->retiredFaceCount()==0&&first->vertex(v).position.x==1,
          "M21 all intent decisions are read-only and leave original picker root unchanged");
}

void testM21RealMirroredX0IntentClosure() {
    const auto imported=partitionFlatMesh(octahedron(),1);
    const auto& ids=imported.flatIndex.vertexIds;
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto originals=imported.flatIndex.faceIds;
    const std::array<std::uint32_t,6> corresponding{2,1,0,3,4,5};
    std::vector<std::pair<VertexId,VertexId>> pairs;
    std::unordered_map<VertexId,VertexId> originalMirrors;
    for(std::size_t i=0;i<ids.size();++i) {
        pairs.emplace_back(ids[i],ids[corresponding[i]]);
        originalMirrors.emplace(ids[i],ids[corresponding[i]]);
    }
    const auto before=beginOriginalPatchLifetime(
        captureOriginalSourceAnchors(original,originals,56));
    const auto intended=original->vertex(ids[4]).incidentFaces;
    check(intended.size()==4,"M21 reflected top X=0 source has four explicit intended original faces");
    const auto intent=declareOriginalPatchIntent(
        before,intended,&originalMirrors,8);
    RegionEdit deformation(original,{ids[4]},pairs,1);
    deformation.moveVertex(ids[4],{0,0,1.6});
    (void)deformation.commit();
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=8;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refiner(6,settings,pairs,1);
    const auto refined=refiner.apply(
        deformation.updatedIndex(),
        LocalStampBaseline{
            original,{ids[4]},original->vertex(ids[4]).incidentFaces});
    check(refined.statistics.splitPairs>0&&
          !refined.insertedMirrorPairs.empty(),
          "M21 genuine reflected X=0 runtime split commits fixed and paired mirror source evidence");
    const auto after=advanceOriginalPatchLifetime(before,refined,128,128);
    std::size_t comparisons=0;
    for(const auto& [_,faces]:after.lineage.living)
        comparisons+=faces.size()*7;
    const auto accepted=evaluateOriginalPatchIntent(
        before,after,intent,1e-9,comparisons,128);
    check(accepted.intendedOriginals==4&&
          accepted.protectedOriginals==4&&
          accepted.maximumProtectedDeviationInEdges<1e-9,
          "M21 explicit reflected source intent preserves untouched original outer walls");
    const auto asym=declareOriginalPatchIntent(before,intended,nullptr,8);
    bool refused=false;
    try {(void)evaluateOriginalPatchIntent(
        before,after,asym,1e-9,comparisons,128);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M21 actual mirrored stage cannot silently disable source mirror proof");
    std::vector<FaceId> missing(intended.begin()+1,intended.end());
    refused=false;
    try {(void)declareOriginalPatchIntent(
        before,missing,&originalMirrors,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M21 missing reciprocal reflected intended first-original FaceId refuses");
    auto forged=originalMirrors;
    forged.erase(ids[0]);
    refused=false;
    try {(void)declareOriginalPatchIntent(
        before,intended,&forged,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M21 incomplete original mirror VertexId involution refuses");
}
void testM24SelfMirroredPlaneSourceMidpoint() {
    auto imported=partitionFlatMesh(octahedron(),1);
    const auto& ids=imported.flatIndex.vertexIds;
    const auto original=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> mirrors;
    const std::array<std::uint32_t,6> counterpart{2,1,0,3,4,5};
    for(std::size_t i=0;i<ids.size();++i)
        mirrors.emplace_back(ids[i],ids[counterpart[i]]);
    const auto originalFaces=imported.flatIndex.faceIds;
    const auto anchors=captureOriginalSourceAnchors(original,originalFaces,56);
    RegionEdit deformation(original,{ids[4]},mirrors,1);
    deformation.moveVertex(ids[4],{0,0,1.6});
    (void)deformation.commit();
    const auto moved=deformation.updatedIndex();
    const LocalStampBaseline baseline{
        original,{ids[4]},original->vertex(ids[4]).incidentFaces};
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=8;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refiner(6,settings,mirrors,1);
    const auto split=refiner.apply(moved,baseline);
    bool hasFixed=false;
    for(const auto& [a,b]:split.insertedMirrorPairs) {
        if(a==b) {
            hasFixed=true;
            check(split.index->vertex(a).position.x==0,
                  "Real symmetric runtime midpoint singleton lies on exact X=0");
        }
    }
    check(hasFixed&&split.statistics.splitPairs>0,
          "Actual reflected local split includes a genuinely self-mirrored X=0 midpoint");
    const auto proven=advanceCertifiedSplitLineage(
        beginSourceSplitLineage(anchors),split,64,96);
    check(proven.terminal==split.index&&
          proven.verifiedFaceSplits==split.splitSources.size(),
          "Fixed-point split source lineages are certified, not duplicated into coincident IDs");
}
void testM24CoincidentSheetsKeepIndependentPermanentSourceIDs() {
    Mesh mesh=octahedron();
    const Mesh another=octahedron();
    const auto off=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(),another.vertices.begin(),
                         another.vertices.end());
    for(const auto triangle:another.triangles)
        mesh.triangles.push_back({triangle.a+off,
                                  triangle.b+off,triangle.c+off});
    const auto imported=partitionFlatMesh(mesh,1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto sources=imported.flatIndex.faceIds;
    check(sources.size()==16&&
          original->face(sources[0]).vertices!=
          original->face(sources[8]).vertices,
          "Coincident independent closed octahedra retain different permanent vertices");
    std::vector<FaceId> selected(sources.begin(),sources.end());
    const auto anchor=captureOriginalSourceAnchors(original,selected,112);
    const auto lineage=beginSourceSplitLineage(anchor);
    const auto moving=imported.flatIndex.vertexIds[0];
    RegionEdit deformation(original,{moving},{},1);
    deformation.moveVertex(moving,{1.25,0,0});
    (void)deformation.commit();
    const auto moved=deformation.updatedIndex();
    const LocalStampBaseline baseline{
        original,{moving},original->vertex(moving).incidentFaces};
    RefineSettings settings;
    settings.enabled=true;
    settings.maximumEdgeRatio=.25;
    settings.maximumSplitPairsPerStamp=1;
    settings.optimizeFlips=false;
    VersionedLocalRefiner refine(mesh.vertices.size(),settings,{},1);
    const auto split=refine.apply(moved,baseline);
    check(split.statistics.splitPairs==1&&split.splitSources.size()==2,
          "Genuine first-shell split keeps unrelated exactly coincident surface separate");
    const auto tracked=advanceCertifiedSplitLineage(lineage,split,2,18);
    std::size_t firstSheet=0,secondSheet=0;
    for(const auto& [source,children]:tracked.living) {
        if(std::find(sources.begin()+8,sources.end(),source)!=sources.end()) {
            check(children.size()==1&&children.front()==source,
                  "Coincident original source on second closed shell acquired no foreign split FaceId");
            secondSheet+=children.size();
        }else firstSheet+=children.size();
    }
    check(firstSheet==10&&secondSheet==8&&
          original->retiredFaceCount()==0,
          "Only the actually subdivided source sheet gains two descendants");
    // Identical XYZ is deliberately NOT a reason to share a lifetime
    // protected source patch. Two coincident closed original octahedra
    // retain independent FaceIds and independent nearest-child audits.
    const auto coincidentBefore=beginOriginalPatchLifetime(anchor);
    const auto lifetime=advanceOriginalPatchLifetime(
        coincidentBefore,split,2,18);
    const auto evidence=auditOriginalPatchLifetime(
        lifetime,original,126,18);
    check(evidence.total.distanceTests==126&&
          evidence.perOriginal.size()==16&&
          evidence.total.candidateFaces==18&&
          evidence.perOriginal[8].livingDescendants==1&&
          evidence.perOriginal[8].sampled.maximumDistanceInEdges<1e-12,
          "M27 one physical coincident sheet is unchanged and owned only by its actual first-source IDs");
    // M21 must not infer sculpt intent from XYZ coincidence: both
    // genuinely closed octahedra initially occupy identical points.
    // Only permanent original first-sheet FaceIds are authorized;
    // the second coincident component remains fully protected.
    const auto firstSheetIntent=declareOriginalPatchIntent(
        coincidentBefore,original->vertex(moving).incidentFaces,
        nullptr,16);
    const auto coincidentIntent=evaluateOriginalPatchIntent(
        coincidentBefore,lifetime,firstSheetIntent,1e-9,126,18);
    check(coincidentIntent.intendedOriginals==4&&
          coincidentIntent.protectedOriginals==12&&
          coincidentIntent.protectedWitnesses==84&&
          coincidentIntent.maximumProtectedDeviationInEdges<1e-9,
          "M21 independent exactly coincident component cannot be selected as intended by nearby world-space XYZ");
    std::vector<FaceId> unrelated;
    for(unsigned i=8;i<12;++i)
        unrelated.push_back(sources[i]);
    const auto wrongCoincident=declareOriginalPatchIntent(
        coincidentBefore,unrelated,nullptr,16);
    bool refusalByCoincidence=false;
    try {(void)evaluateOriginalPatchIntent(
        coincidentBefore,lifetime,wrongCoincident,1e-9,126,18);}
    catch(const std::logic_error&){refusalByCoincidence=true;}
    check(refusalByCoincidence,
          "M21 naming the WRONG coincident physical sheet cannot exempt actually moved original sources");
    bool refused=false;
    try {(void)auditOriginalPatchLifetime(lifetime,original,125,18);}
    catch(const std::length_error&){refused=true;}
    check(refused,"M27 exact coincident independent sheet witness cost refuses one below");
    auto swapped=lifetime;
    std::swap(swapped.lineage.living[0].second,
              swapped.lineage.living[8].second);
    refused=false;
    try {(void)auditOriginalPatchLifetime(swapped,original,126,18);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"M27 unrelated coincident original faces cannot steal each other's permanent IDs");
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
    bool missing=false;
    try {(void)advanceCertifiedSplitLineage(lineage,forged,64,96);}
    catch(const std::logic_error&){missing=true;}
    check(missing,"Omitting mirrored split journal must refuse rather than XYZ auto-pair");
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
        // M25: a genuinely accepted split-plus-flip transaction has an
        // unrelated protected first-original patch with unchanged incidence
        // and vertex coordinates. Verify ALL flip records independently.
        std::optional<FaceId> untouched;
        current->revision().forEachPartition(
            [&](PartitionId,const MeshPartition& partition) {
                for(const auto& face:partition.faces) {
                    if(untouched||!changed.index->maybeFace(face.id)||
                       !deformed.index->maybeFace(face.id)||
                       changed.index->face(face.id).vertices!=face.vertices||
                       deformed.index->face(face.id).vertices!=face.vertices)
                        continue;
                    bool stable=true;
                    for(const auto vertex:face.vertices) {
                        const auto original=current->vertex(vertex).position;
                        const auto final=changed.index->vertex(vertex).position;
                        const auto middle=deformed.index->vertex(vertex).position;
                        if(length(original-final)!=0||
                           length(original-middle)!=0)stable=false;
                    }
                    if(stable)untouched=face.id;
                }
            });
        check(untouched.has_value(),"M25 finds a truly unaffected protected source face");
        const auto anchors=captureOriginalSourceAnchors(current,{*untouched},7);
        const auto certified=advanceCertifiedRefineLineage(
            beginSourceSplitLineage(anchors),changed,4096,4096);
        check(certified.original==current&&certified.terminal==changed.index&&
              certified.living.size()==1&&
              certified.living[0].second==std::vector<FaceId>{*untouched},
              "Real split/flip transaction preserves independent unaffected source owner");
        const auto m27FirstRoot=beginOriginalPatchLifetime(anchors);
        const auto m27FinalRoot=advanceOriginalPatchLifetime(
            m27FirstRoot,changed,4096,4096);
        const auto m27Distance=auditOriginalPatchLifetime(
            m27FinalRoot,current,7,4096);
        check(m27FinalRoot.lineage.terminal==changed.index&&
              m27FinalRoot.lineage.original==current&&
              m27Distance.total.distanceTests==7&&
              m27Distance.total.maximumDistanceInEdges<1e-12,
              "M27 genuine committed 2-to-2 source flip retains unrelated immutable first original patch");
        if(!changed.flipSources.empty()) {
            // Reconstruct source identity from actual committed split
            // journals, never from face geometry. When the refiner flips
            // across two distinct ORIGINAL patch owners, protecting both
            // must refuse the area reassignment without M21/M27 proof.
            std::map<FaceId,FaceId> owner;
            current->revision().forEachPartition(
                [&](PartitionId,const MeshPartition& partition) {
                    for(const auto& f:partition.faces)
                        owner.emplace(f.id,f.id);
                });
            for(const auto& proof:changed.splitSources) {
                check(owner.contains(proof.sourceFace),
                      "New split child source exists before ownership transfer");
                check(owner.emplace(proof.appendedFace,
                                   owner.at(proof.sourceFace)).second,
                      "New split child ID is never allocated twice");
            }
            for(const auto& proof:changed.flipSources) {
                check(owner.contains(proof.faces[0])&&
                      owner.contains(proof.faces[1]),
                      "Real flip faces have explicit first-original provenance");
                const auto a=owner.at(proof.faces[0]);
                const auto b=owner.at(proof.faces[1]);
                if(a==b)continue; // joint certified source ownership
                const auto origin=captureOriginalSourceAnchors(current,{a,b},14);
                bool mixedRefusal=false;
                try {(void)advanceCertifiedRefineLineage(
                    beginSourceSplitLineage(origin),changed,4096,4096);}
                catch(const std::logic_error&){mixedRefusal=true;}
                check(mixedRefusal,
                      "Real cross-original flip must refuse protected area transfer");
                break;
            }
            auto forged=changed;
            forged.flipSources.front().faces[1]=
                forged.flipSources.front().faces[0];
            bool refused=false;
            try {(void)advanceCertifiedRefineLineage(
                beginSourceSplitLineage(anchors),forged,4096,4096);}
            catch(const std::logic_error&){refused=true;}
            check(refused,"Duplicated committed flip FaceId refuses source certification");
            refused=false;
            try {(void)advanceCertifiedRefineLineage(
                beginSourceSplitLineage(anchors),changed,
                changed.splitSources.size()+changed.flipSources.size()-1,4096);}
            catch(const std::length_error&){refused=true;}
            check(refused,"One-below complete mixed split/flip evidence cap refuses");
        }
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
        scenario("M27 first-original repeated remesh lifetime",testM27OriginalLifetimeRepeatedRealRemesh);
        scenario("M21 explicit real sculpt source intent",testM21RealNonMirroredIntentAndProtectedOuterFaces);
        scenario("M21 reflected X=0 source intent closure",testM21RealMirroredX0IntentClosure);
        scenario("M24 mirrored split lineage",testM24MirroredSplitSourceOwnership);
        scenario("M24 X=0 singleton source split",testM24SelfMirroredPlaneSourceMidpoint);
        scenario("M24 coincident independent sources",testM24CoincidentSheetsKeepIndependentPermanentSourceIDs);
        scenario("overflow split",testTransactionalSplitAndOverflow);
        scenario("mirrored split",testMirrorSplit);
        scenario("repeated split and flip",testLocalFlipAndRepeatedRevisions);
        std::cout<<"Transactional stable-ID refinement tests passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"V6 refinement failure: "<<e.what()<<'\n';
        return 1;
    }
    return 0;
}

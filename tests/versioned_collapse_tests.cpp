#include "mesher/versioned_collapse.hpp"
#include "mesher/versioned_refine.hpp"
#include "mesher/repair_surgery.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
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
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
Mesh octahedron() {
    return {{{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
             {0,0,1},{0,0,-1}},
            {{4,0,1},{4,1,2},{4,2,3},{4,3,0},
             {5,1,0},{5,2,1},{5,3,2},{5,0,3}}};
}
Mesh dynamicFlat(const MeshRevision& revision) {
    Mesh output;
    std::unordered_map<VertexId,std::uint32_t> indices;
    revision.forEachPartition([&](PartitionId,const MeshPartition& partition) {
        for(const auto& vertex:partition.vertices) {
            const auto id=static_cast<std::uint32_t>(output.vertices.size());
            check(indices.emplace(vertex.id,id).second,
                  "Stable vertex IDs unique during independent export");
            output.vertices.push_back(vertex.position);
        }
    });
    revision.forEachPartition([&](PartitionId,const MeshPartition& partition) {
        for(const auto& face:partition.faces)
            output.triangles.push_back({
                indices.at(face.vertices[0]),indices.at(face.vertices[1]),
                indices.at(face.vertices[2])});
    });
    return output;
}
void verify(const std::shared_ptr<const RegionTopologyIndex>& index) {
    auto flat=dynamicFlat(index->revision());
    check(validateClosedSphereTopology(flat),
          "Local collapse preserves closed oriented genus-zero topology");
    auto shared=std::shared_ptr<const MeshRevision>(index,&index->revision());
    auto rebuilt=RegionTopologyIndex::build(shared);
    index->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& partition) {
        for(const auto& v:partition.vertices) {
            check(index->vertex(v.id).incidentFaces==
                  rebuilt->vertex(v.id).incidentFaces,
                  "Local vertex connectivity agrees with full independent rebuild");
            check(length(index->vertex(v.id).position-v.position)==0,
                  "Persistent vertex position matches revision");
        }
        for(const auto& f:partition.faces)
            check(index->face(f.id).vertices==rebuilt->face(f.id).vertices,
                  "Local face index agrees with full independent rebuild");
    });
}
void testNoOpAndOneCollapse() {
    auto original=octahedron();
    auto imported=partitionFlatMesh(original,1);
    auto before=RegionTopologyIndex::build(imported.revision);
    CollapseSettings setting;
    setting.enabled=true;
    setting.minimumEdgeRatio=2.0;
    setting.maximumPairsPerStamp=1;
    VersionedDirectionalCollapser c(original.vertices.size(),setting,{},1);
    LocalStampBaseline empty{before,{},{}};
    auto stationary=c.tryEdge(before,empty,imported.flatIndex.vertexIds[0],
                             imported.flatIndex.vertexIds[1]);
    check(stationary.statistics.accepted==0 && stationary.index==before,
          "Already short stationary edges must not be collapsed");

    const VertexId a=imported.flatIndex.vertexIds[0];
    const VertexId b=imported.flatIndex.vertexIds[1];
    const Vec3 midpoint=original.vertices[0]*.6+original.vertices[1]*.4;
    RegionEdit deformation(before,{b},{},1);
    deformation.moveVertex(b,midpoint);
    (void)deformation.commit();
    const auto after=deformation.updatedIndex();
    LocalStampBaseline baseline{before,{b},
                                before->vertex(b).incidentFaces};
    const auto collapsed=c.tryEdge(after,baseline,a,b);
    check(collapsed.statistics.attempted==1,
          "Directional compressed edge must be considered");
    check(collapsed.statistics.accepted==1 &&
          collapsed.statistics.removedVertices==1,
          "One safe compressed edge collapses without global compaction");
    check(collapsed.removedVertices.size()==1 &&
          collapsed.removedFaces.size()==2,
          "Collapse tombstones one vertex and its two vanished triangles");
    check(collapsed.index->retiredVertex(collapsed.removedVertices.front()),
          "Removed stable vertex ID retained as immutable tombstone");
    for(const auto face:collapsed.removedFaces)
        check(collapsed.index->retiredFace(face),
              "Removed stable face IDs retained as immutable tombstones");
    check(collapsed.index->retiredVertexCount()==1 &&
          collapsed.index->retiredFaceCount()==2,
          "Persistent tombstone counts reflect the local collapse");
    check(collapsed.index->updateStatistics().fullMeshTraversals==0,
          "No global mesh pass during local collapse index update");
    check(collapsed.copiedDirectoryNodes<=
          9*collapsed.modifiedPartitions,
          "Commit cost bounded by dirty partition directory paths");
    std::size_t sharedPartitions=0;
    after->revision().forEachPartition([&](PartitionId owner,
                                           const MeshPartition& partition) {
        const auto current=collapsed.index->revision().partition(owner);
        if(current.get()==&partition)++sharedPartitions;
    });
    check(sharedPartitions>0,"Unmodified partition pointers remain shared");
    verify(collapsed.index);
    verify(before);
    verify(after);
    check(before->maybeVertex(b)!=nullptr&&
          after->maybeVertex(b)!=nullptr,
          "Old revisions retain the vertex erased from the latest root");

    // Tombstone IDs are not recycled by a subsequent versioned insertion.
    RegionEdit growth(collapsed.index,{a},{},1);
    const auto newId=growth.appendLocalVertex(
        collapsed.index->vertex(a).owner,{2,0,0});
    check(newId>b&&newId>a,"Stable IDs never recycled after local collapse");
    // Growth is deliberately discarded: it would otherwise add an orphan.
}
void testM25ConservativeProtectedCollapseProof() {
    // Two permanent-ID-distinct closed components: a real compressed
    // collapse touches only the first, while source anchors protect the
    // untouched second. No inferred spatial or global ownership transfer.
    Mesh mesh=octahedron();
    const auto copy=octahedron();
    const auto offset=static_cast<std::uint32_t>(mesh.vertices.size());
    for(auto p:copy.vertices) {
        p.x+=10;
        mesh.vertices.push_back(p);
    }
    for(const auto face:copy.triangles)
        mesh.triangles.push_back({face.a+offset,face.b+offset,face.c+offset});
    const auto imported=partitionFlatMesh(mesh,1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    CollapseSettings settings;
    settings.enabled=true;
    settings.minimumEdgeRatio=2;
    settings.maximumPairsPerStamp=1;
    VersionedDirectionalCollapser collapser(mesh.vertices.size(),settings,{},1);
    const VertexId a=imported.flatIndex.vertexIds[0];
    const VertexId b=imported.flatIndex.vertexIds[1];
    const Vec3 movedPos=mesh.vertices[0]*.6+mesh.vertices[1]*.4;
    RegionEdit deformation(original,{b},{},1);
    deformation.moveVertex(b,movedPos);
    (void)deformation.commit();
    const auto deformed=deformation.updatedIndex();
    LocalStampBaseline baseline{original,{b},original->vertex(b).incidentFaces};
    const auto collapsed=collapser.tryEdge(deformed,baseline,a,b);
    check(collapsed.statistics.accepted==1&&
          collapsed.collapseSources.size()==1&&
          collapsed.collapseSources[0].retiredFaces.size()==2,
          "M25 real source-collapse journal captures committed 2-face retirement");
    std::vector<FaceId> farSources;
    for(std::size_t i=8;i<16;++i)
        farSources.push_back(imported.flatIndex.faceIds[i]);
    const auto anchors=captureOriginalSourceAnchors(original,farSources,56);
    const auto lineage=beginSourceSplitLineage(anchors);
    const auto proved=advanceCertifiedCollapseLineage(
        lineage,collapsed,32,8);
    check(proved.original==original&&proved.terminal==collapsed.index&&
          proved.living==lineage.living&&
          original->maybeVertex(b)!=nullptr&&collapsed.index->retiredVertex(b),
          "Real collapse leaves all protected first-original FaceIds and old readers intact");
    const auto& event=collapsed.collapseSources.front();
    const auto touched=event.before->vertex(event.survivor).incidentFaces.size()+
                       event.before->vertex(event.retired).incidentFaces.size();
    bool refused=false;
    try {(void)advanceCertifiedCollapseLineage(
        lineage,collapsed,touched-1,8);}
    catch(const std::length_error&){refused=true;}
    // The actual unique touched set is smaller than the sum above; compare
    // against the independently measured unique stable-ID face halo.
    std::set<FaceId> actualTouched;
    for(const auto id:{event.survivor,event.retired})
        for(const auto face:event.before->vertex(id).incidentFaces)
            actualTouched.insert(face);
    const auto m27Before=beginOriginalPatchLifetime(anchors);
    const auto m27After=advanceOriginalPatchLifetime(
        m27Before,collapsed,actualTouched.size(),8);
    const auto m27Audit=auditOriginalPatchLifetime(
        m27After,original,56,8);
    check(m27After.anchors.original==original&&
          m27After.lineage.terminal==collapsed.index&&
          m27After.sourceFaceEvidence==actualTouched.size()&&
          m27After.acceptedTransitions==1&&
          m27Audit.total.distanceTests==56&&
          m27Audit.total.maximumDistanceInEdges<1e-12,
          "M27 real certified collapse preserves unchanged distant first-original patch");
    refused=false;
    try {(void)advanceOriginalPatchLifetime(
        m27Before,collapsed,actualTouched.size()-1,8);}
    catch(const std::length_error&){refused=true;}
    check(refused,"M27 real collapse exact source-halo work cap refuses one below");
    refused=false;
    try {(void)auditOriginalPatchLifetime(m27After,original,55,8);}
    catch(const std::length_error&){refused=true;}
    check(refused,"M27 real collapse seven-witness distance budget refuses one below");
    refused=false;
    try {(void)advanceCertifiedCollapseLineage(
        lineage,collapsed,actualTouched.size()-1,8);}
    catch(const std::length_error&){refused=true;}
    check(refused,"One below exact complete committed collapse face-halo budget refuses");
    auto forged=collapsed;
    forged.collapseSources.front().retiredFaces.pop_back();
    refused=false;
    try {(void)advanceCertifiedCollapseLineage(lineage,forged,32,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"Missing source retirement refuses the whole committed collapse");
    auto wrong=collapsed;
    wrong.collapseSources.front().retired=b;
    wrong.collapseSources.front().survivor=b;
    refused=false;
    try {(void)advanceCertifiedCollapseLineage(lineage,wrong,32,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"Forged collapsed endpoint cannot inherit protected original source");

    // A nearby ORIGINAL protected face sharing a surviving vertex must
    // refuse even though its FaceId remains live: collapse moves its XYZ.
    FaceId protectedNear=0;
    for(const auto id:original->vertex(a).incidentFaces) {
        const auto t=original->face(id).vertices;
        if(std::find(t.begin(),t.end(),b)==t.end()) {
            protectedNear=id;break;
        }
    }
    check(protectedNear!=0,"Find face whose geometry a valid collapse would move");
    const auto protectedAnchor=captureOriginalSourceAnchors(
        original,{protectedNear},7);
    refused=false;
    try {(void)advanceCertifiedCollapseLineage(
        beginSourceSplitLineage(protectedAnchor),collapsed,32,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"Protected surviving original face cannot be deformed without M21 intent");
    const auto removedAnchor=captureOriginalSourceAnchors(
        original,{collapsed.removedFaces.front()},7);
    refused=false;
    try {(void)advanceCertifiedCollapseLineage(
        beginSourceSplitLineage(removedAnchor),collapsed,32,8);}
    catch(const std::logic_error&){refused=true;}
    check(refused,"Protected retired original source face is never authorized implicitly");
    check(original->retiredFaceCount()==0&&
          original->maybeVertex(b)!=nullptr,
          "All refused protected-cavity/exterior candidate audits are read only");
}
void testMirroredCollapseAgainstLegacy() {
    SphereOptions options;
    options.vertexCount=500;
    options.relaxationIterations=8;
    const Mesh initial=generateSphere(options);
    const auto mapping=symmetricSphereMirrors(options.vertexCount);
    const auto imported=partitionFlatMesh(initial,16);
    const auto& ids=imported.flatIndex.vertexIds;
    auto before=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> pairs;
    pairs.reserve(ids.size());
    for(std::size_t i=0;i<ids.size();++i)
        pairs.push_back({ids[i],ids[mapping[i]]});
    CollapseSettings settings;
    settings.enabled=true;
    settings.minimumEdgeRatio=2;
    settings.maximumPairsPerStamp=1;
    bool found=false;
    std::set<std::pair<std::uint32_t,std::uint32_t>> seen;
    for(const auto triangle:initial.triangles) {
        for(const auto& [x,y]:{std::pair{triangle.a,triangle.b},
                             std::pair{triangle.b,triangle.c},
                             std::pair{triangle.c,triangle.a}}) {
            auto a=x,b=y;
            if(a>=mapping[a]||b>=mapping[b])continue;
            if(a>b)std::swap(a,b);
            if(!seen.emplace(a,b).second)continue;
            auto compressed=initial;
            const auto p=(compressed.vertices[a]+compressed.vertices[b])*.5;
            compressed.vertices[b]=p;
            compressed.vertices[mapping[b]]={-p.x,p.y,p.z};
            // Both trial engines see the same old unmodified mesh and new
            // compressed positions, not a new mesh-wide "before" vector.
            RegionEdit move(before,{ids[b],ids[mapping[b]]},{},16);
            move.moveVertex(ids[b],p);
            move.moveVertex(ids[mapping[b]],{-p.x,p.y,p.z});
            try {(void)move.commit();}catch(const std::logic_error&){continue;}
            auto after=move.updatedIndex();
            LocalStampBaseline baseline{before,{ids[b],ids[mapping[b]]},
                                        before->vertex(ids[b]).incidentFaces};
            VersionedDirectionalCollapser versioned(ids.size(),settings,pairs,16);
            auto result=versioned.tryEdge(after,baseline,ids[a],ids[b]);
            if(result.statistics.accepted==0)continue;
            DirectionalCollapser legacy(compressed,settings,mapping);
            const auto prior=initial.vertices;
            auto legacyStatistics=legacy.apply(compressed,prior);
            check(legacyStatistics.accepted>0 &&
                  legacyStatistics.removedVertices>=2,
                  "Versioned accepted mirrored collapse also accepted by legacy");
            check(result.statistics.removedVertices==2 &&
                  result.removedVertices.size()==2,
                  "Mirrored collapse removes exactly two stable vertex IDs");
            check(result.removedMirrorPairs.size()==1,
                  "Mirror handoff reports one deleted pair");
            check(versioned.mirrors().size()==ids.size()-2,
                  "Mirror mapping eliminates obsolete rather than renumbering IDs");
            check(result.index->retiredVertexCount()==2,
                  "Both mirrored IDs retained as persistent tombstones");
            check(result.index->retiredFaceCount()==4,
                  "Both mirrored edge triangles removed atomically");
            // Protect one untouched off-X=0 original face on each side
            // of an ACTUAL accepted reflected collapse. No mirrored
            // ancestry is inferred from coincident XYZ alone.
            std::array<FaceId,2> protectedMirrors{};
            before->revision().forEachPartition(
                [&](PartitionId,const MeshPartition& part) {
                    for(const auto& face:part.faces) {
                        if(!result.index->maybeFace(face.id)||
                           !after->maybeFace(face.id)||
                           result.index->face(face.id).vertices!=face.vertices||
                           after->face(face.id).vertices!=face.vertices)
                            continue;
                        bool stable=true;
                        for(const auto v:face.vertices) {
                            const auto p=before->vertex(v).position;
                            if(length(p-after->vertex(v).position)!=0||
                               length(p-result.index->vertex(v).position)!=0)
                                stable=false;
                        }
                        if(!stable)continue;
                        const auto x=before->vertex(face.vertices[0]).position.x+
                                     before->vertex(face.vertices[1]).position.x+
                                     before->vertex(face.vertices[2]).position.x;
                        auto& chosen=protectedMirrors[x<0?0:1];
                        if(!chosen)chosen=face.id;
                    }
                });
            check(protectedMirrors[0]&&protectedMirrors[1],
                  "Find untouched original mirrored exterior on both sides");
            const auto protectedAnchor=captureOriginalSourceAnchors(
                before,{protectedMirrors[0],protectedMirrors[1]},14);
            const auto line=advanceCertifiedCollapseLineage(
                beginSourceSplitLineage(protectedAnchor),result,1024,2);
            check(line.terminal==result.index&&
                  line.living.size()==2&&
                  result.collapseSources.size()==1&&
                  result.collapseSources.front().paired,
                  "Real reflected collapse is certified while outer protected sources persist");
            auto unpaired=result;
            unpaired.collapseSources.front().mirrorRetired=
                unpaired.collapseSources.front().retired;
            bool failed=false;
            try {(void)advanceCertifiedCollapseLineage(
                beginSourceSplitLineage(protectedAnchor),unpaired,1024,2);}
            catch(const std::logic_error&){failed=true;}
            check(failed,"Missing reflected stable vertex source refuses actual paired collapse");
            for(const auto& [v,partner]:versioned.mirrors()) {
                const auto left=result.index->vertex(v).position;
                const auto right=result.index->vertex(partner).position;
                check(length(Vec3{left.x+right.x,left.y-right.y,
                                  left.z-right.z})<1e-10,
                      "Exact reflected geometry after symmetric collapse");
                check(versioned.mirrors().at(partner)==v,
                      "Surviving stable-ID mirror pairs stay reciprocal");
                if(v==partner)check(left.x==0,
                    "Seam stays on symmetry plane after paired collapse");
            }
            // The V8 orchestrator can consume just the deleted pair delta:
            // neither its Inflate nor its Refiner needs a global mirror copy.
            VersionedInflateBrush nextInflate(.0001,{},pairs);
            RefineSettings refineSettings;
            refineSettings.enabled=true;
            VersionedLocalRefiner nextRefine(ids.size(),refineSettings,pairs,16);
            for(const auto& removed:result.removedMirrorPairs) {
                nextInflate.removeMirrorPair(removed.first,removed.second);
                nextRefine.removeMirrorPair(removed.first,removed.second);
            }
            check(nextRefine.mirrors().size()==ids.size()-2,
                  "Refiner retained precisely the surviving mirror IDs");
            const auto next=nextInflate.stamp(result.index,
                std::vector<BrushArea>{{{.35,-.5,.6},.6,.25},
                                       {{-.35,-.5,.6},.6,.25}},16);
            check(next.index!=nullptr,
                  "The next symmetric Inflate supports retired vertex IDs");
            verify(next.index);
            verify(result.index);
            verify(before);
            found=true;
            break;
        }
        if(found)break;
    }
    check(found,"Find a legacy-compatible locally valid symmetric collapse");
}
void testMultipleLocalCollapses() {
    SphereOptions settings;
    settings.vertexCount=400;
    settings.relaxationIterations=8;
    auto flat=generateSphere(settings);
    auto imported=partitionFlatMesh(flat,16);
    auto current=RegionTopologyIndex::build(imported.revision);
    CollapseSettings collapse;
    collapse.enabled=true;
    collapse.minimumEdgeRatio=1.75;
    collapse.maximumPairsPerStamp=3;
    VersionedDirectionalCollapser c(settings.vertexCount,collapse,{},16);
    unsigned total=0;
    for(int iteration=0;iteration<6;++iteration) {
        // Force one shrinking short edge in a local neighborhood; repeated
        // edits must reuse stable IDs without global adjacency rebuilds.
        const auto candidates=current->querySphere({.3,-.6,.65},.65);
        if(candidates.size()<3)continue;
        bool completed=false;
        for(const auto a:candidates) {
            const auto& incident=current->vertex(a).incidentFaces;
            for(const auto f:incident) {
                const auto triangle=current->face(f).vertices;
                for(const auto b:triangle) {
                    if(a==b)continue;
                    const auto p=current->vertex(a).position*.55+
                                 current->vertex(b).position*.45;
                    RegionEdit move(current,{b},{},16);
                    try {
                        move.moveVertex(b,p);
                        (void)move.commit();
                    }catch(const std::logic_error&){continue;}
                    const auto after=move.updatedIndex();
                    LocalStampBaseline baseline{current,{b},
                                                 current->vertex(b).incidentFaces};
                    auto result=c.tryEdge(after,baseline,a,b);
                    if(result.statistics.accepted==0)continue;
                    current=result.index;
                    total+=result.statistics.removedVertices;
                    verify(current);
                    check(current->updateStatistics().fullMeshTraversals==0,
                          "Repeated collapse never performs full-mesh index build");
                    completed=true;
                    break;
                }
                if(completed)break;
            }
            if(completed)break;
        }
        if(!completed)break;
    }
    check(total>=2,"Multiple successful localized collapse operations");
}
}
int main() {
    try {
        testNoOpAndOneCollapse();
        testM25ConservativeProtectedCollapseProof();
        testMirroredCollapseAgainstLegacy();
        testMultipleLocalCollapses();
        std::cout<<"Versioned local collapse regression suite passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Versioned collapse failure: "<<e.what()<<'\n';
        return 1;
    }
    return 0;
}

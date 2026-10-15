#include "mesher/versioned_collapse.hpp"
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
        for(const auto [x,y]:{std::pair{triangle.a,triangle.b},
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
        testMirroredCollapseAgainstLegacy();
        testMultipleLocalCollapses();
        std::cout<<"Versioned local collapse regression suite passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Versioned collapse failure: "<<e.what()<<'\n';
        return 1;
    }
}

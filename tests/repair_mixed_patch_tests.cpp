#include "mesher/repair_mixed_patch.hpp"
#include "mesher/repair_mixed_edges.hpp"
#include "mesher/repair_zipper.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>

using namespace mesher;
namespace {
void require(bool ok,const char* msg) {
    if(!ok)throw std::logic_error(msg);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> source;
    FlatMeshIndex order;
};
Fixture tetra() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    mesh.triangles={{0,2,1},{0,1,3},{0,3,2},{1,2,3}};
    auto imported=partitionFlatMesh(mesh,64);
    return {RegionTopologyIndex::build(imported.revision),
            std::move(imported.flatIndex)};
}
struct Planned {
    Fixture base;
    MixedClosureStaging stage;
    MutableMeshView view;
    VertexId midpoint{};
    explicit Planned(Fixture f)
      : base(std::move(f)),
        view(base.source->revision().createMutableView()) {
        stage.sourceView=std::make_shared<MixedClosureSourceView>(base.source);
        stage.positions.bind(stage.sourceView);
        const auto a=base.order.vertexIds[0];
        const auto b=base.order.vertexIds[1];
        const auto e=canonicalMixedEdge(a,b);
        const auto neighbors=stage.sourceView->edgeSides({e.first,e.second});
        require(neighbors.size()==2,
                "M14E tetra source shared edge must have two oriented sides");
        for(const auto& side:neighbors) {
            const auto& f=stage.sourceView->face(side.face);
            stage.faces.push_back({side.face,f.owner,{},
                                   MixedClosureFaceKind::Survivor,0,
                                   stage.sourceView});
            stage.survivingFaceIds.push_back(side.face);
            for(const auto vertex:f.vertices) {
                stage.positions.emplace(vertex,stage.sourceView->position(vertex));
                stage.vertexOwners.emplace(vertex,stage.sourceView->owner(vertex));
            }
        }
        const auto middle=(stage.positions.at(a)+stage.positions.at(b))*.5;
        midpoint=view.addVertex(stage.vertexOwners.at(a),middle);
        stage.positions.emplace(midpoint,middle);
        stage.vertexOwners.emplace(midpoint,stage.vertexOwners.at(a));
        applyMixedConformingEdgeSplit(stage.faces,e,midpoint,64);
    }
};
void testCompletePatchAndSourceIdentity() {
    Planned planned(tetra());
    auto patch=planPrivateMixedClosurePatch(planned.stage,64);
    require(patch.sourceR03==planned.base.source&&
            patch.changedSurvivors==2&&patch.newFaces==2&&
            patch.generatedVertices.size()==1&&
            patch.generatedVertices.front().id==planned.midpoint&&
            patch.faceWrites.size()==4,
            "M14E must capture BOTH source FaceId modifications and BOTH children");
    std::size_t survivorChildren=0;
    for(const auto& entry:patch.faceWrites)
        if(!entry.original&&entry.kind==MixedClosureFaceKind::Survivor)
            ++survivorChildren;
    require(survivorChildren==2,
            "M14E original edge splits must retain newly generated SURVIVOR children");
    const auto initial=planned.base.source->face(
        planned.stage.survivingFaceIds.front()).vertices;
    const auto result=applyPrivateMixedClosurePatch(
        planned.view,*planned.base.source,patch,16);
    require(result.createdFaces.size()==2&&
            result.touchedPartitions.size()==1,
            "M14E one private application must assign both new FaceIds");
    const auto candidate=RegionTopologyIndex::build(planned.view.commit().revision);
    validatePrivateClosedManifold(*candidate,10000);
    require(planned.base.source->face(planned.stage.survivingFaceIds.front()).vertices==initial&&
            planned.base.source->revision().partitionCount()==
                candidate->revision().partitionCount(),
            "M14E completed COW must preserve original immutable revision");
    std::cout<<"[r06-m14e] explicit complete source patch and final manifold PASS\n";
}
void testOmittedSurvivorOverrideRefusedBeforeCommit() {
    Planned planned(tetra());
    planned.stage.faces.erase(planned.stage.faces.begin());
    bool refused=false;
    try {(void)planPrivateMixedClosurePatch(planned.stage,64);}
    catch(const std::logic_error&) {refused=true;}
    require(refused&&planned.stage.sourceView->pendingFaceEdits()==2,
            "M14E omitted original survivor edit must refuse before COW write");
    const auto originalId=planned.stage.survivingFaceIds.front();
    require(planned.base.source->face(originalId).vertices!=
                planned.stage.sourceView->face(originalId).vertices,
            "M14E the source is immutable even while the private override exists");
    std::cout<<"[r06-m14e] omitted required survivor patch rejected PASS\n";
}
void testOpenPatchFailsFinalGate() {
    Planned planned(tetra());
    planned.stage.faces.pop_back(); // no complete pair of new split children
    const auto patch=planPrivateMixedClosurePatch(planned.stage,64);
    require(patch.changedSurvivors==2&&patch.newFaces==1,
            "M14E an incomplete patch may be syntactically valid");
    (void)applyPrivateMixedClosurePatch(
        planned.view,*planned.base.source,patch,16);
    const auto candidate=RegionTopologyIndex::build(planned.view.commit().revision);
    bool refused=false;
    try{validatePrivateClosedManifold(*candidate,10000);}
    catch(const std::exception&) {refused=true;}
    require(refused,
            "M14E the closed manifold test must reject an incomplete generated patch");
    validatePrivateClosedManifold(*planned.base.source,10000);
    std::cout<<"[r06-m14e] nonmanifold consolidated candidate rejected, original intact PASS\n";
}
void testMissingReservedVertexRejected() {
    Planned planned(tetra());
    const auto patch=planPrivateMixedClosurePatch(planned.stage,64);
    const auto owner=patch.generatedVertices.front().owner;
    auto& vertices=planned.view.editPartition(owner).vertices;
    std::erase_if(vertices,[&](const PartitionVertex& v) {
        return v.id==planned.midpoint;
    });
    bool refused=false;
    try {(void)applyPrivateMixedClosurePatch(
        planned.view,*planned.base.source,patch,16);}
    catch(const std::logic_error&) {refused=true;}
    require(refused,
            "M14E complete patch must refuse missing reserved generated vertex");
    validatePrivateClosedManifold(*planned.base.source,10000);
}
void testForeignRootAndFiniteBudget() {
    Planned planned(tetra());
    const auto patch=planPrivateMixedClosurePatch(planned.stage,64);
    const auto other=tetra();
    bool refused=false;
    try {(void)applyPrivateMixedClosurePatch(
        planned.view,*other.source,patch,16);}
    catch(const std::logic_error&) {refused=true;}
    require(refused,"M14E must reject foreign root before COW patch application");
    refused=false;
    try {(void)applyPrivateMixedClosurePatch(
        planned.view,*planned.base.source,patch,1);}
    catch(const std::length_error&) {refused=true;}
    require(refused,"M14E must reject exhausted new-face budget before COW edit");
}
void testSelfIntersectionsNotAManifoldFailure() {
    // Two separately closed but intersecting tetrahedral surfaces form
    // disjoint topological 2-manifolds. Existing geometric crossings do
    // not invalidate their indexed edge/vertex manifold connectivity.
    Mesh mesh;
    const auto addTetra=[&](Vec3 offset) {
        const auto start=static_cast<std::uint32_t>(mesh.vertices.size());
        for(const Vec3 p: {Vec3{0,0,0},Vec3{2,0,0},
                           Vec3{0,2,0},Vec3{0,0,2}})
            mesh.vertices.push_back(p+offset);
        for(const Triangle t: {Triangle{0,2,1},Triangle{0,1,3},
                               Triangle{0,3,2},Triangle{1,2,3}})
            mesh.triangles.push_back({t.a+start,t.b+start,t.c+start});
    };
    addTetra({0,0,0});
    addTetra({.3,.3,.3});
    const auto index=RegionTopologyIndex::build(
        partitionFlatMesh(mesh,64).revision);
    validatePrivateClosedManifold(*index,10000);
    std::cout<<"[r06-m14e] self-intersecting closed source accepted topologically PASS\n";
}
}
int main() {
    try {
        testCompletePatchAndSourceIdentity();
        testOmittedSurvivorOverrideRefusedBeforeCommit();
        testOpenPatchFailsFinalGate();
        testMissingReservedVertexRejected();
        testForeignRootAndFiniteBudget();
        testSelfIntersectionsNotAManifoldFailure();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M14E full-patch gate FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

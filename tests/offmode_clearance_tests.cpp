#include "mesher/offmode_clearance.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/region_edit.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* text) {
    if(!ok)throw std::runtime_error(text);
}
void triangle(Mesh& mesh,Vec3 a,Vec3 b,Vec3 c) {
    const auto start=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.insert(mesh.vertices.end(),{a,b,c});
    mesh.triangles.push_back({start,start+1,start+2});
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    FlatMeshIndex ids;
};
Fixture fixture(Mesh mesh) {
    auto imported=partitionFlatMesh(mesh,1);
    return {RegionTopologyIndex::build(imported.revision),imported.flatIndex};
}
Fixture planarPair(double gap) {
    Mesh m;
    triangle(m,{-1,-1,0},{1,-1,0},{0,1,0});
    triangle(m,{-1,-1,gap},{1,-1,gap},{0,1,gap});
    return fixture(std::move(m));
}
std::vector<VertexId> group(const Fixture& f,unsigned number) {
    const auto begin=f.ids.vertexIds.begin()+
        static_cast<std::ptrdiff_t>(number*3);
    return {begin,begin+3};
}
std::shared_ptr<const RegionTopologyIndex> shifted(
    const std::shared_ptr<const RegionTopologyIndex>& root,
    const std::vector<VertexId>& ids,Vec3 delta) {
    RegionEdit edit(root,ids,{},1);
    for(const auto v:ids)
        edit.moveVertex(v,root->vertex(v).position+delta);
    (void)edit.commit();
    return edit.updatedIndex();
}
void testSafeAndTooClose() {
    auto f=planarPair(.25);
    const auto changed=group(f,1);
    const auto oldPicker=f.root->pickingRootIdentity();
    auto clean=shifted(f.root,changed,{.02,0,0});
    const auto clear=assessOffModeMeshClearance(f.root,clean,changed,{});
    require(clear.status==OffModeClearanceStatus::Clear&&clear.allowed(),
            "Unrelated separated parallel surfaces remain mesh-clear");
    auto near=shifted(f.root,changed,{0,0,-.18});
    const auto close=assessOffModeMeshClearance(f.root,near,changed,{});
    require(close.status==OffModeClearanceStatus::TooClose&&!close.allowed()&&
            close.contact.status==OffModeContactStatus::Clean&&
            close.examinedNearPairs==1&&close.distanceTests>=1&&
            close.smallestDistance>.06&&close.smallestDistance<.08&&
            close.largestRequiredSpacing>.09,
            "Static endpoint pair distance refuses mesh-scale clearance loss BEFORE contact");
    require(f.root->pickingRootIdentity()==oldPicker&&
            f.root->vertex(changed.front()).position.z==.25,
            "Read-only M03 never mutates caller's original COW index/picker");
    const auto collided=shifted(f.root,changed,{0,0,-.25});
    const auto intersection=assessOffModeMeshClearance(
        f.root,collided,changed,{});
    require(intersection.status==OffModeClearanceStatus::Contact&&
            !intersection.allowed(),
            "M03 keeps the M01 improper-contact veto, not an overlap-repair policy");
}
void testExistingGapAndLegalSliding() {
    auto f=planarPair(.04);
    const auto moving=group(f,1);
    const auto shiftedSheet=shifted(f.root,moving,{.08,0,0});
    const auto slide=assessOffModeMeshClearance(f.root,shiftedSheet,moving,{});
    require(slide.status==OffModeClearanceStatus::TangentialSlide&&
            slide.allowed()&&slide.tangentialPairs==1&&
            slide.smallestDistance>.039,
            "Proven rigid tangent displacement maintains old small gap without cleaning it");
    const auto approaching=shifted(f.root,moving,{.08,0,-.01});
    const auto unsafe=assessOffModeMeshClearance(
        f.root,approaching,moving,{});
    require(unsafe.status==OffModeClearanceStatus::TooClose&&
            !unsafe.allowed(),
            "Tangential part cannot excuse a new normal approach");
    // Even a zero-gap *new* independent face contact is not a
    // certified sliding clearance: near-coplanar numerics refuse.
    const auto touching=shifted(f.root,moving,{.08,0,-.04});
    const auto result=assessOffModeMeshClearance(
        f.root,touching,moving,{});
    require(!result.allowed(),
            "Unproven exact grazing never bypasses M01's contact policy");
    const auto unchanged=assessOffModeMeshClearance(
        f.root,f.root,moving,{});
    require(unchanged.allowed(),
            "True identical old root requires no artificial cleanup");
}
void testBothMovingAndMixedDensity() {
    auto f=planarPair(.24);
    const auto ids=f.ids.vertexIds;
    RegionEdit edit(f.root,ids,{},1);
    for(unsigned i=0;i<ids.size();++i) {
        const Vec3 delta{i<3?Vec3{0,0,.1}:Vec3{0,0,-.1}};
        edit.moveVertex(ids[i],f.root->vertex(ids[i]).position+delta);
    }
    (void)edit.commit();
    const auto both=assessOffModeMeshClearance(
        f.root,edit.updatedIndex(),ids,{});
    require(both.status==OffModeClearanceStatus::TooClose&&
            both.examinedNearPairs==1,
            "Both independently moving sheets tested in one fully staged final revision");
    Mesh m;
    triangle(m,{-20,-20,0},{20,-20,0},{0,20,0});
    triangle(m,{0,0,.025},{.2,0,.025},{0,.2,.025});
    auto mixed=fixture(std::move(m));
    const auto fine=group(mixed,1);
    auto next=shifted(mixed.root,fine,{.01,0,0});
    // Also seed the coarse stable face (an over-inclusive local halo):
    // its larger candidate search radius visits the close fine face, but
    // the *admitted* pair spacing still uses the smaller fine edge scale.
    const auto small=assessOffModeMeshClearance(
        mixed.root,next,mixed.ids.vertexIds,{});
    require(small.allowed()&&small.examinedNearPairs>=1&&
            small.largestRequiredSpacing<.011,
            "Mixed triangle densities use smaller local edge, never coarse mesh scale");
}
void testOldBadUnrelatedAndAdjacency() {
    Mesh mesh;
    triangle(mesh,{-1,-1,0},{1,-1,0},{0,1,0});
    triangle(mesh,{-1,-1,0},{1,-1,0},{0,1,0});
    triangle(mesh,{40,0,0},{42,0,0},{41,2,0});
    auto f=fixture(std::move(mesh));
    const auto remote=group(f,2);
    const auto next=shifted(f.root,remote,{0,0,.03});
    const auto unrelated=assessOffModeMeshClearance(
        f.root,next,remote,{});
    require(unrelated.allowed()&&
            unrelated.contact.candidateWork.reportedContacts==0,
            "Pre-existing remote overlapping sheet is not globally enumerated or repaired");
    Mesh adjacent;
    adjacent.vertices={{0,0,0},{1,0,0},{0,1,0},{1,1,0}};
    adjacent.triangles={{0,1,2},{1,3,2}};
    auto local=fixture(std::move(adjacent));
    const auto none=assessOffModeMeshClearance(
        local.root,local.root,local.ids.vertexIds,{});
    require(none.allowed(),"Shared stable edge adjacency imposes no zero-gap clearance veto");
}
void testRealWorkerStageRefusesClearanceBeforeContact() {
    Mesh mesh;
    triangle(mesh,{0,0,0},{2,0,0},{0,2,0});
    triangle(mesh,{0,0,.14},{2,0,.14},{0,2,.14});
    SculptCollisionSettings settings;
    settings.offModeConstraints.enabled=true;
    settings.offModeConstraints.backtrack.enabled=false; // frozen M03 near-gap veto
    VersionedSculptBrush brush(mesh,.12,{},{},{},false,256,{},settings);
    const auto original=brush.snapshot();
    const auto originalPicker=original->pickingRootIdentity();
    const auto checkpoint=brush.checkpoint();
    Mesh dummy;
    const std::array<BrushArea,1> close{{{{0,0,0},.13,.3}}};
    brush.stamp(dummy,close);
    require(brush.rejectedStamps()==1&&
            brush.publishedRevisions()==0&&
            brush.snapshot()==original&&
            brush.snapshot()->pickingRootIdentity()==originalPicker&&
            brush.checkpoint().index==checkpoint.index&&
            brush.historyStatistics().undoDepth==0&&
            brush.lastDirtyPartitions().empty()&&
            brush.lastRejection().find("mesh-scale spacing")!=std::string::npos&&
            brush.lastStepResourceWork().evaluated&&
            brush.lastStepResourceWork().privateCopiedBytes>0,
            "M03 real Inflate approaches a separate sheet without crossing: pre-CAS clearance veto, no dirty root/history");
    const std::array<BrushArea,1> farther{{{{0,0,.14},.13,.3}}};
    brush.stamp(dummy,farther);
    require(brush.rejectedStamps()==1&&
            brush.publishedRevisions()==1&&
            brush.snapshot()!=original&&
            brush.historyStatistics().undoDepth==1,
            "M03 rejects close approach but later accepts legitimate outward edit");
    const auto accepted=brush.snapshot();
    require(brush.undo()&&brush.snapshot()==original&&
            brush.redo()&&brush.snapshot()==accepted,
            "M03 normal clearance refusal does not poison exact root history");
}
void testAllFiniteBoundaries() {
    auto f=planarPair(.25);
    const auto ids=group(f,1);
    const auto near=shifted(f.root,ids,{0,0,-.18});
    const auto nominal=assessOffModeMeshClearance(f.root,near,ids,{});
    require(nominal.status==OffModeClearanceStatus::TooClose&&
            nominal.distanceTests==2&&
            nominal.neighborhoodWork.reportedContacts==1,
            "Nominal clearance checks current and historical triangle distances");
    const std::array<std::size_t,6> used{{
        nominal.neighborhoodWork.seedFaces,
        nominal.neighborhoodWork.boxesTested,
        nominal.neighborhoodWork.partitionsTested,
        nominal.neighborhoodWork.candidateTriangles,
        nominal.neighborhoodWork.narrowPhaseTests,
        nominal.neighborhoodWork.reportedContacts
    }};
    for(unsigned dimension=0;dimension<used.size();++dimension) {
        require(used[dimension]>0,"All six real BVH cost dimensions exercised");
        OffModeClearanceLimits limits;
        switch(dimension) {
        case 0:limits.neighborhood.maxSeedFaces=used[dimension]-1;break;
        case 1:limits.neighborhood.maxBoxTests=used[dimension]-1;break;
        case 2:limits.neighborhood.maxPartitionTests=used[dimension]-1;break;
        case 3:limits.neighborhood.maxCandidateTriangles=used[dimension]-1;break;
        case 4:limits.neighborhood.maxNarrowPhaseTests=used[dimension]-1;break;
        default:limits.neighborhood.maxReportedContacts=used[dimension]-1;
        }
        const auto denied=assessOffModeMeshClearance(f.root,near,ids,{},limits);
        require(denied.status==OffModeClearanceStatus::Uncertain&&
                !denied.allowed(),
                "Every one-under BVH work budget refuses, never truncates to Clear");
    }
    OffModeClearanceLimits limits;
    limits.maxDistanceTests=0;
    require(assessOffModeMeshClearance(
        f.root,near,ids,{},limits).status==OffModeClearanceStatus::Uncertain,
        "Distance-test budget zero is an explicit refusal");
    limits={};
    limits.contacts.query.maxSeedFaces=0;
    require(assessOffModeMeshClearance(
        f.root,near,ids,{},limits).status==OffModeClearanceStatus::Uncertain,
        "Existing M01 work exhaustion remains an admission veto");
    limits={};
    limits.minimumSpacingRatio=0;
    require(assessOffModeMeshClearance(
        f.root,near,ids,{},limits).status==OffModeClearanceStatus::Uncertain,
        "Invalid numeric clearance policy fails closed");
    limits.minimumSpacingRatio=std::numeric_limits<double>::infinity();
    require(assessOffModeMeshClearance(
        f.root,near,ids,{},limits).status==OffModeClearanceStatus::Uncertain,
        "Nonfinite clearance policy fails closed");
}
}
int main() {
    try {
        testSafeAndTooClose();
        testExistingGapAndLegalSliding();
        testBothMovingAndMixedDensity();
        testOldBadUnrelatedAndAdjacency();
        testAllFiniteBoundaries();
        testRealWorkerStageRefusesClearanceBeforeContact();
        std::cout<<"V18-M03 mesh-local distance and tangential admission: PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"V18-M03 mesh clearance: "<<e.what()<<'\n';
        return 1;
    }
}

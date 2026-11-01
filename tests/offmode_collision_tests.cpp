#include "mesher/offmode_collision.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/region_edit.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool valid,const char* text) {
    if(!valid)throw std::runtime_error(text);
}
void tetra(Mesh& mesh,Vec3 at,double half) {
    const auto start=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto p:std::array<Vec3,4>{{
        {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
    }})mesh.vertices.push_back(at+p*half);
    for(const auto f:std::array<std::array<std::uint32_t,3>,4>{{
        {0,1,2},{0,2,3},{0,3,1},{1,3,2}
    }})mesh.triangles.push_back({
        start+f[0],start+f[1],start+f[2]});
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    FlatMeshIndex ids;
};
Fixture fixture(Mesh mesh) {
    const auto imported=partitionFlatMesh(mesh,1);
    return {RegionTopologyIndex::build(imported.revision),imported.flatIndex};
}
Fixture twoSeparated() {
    Mesh mesh;
    tetra(mesh,{-2,0,0},.5);
    tetra(mesh,{2,0,0},.5);
    return fixture(std::move(mesh));
}
std::vector<VertexId> firstFour(const Fixture& f) {
    return {f.ids.vertexIds.begin(),f.ids.vertexIds.begin()+4};
}
std::shared_ptr<const RegionTopologyIndex> moveVertices(
    const std::shared_ptr<const RegionTopologyIndex>& before,
    const std::vector<VertexId>& ids,Vec3 delta) {
    RegionEdit edit(before,ids,{},1);
    for(const auto id:ids)
        edit.moveVertex(id,before->vertex(id).position+delta);
    (void)edit.commit();
    return edit.updatedIndex();
}

void cleanAndNewContacts() {
    auto f=twoSeparated();
    auto ids=firstFour(f);
    auto clean=assessOffModePostStateContacts(f.root,f.root,ids,{});
    require(clean.status==OffModeContactStatus::Clean,
            "Separated sheets have no old or candidate contacts");
    require(clean.conservativelyContactFree()&&
            clean.beforeWork.reportedContacts==0&&
            clean.candidateWork.reportedContacts==0,
            "Clean complete query records correct work");
    auto moved=moveVertices(f.root,ids,{3.9,0,0});
    const auto oldPointer=f.root->pickingRootIdentity();
    auto result=assessOffModePostStateContacts(f.root,moved,ids,{});
    require(result.status==OffModeContactStatus::NewContact&&
            !result.introduced.empty()&&
            !result.conservativelyContactFree(),
            "One moving sheet creates new contact against stationary geometry");
    require(result.beforeWork.reportedContacts==0&&
            result.candidateWork.reportedContacts==result.introduced.size(),
            "Separate before/post queries count all newly contacting pairs");
    require(f.root->pickingRootIdentity()==oldPointer&&
            f.root!=moved,
            "Read-only ledger never mutates original held immutable snapshot");
}
void bothSheetsMoving() {
    auto f=twoSeparated();
    auto ids=f.ids.vertexIds;
    RegionEdit edit(f.root,ids,{},1);
    for(std::size_t i=0;i<ids.size();++i) {
        const auto p=f.root->vertex(ids[i]).position;
        edit.moveVertex(ids[i],p+Vec3{i<4?1.9:-1.9,0,0});
    }
    (void)edit.commit();
    const auto now=edit.updatedIndex();
    const auto result=assessOffModePostStateContacts(f.root,now,ids,{});
    require(result.status==OffModeContactStatus::NewContact&&
            !result.introduced.empty()&&
            result.beforeWork.reportedContacts==0,
            "Compare both moved sheets in same final post-state, not old positions");
    require(result.candidateWork.seedFaces==8,
            "BVH seeds include incident triangles of BOTH moved components");
}
void historicalCrossingsUnrelatedAndUnchanged() {
    Mesh mesh;
    tetra(mesh,{0,0,0},1);
    tetra(mesh,{0,0,0},1); // source-independent existing overlap
    tetra(mesh,{6,0,0},.5);
    auto f=fixture(std::move(mesh));
    const std::vector<VertexId> far{
        f.ids.vertexIds.begin()+8,f.ids.vertexIds.end()};
    auto moved=moveVertices(f.root,far,{0,.1,0});
    const auto clean=assessOffModePostStateContacts(
        f.root,moved,far,{});
    require(clean.status==OffModeContactStatus::Clean&&
            clean.beforeWork.reportedContacts==0&&
            clean.candidateWork.reportedContacts==0,
            "Unrelated initial crossings are not globally retouched or refused");
    const std::vector<FaceId> firstSource{
        f.ids.faceIds.begin(),f.ids.faceIds.begin()+4};
    const auto old=assessOffModePostStateContacts(
        f.root,f.root,{},firstSource);
    require(old.status==OffModeContactStatus::UnchangedPreexisting&&
            !old.retained.empty()&&old.introduced.empty(),
            "Exactly unchanged pre-existing source collision is ledgered");
    const auto affected=firstFour(f);
    moved=moveVertices(f.root,affected,{0,.1,0});
    const auto uncertain=assessOffModePostStateContacts(
        f.root,moved,affected,{});
    require(uncertain.status==OffModeContactStatus::Uncertain&&
            !uncertain.uncertain.empty()&&
            !uncertain.conservativelyContactFree(),
            "Moving previously overlapping stable FaceIds cannot be automatically grandfathered");
    // Intentionally CLEAN up a previously overlapping component only
    // to distinguish resolved old contacts. V18 never automatically
    // performs this cleanup; the ledger merely records the hypothetical.
    moved=moveVertices(f.root,affected,{12,0,0});
    const auto resolved=assessOffModePostStateContacts(
        f.root,moved,affected,{});
    require(resolved.status==OffModeContactStatus::Clean&&
            resolved.resolvedOldContacts>0,
            "A vanished old collision is not mistaken for a new contact");
}
void validAdjacencyAndInput() {
    Mesh mesh;
    tetra(mesh,{0,0,0},1);
    auto f=fixture(std::move(mesh));
    const auto adjacent=assessOffModePostStateContacts(
        f.root,f.root,f.ids.vertexIds,{});
    require(adjacent.status==OffModeContactStatus::Clean,
            "Normal shared edge/vertex adjacency is not a crossing");
    require(assessOffModePostStateContacts(
        f.root,f.root,{},{}).status==OffModeContactStatus::Clean,
            "No-op same root requires no collision work");
    auto moved=moveVertices(f.root,firstFour(f),{1,0,0});
    require(assessOffModePostStateContacts(
        f.root,moved,{},{}).status==OffModeContactStatus::Uncertain,
        "Distinct revision with missing dirty inputs is NOT certified");
    std::vector<VertexId> invalid{999999999};
    require(assessOffModePostStateContacts(
        f.root,moved,invalid,{}).status==OffModeContactStatus::Uncertain,
        "Unknown stable original ID refuses instead of silently producing Clean");
    OffModeContactLimits limit;
    limit.maxChangedInputRecords=0;
    require(assessOffModePostStateContacts(
        f.root,moved,firstFour(f),{},limit).status==
            OffModeContactStatus::Uncertain,
        "Finite changed-input admission refuses one-under input work");
}
void allSixFiniteBudgetDimensions() {
    auto f=twoSeparated();
    const auto ids=firstFour(f);
    auto moved=moveVertices(f.root,ids,{3.9,0,0});
    const auto full=assessOffModePostStateContacts(
        f.root,moved,ids,{});
    require(full.status==OffModeContactStatus::NewContact,
            "Finite baseline fixture must have supported new contact");
    const std::array<std::size_t,6> used{{
        full.candidateWork.seedFaces,
        full.candidateWork.boxesTested,
        full.candidateWork.partitionsTested,
        full.candidateWork.candidateTriangles,
        full.candidateWork.narrowPhaseTests,
        full.candidateWork.reportedContacts
    }};
    for(unsigned dimension=0;dimension<used.size();++dimension) {
        require(used[dimension]>0,
                "Every tested actual BVH work category must be executed");
        OffModeContactLimits limits;
        switch(dimension) {
        case 0: limits.query.maxSeedFaces=used[dimension]-1;break;
        case 1: limits.query.maxBoxTests=used[dimension]-1;break;
        case 2: limits.query.maxPartitionTests=used[dimension]-1;break;
        case 3: limits.query.maxCandidateTriangles=used[dimension]-1;break;
        case 4: limits.query.maxNarrowPhaseTests=used[dimension]-1;break;
        default:limits.query.maxReportedContacts=used[dimension]-1;break;
        }
        const auto result=assessOffModePostStateContacts(
            f.root,moved,ids,{},limits);
        require(result.status==OffModeContactStatus::Uncertain&&
                !result.conservativelyContactFree()&&
                result.introduced.empty()&&result.retained.empty(),
                "Every actual one-under finite work limit refuses and exposes no partial clean ledger");
    }
}
}
int main() {
    try {
        cleanAndNewContacts();
        bothSheetsMoving();
        historicalCrossingsUnrelatedAndUnchanged();
        validAdjacencyAndInput();
        allSixFiniteBudgetDimensions();
        std::cout<<"V18-M01 bounded immutable before/after contact ledger: PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"V18-M01 contact ledger: "<<e.what()<<'\n';
        return 1;
    }
    return 0;
}

#include "mesher/repair_inspection.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/desktop_offmode_fixture.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool condition,const char* reason) {
    if(!condition)throw std::runtime_error(reason);
}
void tetra(Mesh& mesh,Vec3 at,double half) {
    const auto base=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto point:std::array<Vec3,4>{{
        {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
    }})mesh.vertices.push_back(at+point*half);
    for(const auto face:std::array<std::array<std::uint32_t,3>,4>{{
        {0,1,2},{0,2,3},{0,3,1},{1,3,2}
    }})mesh.triangles.push_back({
        base+face[0],base+face[1],base+face[2]});
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    FlatMeshIndex ids;
};
Fixture fixture(Mesh mesh) {
    const auto built=partitionFlatMesh(mesh,2);
    return {RegionTopologyIndex::build(built.revision),built.flatIndex};
}
std::shared_ptr<const RegionTopologyIndex> move(
    const std::shared_ptr<const RegionTopologyIndex>& source,
    const std::vector<VertexId>& vertices,Vec3 delta) {
    RegionEdit edit(source,vertices,{},2);
    for(auto id:vertices)edit.moveVertex(id,source->vertex(id).position+delta);
    (void)edit.commit();
    return edit.updatedIndex();
}
void cleanAndFrozen() {
    Mesh mesh;
    tetra(mesh,{-2,0,0},.5);
    tetra(mesh,{2,0,0},.5);
    tetra(mesh,{10,0,0},.5);
    const auto f=fixture(std::move(mesh));
    const std::vector<VertexId> first(f.ids.vertexIds.begin(),
                                      f.ids.vertexIds.begin()+4);
    const std::vector<VertexId> last(f.ids.vertexIds.begin()+8,
                                     f.ids.vertexIds.end());
    const auto clean=move(f.root,last,{0,.1,0});
    RepairInspectionSession session;
    require(session.inspect({f.root,clean,last,{}}).empty()&&!session.frozen(),
            "Unrelated geometry must not freeze a clean stamp");
    const auto contact=move(clean,first,{3.9,0,0});
    const auto beforeIdentity=clean->pickingRootIdentity();
    const auto diagnostic=session.inspect({clean,contact,first,{}});
    require(!diagnostic.empty()&&session.frozen(),
            "Intersecting moved and unmoved tetrahedra must freeze");
    const auto frozen=session.frozenSnapshot();
    require(frozen&&frozen->candidate==contact&&!frozen->contacts.empty()&&
            frozen->affectedFaces.size()>1&&
            !frozen->incidentVertices.empty()&&
            !frozen->incidentEdges.empty()&&
            frozen->diagnostic.empty()&&frozen->work.narrowPhaseTests>0,
            "Frozen read-only preview records BVH evidence and incident topology");
    const auto geometry=buildRepairInspectionOverlay(*frozen);
    bool red=false,darkRed=false;
    for(const auto& vertex:geometry) {
        if(vertex.material==2.0f)red=true;
        if(vertex.material==3.0f)darkRed=true;
    }
    require(red&&darkRed&&geometry.size()==
            frozen->incidentVertices.size()*24+
            frozen->incidentEdges.size()*24,
            "All marked vertices/edges have actual colored triangle render geometry");
    require(session.inspect({contact,clean,first,{}}).find("frozen")!=
            std::string::npos&&session.frozenSnapshot()==frozen,
            "Once frozen, later requests cannot overwrite inspection evidence");
    require(clean->pickingRootIdentity()==beforeIdentity,
            "Candidate preview does not mutate old published picker or geometry");
}
void unrelatedPreexistingAndBudget() {
    Mesh mesh;
    tetra(mesh,{0,0,0},1);
    tetra(mesh,{0,0,0},1); // unrelated old overlap
    tetra(mesh,{10,0,0},.5);
    const auto f=fixture(std::move(mesh));
    const std::vector<VertexId> far(f.ids.vertexIds.begin()+8,
                                    f.ids.vertexIds.end());
    const auto moved=move(f.root,far,{0,0,.1});
    RepairInspectionSession session;
    require(session.inspect({f.root,moved,far,{}}).empty()&&!session.frozen(),
            "Untouched pre-existing intersections must not globally freeze a stamp");

    std::vector<VertexId> deliberatelyOverBudget(65537,far.front());
    RepairInspectionSession finite;
    require(!finite.inspect({f.root,moved,deliberatelyOverBudget,{}}).empty()&&
            finite.frozen()&&finite.frozenSnapshot()&&
            !finite.frozenSnapshot()->diagnostic.empty()&&
            finite.frozenSnapshot()->contacts.empty(),
            "Incomplete bounded query must freeze with a diagnostic, never report false clean");
}
void interiorFaceIntersectionWithoutCrossingVertices() {
    Mesh mesh;
    // Horizontal and vertical triangles cross through their interiors.
    // No vertex of either triangle is located inside the opposing face.
    mesh.vertices={
        {-2,-2,-3},{2,-2,-3},{0,2,-3},
        {0,-1,-1},{0,-1,1},{0,2,1}};
    mesh.triangles={{0,1,2},{3,4,5}};
    const auto f=fixture(std::move(mesh));
    const std::vector<VertexId> moving(f.ids.vertexIds.begin(),
                                       f.ids.vertexIds.begin()+3);
    const auto candidate=move(f.root,moving,{0,0,3});
    RepairInspectionSession session;
    require(!session.inspect({f.root,candidate,moving,{}}).empty()&&
            session.frozen(),"Face-interior intersections must be found without crossed source vertices");
    const auto evidence=session.frozenSnapshot();
    require(evidence&&evidence->contacts.size()==1&&
            evidence->affectedFaces.size()==2&&
            evidence->incidentVertices.size()==6,
            "R01 identifies both participating triangles and marks their six incident vertices");
}
void simultaneousIndependentIslands() {
    Mesh mesh;
    tetra(mesh,{-2,0,0},.5);
    tetra(mesh,{2,0,0},.5);
    tetra(mesh,{8,0,0},.5);
    tetra(mesh,{12,0,0},.5);
    const auto f=fixture(std::move(mesh));
    std::vector<VertexId> movers(f.ids.vertexIds.begin(),
                                 f.ids.vertexIds.begin()+4);
    movers.insert(movers.end(),f.ids.vertexIds.begin()+8,
                  f.ids.vertexIds.begin()+12);
    const auto moved=move(f.root,movers,{3.9,0,0});
    RepairInspectionSession session;
    require(!session.inspect({f.root,moved,movers,{}}).empty(),
            "Two spatially independent contacts must trigger a single freeze");
    const auto evidence=session.frozenSnapshot();
    require(evidence&&evidence->contacts.size()>=2,
            "R01 retains full changed-neighborhood pair set, not only first contact");
    bool firstIsland=false,secondIsland=false;
    for(const auto& p:evidence->contacts) {
        const bool early=std::find(f.ids.faceIds.begin(),
            f.ids.faceIds.begin()+8,p.first)!=f.ids.faceIds.begin()+8;
        const bool late=std::find(f.ids.faceIds.begin()+8,
            f.ids.faceIds.end(),p.first)!=f.ids.faceIds.end();
        firstIsland=firstIsland||early;
        secondIsland=secondIsland||late;
    }
    require(firstIsland&&secondIsland,
            "One stamp must preserve witnesses from both independent intersection islands");
}
void actualPostRemeshStamp() {
    const auto mesh=desktopOffModeNearSheetsFixture();
    auto session=std::make_shared<RepairInspectionSession>();
    const SculptStepValidator validator=[session](const ProposedSculptStep& proposed) {
        return session->inspect(proposed);
    };
    VersionedSculptBrush brush(mesh,.9,{},{},{},false,256,validator);
    const auto old=brush.snapshot();
    Mesh dummy;
    const std::array<BrushArea,1> near{{{{0,0,0},.5,.3}}};
    brush.stamp(dummy,near);
    require(session->frozen()&&session->frozenSnapshot()&&
            !session->frozenSnapshot()->contacts.empty(),
            "Actual brush after full Inflate/Refine/Collapse staging triggers R01");
    require(brush.snapshot()==old&&brush.rejectedStamps()==1&&
            brush.historyStatistics().undoDepth==0&&
            brush.lastDirtyPartitions().empty(),
            "R01 captures attempted candidate but never publishes it or its history");
    brush.stamp(dummy,near);
    require(brush.snapshot()==old&&brush.rejectedStamps()==2&&
            brush.historyStatistics().undoDepth==0,
            "Further queued post-freeze stamps cannot change committed geometry");
}
}
int main() {
    try {
        cleanAndFrozen();
        unrelatedPreexistingAndBudget();
        interiorFaceIntersectionWithoutCrossingVertices();
        simultaneousIndependentIslands();
        actualPostRemeshStamp();
        std::cout<<"R01 post-stamp candidate freeze and visual evidence: PASS\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"R01: "<<error.what()<<'\n';
        return 1;
    }
}

#include "mesher/repair_inspection.hpp"
#include "mesher/repair_preview.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/desktop_offmode_fixture.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool condition,const char* reason) {
    if(!condition)throw std::runtime_error(reason);
}
void cube(Mesh& mesh,Vec3 center,double h) {
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto v:std::array<Vec3,8>{{
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
    }})mesh.vertices.push_back(center+v*h);
    for(const auto f:std::array<std::array<std::uint32_t,3>,12>{{
        {0,2,1},{0,3,2},{4,5,6},{4,6,7},
        {0,1,5},{0,5,4},{3,7,6},{3,6,2},
        {0,4,7},{0,7,3},{1,2,6},{1,6,5}
    }})mesh.triangles.push_back(
        {first+f[0],first+f[1],first+f[2]});
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    FlatMeshIndex ids;
};
Fixture fixture(Mesh mesh) {
    const auto partitioned=partitionFlatMesh(mesh,8);
    return {RegionTopologyIndex::build(partitioned.revision),
            partitioned.flatIndex};
}
void independentIslandsAndClassification() {
    Mesh mesh;
    for(double y:{-3.0,3.0}) {
        cube(mesh,{-0.55,y-.1,-.2},.9);
        cube(mesh,{ .55,y+.15,.2},.9);
    }
    const auto f=fixture(std::move(mesh));
    const auto contacts=f.root->triangleIntersections(f.ids.vertexIds,{});
    require(!contacts.empty(),"Input fixture must genuinely intersect in two separate places");
    const auto beforeRoot=f.root->pickingRootIdentity();
    const auto preview=previewApproximateExcision(*f.root,contacts);
    require(preview.contactIslands>=2,
            "R02 must enumerate independent triangle-contact islands");
    require(preview.classifiedFaces>0,
            "R02 must consult V14 fragment sampling, not blindly delete red contact faces");
    std::cout<<"[r02-cubes] contact_islands="<<preview.contactIslands
             <<" vertices="<<preview.proposedVertices.size()
             <<" classified="<<preview.classifiedFaces
             <<" selected="<<preview.proposedFaces.size()
             <<" uncertain="<<preview.uncertainFaces.size()
             <<" contour_loops="<<preview.contours.size()
             <<" complete="<<preview.complete
             <<" reason="<<preview.diagnostic<<"\n";
    require(preview.proposedVertices.size()==32&&
            preview.proposedFaces.size()==48&&
            preview.contours.empty()&&preview.excisionComponents==4&&
            preview.complete,
            "Whole-vertex fan of each coarse cube is an entire closed component: deletion must have no boundary");
    for(const auto id:preview.proposedFaces) {
        require(static_cast<bool>(f.root->maybeFace(id)),
                "R02 selected faces must be real stable FaceIds");
        require(std::find(preview.uncertainFaces.begin(),
            preview.uncertainFaces.end(),id)==preview.uncertainFaces.end(),
            "Uncertain V14 face may never be approved for removal");
    }
    for(const auto& loop:preview.contours) {
        require(loop.vertexIds.size()>=3&&
                loop.vertexIds.size()==loop.survivingFaces.size(),
                "R02 contours must carry retained-winding directed edge evidence");
        for(const auto id:loop.vertexIds)
            require(static_cast<bool>(f.root->maybeVertex(id)),
                    "R02 boundary loop vertices must belong to frozen revision");
    }
    require(f.root->pickingRootIdentity()==beforeRoot&&
            f.root->retiredFaceCount()==0&&f.root->retiredVertexCount()==0,
            "R02 read-only classification must preserve original indexed root and tombstones");
}

void syntheticSphereVertexFansAndBoundaryLoops() {
    // The low-level R02 preview accepts R01's trusted contact face IDs.
    // Create two synthetic independent face-pairs on a CLOSED SPHERE to
    // directly test vertex-fan removal, clean loops and separation.
    SphereOptions sphereOptions;
    sphereOptions.vertexCount=320;
    sphereOptions.symmetric=false;
    sphereOptions.relaxationIterations=8;
    const auto f=fixture(generateSphere(sphereOptions));
    FaceId highest=0,lowest=0;
    double high=-1e20,low=1e20;
    for(const auto id:f.ids.faceIds) {
        const auto& tri=f.root->face(id).vertices;
        const double y=(f.root->vertex(tri[0]).position.y+
                        f.root->vertex(tri[1]).position.y+
                        f.root->vertex(tri[2]).position.y)/3.0;
        if(y>high) {high=y;highest=id;}
        if(y<low) {low=y;lowest=id;}
    }
    require(highest!=lowest&&high>.75&&low<-.75,
            "Regression sphere must have well-separated polar face centers");
    const auto neighbor=[&](FaceId face) {
        const auto& tri=f.root->face(face).vertices;
        auto a=tri[0],b=tri[1];
        if(a>b)std::swap(a,b);
        for(const auto& side:f.root->edgeSides({a,b}))
            if(side.face!=face)return side.face;
        throw std::runtime_error("No adjacent triangle on closed sphere");
    };
    const auto canonical=[](FaceId a,FaceId b) {
        if(a>b)std::swap(a,b);
        return RegionTopologyIndex::CollisionPair{a,b,false};
    };
    const std::vector<RegionTopologyIndex::CollisionPair> contacts{
        canonical(highest,neighbor(highest)),
        canonical(lowest,neighbor(lowest))
    };
    const auto originalIdentity=f.root->pickingRootIdentity();
    const auto plan=previewApproximateExcision(*f.root,contacts);
    std::cout<<"[r02-sphere] vertices="<<plan.proposedVertices.size()
             <<" faces="<<plan.proposedFaces.size()
             <<" loops="<<plan.contours.size()
             <<" components="<<plan.excisionComponents
             <<" complete="<<plan.complete
             <<" diagnostic="<<plan.diagnostic<<"\\n";
    require(plan.complete&&plan.contactIslands==2&&
            plan.excisionComponents==2&&plan.contours.size()==2&&
            plan.expandedBoundaryVertices.empty(),
            "Two disjoint vertex-star patches must yield exactly two independent directed holes");
    require(plan.boundaryEdges>=12&&plan.proposedFaces.size()>4,
            "Removing contact triangle vertices must expand through their complete incident face fans");
    const std::set<VertexId> removedVertices(
        plan.proposedVertices.begin(),plan.proposedVertices.end());
    const std::set<FaceId> removedFaces(
        plan.proposedFaces.begin(),plan.proposedFaces.end());
    for(const auto vertex:removedVertices) {
        for(const auto face:f.root->vertex(vertex).incidentFaces)
            require(removedFaces.contains(face),
                    "Every proposed vertex requires its entire incident face fan to be retired");
    }
    for(const auto& loop:plan.contours) {
        require(loop.vertexIds.size()>=3&&
                loop.vertexIds.size()==loop.survivingFaces.size(),
                "A survivor-oriented contour is a directed closed loop");
        for(const auto id:loop.vertexIds)
            require(!removedVertices.contains(id),
                    "No removed vertex can also appear in a surviving boundary loop");
    }
    require(f.root->pickingRootIdentity()==originalIdentity&&
            f.root->retiredFaceCount()==0&&f.root->retiredVertexCount()==0,
            "Read-only synthetic vertex star preview cannot mutate candidate or retired IDs");
}

void noContactsAndBudget() {
    Mesh mesh;
    cube(mesh,{-3,0,0},.5);
    cube(mesh,{3,0,0},.5);
    const auto f=fixture(std::move(mesh));
    const auto empty=previewApproximateExcision(*f.root,{});
    require(empty.complete&&empty.classifiedFaces==0&&
            empty.proposedFaces.empty()&&empty.contours.empty()&&
            empty.contactIslands==0,
            "R02 must not classify or modify unrelated noncontact geometry");
    bool invalid=false;
    try {
        RepairPreviewLimits limits;
        limits.sampleDepth=5;
        (void)previewApproximateExcision(*f.root,{},limits);
    }catch(const std::invalid_argument&) {invalid=true;}
    require(invalid,"R02 invalid work bounds must explicitly refuse");
}
void freezeAndOverlay() {
    const auto mesh=desktopOffModeNearSheetsFixture();
    auto session=std::make_shared<RepairInspectionSession>(true);
    const SculptStepValidator validator=[session](const ProposedSculptStep& p) {
        return session->inspect(p);
    };
    VersionedSculptBrush brush(mesh,.9,{},{},{},false,256,validator);
    const auto old=brush.snapshot();
    const auto oldIdentity=old->pickingRootIdentity();
    Mesh dummy;
    const std::array<BrushArea,1> near{{{{0,0,0},.5,.3}}};
    brush.stamp(dummy,near);
    require(session->frozen(),"R02 must freeze on first newly intersecting full candidate");
    const auto captured=session->frozenSnapshot();
    require(captured&&captured->previewEnabled&&
            !captured->contacts.empty()&&captured->excision.classifiedFaces>0,
            "R02 must retain classified preview with post-remesh contacts");
    const auto overlay=buildRepairInspectionOverlay(*captured);
    require(!overlay.empty()&&
            std::any_of(overlay.begin(),overlay.end(),[](const auto& v) {
                return v.material==2.0f;
            }),"R01 red contact evidence must remain alongside R02 additions");
    require(brush.snapshot()==old&&
            old->pickingRootIdentity()==oldIdentity&&
            brush.historyStatistics().undoDepth==0&&
            old->retiredFaceCount()==0,
            "R02 must never publish a private candidate or excision and must preserve undo state");
    brush.stamp(dummy,near);
    require(brush.snapshot()==old&&brush.historyStatistics().undoDepth==0,
            "R02 frozen subsequent stamps must be refused without worker root changes");
}
}
int main() {
    try {
        noContactsAndBudget();
        independentIslandsAndClassification();
        syntheticSphereVertexFansAndBoundaryLoops();
        freezeAndOverlay();
        std::cout<<"R02 read-only independent-island classification and preview: PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R02 preview FAILED: "<<e.what()<<'\n';
        return 1;
    }
}

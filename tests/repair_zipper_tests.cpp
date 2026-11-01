#include "mesher/repair_zipper.hpp"
#include "mesher/repair_inspection.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/sculpt_fixtures.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/region_edit.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool good,const char* detail) {
    if(!good)throw std::runtime_error(detail);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    FlatMeshIndex ids;
};
Fixture fixture(Mesh mesh) {
    auto partitioned=partitionFlatMesh(mesh,64);
    return {RegionTopologyIndex::build(partitioned.revision),partitioned.flatIndex};
}
FaceId closest(const Fixture& fixture,Vec3 target) {
    FaceId result=0;
    double best=1e100;
    for(const auto id:fixture.ids.faceIds) {
        const auto& face=fixture.root->face(id).vertices;
        const auto c=(fixture.root->vertex(face[0]).position+
                      fixture.root->vertex(face[1]).position+
                      fixture.root->vertex(face[2]).position)/3.0;
        const auto d=length(c-target);
        if(d<best){best=d;result=id;}
    }
    return result;
}
void holeFixture(const char* name,Mesh mesh,Vec3 p,Vec3 q,bool expectedPair=true) {
    const auto f=fixture(std::move(mesh));
    auto first=closest(f,p),second=closest(f,q);
    require(first!=second,"R04 synthetic contact faces must be distinct");
    if(first>second)std::swap(first,second);
    const auto preview=previewApproximateExcision(*f.root,{{first,second,false}});
    require(preview.complete&&preview.contours.size()==(expectedPair?2u:1u),
            "R04 fixture must expose the expected number of open rims");
    const auto cut=applyPrivateExcision(f.root,preview);
    if(!expectedPair) {
        bool refused=false;
        try{(void)applyForcedPrivateZipper(cut,preview);}
        catch(const std::logic_error&){refused=true;}
        require(refused,"R04 MUST refuse hollow-mouth single-loop cuts instead of silently capping");
        std::cout<<"[r04] "<<name<<" one-loop zipper refused as intended: PASS\n";
        return;
    }
    const auto baseline=cut.index;
    ForcedZipperSettings settings;
    settings.maxSeamEdgeFactor=2.0;
    const auto zipper=applyForcedPrivateZipper(cut,preview,settings);
    require(zipper.index&&zipper.index!=baseline&&
            zipper.matchedLoops.size()==1&&
            zipper.blueFaces.size()>=3&&
            zipper.totalNewFaces>=zipper.blueFaces.size()&&
            zipper.minimumSeamQuality>=settings.minSeamQuality,
            "R04 should create real blue zipper triangles on a private closed revision");
    for(const auto id:preview.proposedFaces)
        require(!zipper.index->maybeFace(id)&&f.root->maybeFace(id),
                "R04 zipper must not resurrect excised faces");
    for(const auto id:zipper.blueFaces)
        require(zipper.index->maybeFace(id)&&!baseline->maybeFace(id),
                "R04 scar must use NEW stable FaceIds and exist only in private revision");
    require(f.root->retiredFaceCount()==0&&baseline->retiredFaceCount()>0,
            "R04 must preserve original source and R03 cut revisions");
    std::cout<<"[r04] "<<name
             <<" loops="<<preview.contours.size()
             <<" seam_triangles="<<zipper.blueFaces.size()
             <<" inserted_vertices="<<zipper.insertedVertices.size()
             <<" boundary_splits="<<zipper.splitBoundaryEdges
             <<" refinement_steps="<<zipper.refinementSteps
             <<" minimum_quality="<<zipper.minimumSeamQuality
             <<" : PASS\n";
}
void oneLoopRefuses() {
    SphereOptions options;
    options.vertexCount=320;
    options.symmetric=false;
    options.relaxationIterations=8;
    auto f=fixture(generateSphere(options));
    auto face=closest(f,{0,1,0});
    const auto& tri=f.root->face(face).vertices;
    auto a=tri[0],b=tri[1];
    if(a>b)std::swap(a,b);
    FaceId adjacent=0;
    for(const auto& side:f.root->edgeSides({a,b}))
        if(side.face!=face)adjacent=side.face;
    const auto preview=previewApproximateExcision(
        *f.root,{{std::min(face,adjacent),std::max(face,adjacent),false}});
    require(preview.complete&&preview.contours.size()==1,
            "R04 refusal fixture needs one complete hole");
    const auto cut=applyPrivateExcision(f.root,preview);
    bool refused=false;
    try{(void)applyForcedPrivateZipper(cut,preview);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R04 must never cap a single hole in forced zipper mode");
}
void twoIndependentZippers() {
    // Two widely separated complete torus components, with one nominated
    // pinch in each. Four resulting survivor contours MUST match in two
    // independent pairs, never crossed between distant mesh components.
    Mesh combined;
    const auto source=generateTorusFixture(5000,1,5);
    for(const double x:{-3.0,3.0}) {
        const auto first=static_cast<std::uint32_t>(combined.vertices.size());
        for(const auto p:source.vertices)
            combined.vertices.push_back(p+Vec3{x,0,0});
        for(const auto face:source.triangles)
            combined.triangles.push_back({
                first+face.a,first+face.b,first+face.c});
    }
    const auto f=fixture(std::move(combined));
    std::vector<RegionTopologyIndex::CollisionPair> contacts;
    for(const double x:{-3.0,3.0}) {
        auto a=closest(f,{x+.12,0,0}),b=closest(f,{x-.12,0,0});
        require(a!=b,"R04 multi-pair fixture needs different pinch triangles");
        if(a>b)std::swap(a,b);
        contacts.push_back({a,b,false});
    }
    const auto preview=previewApproximateExcision(*f.root,contacts);
    std::cout<<"[r04-multi] loops="<<preview.contours.size()
             <<" complete="<<preview.complete
             <<" components="<<preview.excisionComponents
             <<" faces="<<preview.proposedFaces.size()
             <<" diagnostic="<<preview.diagnostic<<"\n";
    require(preview.complete&&preview.contours.size()==4&&
            preview.excisionComponents==4,
            "R04 multi-region torus fixture must create four independent rims");
    const auto cut=applyPrivateExcision(f.root,preview);
    const auto zipped=applyForcedPrivateZipper(cut,preview);
    require(zipped.matchedLoops.size()==2&&zipped.blueFaces.size()>12,
            "R04 must zip two independent pairs in ONE private closed transaction");
    std::set<std::size_t> used;
    for(const auto& [a,b]:zipped.matchedLoops) {
        require(used.insert(a).second&&used.insert(b).second,
                "No R04 boundary contour may participate in multiple zipper pairs");
        const auto pos=[&](std::size_t index) {
            Vec3 c{};
            for(const auto id:preview.contours[index].vertexIds)
                c=c+cut.index->vertex(id).position;
            return c/double(preview.contours[index].vertexIds.size());
        };
        require(length(pos(a)-pos(b))<1.0,
                "Two independent R04 zipper pairs must not cross distant geometry");
    }
    std::cout<<"[r04] two independent torus zipper pairs, blue_faces="
             <<zipped.blueFaces.size()<<" : PASS\n";
}

void genuineSession() {
    Mesh mesh;
    const auto tetra=[&](Vec3 at) {
        const auto base=static_cast<std::uint32_t>(mesh.vertices.size());
        for(const auto p:std::array<Vec3,4>{{
            {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
        }})mesh.vertices.push_back(at+p*.5);
        for(const auto f:std::array<std::array<std::uint32_t,3>,4>{{
            {0,1,2},{0,2,3},{0,3,1},{1,3,2}
        }})mesh.triangles.push_back({base+f[0],base+f[1],base+f[2]});
    };
    tetra({-2,0,0});tetra({2,0,0});
    const auto base=fixture(std::move(mesh));
    std::vector<VertexId> moving(base.ids.vertexIds.begin(),
                                 base.ids.vertexIds.begin()+4);
    RegionEdit edit(base.root,moving,{},64);
    for(const auto id:moving)
        edit.moveVertex(id,base.root->vertex(id).position+Vec3{3.9,0,0});
    (void)edit.commit();
    RepairInspectionSession session(true,true,true);
    require(!session.inspect({base.root,edit.updatedIndex(),moving,{}}).empty(),
            "R04 real closed-shell contact must freeze");
    const auto snapshot=session.frozenSnapshot();
    require(snapshot&&snapshot->zipperEnabled&&snapshot->privateCut&&
            !snapshot->candidate->retiredFaceCount(),
            "R04 must work only with a private already-cut COW candidate");
    if(snapshot->privateZipper) {
        const auto overlay=buildRepairInspectionOverlay(*snapshot);
        require(!overlay.empty()&&
                std::all_of(overlay.begin(),overlay.end(),[](const auto& v){
                    return v.material==8.0f;
                }),"Successful R04 viewport overlay must be BLUE only");
    }else
        require(!snapshot->zipperDiagnostic.empty(),
                "Unsupported real topology must give an explicit R04 refusal");
    std::cout<<"[r04] live inspected collision zip="<<snapshot->privateZipper.has_value()
             <<" refusal="<<snapshot->zipperDiagnostic<<"\n";
}
}
int main() {
    try {
        holeFixture("torus",generateTorusFixture(5000,1,5),
                    {.12,0,0},{-.12,0,0});
        // Pinch the torus along its opening axis. The source stays closed
        // and manifold, but the annular zipper becomes a narrow ribbon
        // at a different local aspect ratio than the baseline torus.
        auto squeezed=generateTorusFixture(5000,1,5);
        for(auto& p:squeezed.vertices)p.x*=.35;
        holeFixture("squeezed torus",std::move(squeezed),
                    {.042,0,0},{-.042,0,0});
        holeFixture("hollow mouth",generateHollowSphereFixture(5000,1,65),
                    {.095,-.96,0},{.095,-.93,0},false);
        oneLoopRefuses();
        twoIndependentZippers();
        genuineSession();
        std::cout<<"R04 private forced zipper CPU test: PASS\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<"R04 forced zipper FAILED: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}

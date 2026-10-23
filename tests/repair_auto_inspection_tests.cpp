#include "mesher/repair_inspection.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/sculpt_fixtures.hpp"
#include "mesher/region_edit.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/desktop_offmode_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> mesh;
    FlatMeshIndex ids;
};
Fixture fixture(Mesh input) {
    auto imported=partitionFlatMesh(input,64);
    return {RegionTopologyIndex::build(imported.revision),imported.flatIndex};
}
FaceId nearest(const Fixture& f,Vec3 point) {
    FaceId best=0;double squared=1e99;
    for(const auto id:f.ids.faceIds) {
        const auto tri=f.mesh->face(id).vertices;
        const auto mid=(f.mesh->vertex(tri[0]).position+
                        f.mesh->vertex(tri[1]).position+
                        f.mesh->vertex(tri[2]).position)/3;
        const auto d=length(mid-point);
        if(d<squared){squared=d;best=id;}
    }
    return best;
}
void torusGreenSuccessfulOverlay() {
    const auto f=fixture(generateTorusFixture(5000,1,5));
    auto a=nearest(f,{.12,0,0}),b=nearest(f,{-.12,0,0});
    if(a>b)std::swap(a,b);
    const auto contacts=std::vector<RegionTopologyIndex::CollisionPair>{
        {a,b,false}};
    const auto preview=previewApproximateExcision(*f.mesh,contacts);
    require(preview.complete&&preview.contours.size()==2,
            "R06-M05 torus fixture must have two real R03 rims");
    const auto cut=applyPrivateExcision(f.mesh,preview);
    const auto plan=planPrivateAutomaticClosure(*f.mesh,cut,preview,contacts);
    require(plan.ready(),"R06-M05 torus must have an independent two-cap auto plan");
    const auto result=applyCertifiedPrivateAutomaticClosure(
        f.mesh,cut,preview,plan,contacts);
    RepairInspectionSnapshot frozen;
    frozen.candidate=f.mesh;
    frozen.autoEnabled=true;
    frozen.previewEnabled=true;
    frozen.excisionEnabled=true;
    frozen.excision=preview;
    frozen.privateCut=cut;
    frozen.autoPlan=plan;
    frozen.privateAuto=result;
    const auto vertices=buildRepairInspectionOverlay(frozen);
    const auto hasMaterial=[](const auto& points,float material) {
        return std::any_of(points.begin(),points.end(),
            [material](const auto& p){return p.material==material;});
    };
    require(!vertices.empty()&&vertices.size()%3==0&&
            hasMaterial(vertices,9.0f)&&hasMaterial(vertices,12.0f),
            "R06-M12 successful auto torus must draw actual green faces AND triangulation edges");
    // M13 exact-position visual check: normal colored overlays are
    // deliberately displaced, but the opt-in diagnostic must place its
    // actual colored face vertices at the authoritative mesh XYZ.
    frozen.exactOverlay=true;
    std::vector<FaceId> capIds=result.greenFaces;
    frozen.seamAudit=auditPrivateRepairSeam(*result.index,capIds);
    const auto undisplaced=buildRepairInspectionOverlay(frozen);
    require(hasMaterial(undisplaced,13.0f),
            "M13 exact overlay must draw a distinct real seam marker");
    const auto first=result.index->face(result.greenFaces.front()).vertices;
    const auto p=result.index->vertex(first[0]).position;
    const auto firstGreen=std::find_if(undisplaced.begin(),undisplaced.end(),
        [](const auto& v){return v.material==9.0f;});
    require(firstGreen!=undisplaced.end()&&
            std::abs(firstGreen->px-static_cast<float>(p.x))<1e-7&&
            std::abs(firstGreen->py-static_cast<float>(p.y))<1e-7&&
            std::abs(firstGreen->pz-static_cast<float>(p.z))<1e-7,
            "M13 exact colored cap triangle must use source vertex XYZ without normal offset");
    frozen.exactOverlay=false;
    frozen.seamAudit.reset();
    const auto before=f.mesh->pickingRootIdentity();
    require(f.mesh->pickingRootIdentity()==before&&
            f.mesh->retiredFaceCount()==0,
            "R06-M05 overlay must not publish or modify candidate source");
    frozen.privateAuto.reset();
    MixedPrivateClosureResult warning;
    warning.index=result.index;
    warning.greenFaces=result.greenFaces;
    warning.qualityWarningFaces.push_back(result.greenFaces.front());
    frozen.warningPreview=warning;
    const auto visual=buildRepairInspectionOverlay(frozen);
    require(hasMaterial(visual,9.0f)&&hasMaterial(visual,10.0f)&&
            hasMaterial(visual,11.0f)&&hasMaterial(visual,12.0f),
            "R06-M12 private warning overlay must show green surface and edges, light red face and red edges");
    frozen.warningPreview.reset();
    const auto refusal=buildRepairInspectionOverlay(frozen);
    require(!refusal.empty()&&
            std::all_of(refusal.begin(),refusal.end(),
                [](const auto& v){return v.material==6.0f;}),
            "R06-M05 refusal must preserve visible R03 gold open rims, no ghost cap");
    std::cout<<"[r06-m05] real torus green faces; refusal displays real open gold rims PASS\n";
}
void forcedModesRetainPriority() {
    RepairInspectionSession zip(false,false,true,false);
    RepairInspectionSession cap(false,false,false,true);
    RepairInspectionSession automatic(false,false,false,false,true);
    require(!zip.frozen()&&!cap.frozen()&&!automatic.frozen(),
            "R06-M05 constructors must retain independent frozen-session defaults");
}
void realCandidateZeroLoopRefusesExterior() {
    Mesh mesh;
    const auto tetra=[&](Vec3 position) {
        const auto base=static_cast<std::uint32_t>(mesh.vertices.size());
        for(const auto p:std::array<Vec3,4>{{
            {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
        }})mesh.vertices.push_back(position+p*.5);
        for(const auto t:std::array<std::array<std::uint32_t,3>,4>{{
            {0,1,2},{0,2,3},{0,3,1},{1,3,2}
        }})mesh.triangles.push_back({base+t[0],base+t[1],base+t[2]});
    };
    tetra({-2,0,0});tetra({2,0,0});
    const auto f=fixture(std::move(mesh));
    std::vector<VertexId> moving(f.ids.vertexIds.begin(),
                                 f.ids.vertexIds.begin()+4);
    RegionEdit edit(f.mesh,moving,{},64);
    for(const auto id:moving)
        edit.moveVertex(id,f.mesh->vertex(id).position+Vec3{3.9,0,0});
    (void)edit.commit();
    RepairInspectionSession session(false,false,false,false,true);
    require(!session.inspect({f.mesh,edit.updatedIndex(),moving,{}}).empty() &&
            session.frozen(),"R06-M05 actual candidate crossing must freeze inspector");
    const auto snap=session.frozenSnapshot();
    require(snap&&snap->autoEnabled&&snap->previewEnabled&&
            snap->excisionEnabled&&snap->privateCut&&
            !snap->privateAuto&&
            snap->autoDiagnostic.find("zero-loop")!=std::string::npos,
            "R06-M05 must fail closed when candidate removes an outward whole component");
    require(f.mesh->retiredFaceCount()==0&&
            edit.updatedIndex()->retiredFaceCount()==0,
            "R06-M05 frozen refusal must not modify candidate or worker root");
    const auto gold=buildRepairInspectionOverlay(*snap);
    require(gold.empty() || std::all_of(gold.begin(),gold.end(),
        [](const auto& v){return v.material==6.0f;}),
        "R06-M05 zero-rim refusal must not fabricate green or blue diagnostic triangles");
    require(session.inspect({f.mesh,edit.updatedIndex(),moving,{}}).find("frozen")!=
                std::string::npos,
            "R06-M05 inspector must reject further stamps after freeze");
    std::cout<<"[r06-m05] real post-candidate whole-exterior excision correctly refused PASS\n";
}
void actualWorkerPreCasFreeze() {
    auto source=desktopOffModeNearSheetsFixture();
    auto session=std::make_shared<RepairInspectionSession>(
        false,false,false,false,true);
    const SculptStepValidator validator=[session](
        const ProposedSculptStep& proposed) {return session->inspect(proposed);};
    VersionedSculptBrush brush(source,.9,{},{},{},false,256,validator);
    const auto initial=brush.snapshot();
    Mesh ignored;
    const std::array<BrushArea,1> near{{{{0,0,0},.5,.3}}};
    brush.stamp(ignored,near);
    const auto frozen=session->frozenSnapshot();
    require(frozen&&frozen->autoEnabled&&session->frozen()&&
            !frozen->contacts.empty()&&
            brush.snapshot()==initial&&brush.rejectedStamps()==1&&
            brush.historyStatistics().undoDepth==0&&
            brush.lastDirtyPartitions().empty(),
            "R06-M05 actual worker pre-CAS inspector cannot commit a root or history");
    brush.stamp(ignored,near);
    require(brush.snapshot()==initial&&brush.rejectedStamps()==2,
            "R06-M05 further queued stamps after freeze never publish geometry");
    std::cout<<"[r06-m05] actual worker stamp private auto freezes before CAS PASS\n";
}
}
int main() {
    try {
        torusGreenSuccessfulOverlay();
        forcedModesRetainPriority();
        realCandidateZeroLoopRefusesExterior();
        actualWorkerPreCasFreeze();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M05 frozen automatic inspection FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

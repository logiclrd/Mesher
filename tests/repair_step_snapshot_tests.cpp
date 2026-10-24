#include "mesher/repair_step_snapshot.hpp"
#include "mesher/repair_step_gate.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;
namespace {
void check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
struct Tetra {
    std::shared_ptr<const RegionTopologyIndex> source;
    std::array<VertexId,4> v{};
    RepairContour rim;
};
Tetra openTetra() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    mesh.triangles={{0,1,3},{0,3,2},{1,2,3}};
    const auto flat=partitionFlatMesh(mesh,64);
    Tetra t;
    t.source=RegionTopologyIndex::build(flat.revision);
    for(std::size_t i=0;i<t.v.size();++i)
        t.v[i]=flat.flatIndex.vertexIds[i];
    t.rim.vertexIds={t.v[0],t.v[1],t.v[2]};
    return t;
}
bool containsEdge(const std::vector<RegionTopologyIndex::Edge>& edges,
                  VertexId a,VertexId b) {
    const RegionTopologyIndex::Edge target{
        std::min(a,b),std::max(a,b)};
    return std::find(edges.begin(),edges.end(),target)!=edges.end();
}
void originalFacesAndEffectiveSplit() {
    const auto t=openTetra();
    MixedClosureStaging stage(t.source,{t.rim});
    auto view=t.source->revision().createMutableView();
    const auto a=t.v[0],b=t.v[1],c=t.v[2];
    const auto owner=t.source->vertex(a).owner;
    auto before=captureRepairStepGeometry(stage,"initial","R03");
    check(before.source==t.source&&before.boundaryLoops.size()==1&&
          before.boundaryLoops.front().size()==3&&
          before.generatedFaces.empty(),
          "initial step must reference the exact immutable R03 root and open rim");
    stage.ensureFace(a,c,b,MixedClosureFaceKind::Cap,owner,1);
    auto capped=captureRepairStepGeometry(stage,"ensureFace","cap");
    const auto capDelta=diffRepairStepGeometry(before,capped);
    check(capped.boundaryLoops.empty()&&capDelta.addedFaces.size()==1&&
          capDelta.removedFaces.empty()&&
          capDelta.addedVertices.empty()&&
          capDelta.modifiedSurvivorFaces.empty(),
          "one initial cap triangle closes the rim but never edits an original face");
    check(capDelta.addedEdges.empty(),
          "adding a cap over an existing open rim adds faces, not new indexed edges");
    check(repairStepFaceKey(capped.generatedFaces.front()).oriented==
          PrivateRepairTriangle{a,c,b},
          "oriented cap corner identities must survive the capture unchanged");
    stage.ensureFace(c,b,a,MixedClosureFaceKind::Cap,owner,1);
    auto idempotent=captureRepairStepGeometry(stage,"no-op","ensureFace");
    const auto same=diffRepairStepGeometry(capped,idempotent);
    check(same.addedFaces.empty()&&same.removedFaces.empty()&&
          same.addedEdges.empty()&&same.removedEdges.empty(),
          "idempotent cyclic face must not appear as a geometry edit");

    MixedClosureSplitSettings limits;
    limits.maxTrackedFaces=64;
    const auto midpoint=stage.splitEdge(view,a,b,limits);
    check(midpoint.size()==1,"expected one shared edge-split midpoint");
    auto split=captureRepairStepGeometry(stage,"splitEdge","a,b");
    const auto change=diffRepairStepGeometry(capped,split);
    check(split.source==t.source&&
          split.generatedPositions.contains(midpoint.front())&&
          !capped.generatedPositions.contains(midpoint.front())&&
          !t.source->maybeVertex(midpoint.front())&&
          change.addedVertices==std::vector<VertexId>{midpoint.front()},
          "transactional midpoint must be an exact newly generated identity");
    check(split.survivorOverrides.size()==1&&
          change.modifiedSurvivorFaces.size()==1&&
          split.generatedFaces.size()==3&&
          change.addedFaces.size()==3&&change.removedFaces.size()==1,
          "shared split must capture edited original survivor plus both new face children");
    check(containsEdge(change.removedEdges,a,b)&&
          containsEdge(change.addedEdges,a,midpoint.front())&&
          containsEdge(change.addedEdges,midpoint.front(),b),
          "effective split must remove AB and add AM, MB across BOTH sides");
    check(split.effectiveSourceFace(
        change.modifiedSurvivorFaces.front())!=
        t.source->face(change.modifiedSurvivorFaces.front()).vertices,
        "the override must show modified gray geometry while immutable R03 remains unchanged");
    check(split.boundaryLoops.empty()&&split.boundaryDiagnostic.empty(),
          "a conforming split of a sealed cap must not recreate an open yellow rim");
    check(capped.generatedFaces.size()==1&&
          capped.generatedFaces.front().vertices==PrivateRepairTriangle{a,c,b},
          "previous frame must remain an owned immutable copy after later stage mutation");
    std::cout<<"[r06-m16c2b] cap, exact split, original survivor overrides, edge/face diff PASS\n";
}
void noCoordinateWeldingAndFiniteness() {
    const auto t=openTetra();
    MixedClosureStaging stage(t.source,{t.rim});
    auto view=t.source->revision().createMutableView();
    const auto owner=t.source->vertex(t.v[0]).owner;
    const Vec3 duplicate=t.source->vertex(t.v[0]).position;
    const auto newId=stage.addVertex(view,duplicate,owner);
    auto packet=captureRepairStepGeometry(stage,"addVertex","coincident");
    check(packet.generatedPositions.contains(newId)&&
          newId!=t.v[0]&&packet.position(newId).x==duplicate.x,
          "independent coincident XYZ must retain distinct stable IDs");
    bool exhausted=false;
    try {(void)captureRepairStepGeometry(stage,"addVertex","overbudget",32);}
    catch(const std::length_error&) {exhausted=true;}
    check(exhausted,"finite packet size must refuse BEFORE cloning a huge stage");
    bool badSource=false;
    auto second=openTetra();
    MixedClosureStaging other(second.source,{second.rim});
    const auto packet2=captureRepairStepGeometry(other,"R03","new root");
    try {(void)diffRepairStepGeometry(packet,packet2);}
    catch(const std::invalid_argument&) {badSource=true;}
    check(badSource,"diff must reject frames from distinct original roots");
    std::cout<<"[r06-m16c2b] no coordinate welding, byte budget, mismatched root refusal PASS\n";
}
void observerGateAndPreviousCurrent() {
    const auto t=openTetra();
    MixedClosureStaging stage(t.source,{t.rim});
    RepairStepGate<RepairStepGeometry> gate;
    auto before=captureRepairStepGeometry(stage,"initial","R03");
    const auto owner=t.source->vertex(t.v[0]).owner;
    stage.setOperationObserver([&](const MixedClosureStaging& state,
                                   const std::string& op,const std::string& args) {
        gate.publishAndWait(captureRepairStepGeometry(state,op,args));
    });
    // The test uses Continue (no threading needed) to exercise the
    // same observer/packet/gate composition that the viewport will poll.
    gate.continueRunning();
    stage.ensureFace(t.v[0],t.v[2],t.v[1],MixedClosureFaceKind::Cap,owner,1);
    auto v=gate.view();
    check(v.current&&v.sequence==1&&v.current->operation=="ensureFace"&&
          v.current->arguments.find("kind=CAP")!=std::string::npos,
          "observer must carry exact edit arguments across the immutable gate");
    check(!diffRepairStepGeometry(before,*v.current).addedFaces.empty(),
          "frame delivered through gate must be diffable with initial R03");
    stage.setOperationObserver({});
    gate.finish();
    std::cout<<"[r06-m16c2b] observer-to-gate owned render packet PASS\n";
}
}
int main() {
    try {
        originalFacesAndEffectiveSplit();
        noCoordinateWeldingAndFiniteness();
        observerGateAndPreviousCurrent();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"[r06-m16c2b] FAIL: "<<e.what()<<"\n";
        return 1;
    }
}

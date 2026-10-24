#include "mesher/repair_step_snapshot.hpp"
#include "mesher/repair_step_gate.hpp"
#include "mesher/repair_mixed_closure.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;
namespace {
void check(bool yes,const char* why) {
    if(!yes)throw std::runtime_error(why);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    RepairContour rim;
};
Fixture tetra() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    mesh.triangles={{0,1,3},{0,3,2},{1,2,3}};
    const auto flat=partitionFlatMesh(mesh,64);
    Fixture f;
    f.root=RegionTopologyIndex::build(flat.revision);
    f.rim.vertexIds={flat.flatIndex.vertexIds[0],
                     flat.flatIndex.vertexIds[1],
                     flat.flatIndex.vertexIds[2]};
    return f;
}
struct Captures {
    std::vector<RepairStepGeometry> frames;
    void operator()(const MixedClosureStaging& s,
                    const std::string& operation,const std::string& args) {
        frames.push_back(captureRepairStepGeometry(s,operation,args));
    }
    std::size_t count(const std::string& op) const {
        return std::count_if(frames.begin(),frames.end(),
            [&](const auto& f){return f.operation==op;});
    }
};
void provisionalConversionAndRetirement() {
    const auto f=tetra();
    MixedClosureStaging stage(f.root,{f.rim});
    auto view=f.root->revision().createMutableView();
    Captures capture;
    stage.setOperationObserver([&](const auto& s,const auto& op,const auto& args) {
        capture(s,op,args);
    });
    const auto owner=f.root->vertex(f.rim.vertexIds[0]).owner;
    const auto start=std::numeric_limits<VertexId>::max()-100;
    for(VertexId k=0;k<3;++k)
        stage.stageProvisionalVertex(start-k,
            Vec3{-5,double(k==1),double(k==2)},owner,16);
    check(capture.count("stageProvisionalVertex")==3,
          "every genuine provisional allocation needs a snapshot");
    (void)stage.ensureFace(start,start-1,start-2,MixedClosureFaceKind::Cap,
                           owner,3.0);
    auto old=capture.frames.back();
    const auto created=stage.resolveProvisionalVertices(view,16);
    check(created.size()==3&&capture.count("resolveProvisionalVertices")==1,
          "bulk provisional-to-stable replacement must be one complete event");
    const auto& replacement=capture.frames.back();
    check(replacement.arguments.find("converted=3")!=std::string::npos,
          "conversion must report exact number of permanent IDs");
    check(replacement.generatedFaces.size()==1&&
          replacement.generatedPositions.size()==3&&
          !replacement.generatedPositions.contains(start)&&
          replacement.generatedPositions.contains(created[0])&&
          old.generatedPositions.contains(start)&&
          old.generatedFaces[0].vertices[0]==start,
          "old frame must retain provisional IDs; new frame must contain real IDs");
    auto changes=diffRepairStepGeometry(old,replacement);
    check(changes.addedVertices.size()==3&&changes.removedVertices.size()==3&&
          changes.addedFaces.size()==1&&changes.removedFaces.size()==1,
          "stable ID remapping must be visible without accidental XYZ welding");
    check(stage.resolveProvisionalVertices(view,16).empty()&&
          capture.count("resolveProvisionalVertices")==1,
          "empty remapping must not claim a successful operation");
    // Retire an unattached new vertex; the stage owns its ID and position.
    const auto loose=stage.addVertex(view,{-6,0,0},owner);
    check(capture.count("addVertex")==1,"independent vertex allocation emitted");
    stage.retirePrivateVertex(view,loose);
    check(capture.count("retirePrivateVertex")==1&&
          capture.frames.back().arguments.find(std::to_string(loose))!=std::string::npos,
          "permanent vertex retirement must report the exact identity");
    std::cout<<"[r06-m16c2c] provisional IDs, full atomic remap, identity diff, retirement PASS\n";
}
void flipOperation() {
    const auto f=tetra();
    MixedClosureStaging stage(f.root,{f.rim});
    auto view=f.root->revision().createMutableView();
    const auto owner=f.root->vertex(f.rim.vertexIds[0]).owner;
    const std::array<Vec3,4> coords{{
        {5.0,0,0},{8.0,0,0},
        {7.269881588553663,0.12268701074125805,0},
        {8.0855206755773086,-0.9681983813799359,0}
    }};
    std::vector<VertexId> ids;
    for(const auto p:coords)ids.push_back(stage.addVertex(view,p,owner));
    (void)stage.ensureFace(ids[0],ids[1],ids[2],
                           MixedClosureFaceKind::Cap,owner,3.0);
    (void)stage.ensureFace(ids[1],ids[0],ids[3],
                           MixedClosureFaceKind::Cap,owner,3.0);
    Captures capture;
    stage.setOperationObserver([&](const auto& s,const auto& op,const auto& args) {
        capture(s,op,args);
    });
    const auto before=captureRepairStepGeometry(stage,"before","flip");
    check(stage.improveInteriorDiagonal(false,.1,.1,2.5,2.5)==1&&
          capture.count("improveInteriorDiagonal")==1,
          "positive skinny diagonal fixture must perform exactly one flip event");
    const auto& after=capture.frames.back();
    const auto delta=diffRepairStepGeometry(before,after);
    check(after.arguments.find("before=")!=std::string::npos&&
          delta.addedFaces.size()==2&&delta.removedFaces.size()==2&&
          delta.addedEdges.size()==1&&delta.removedEdges.size()==1,
          "flip must record both oriented faces and indexed diagonal replacement");
    check(stage.improveInteriorDiagonal(false,.1,.1,2.5,2.5)==0&&
          capture.count("improveInteriorDiagonal")==1,
          "zero-change flip does not advance debugger");
    std::cout<<"[r06-m16c2c] full two-face flip, precise arguments, no-op PASS\n";
}
void collapseOperation() {
    const auto f=tetra();
    MixedClosureStaging stage(f.root,{f.rim});
    auto view=f.root->revision().createMutableView();
    const auto owner=f.root->vertex(f.rim.vertexIds[0]).owner;
    const std::array<Vec3,4> coords{{
        {-2,0,0},{-2,2,0},{-2,1,1.732},{-2,1,.002}
    }};
    std::vector<VertexId> ids;
    for(const auto p:coords)ids.push_back(stage.addVertex(view,p,owner));
    (void)stage.ensureFace(ids[3],ids[0],ids[1],
                           MixedClosureFaceKind::Cap,owner,3);
    (void)stage.ensureFace(ids[3],ids[1],ids[2],
                           MixedClosureFaceKind::Cap,owner,3);
    (void)stage.ensureFace(ids[3],ids[2],ids[0],
                           MixedClosureFaceKind::Cap,owner,3);
    Captures capture;
    stage.setOperationObserver([&](const auto& s,const auto& op,const auto& args) {
        capture(s,op,args);
    });
    auto before=captureRepairStepGeometry(stage,"before","collapse");
    const auto retired=stage.collapseInteriorCapFan({ids[3]},false,.1,2.5);
    check(retired==std::vector<VertexId>{ids[3]}&&
          capture.count("collapseInteriorCapFan")==1,
          "skinny redundant cap fan should retire exactly its own interior center");
    const auto& after=capture.frames.back();
    auto delta=diffRepairStepGeometry(before,after);
    check(!after.generatedPositions.contains(ids[3])&&
          after.generatedFaces.size()==1&&
          delta.removedVertices==std::vector<VertexId>{ids[3]}&&
          delta.addedFaces.size()==1&&delta.removedFaces.size()==3,
          "fan collapse snapshot must represent one complete 3-to-1 transformation");
    stage.retirePrivateVertex(view,ids[3]);
    check(capture.count("retirePrivateVertex")==1,
          "private-view retirement must also be recorded");
    check(stage.collapseInteriorCapFan({ids[3]},false,.1,2.5).empty()&&
          capture.count("collapseInteriorCapFan")==1,
          "no candidate must not produce a second collapse event");
    std::cout<<"[r06-m16c2c] 3-to-1 cap fan, retired vertex, no-op PASS\n";
}
void fairingOperation() {
    const auto f=tetra();
    MixedClosureStaging stage(f.root,{f.rim});
    auto view=f.root->revision().createMutableView();
    const auto owner=f.root->vertex(f.rim.vertexIds[0]).owner;
    const std::array<Vec3,5> coords{{
        {-2,-1,-1},{-2,1,-1},{-2,1,1},{-2,-1,1},
        {-2,.995,.01}
    }};
    std::vector<VertexId> ids;
    for(const auto p:coords)ids.push_back(stage.addVertex(view,p,owner));
    for(std::size_t k=0;k<4;++k)
        (void)stage.ensureFace(ids[4],ids[k],ids[(k+1)%4],
                               MixedClosureFaceKind::Cap,owner,3);
    Captures capture;
    stage.setOperationObserver([&](const auto& s,const auto& op,const auto& args) {
        capture(s,op,args);
    });
    auto before=captureRepairStepGeometry(stage,"before","fair");
    const auto changed=stage.fairInteriorVertex({ids[4]},false,.1,.1,2.5,2.5);
    check(changed==1&&capture.count("fairInteriorVertex")==1,
          "positive skinny-center fixture must move one interior vertex");
    const auto& after=capture.frames.back();
    auto diff=diffRepairStepGeometry(before,after);
    check(diff.movedVertices==std::vector<VertexId>{ids[4]}&&
          after.arguments.find(" from=")!=std::string::npos&&
          after.arguments.find(" to=")!=std::string::npos,
          "fairing must report exact before/after XYZ for the moved vertex");
    for(std::size_t k=0;k<4;++k)
        check(before.generatedPositions.at(ids[k]).x==
              after.generatedPositions.at(ids[k]).x&&
              before.generatedPositions.at(ids[k]).y==
              after.generatedPositions.at(ids[k]).y&&
              before.generatedPositions.at(ids[k]).z==
              after.generatedPositions.at(ids[k]).z,
              "fairing must leave boundary positions unchanged");
    std::cout<<"[r06-m16c2c] fairing interior only, exact moved identity PASS\n";
}
void terminalCompletionAndObserverFailure() {
    // Use the exact already-open three-sided tetra as a synthetic R03
    // source. Repair a single oriented rim with one legal private cap.
    const auto f=tetra();
    PrivateExcisionResult cut;
    cut.index=f.root;
    cut.boundaryLoops=1;
    cut.boundaryEdges=3;
    RepairExcisionPreview proposal;
    proposal.complete=true;
    proposal.boundaryEdges=3;
    auto rim=f.rim;
    for(std::size_t i=0;i<rim.vertexIds.size();++i) {
        const auto a=rim.vertexIds[i],b=rim.vertexIds[(i+1)%rim.vertexIds.size()];
        const RegionTopologyIndex::Edge edge{std::min(a,b),std::max(a,b)};
        const auto sides=f.root->maybeEdgeSides(edge);
        check(sides&&sides->size()==1,
              "synthetic R03 tetra must have exactly one source side per rim edge");
        rim.survivingFaces.push_back(sides->front().face);
    }
    proposal.contours.push_back(rim);
    AutoClosurePlan plan;
    plan.status=AutoClosurePlanStatus::Ready;
    plan.cappedContours={0};
    AutoClosureRegion region;
    region.contours={0};
    region.caps={0};
    plan.regions={region};
    plan.geometryRegions=1;
    MixedPrivateClosureLimits limits;
    std::vector<MixedClosureTerminalEvent> terminal;
    limits.terminalObserver=[&](const auto& event) {
        terminal.push_back(event);
    };
    const auto applied=applyPrivateMixedClosure(cut,proposal,plan,limits);
    check(applied.index&&applied.greenFaces.size()==1&&
          applied.blueFaces.empty()&&terminal.size()==1&&
          terminal.front().outcome==MixedClosureTerminalOutcome::Completed&&
          terminal.front().phase=="completed"&&
          !terminal.front().untrustedWarningPreview,
          "certified private cap must report one genuine completion event");
    limits.staging.operationObserver=[](const auto&,const auto&,const auto&) {
        throw std::length_error("diagnostic limit");
    };
    terminal.clear();
    bool observerFailed=false;
    try {(void)applyPrivateMixedClosure(cut,proposal,plan,limits);}
    catch(const RepairStepObserverFailure& e) {
        observerFailed=std::string(e.what()).find("diagnostic observer failed")!=
            std::string::npos;
    }
    check(observerFailed&&terminal.size()==1&&
          terminal.front().outcome==MixedClosureTerminalOutcome::ObserverFailed&&
          terminal.front().phase=="initial_staging",
          "a diagnostic observer error must not be recorded as a geometry refusal");
    std::cout<<"[r06-m16c2c] real closed private cap, success and diagnostic-failure terminal classification PASS\\n";
}
void failureEnvelopeAndCancellation() {
    MixedPrivateClosureLimits limits;
    std::vector<MixedClosureTerminalEvent> outcomes;
    limits.terminalObserver=[&](const auto& report) {
        outcomes.push_back(report);
    };
    bool refused=false;
    try {
        (void)applyPrivateMixedClosure(
            PrivateExcisionResult{},RepairExcisionPreview{},
            AutoClosurePlan{},limits);
    }catch(const std::logic_error& e) {
        refused=std::string(e.what()).find("R06-M04A")!=std::string::npos;
    }
    check(refused&&outcomes.size()==1&&
          outcomes[0].outcome==MixedClosureTerminalOutcome::Refused&&
          outcomes[0].phase=="initial_staging"&&
          outcomes[0].detail.find("R06-M04A")!=std::string::npos,
          "pre-stage refusal must retain the original exception and typed phase");
    limits.terminalObserver=[](const auto&) {
        throw std::runtime_error("secondary HUD observer failure");
    };
    refused=false;
    try {
        (void)applyPrivateMixedClosure(
            PrivateExcisionResult{},RepairExcisionPreview{},
            AutoClosurePlan{},limits);
    }catch(const std::logic_error& e) {
        refused=std::string(e.what()).find("R06-M04A")!=std::string::npos;
    }
    check(refused,"failed terminal observer may not replace a real geometry refusal");
    const auto f=tetra();
    MixedClosureStaging stage(f.root,{f.rim});
    std::size_t callbacks=0;
    stage.setOperationObserver([&](const auto&,const auto&,const auto&) {
        ++callbacks;
        throw RepairStepCancelled{};
    });
    bool cancelled=false;
    try {
        const auto owner=f.root->vertex(f.rim.vertexIds[0]).owner;
        (void)stage.ensureFace(f.rim.vertexIds[0],
              f.rim.vertexIds[2],f.rim.vertexIds[1],
              MixedClosureFaceKind::Cap,owner,1);
    }catch(const RepairStepCancelled&) {cancelled=true;}
    check(cancelled&&callbacks==1,
          "intentional step-gate cancellation must retain its distinct type");
    stage.setOperationObserver([](const auto&,const auto&,const auto&) {
        throw std::length_error("finite diagnostic snapshot bytes");
    });
    bool diagnosticFailure=false;
    try {
        auto view=f.root->revision().createMutableView();
        const auto owner=f.root->vertex(f.rim.vertexIds[0]).owner;
        (void)stage.addVertex(view,{-10,0,0},owner);
    }catch(const RepairStepObserverFailure& e) {
        diagnosticFailure=std::string(e.what()).find(
            "diagnostic observer failed at addVertex")!=std::string::npos;
    }
    check(diagnosticFailure,
          "snapshot-budget logic_error must escape as diagnostic runtime_error, not a geometry fallback");
    std::cout<<"[r06-m16c2c] pre-stage failure envelope and cancellation identity PASS\n";
}
}
int main() {
    try {
        provisionalConversionAndRetirement();
        flipOperation();
        collapseOperation();
        fairingOperation();
        terminalCompletionAndObserverFailure();
        failureEnvelopeAndCancellation();
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"[r06-m16c2c] FAILED "<<error.what()<<"\n";
        return 1;
    }
}

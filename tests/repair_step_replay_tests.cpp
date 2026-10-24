#include "mesher/repair_case.hpp"
#include "mesher/repair_inspection.hpp"
#include "mesher/repair_step_trace.hpp"
#include "mesher/repair_step_replay_session.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/sculpt_fixtures.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace mesher;
namespace {
using namespace std::chrono_literals;
void require(bool yes,const char* reason) {
    if(!yes)throw std::runtime_error(reason);
}
RepairCase makeRealCase() {
    const auto imported=partitionFlatMesh(generateTorusFixture(5000,1,5),64);
    RepairCase saved;
    saved.candidate.index=RegionTopologyIndex::build(imported.revision);
    saved.candidate.initialVertexCount=5000;
    const auto near=[&](Vec3 xyz) {
        FaceId best=0;double minDistance=1e100;
        for(const auto id:imported.flatIndex.faceIds) {
            const auto tri=saved.candidate.index->face(id).vertices;
            const auto p=(saved.candidate.index->vertex(tri[0]).position+
                          saved.candidate.index->vertex(tri[1]).position+
                          saved.candidate.index->vertex(tri[2]).position)/3;
            if(const auto d=length(p-xyz);d<minDistance) {
                minDistance=d;best=id;
            }
        }
        return best;
    };
    auto first=near({.12,0,0}),second=near({-.12,0,0});
    if(first>second)std::swap(first,second);
    saved.contacts={{first,second,false}};
    saved.expectedClosure=true;
    saved.symmetricSource=false;
    saved.intendedSelection=RepairCaseIntent::PreferCaps;
    return saved;
}
void exactRealReplayCanPauseAndResume(const RepairCase& saved) {
    // Normal mode is the authority: the opt-in callback must not change
    // the classifier choice, admitted closure or any stable source record.
    const auto baseline=replayPrivateAutomaticRepair(saved);
    require(baseline->privateAuto&&baseline->autoPlan&&
            baseline->autoPlan->ready()&&baseline->privateCut,
            "5K real torus fixture must admit an ordinary complete R06 closure");
    const auto root=saved.candidate.index;
    const auto initialTombstones=root->retiredFaceCount();

    auto trace=std::make_shared<RepairStepTrace>();
    std::shared_ptr<const RepairInspectionSnapshot> stepped;
    std::exception_ptr failure;
    std::thread worker([&] {
        try {
            stepped=replayPrivateAutomaticRepair(
                saved,AutoClosureClassifier::Area,false,false,false,trace);
        }catch(...) {failure=std::current_exception();}
    });
    try {
        require(trace->gate().waitForSequence(1,20s),
                "a real 5K replay never reached its first staging operation");
        const auto first=trace->gate().view();
        require(first.waiting&&first.current&&first.sequence==1&&
                !first.finished&&first.current->source!=root,
                "first real step must pause worker against exact R03 cut");
        require(trace->gate().next(),
                "Next must release one real pending operation");
        require(trace->gate().waitForSequence(2,20s),
                "second real staging operation did not reach observer");
        const auto second=trace->gate().view();
        require(second.waiting&&second.sequence==2&&
                second.previous&&second.current&&
                second.previous->operation==first.current->operation,
                "two immutable real operations must be retained");
        require(trace->gate().showPrevious()&&
                trace->gate().view().displayed==second.previous,
                "Previous must select actual older snapshot without advancing worker");
        trace->gate().showCurrent();
        require(trace->gate().view().displayed==second.current,
                "Current must restore latest real-state snapshot");
        // Frame comparison deliberately uses stable IDs and shared R03.
        const auto difference=diffRepairStepGeometry(
            *second.previous,*second.current);
        (void)difference;
        trace->gate().continueRunning();
        worker.join();
        if(failure)std::rethrow_exception(failure);
        const auto finished=trace->gate().view();
        require(finished.finished&&finished.running&&finished.sequence>=2&&
                stepped&&stepped->privateAuto&&stepped->privateCut&&
                first.current->source==stepped->privateCut->index&&
                stepped->autoPlan&&
                stepped->autoPlan->cappedContours==
                    baseline->autoPlan->cappedContours&&
                stepped->privateAuto->generatedFaces==
                    baseline->privateAuto->generatedFaces&&
                stepped->privateAuto->postRetirementFaces==
                    baseline->privateAuto->postRetirementFaces&&
                stepped->privateAuto->refinementSteps==
                    baseline->privateAuto->refinementSteps&&
                stepped->autoDiagnostic==baseline->autoDiagnostic,
                "opt-in step tracing changed an admitted real automatic closure");
        require(root->retiredFaceCount()==initialTombstones&&
                root->pickingRootIdentity()==saved.candidate.index->pickingRootIdentity()&&
                baseline->privateAuto->index!=root,
                "real step mode cannot edit immutable candidate or publish a worker root");
        const auto events=trace->terminalEvents();
        require(std::any_of(events.begin(),events.end(),
            [](const auto& e) {
                return e.outcome==MixedClosureTerminalOutcome::Completed&&
                       e.phase=="certified_auto"&&!e.untrustedWarningPreview;
            }),"real stepped M04C must provide typed certified-private completion");
        std::cout<<"[r06-m16c3] 5K real replay Next/Previous/Current/Continue, exact classifier and private result PASS\n";
    }catch(...) {
        trace->cancel();
        if(worker.joinable())worker.join();
        throw;
    }
}
void cancellationDoesNotFabricateGeometryFailure(const RepairCase& saved) {
    auto trace=std::make_shared<RepairStepTrace>();
    std::atomic<bool> cancelled{false};
    std::exception_ptr wrongFailure;
    std::thread worker([&] {
        try {
            (void)replayPrivateAutomaticRepair(
                saved,AutoClosureClassifier::Area,false,false,false,trace);
        }catch(const RepairStepCancelled&) {
            cancelled=true;
        }catch(...) {
            wrongFailure=std::current_exception();
        }
    });
    try {
        require(trace->gate().waitForSequence(1,20s),
                "cancel fixture must be paused inside real private closure");
        const auto before=trace->gate().view();
        require(before.waiting&&before.current,
                "cancel cannot race with a nonpaused producer");
        trace->cancel();
        worker.join();
        if(wrongFailure)std::rethrow_exception(wrongFailure);
        const auto after=trace->gate().view();
        require(cancelled&&after.cancelled&&after.finished&&
                after.current==before.current&&
                !trace->gate().next(),
                "Cancel must wake worker, retain immutable evidence and grant no geometry step");
        const auto events=trace->terminalEvents();
        require(std::none_of(events.begin(),events.end(),
            [](const auto& event) {
                return event.outcome==MixedClosureTerminalOutcome::Refused;
            }),"user cancellation must not manufacture geometric refusal");
        require(saved.candidate.index->retiredFaceCount()==0,
                "cancelled trace must not change original saved-case mesh");
        std::cout<<"[r06-m16c3] real replay blocked-worker Cancel/Join without publication or false refusal PASS\n";
    }catch(...) {
        trace->cancel();
        if(worker.joinable())worker.join();
        throw;
    }
}
void viewportBridgeSession(const RepairCase& saved) {
    RepairReplayStepSession session(
        saved,AutoClosureClassifier::Area,false,false,false);
    session.start(); // no Vulkan calls; production starts after onReady
    bool gotFirst=false;
    for(int n=0;n<3000;++n) {
        if(auto frame=session.takeSnapshot()) {
            if(frame->inspectionStepReplaceMesh) {
                require(frame->inspectionFrozen&&
                        frame->inspectionAutoRequested&&
                        !frame->inspectionOverlay.empty()&&
                        !frame->repairStepLines.empty()&&
                        frame->repairStepLines[0].find("STEP 1")!=std::string::npos,
                        "viewport bridge must receive finite exact R03 stage as replacement mesh");
                require(frame->classifierChoices[0]!="UNAVAILABLE"&&
                        frame->classifierChoices[1]!="UNAVAILABLE",
                        "classifier comparison must be available BEFORE first pause");
                gotFirst=true;
                break;
            }
        }
        std::this_thread::sleep_for(2ms);
    }
    require(gotFirst,"viewport bridge never displayed the first stage frame");
    session.command(0); // Next
    bool gotSecond=false;
    for(int n=0;n<3000;++n) {
        if(auto frame=session.takeSnapshot()) {
            if(frame->inspectionStepReplaceMesh&&
               frame->repairStepLines[0].find("STEP 2")!=std::string::npos) {
                gotSecond=true;
                break;
            }
        }
        std::this_thread::sleep_for(2ms);
    }
    require(gotSecond,"viewport bridge did not display second coherent edit");
    session.command(1); // Previous: presentation only
    const auto older=session.takeSnapshot();
    require(older&&older->inspectionStepReplaceMesh&&
            older->repairStepLines[0].find("PREVIOUS")!=std::string::npos,
            "viewport bridge Previous must not advance source staging");
    session.command(2);
    const auto current=session.takeSnapshot();
    require(current&&current->repairStepLines[0].find("CURRENT")!=std::string::npos,
            "viewport bridge Current must restore latest stage frame");
    session.command(3); // Continue; join is performed on teardown
    bool finished=false;
    for(int n=0;n<4000;++n) {
        (void)session.takeSnapshot();
        if(session.caption().find("FINISHED")!=std::string::npos) {
            if(auto final=session.takeSnapshot()) {
                require(final->inspectionStepReplaceMesh&&
                        final->inspectionAutoSucceeded&&
                        final->repairStepLines.back().find("ESC EXIT")!=std::string::npos,
                        "terminal viewport state must preserve last staged mesh and controls");
            }
            finished=true;
            break;
        }
        std::this_thread::sleep_for(2ms);
    }
    require(finished,"viewport session worker failed to complete after Continue");
    session.cancelAndJoin();
    std::cout<<"[r06-m16d3] read-only viewport bridge complete R03 scene and terminal replay PASS\n";
}
void refusalProvenanceUnchanged(const RepairCase& accepted) {
    auto refused=accepted;
    refused.symmetricSource=true; // real saved-case symmetry mismatch
    const auto original=replayPrivateAutomaticRepair(refused);
    auto trace=std::make_shared<RepairStepTrace>();
    // Continue also works before any snapshots and does not block.
    trace->gate().continueRunning();
    const auto diagnostic=replayPrivateAutomaticRepair(
        refused,AutoClosureClassifier::Area,false,false,false,trace);
    require(!original->privateAuto&&!diagnostic->privateAuto&&
            diagnostic->autoDiagnostic==original->autoDiagnostic&&
            diagnostic->autoPlan->cappedContours==
                original->autoPlan->cappedContours&&
            trace->gate().view().finished,
            "traced real mirror refusal must retain precise untraced refusal");
    const auto events=trace->terminalEvents();
    require(std::any_of(events.begin(),events.end(),
        [](const auto& e) {
            return e.outcome==MixedClosureTerminalOutcome::Refused&&
                   !e.untrustedWarningPreview;
        }),"real refused replay must expose terminal refusal provenance");
    std::cout<<"[r06-m16c3] real symmetry refusal exact outcome and terminal provenance PASS\n";
}
}
int main() {
    try {
        const auto original=makeRealCase();
        // Run through the actual archived .r06case serializer, not merely
        // an in-memory approximation of the replay contract.
        const auto path=std::filesystem::current_path()/
            "r06-m16c3-exact-saved-case.r06case";
        std::filesystem::remove(path);
        writeRepairCase(path,original);
        const auto saved=readRepairCase(path);
        std::filesystem::remove(path);
        require(saved.contacts==original.contacts&&
                saved.expectedClosure==original.expectedClosure&&
                saved.symmetricSource==original.symmetricSource&&
                saved.candidate.index&&
                saved.candidate.index!=original.candidate.index,
                "real step-replay fixture must preserve exact saved R01 contact IDs");
        exactRealReplayCanPauseAndResume(saved);
        cancellationDoesNotFabricateGeometryFailure(saved);
        viewportBridgeSession(saved);
        refusalProvenanceUnchanged(saved);
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"[r06-m16c3] FAILED: "<<error.what()<<"\n";
        return 1;
    }
}

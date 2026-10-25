#include "mesher/repair_step_replay_session.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace mesher {
RepairReplayStepSession::RepairReplayStepSession(
    RepairCase saved,AutoClosureClassifier classifier,bool warningOnly,
    bool seamAudit,bool exactOverlay,bool scaleAware,
    bool optimizeFront,bool optimizeCurvatureFront,bool guideCurvaturePoisson,
    bool probeSeamFeasibility,bool probeDiagonalFeasibility,
    bool stageFeasibilityPreview,bool stageDiagonalPreview,
    bool diagnoseRecursiveCap)
    :saved_(std::move(saved)),classifier_(classifier),
     warningOnly_(warningOnly),seamAudit_(seamAudit),exactOverlay_(exactOverlay),
     scaleAware_(scaleAware),optimizeFront_(optimizeFront),
     optimizeCurvatureFront_(optimizeCurvatureFront),
     guideCurvaturePoisson_(guideCurvaturePoisson),
     probeSeamFeasibility_(probeSeamFeasibility),
     probeDiagonalFeasibility_(probeDiagonalFeasibility),
     stageFeasibilityPreview_(stageFeasibilityPreview),
     stageDiagonalPreview_(stageDiagonalPreview),
     diagnoseRecursiveCap_(diagnoseRecursiveCap),
     trace_(std::make_shared<RepairStepTrace>()) {
    if(!saved_.candidate.index||saved_.contacts.empty())
        throw std::invalid_argument("R06-M16D step replay requires an exact archived case");
}
RepairReplayStepSession::~RepairReplayStepSession() {cancelAndJoin();}
void RepairReplayStepSession::start() {
    bool expected=false;
    if(!started_.compare_exchange_strong(expected,true))return;
    try {
        worker_=std::thread([this] {
            try {
                auto value=replayPrivateAutomaticRepair(
                    saved_,classifier_,warningOnly_,seamAudit_,exactOverlay_,
                    trace_,scaleAware_,optimizeFront_,optimizeCurvatureFront_,
                    guideCurvaturePoisson_,probeSeamFeasibility_,
                    probeDiagonalFeasibility_,stageFeasibilityPreview_,
                    stageDiagonalPreview_,diagnoseRecursiveCap_);
                std::lock_guard lock(stateMutex_);
                result_=std::move(value);
            }catch(const RepairStepCancelled&) {
                // Intentional exit never becomes a private-geometry refusal.
            }catch(const std::exception& e) {
                std::lock_guard lock(stateMutex_);
                error_=e.what();
            }catch(...) {
                std::lock_guard lock(stateMutex_);
                error_="unknown worker failure";
            }
            trace_->finish();
            done_.store(true,std::memory_order_release);
        });
    }catch(...) {
        started_.store(false);
        throw;
    }
}
void RepairReplayStepSession::command(int cmd) {
    switch(cmd) {
    case 0:(void)trace_->gate().next();break;
    case 1:(void)trace_->gate().showPrevious();break;
    case 2:trace_->gate().showCurrent();break;
    case 3:trace_->gate().continueRunning();break;
    case 4:
        // A completed repair is not retroactively "cancelled" by a
        // late keypress; only a calculation that can still pause may stop.
        if(!trace_->gate().view().finished)trace_->cancel();
        break;
    default:break;
    }
}
void RepairReplayStepSession::cancelAndJoin() {
    trace_->cancel();
    if(worker_.joinable())worker_.join();
}
std::string RepairReplayStepSession::caption() const {
    const auto state=trace_->gate().view();
    if(state.cancelled)return "R06 STEP CANCELLED";
    if(!started_.load())return "R06 STEP NOT STARTED";
    if(state.finished)return "R06 STEP EXECUTION FINISHED";
    return "R06 STEP "+std::to_string(state.sequence)+
           (state.waiting?" PAUSED":state.running?" CONTINUING":" EXECUTING");
}
std::optional<ViewportMeshUpdate> RepairReplayStepSession::takeSnapshot() {
    const auto state=trace_->gate().view();
    const bool done=done_.load(std::memory_order_acquire);
    if(presentedInitial_&&lastFrame_==state.displayed.get()&&
       lastSequence_==state.sequence&&
       lastFinished_==state.finished&&lastCancelled_==state.cancelled&&
       lastDone_==done&&lastWaiting_==state.waiting&&
       lastRunning_==state.running)return std::nullopt;
    if(!state.displayed&&!done&&!state.cancelled)return std::nullopt;

    presentedInitial_=true;
    lastFrame_=state.displayed.get();
    lastSequence_=state.sequence;
    lastFinished_=state.finished;
    lastCancelled_=state.cancelled;
    lastDone_=done;
    lastWaiting_=state.waiting;
    lastRunning_=state.running;
    std::shared_ptr<const RepairInspectionSnapshot> final;
    std::string failure;
    if(done) {
        std::lock_guard lock(stateMutex_);
        final=result_;
        failure=error_;
    }
    // No repair verdict is implied by an observer/worker failure.
    const bool diagnosticError=!failure.empty();
    ViewportMeshUpdate update;
    update.index=saved_.candidate.index;
    update.inspectionFrozen=true;
    update.inspectionPreview=true;
    update.inspectionCutRequested=true;
    update.inspectionAutoRequested=true;
    update.inspectionStepFinished=done||state.cancelled;
    update.inspectionStepCancelled=state.cancelled;
    update.inspectionStepError=diagnosticError;
    const auto comparison=trace_->classifier();
    update.classifierChoices=comparison.choices;
    update.classifierComparable=comparison.comparable;
    update.classifiersAgree=comparison.agree;
    update.activeClassifier=comparison.active;
    if(final) {
        update.inspectionAutoSucceeded=bool(final->privateAuto);
        update.inspectionQualityWarningPreview=bool(final->warningPreview);
        update.inspectionCutSucceeded=bool(final->privateCut);
        update.classifierChoices=final->classifierChoices;
        update.classifierComparable=final->classifierComparable;
        update.classifiersAgree=final->classifiersAgree;
        update.activeClassifier=
            final->activeClassifier==AutoClosureClassifier::Proximity?1:0;
    }
    auto& lines=update.repairStepLines;
    if(state.displayed) {
        const auto& frame=*state.displayed;
        auto scene=buildRepairStepScene(
            frame,state.previous.get(),state.current.get(),
            state.showingPrevious);
        update.inspectionStepReplaceMesh=true;
        update.inspectionOverlay=std::move(scene.triangles);
        lines.push_back("STEP "+std::to_string(
             state.sequence-(state.showingPrevious?std::size_t{1}:std::size_t{0}))+
             (state.showingPrevious?" PREVIOUS":" CURRENT")+
             (state.waiting?" PAUSED":state.running?" CONTINUE":" EXECUTING"));
        // Operation details always describe the MOST RECENT worker edit,
        // even when displaying its cached predecessor for comparison.
        const auto& latest=*state.current;
        lines.push_back("LATEST OP: "+latest.operation);
        lines.push_back("LATEST ARGS: "+latest.arguments);
        if(state.showingPrevious) {
            lines.push_back("DISPLAY: STATE BEFORE LATEST OP");
            lines.push_back("PREV OP: "+frame.operation);
        }
        if(state.previous&&state.current) {
            const auto diff=diffRepairStepGeometry(
                *state.previous,*state.current);
            lines.push_back("DELTA: NEW FACES "+std::to_string(diff.addedFaces.size())+
                " OLD FACES "+std::to_string(diff.removedFaces.size())+
                " MOVED VERTICES "+std::to_string(diff.movedVertices.size())+
                " SOURCE FACES "+std::to_string(diff.modifiedSurvivorFaces.size()));
        }
        lines.push_back("COLORS: NEW CYAN OLD PINK CAP GREEN ZIP BLUE");
        if(diagnoseRecursiveCap_) {
            lines.push_back("M20A ORANGE ANOMALY / PURPLE PROPOSED SECOND HOLE");
            if(!frame.capAnomalyStatus.empty())
                lines.push_back(frame.capAnomalyStatus);
            lines.push_back("M20A FACE PROPOSALS "+
                std::to_string(scene.suspectCapFaces)+
                " DISPLAYED / RIM EDGES "+
                std::to_string(scene.secondaryContourEdges));
        }
        if(!frame.boundaryDiagnostic.empty())
            lines.push_back("BOUNDARY: "+frame.boundaryDiagnostic);
        if(scene.warningOnly)
            lines.push_back("UNTRUSTED QUALITY WARNING PREVIEW - NOT CERTIFIED");
    }else {
        // A refusal can happen before the first stage operation exists.
        // Fall back to the normal exact R03/candidate inspection display.
        update.inspectionStepReplaceMesh=false;
        if(final) {
            update.index=final->privateAuto?final->privateAuto->index:
                final->warningPreview?final->warningPreview->index:
                final->privateCut?final->privateCut->index:final->candidate;
            update.inspectionOverlay=buildRepairInspectionOverlay(*final);
            std::set<PartitionId> all;
            update.index->revision().forEachPartition(
                [&](PartitionId id,const MeshPartition&){all.insert(id);});
            saved_.candidate.index->revision().forEachPartition(
                [&](PartitionId id,const MeshPartition&){all.insert(id);});
            update.dirtyPartitions.assign(all.begin(),all.end());
        }
        lines.push_back("STEP 0 NO STAGED GEOMETRY");
    }
    if(stageFeasibilityPreview_)
        lines.push_back("M19A PROVISIONAL POSITION PREVIEW - NOT CERTIFIED; NO PUBLICATION");
    if(stageDiagonalPreview_)
        lines.push_back("M19D PROVISIONAL DIAGONAL PREVIEW - NOT CERTIFIED; NO PUBLICATION");
    if(diagnoseRecursiveCap_)
        lines.push_back("M20A EXCISION PROPOSAL ONLY - NO FACES REMOVED; NO PUBLICATION");
    if(state.cancelled)lines.push_back("WORKER CANCELLED - NO GEOMETRY REFUSAL");
    else if(!failure.empty())lines.push_back("WORKER ERROR: "+failure);
    else if(done&&final) {
        lines.push_back(final->privateAuto?
            "M04C PRIVATE AUTO COMPLETE":"M04C PRIVATE AUTO REFUSED");
        if(!final->autoDiagnostic.empty())
            lines.push_back("REASON: "+final->autoDiagnostic);
    }else if(done)lines.push_back("WORKER FINISHED WITHOUT RESULT");
    const auto terminal=trace_->terminalEvents();
    if(!terminal.empty()) {
        const auto& last=terminal.back();
        lines.push_back("TERMINAL "+last.phase+": "+last.detail);
        if(last.untrustedWarningPreview)
            lines.push_back("TERMINAL IS UNTRUSTED WARNING-ONLY PREVIEW");
    }
    lines.push_back("N NEXT / P PREVIOUS / C CURRENT / ENTER CONTINUE");
    lines.push_back("BACKSPACE CANCEL / PGUP PGDN ARGUMENT SCROLL / ESC EXIT");
    return update;
}
} // namespace mesher

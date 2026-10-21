#include "mesher/async_sculpt.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/offmode_collision.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using namespace mesher;

namespace {
void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
void appendTetra(Mesh& mesh,Vec3 c,double scale=1) {
    const auto at=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto p:std::array<Vec3,4>{{
        {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
    }})mesh.vertices.push_back(c+p*scale);
    for(const auto t:std::array<std::array<unsigned,3>,4>{{
        {0,1,2},{0,2,3},{0,3,1},{1,3,2}
    }})mesh.triangles.push_back({at+t[0],at+t[1],at+t[2]});
}
Mesh detachedWithRemoteCleanPatch() {
    Mesh mesh;
    appendTetra(mesh,{-1.15,0,0});
    appendTetra(mesh,{1.15,.19,-.11});
    // A third independent, far-away clean patch permits a SECOND valid
    // 4D worker input on the exact root after an initial unsafe refusal.
    appendTetra(mesh,{40,0,0},8);
    return mesh;
}
constexpr BrushArea crossingArea{{0,0,0},6,.3};
constexpr BrushArea distantArea{{32,-8,-8},2,.3};
std::size_t waitDrained(AsyncSculptWorker& worker) {
    const auto until=std::chrono::steady_clock::now()+
                     std::chrono::seconds(15);
    while(!worker.canBegin()&&std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    require(worker.canBegin(),"V18 queued 4D Begin/End worker did not drain");
    const auto state=worker.status();
    require(state.pendingSteps==0&&state.pendingSegments==0&&
            state.inputCommands==0,
            "V18 all input commands drain even after a rejected contact");
    return state.completedStamps;
}

double findActuallyCrossingStrength(const Mesh& mesh) {
    Mesh dummy;
    for(const double strength:{.35,.55,.8,1.,1.3,2.,3.}) {
        bool sawCandidate=false;
        bool foundNew=false;
        VersionedSculptBrush trial(mesh,strength,{},{},{},false,256,
            [&](const ProposedSculptStep& step) -> std::string {
                sawCandidate=true;
                const auto result=assessOffModePostStateContacts(
                    step.before,step.candidate,
                    step.changedVertices,step.changedFaces);
                foundNew=result.status==OffModeContactStatus::NewContact;
                return {};
            });
        const std::array<BrushArea,1> area{{crossingArea}};
        trial.stamp(dummy,area);
        if(sawCandidate&&foundNew&&trial.publishedRevisions()==1)
            return strength;
    }
    throw std::runtime_error(
        "V18 baseline produced no genuinely post-sculpt new contact");
}
SculptCollisionSettings enabled() {
    SculptCollisionSettings settings;
    settings.offModeConstraints.enabled=true;
    settings.offModeConstraints.backtrack.enabled=false; // frozen M02 refusal semantics
    return settings;
}
void testIllegalCombinations() {
    Mesh mesh;
    appendTetra(mesh,{0,0,0});
    for(unsigned combination=0;combination<2;++combination) {
        auto settings=enabled();
        settings.physicalExact=combination==0;
        settings.enabled=combination!=0;
        bool refused=false;
        try {
            VersionedSculptBrush invalid(mesh,.05,{},{},{},false,256,{},settings);
        }catch(const std::invalid_argument&) {refused=true;}
        require(refused,
                "V18 OFF-mode constraints cannot silently combine with either Boolean policy");
    }
}
void testDirectPrivateRefusalAndWork(const Mesh& mesh,double strength) {
    Mesh dummy;
    const auto settings=enabled();
    VersionedSculptBrush guarded(mesh,strength,{},{},{},false,256,{},settings);
    const auto before=guarded.snapshot();
    const auto picker=before->pickingRootIdentity();
    const auto checkpoint=guarded.checkpoint();
    const auto oldHistory=guarded.historyStatistics();
    const std::array<BrushArea,1> collision{{crossingArea}};
    guarded.stamp(dummy,collision);
    const auto cost=guarded.lastStepResourceWork();
    const auto aggregate=guarded.resourceTotals();
    require(guarded.rejectedStamps()==1&&
            guarded.completedStamps()==1&&
            guarded.collisionCheckedStamps()==1&&
            guarded.publishedRevisions()==0&&
            guarded.snapshot()==before&&
            guarded.snapshot()->pickingRootIdentity()==picker&&
            guarded.lastDirtyPartitions().empty()&&
            guarded.historyStatistics().undoDepth==oldHistory.undoDepth&&
            guarded.historyStatistics().redoDepth==oldHistory.redoDepth&&
            guarded.checkpoint().index==checkpoint.index&&
            guarded.checkpoint().mirrorPairs==checkpoint.mirrorPairs,
            "M02 unsafe complete original candidate refuses without COW, history, mirror or render publication");
    require(guarded.lastRejection().find("V18 OFF-mode")!=std::string::npos&&
            guarded.lastRejection().find("new nonadjacent contact")!=std::string::npos,
            "M02 reports the specific conservative new-contact reason");
    require(cost.evaluated&&cost.refused&&!cost.published&&
            cost.privateCopiedBytes>0&&aggregate.attempts==1&&
            aggregate.refused==1&&aggregate.published==0&&
            aggregate.attemptedPrivateCopiedBytes>=cost.privateCopiedBytes&&
            aggregate.publishedPrivateCopiedBytes==0,
            "M02 refused candidate accounts authentic attempted private COW and no published costs");
    require(!guarded.terminalFailure(),
            "M02 pre-CAS geometry veto is recoverable, not post-CAS quarantine");

    // SAME brush accepts subsequent motion on a distant independent
    // surface without a new worker or corrupt history after rollback.
    const std::array<BrushArea,1> valid{{distantArea}};
    guarded.stamp(dummy,valid);
    require(guarded.rejectedStamps()==1&&
            guarded.publishedRevisions()==1&&
            guarded.snapshot()!=before&&
            !guarded.lastDirtyPartitions().empty()&&
            guarded.historyStatistics().undoDepth==1,
            "A later valid OFF-mode edit publishes one immutable Step after prior contact refusal");
    const auto accepted=guarded.snapshot();
    require(guarded.undo()&&guarded.snapshot()==before&&
            guarded.redo()&&guarded.snapshot()==accepted,
            "M02 undo/redo restores original/accepted immutable root pointers after refusal");
}
void testPreserveUnrelatedHistoricalCrossings() {
    Mesh source;
    appendTetra(source,{0,0,0});
    appendTetra(source,{0,0,0}); // intentionally pre-existing bad contact
    appendTetra(source,{40,0,0},8); // independent local edit target
    const auto indexed=partitionFlatMesh(source,256);
    const std::vector<FaceId> oldOverlap{
        indexed.flatIndex.faceIds.begin(),
        indexed.flatIndex.faceIds.begin()+8};
    Mesh dummy;
    VersionedSculptBrush guarded(source,.05,{},{},{},false,256,{},enabled());
    const auto initial=guarded.snapshot();
    const auto existing=initial->triangleIntersections({},oldOverlap);
    require(!existing.empty(),
            "Source contains genuine unrelated old nonadjacent crossings");
    const std::array<BrushArea,1> remote{{distantArea}};
    guarded.stamp(dummy,remote);
    require(guarded.rejectedStamps()==0&&
            guarded.publishedRevisions()==1&&
            guarded.snapshot()!=initial&&
            guarded.historyStatistics().undoDepth==1&&
            guarded.lastStepResourceWork().published,
            "Real opt-in M02 stamp must NOT globally refuse unrelated old crossing sources");
    const auto after=guarded.snapshot()->triangleIntersections({},oldOverlap);
    require(after==existing,
            "M02 leaves unrelated source-independent historical crossings untouched");
}
void testBudgetAndClean() {
    Mesh tetra;
    appendTetra(tetra,{0,0,0});
    Mesh dummy;
    const std::array<BrushArea,1> edit{{{{-1,-1,-1},.5,.3}}};
    VersionedSculptBrush safe(tetra,.05,{},{},{},false,256,{},enabled());
    const auto original=safe.snapshot();
    safe.stamp(dummy,edit);
    require(safe.rejectedStamps()==0&&
            safe.publishedRevisions()==1&&
            safe.collisionCheckedStamps()==1&&
            safe.snapshot()!=original&&
            safe.historyStatistics().undoDepth==1,
            "Explicit OFF-mode candidate with no new collision accepts exactly once");

    auto limited=enabled();
    limited.offModeConstraints.contactLimits.query.maxSeedFaces=0;
    VersionedSculptBrush bounded(tetra,.05,{},{},{},false,256,{},limited);
    const auto prior=bounded.snapshot();
    bounded.stamp(dummy,edit);
    require(bounded.rejectedStamps()==1&&
            bounded.publishedRevisions()==0&&
            bounded.snapshot()==prior&&
            bounded.historyStatistics().undoDepth==0&&
            bounded.lastRejection().find("budget")!=std::string::npos&&
            bounded.resourceTotals().budgetRefusals==1&&
            bounded.lastStepResourceWork().privateCopiedBytes>0,
            "M02 finite work exhaustion safely refuses full Step with measured attempted work");
}
void testActualAsync4DFifo(const Mesh& mesh,double strength) {
    auto settings=enabled();
    AsyncSculptWorker worker(mesh,.025,strength,.2,{},{},{},false,{},settings);
    const auto initial=worker.initialRevision();
    worker.begin(crossingArea.center,crossingArea.radius);
    worker.end(crossingArea.center,0);
    const auto total=waitDrained(worker);
    const auto refused=worker.status();
    require(total>=1&&refused.rejectedStamps>=1&&
            refused.publishedRevisions==0&&
            refused.history.undoDepth==0&&
            refused.resources.refused>=1&&
            refused.resources.attemptedPrivateCopiedBytes>0&&
            refused.lastRejection.find("V18 OFF-mode")!=std::string::npos&&
            worker.initialRevision()==initial,
            "M02 actual queued crossing is rejected with unchanged source/picker/history and charged work");
    const auto notice=worker.takeSnapshot();
    if(notice)
        require(notice->index==initial&&notice->dirtyPartitions.empty(),
                "Rejected Step cannot publish any dirty geometry frame");

    worker.begin(distantArea.center,distantArea.radius);
    worker.end(distantArea.center,0);
    waitDrained(worker);
    const auto recovered=worker.status();
    require(recovered.rejectedStamps==refused.rejectedStamps&&
            recovered.publishedRevisions==1&&
            recovered.history.undoDepth==1&&
            recovered.pendingSteps==0&&recovered.pendingSegments==0&&
            recovered.resources.published==1&&
            recovered.resources.refused>=1,
            "Worker continues queued real 4D input after bounded V18 refusal");
    const auto accepted=worker.initialRevision();
    require(accepted&&accepted!=initial,
            "Subsequent valid worker Step publishes its final immutable index root");
    worker.undo();
    const auto until=std::chrono::steady_clock::now()+
                     std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<until) {
        const auto status=worker.status();
        if(status.history.undoDepth==0&&worker.initialRevision()==initial)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(worker.initialRevision()==initial,
            "M02 FIFO worker undo returns to pointer-exact original after a rejected Step");
}
}
int main() {
    try {
        testIllegalCombinations();
        const auto mesh=detachedWithRemoteCleanPatch();
        const auto strength=findActuallyCrossingStrength(mesh);
        testDirectPrivateRefusalAndWork(mesh,strength);
        testPreserveUnrelatedHistoricalCrossings();
        testBudgetAndClean();
        testActualAsync4DFifo(mesh,strength);
        std::cout<<"V18-M02 private OFF-mode terminal admission: PASS (strength "
                 <<strength<<")\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"V18-M02 private OFF-mode terminal admission: "
                 <<e.what()<<'\n';
        return 1;
    }
}

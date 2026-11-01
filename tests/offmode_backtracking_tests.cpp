#include "mesher/async_sculpt.hpp"
#include "mesher/offmode_clearance.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace mesher;
namespace {
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
Mesh closeParallelSheets() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{2,0,0},{0,2,0},
                   {0,0,.14},{2,0,.14},{0,2,.14}};
    mesh.triangles={{0,1,2},{3,4,5}};
    return mesh;
}
Mesh withRemoteEdit() {
    auto mesh=closeParallelSheets();
    mesh.vertices.insert(mesh.vertices.end(),
        {{30,0,0},{32,0,0},{30,2,0}});
    mesh.triangles.push_back({6,7,8});
    return mesh;
}
SculptCollisionSettings constrained() {
    SculptCollisionSettings cfg;
    cfg.offModeConstraints.enabled=true;
    return cfg;
}
constexpr BrushArea approach{{0,0,0},.13,.3};
constexpr BrushArea outward{{0,0,.14},.13,.3};
constexpr BrushArea distant{{30,0,0},.13,.3};

void acceptedBoundedNormalMotion() {
    const auto initial=closeParallelSheets();
    VersionedSculptBrush brush(initial,.12,{},{},{},false,256,{},constrained());
    const auto root=brush.snapshot();
    const auto picker=root->pickingRootIdentity();
    Mesh dummy;
    const std::array<BrushArea,1> areas{{approach}};
    brush.stamp(dummy,areas);
    const auto work=brush.lastStepResourceWork();
    const auto back=work.backtracking;
    require(brush.completedStamps()==1&&brush.rejectedStamps()==0&&
            brush.publishedRevisions()==1&&
            brush.snapshot()!=root&&
            brush.historyStatistics().undoDepth==1&&
            back.trials>=2&&back.trials<=5&&back.declined>=1&&
            back.chosenNormalFraction>0&&
            back.chosenNormalFraction<1&&
            work.published&&!work.refused&&
            back.privateCopiedBytes>0&&
            work.privateCopiedBytes>back.privateCopiedBytes&&
            back.distanceTests>=2,
            "Dangerous full normal stamp backtracks to one certified bounded private revision");
    const auto first=brush.originalOrder()->vertexIds[0];
    const auto before=root->vertex(first).position;
    const auto after=brush.snapshot()->vertex(first).position;
    require(after.z>before.z&&after.z<.05&&
            after.x==before.x&&after.y==before.y,
            "Only the normal component was attenuated; no spurious tangent displacement");
    require(root->pickingRootIdentity()==picker&&
            root->vertex(first).position.z==0,
            "All speculative candidates retained the previous immutable picker root");
    const auto accepted=brush.snapshot();
    require(brush.undo()&&brush.snapshot()==root&&
            brush.redo()&&brush.snapshot()==accepted,
            "All bounded trials end with exactly one pointer-exact root/history transition");
}

void noRetryAndBudgetRefusals() {
    Mesh dummy;
    auto one=constrained();
    one.offModeConstraints.backtrack.maxTrials=1;
    VersionedSculptBrush limited(closeParallelSheets(),.12,{},{},{},
                                 false,256,{},one);
    const auto base=limited.snapshot();
    const std::array<BrushArea,1> positions{{approach}};
    limited.stamp(dummy,positions);
    const auto denied=limited.lastStepResourceWork();
    require(limited.rejectedStamps()==1&&
            limited.publishedRevisions()==0&&
            limited.snapshot()==base&&
            limited.historyStatistics().undoDepth==0&&
            limited.lastDirtyPartitions().empty()&&
            denied.backtracking.trials==1&&
            denied.backtracking.refused&&
            denied.refused&&!denied.published&&denied.evaluated&&
            denied.privateCopiedBytes>denied.backtracking.privateCopiedBytes&&
            limited.lastRejection().find("exhausted")!=std::string::npos,
            "A single allowed trial refuses the full unsafe Step and bills all attempted work");
    // Same live brush and immutable root must remain usable.
    const std::array<BrushArea,1> safe{{outward}};
    limited.stamp(dummy,safe);
    require(limited.rejectedStamps()==1&&
            limited.publishedRevisions()==1&&
            limited.historyStatistics().undoDepth==1,
            "A later safe full normal stamp still publishes after exhausted backtracking");

    auto bytes=constrained();
    bytes.offModeConstraints.backtrack.maxTrialCopiedBytes=0;
    VersionedSculptBrush capped(closeParallelSheets(),.12,{},{},{},
                                false,256,{},bytes);
    const auto source=capped.snapshot();
    capped.stamp(dummy,positions);
    const auto used=capped.lastStepResourceWork();
    require(capped.rejectedStamps()==1&&
            capped.publishedRevisions()==0&&
            capped.snapshot()==source&&
            used.backtracking.refused&&used.backtracking.trials==1&&
            used.privateCopiedBytes>0&&used.refused&&
            capped.resourceTotals().budgetRefusals==1,
            "Cumulative trial COW exhaustion refuses safely with honest nonzero billed work");
    auto invalid=constrained();
    invalid.offModeConstraints.backtrack.maxTrials=9;
    bool threw=false;
    try {
        VersionedSculptBrush rejected(closeParallelSheets(),.12,
                                      {},{},{},false,256,{},invalid);
    }catch(const std::invalid_argument&) {threw=true;}
    require(threw,"Unbounded private retry settings reject at construction");
}
void conservativeLargeStepPolicy() {
    Mesh source;
    source.vertices={{0,0,0},{2,0,0},{0,2,0}};
    source.triangles={{0,1,2}};
    auto setup=constrained();
    VersionedSculptBrush brush(source,5.0,{},{},{},false,256,{},setup);
    const auto original=brush.snapshot();
    const auto stable=brush.originalOrder()->vertexIds[0];
    Mesh dummy;
    const std::array<BrushArea,1> areas{{approach}};
    brush.stamp(dummy,areas);
    const auto back=brush.lastStepResourceWork().backtracking;
    require(brush.publishedRevisions()==1&&brush.rejectedStamps()==0&&
            back.trials>=3&&back.chosenNormalFraction<=.25&&
            back.chosenNormalFraction>0&&
            brush.snapshot()->vertex(stable).position.z<=1.0&&
            original->vertex(stable).position.z==0,
            "Endpoint-clean huge Inflate is attenuated under local pre-stamp edge bound");
    auto shortBudget=constrained();
    shortBudget.offModeConstraints.backtrack.maxTrials=1;
    VersionedSculptBrush reject(source,5.0,{},{},{},false,256,{},shortBudget);
    const auto previous=reject.snapshot();
    reject.stamp(dummy,areas);
    require(reject.rejectedStamps()==1&&reject.publishedRevisions()==0&&
            reject.snapshot()==previous&&
            reject.lastStepResourceWork().backtracking.refused&&
            reject.lastDirtyPartitions().empty(),
            "Unresolved tunnelling-risk large stamp refuses without any history or root publication");
}

void queued4DWorkerAfterBacktracking() {
    AsyncSculptWorker worker(withRemoteEdit(),.02,.12,.2,{},{},{},
                             false,{},constrained());
    const auto original=worker.initialRevision();
    worker.begin(approach.center,approach.radius);
    worker.end(approach.center,0);
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(15);
    while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    require(worker.canBegin(),"Bounded backtracking first 4D input drained");
    const auto first=worker.status();
    require(first.completedStamps>=1&&first.publishedRevisions==1&&
            first.rejectedStamps==0&&first.history.undoDepth==1&&
            first.resources.published==1&&first.resources.attempts==1,
            "One accepted backtracked 4D Step yields exactly one root/history publication");
    worker.begin(distant.center,distant.radius);
    worker.end(distant.center,0);
    const auto later=std::chrono::steady_clock::now()+
                     std::chrono::seconds(15);
    while(!worker.canBegin()&&std::chrono::steady_clock::now()<later)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto second=worker.status();
    require(worker.canBegin()&&second.publishedRevisions==2&&
            second.history.undoDepth==2&&second.resources.published==2&&
            second.pendingSteps==0&&second.pendingSegments==0,
            "A later independent real 4D Step drains normally after bounded accepted retry");
    require(worker.initialRevision()!=original,
            "Worker holds a newly accepted immutable source after both stamped Steps");
}
}
int main() {
    try {
        acceptedBoundedNormalMotion();
        noRetryAndBudgetRefusals();
        conservativeLargeStepPolicy();
        queued4DWorkerAfterBacktracking();
        std::cout<<"V18-M04 bounded private normal backtracking: PASS\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"V18-M04: "<<error.what()<<'\n';
        return 1;
    }
    return 0;
}

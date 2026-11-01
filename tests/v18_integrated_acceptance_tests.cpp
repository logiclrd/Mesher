#include "mesher/desktop_collision_policy.hpp"
#include "mesher/desktop_offmode_fixture.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <system_error>

using namespace mesher;
namespace {
void require(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}
void realFixtureAcceptance() {
    auto mesh=desktopOffModeNearSheetsFixture();
    require(mesh.vertices.size()==9&&mesh.triangles.size()==3,
            "M07 named GPU fixture retains three independent face owners");
    auto cfg=desktopCollisionSettings({false,true,true});
    VersionedSculptBrush brush(mesh,.5,{},{},{},false,256,{},cfg);
    auto original=brush.snapshot();
    const auto first=brush.originalOrder()->vertexIds[0];
    const auto other=brush.originalOrder()->vertexIds[3];
    const auto remote=brush.originalOrder()->vertexIds[6];
    const auto remotePosition=original->vertex(remote).position;
    Mesh dummy;
    const std::array<BrushArea,1> close{{{{0,0,0},.5,.3}}};
    const std::array<BrushArea,1> safe{{{{3.5,0,0},.5,.3}}};
    brush.stamp(dummy,close);
    const auto accepted=brush.snapshot();
    const auto firstWork=brush.lastStepResourceWork();
    require(brush.completedStamps()==1&&brush.rejectedStamps()==0&&
            brush.publishedRevisions()==1&&accepted!=original&&
            firstWork.backtracking.trials>=2&&
            firstWork.backtracking.chosenNormalFraction>0&&
            firstWork.backtracking.chosenNormalFraction<1&&
            firstWork.backtracking.privateCopiedBytes>0&&
            firstWork.privateCopiedBytes>firstWork.backtracking.privateCopiedBytes,
            "Real visible near-sheet source requires certified normal attenuation before one CAS");
    require(accepted->vertex(first).position.z>0&&
            accepted->vertex(first).position.z<.3&&
            accepted->vertex(other).position.z==.55&&
            accepted->vertex(remote).position.x==remotePosition.x&&
            accepted->vertex(remote).position.z==remotePosition.z,
            "Near-sheet accepted normal displacement is bounded and does not touch other or remote surface");
    require(brush.undo()&&brush.snapshot()==original&&
            brush.redo()&&brush.snapshot()==accepted,
            "Named GUI fixture's accepted Step supports pointer-exact undo/redo");

    // Repeated bounded normal motion should approach the spacing floor.
    // Eventually a finite 5-probe policy must report a recoverable
    // refusal rather than forging a zero-length accepted Step.
    bool refused=false;
    for(unsigned i=0;i<64;++i) {
        const auto before=brush.snapshot();
        const auto undo=brush.historyStatistics().undoDepth;
        brush.stamp(dummy,close);
        if(brush.rejectedStamps()!=0) {
            const auto rejection=brush.lastStepResourceWork();
            require(brush.snapshot()==before&&
                    brush.historyStatistics().undoDepth==undo&&
                    brush.lastDirtyPartitions().empty()&&
                    rejection.refused&&!rejection.published&&
                    rejection.backtracking.refused&&
                    !brush.lastRejection().empty(),
                    "Finite near-sheet refusal leaves root, history, dirty owners and reason intact");
            refused=true;
            break;
        }
    }
    require(refused,
            "Repeated named near-sheet GUI strokes must eventually refuse at mesh-scale spacing");

    const auto beforeRecovery=brush.snapshot();
    const auto undoBeforeRecovery=brush.historyStatistics().undoDepth;
    brush.stamp(dummy,safe);
    const auto afterRecovery=brush.snapshot();
    require(brush.publishedRevisions()>1&&
            afterRecovery!=beforeRecovery&&
            brush.historyStatistics().undoDepth==undoBeforeRecovery+1&&
            afterRecovery->vertex(remote).position.z>remotePosition.z,
            "After a genuine near-sheet refusal the same worker accepts an unrelated safe stroke");
    require(brush.undo()&&brush.snapshot()==beforeRecovery&&
            brush.redo()&&brush.snapshot()==afterRecovery,
            "Recovery from an actual refusal retains exact undo/redo root history");

    // Round-trip the *actual edited* collision-avoiding surface using
    // unchanged V13 disk records. Mode is not serialized and only
    // explicit caller policy re-arms avoidance after recovery.
    const auto file=std::filesystem::temp_directory_path()/
        "mesher-v18-m07-integrated-fixture.mshr";
    DurableCheckpoint recovered;
    try {
        writeCheckpoint(file,brush.checkpoint());
        recovered=readCheckpoint(file);
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(file,ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(file,ec);
    require(recovered.index&&recovered.index->retiredVertexCount()==
                afterRecovery->retiredVertexCount()&&
            recovered.mirrorPairs.empty(),
            "M07 edited source disk round trip preserves immutable stable source and asymmetric identity");
    VersionedSculptBrush guarded(recovered,.5,{},{},{},256,{},cfg);
    VersionedSculptBrush ordinary(recovered,.5,{},{},{});
    require(guarded.snapshot()&&ordinary.snapshot()&&
            !guarded.checkpoint().verifiedOriginalFidelity&&
            !ordinary.checkpoint().verifiedOriginalFidelity,
            "Recovered OFF-mode never manufactures M29 protected first-original credentials");
    const auto g=guarded.snapshot();
    const auto recoveredRemote=g->vertex(remote).position;
    const std::array<BrushArea,1> resumedSafe{{
        {recoveredRemote,.5,.3}
    }};
    guarded.stamp(dummy,resumedSafe);
    require(guarded.publishedRevisions()==1&&guarded.snapshot()!=g&&
            guarded.lastStepResourceWork().backtracking.trials>=1,
            "Rearmed recovered V18 policy performs actual full geometric private trials");
    ordinary.stamp(dummy,resumedSafe);
    require(ordinary.publishedRevisions()==1&&
            ordinary.lastStepResourceWork().backtracking.trials==0,
            "Unarmed recovery of same edited checkpoint remains ordinary legacy Inflate");
}
}
int main() {
    try {
        realFixtureAcceptance();
        std::cout<<"V18-M07 integrated named physical GPU fixture and disk/CPU admission: PASS\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"V18-M07: "<<error.what()<<'\n';
        return 1;
    }
    return 0;
}

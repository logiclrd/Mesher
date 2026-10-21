#include "mesher/desktop_collision_policy.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace mesher;
namespace {
void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
Mesh parallelSheets() {
    Mesh m;
    m.vertices={{0,0,0},{2,0,0},{0,2,0},
                {0,0,.14},{2,0,.14},{0,2,.14}};
    m.triangles={{0,1,2},{3,4,5}};
    return m;
}
void selectionsDoNotAliasMerge() {
    const auto defaults=desktopCollisionSettings({});
    require(!defaults.enabled&&!defaults.physicalExact&&
            !defaults.offModeConstraints.enabled,
            "Default desktop sculpt policy preserves legacy unarmed Inflate");
    const auto enabled=desktopCollisionSettings({false,true,true});
    require(enabled.offModeConstraints.enabled&&!enabled.physicalExact&&
            !enabled.enabled&&enabled.offModeConstraints.backtrack.enabled&&
            enabled.offModeConstraints.backtrack.maxTrials==5,
            "Explicit OFF-mode CLI selection enables M02-M05 backtracking, not protected Merge");
    const auto protectedOnly=desktopCollisionSettings({true,false,true});
    require(protectedOnly.physicalExact&&
            !protectedOnly.offModeConstraints.enabled,
            "V17 protected Merge remains separately source-authorized");
    for(const auto invalid:{
        DesktopCollisionSelection{true,true,true},
        DesktopCollisionSelection{false,true,false}}) {
        bool refused=false;
        try {(void)desktopCollisionSettings(invalid);}
        catch(const std::invalid_argument&) {refused=true;}
        require(refused,
                "Incompatible protected or noninteractive collision modes fail before initializing viewport");
    }
}
void freshSymmetricDesktopSourceIsArmedNotMerged() {
    SphereOptions options;
    options.vertexCount=96;
    options.relaxationIterations=3;
    options.symmetric=true;
    const auto sphere=generateSphere(options);
    require(validateSphere(sphere),
            "M06 small realistic symmetric desktop fixture is a valid original source");
    const auto policy=desktopCollisionSettings({false,true,true});
    VersionedSculptBrush brush(sphere,.002,{},{},{},true,256,{},policy);
    const auto original=brush.snapshot();
    require(original&&
            !brush.checkpoint().mirrorPairs.empty()&&
            !brush.checkpoint().verifiedOriginalFidelity,
            "Real fresh symmetric desktop mesh arms OFF avoidance, retaining reciprocal pairs but never sealed Merge authority");
}

void oldCheckpointDoesNotRestoreFeatureFlags() {
    auto source=parallelSheets();
    VersionedSculptBrush ordinary(source,.12,{},{},{},false);
    const auto old=ordinary.checkpoint();
    const auto file=std::filesystem::temp_directory_path()/
        "mesher-v18-m06-recovered-unchanged-geometry.mshr";
    DurableCheckpoint read;
    try {
        writeCheckpoint(file,old);
        read=readCheckpoint(file);
    }catch(...) {
        std::error_code error;
        std::filesystem::remove(file,error);
        throw;
    }
    std::error_code error;
    std::filesystem::remove(file,error);
    require(read.index&&read.index->retiredVertexCount()==
                old.index->retiredVertexCount()&&read.mirrorPairs.empty(),
            "Existing V12/V13 checkpoint loads immutable original IDs, not selection state");
    const std::array<BrushArea,1> approach{{{{0,0,0},.13,.3}}};
    Mesh dummy;
    // No saved mode bit exists; recovered sessions inherit ONLY the
    // explicitly supplied current CLI choice.
    VersionedSculptBrush legacy(read,.12,{},{},{});
    const auto previous=legacy.snapshot();
    legacy.stamp(dummy,approach);
    require(legacy.publishedRevisions()==1&&
            legacy.snapshot()!=previous&&
            legacy.rejectedStamps()==0&&
            legacy.lastStepResourceWork().backtracking.trials==0,
            "Ordinary recovery remains unguarded: V18 cannot silently re-enable");
    auto optIn=desktopCollisionSettings({false,true,true});
    optIn.offModeConstraints.backtrack.maxTrials=1;
    VersionedSculptBrush guarded(read,.12,{},{},{},256,{},optIn);
    const auto preserved=guarded.snapshot();
    guarded.stamp(dummy,approach);
    require(guarded.snapshot()==preserved&&
            guarded.rejectedStamps()==1&&
            guarded.historyStatistics().undoDepth==0&&
            guarded.lastStepResourceWork().backtracking.trials==1&&
            guarded.lastStepResourceWork().refused&&
            guarded.lastDirtyPartitions().empty(),
            "Explicit recovered-session collision policy refuses a real unsafe normal Step");
    const std::array<BrushArea,1> outward{{{{0,0,.14},.13,.3}}};
    guarded.stamp(dummy,outward);
    require(guarded.rejectedStamps()==1&&
            guarded.publishedRevisions()==1&&
            guarded.historyStatistics().undoDepth==1&&
            guarded.snapshot()!=preserved,
            "An opt-in recovered worker resumes normal edits after refusal");
    const auto accepted=guarded.snapshot();
    require(guarded.undo()&&guarded.snapshot()==preserved&&
            guarded.redo()&&guarded.snapshot()==accepted,
            "After opt-in recovered refusal, exact root undo/redo remains valid");
    // Re-reading same disk file never acquires transient session
    // collision choices, even after independently successful edits.
    const auto again=desktopCollisionSettings({});
    require(!again.offModeConstraints.enabled,
            "A later unarmed session does not inherit a previous armed session");
}
}
int main() {
    try {
        selectionsDoNotAliasMerge();
        freshSymmetricDesktopSourceIsArmedNotMerged();
        oldCheckpointDoesNotRestoreFeatureFlags();
        std::cout<<"V18-M06 desktop policy and legacy checkpoint contract: PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"V18-M06: "<<e.what()<<'\n';
        return 1;
    }
}

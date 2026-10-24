#include "mesher/repair_mixed_closure.hpp"
#include "mesher/repair_scale_aware_policy.hpp"
#include "mesher/repair_case.hpp"
#include "mesher/repair_auto_plan.hpp"
#include "mesher/repair_preview.hpp"
#include "mesher/repair_excision.hpp"
#include <filesystem>
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* message) {
    if(!yes)throw std::runtime_error(message);
}
struct Case {
    PrivateExcisionResult cut;
    RepairExcisionPreview proposal;
    AutoClosurePlan plan;
    std::shared_ptr<const RegionTopologyIndex> source;
};
Case openTetra() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    mesh.triangles={{0,1,3},{0,3,2},{1,2,3}};
    const auto flat=partitionFlatMesh(mesh,64);
    Case out;
    out.source=RegionTopologyIndex::build(flat.revision);
    out.cut.index=out.source;
    out.cut.boundaryLoops=1;
    out.cut.boundaryEdges=3;
    out.proposal.complete=true;
    out.proposal.boundaryEdges=3;
    RepairContour rim;
    rim.vertexIds={flat.flatIndex.vertexIds[0],
                   flat.flatIndex.vertexIds[1],
                   flat.flatIndex.vertexIds[2]};
    for(std::size_t i=0;i<rim.vertexIds.size();++i) {
        auto a=rim.vertexIds[i],b=rim.vertexIds[(i+1)%rim.vertexIds.size()];
        if(a>b)std::swap(a,b);
        const auto sides=out.source->maybeEdgeSides({a,b});
        require(sides&&sides->size()==1,
                "synthetic tetra must present one existing side of each cap rim");
        rim.survivingFaces.push_back(sides->front().face);
    }
    out.proposal.contours.push_back(rim);
    out.plan.status=AutoClosurePlanStatus::Ready;
    out.plan.cappedContours={0};
    AutoClosureRegion region;
    region.contours={0};
    region.caps={0};
    out.plan.regions={region};
    out.plan.geometryRegions=1;
    return out;
}
void ordinaryUnchangedAndEnabledDensity() {
    const auto fixture=openTetra();
    MixedPrivateClosureLimits original;
    // Force one oversized but healthy cap edge; the source-side quality
    // check still has to approve both children.
    original.maxCapEdgeFactor=1.02;
    original.spatialCapDensityEdgeFactor=1.01;
    const auto baseline=applyPrivateMixedClosure(
        fixture.cut,fixture.proposal,fixture.plan,original);
    require(baseline.index&&
        baseline.scaleAwareSplitOperations==0,
        "legacy disabled path stays available with no M17C actions");
    auto enabled=original;
    enabled.scaleAwareRefinement=true;
    const auto revised=applyPrivateMixedClosure(
        fixture.cut,fixture.proposal,fixture.plan,enabled);
    require(revised.index&&revised.refinementSteps>=1&&
        revised.scaleAwareSplitOperations>=1&&
        revised.scaleAwareSplitOperations<=enabled.maxScaleAwareSplitOperations&&
        revised.splitEvents.size()>=revised.scaleAwareSplitOperations&&
        revised.minimumCapQuality>=enabled.minCapQuality,
        "enabled density path must produce certified finite stage-owned splits");
    require(fixture.source->retiredFaceCount()==0&&
        fixture.cut.index->retiredFaceCount()==0,
        "neither mode publishes its result to original source");
    std::cout<<"[r06-m17c] enabled finite healthy density and unchanged disabled path PASS\n";
}
void underQualityHandsOffInsteadOfSplitting() {
    require(shouldRepairShapeBeforeDensity(.01,.1,1.5,1.,2.)&&
            !shouldRepairShapeBeforeDensity(.01,.1,3.,1.,2.)&&
            !shouldRepairShapeBeforeDensity(.2,.1,1.5,1.,2.),
            "under-quality within local scale requires shape repair while coarse size allows bounded density");
    const auto f=openTetra();
    MixedPrivateClosureLimits bounds;
    bounds.scaleAwareRefinement=true;
    bounds.minCapQuality=.95; // original planar right cap q about .866
    std::size_t splits=0;
    std::vector<std::string> declinedAttempts;
    std::vector<MixedClosureTerminalEvent> terminal;
    bounds.staging.operationObserver=[&](const auto&,const std::string& op,
                                         const std::string&) {
        if(op=="splitEdge")++splits;
        if(op=="TryDiagonalFlip"||op=="TryCapFanCollapse"||
           op=="TryInteriorFairing"||op=="TryQualityRestoringSplit")
            declinedAttempts.push_back(op);
    };
    bounds.terminalObserver=[&](const auto& event) {
        terminal.push_back(event);
    };
    bool shapeRefused=false,tipCertified=false;
    std::string reason;
    try {
        const auto result=applyPrivateMixedClosure(
            f.cut,f.proposal,f.plan,bounds);
        // A geometrically fixed, planar acute R03 rim tip can legitimately
        // receive the EXISTING independent geometric quality certificate.
        // This is not an under-quality free interior face and MUST NOT be
        // forced through extra subdivision just to fit an impossible q.
        tipCertified=result.index&&
            !result.boundaryTipQualityExemptFaces.empty();
    }catch(const std::logic_error& e) {
        reason=e.what();
        shapeRefused=reason.find("R06-M17C under-quality at shape scale")!=
            std::string::npos;
    }
    require(splits==0&&(shapeRefused||tipCertified),
        "shape-scale tip may refuse or retain genuine fixed-rim certificate, never split to chase q");
    if(shapeRefused) {
        require(terminal.size()==1&&
                terminal.front().outcome==MixedClosureTerminalOutcome::Refused,
                "true M17C shape refusal must preserve typed terminal provenance");
        require(declinedAttempts.size()>=4&&
                declinedAttempts[0]=="TryDiagonalFlip"&&
                declinedAttempts[1]=="TryCapFanCollapse"&&
                declinedAttempts[2]=="TryInteriorFairing"&&
                declinedAttempts[3]=="TryQualityRestoringSplit",
                "the step debugger must show every failed shape operation and strictly-restorative split preflight");
    }
    std::cout<<"[r06-m17c] shape priority; immutable fixed-rim tip certificate or refusal without splits PASS\n";
}
void refusalOneRealSavedCaseAttemptTrace() {
    // This is the ORIGINAL 531KB captured post-stamp physical refusal,
    // not a toy tetra and not recreated brush motion. M17C is opt-in.
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/
        "repair-cases"/"refusal-1.r06case";
    const auto saved=readRepairCase(path);
    const auto initialRetiredFaces=saved.candidate.index?
        saved.candidate.index->retiredFaceCount():0;
    require(saved.candidate.index&&!saved.contacts.empty(),
            "real refusal-1 saved candidate and R01 contacts must load");
    const auto excision=previewApproximateExcision(
        *saved.candidate.index,saved.contacts);
    require(excision.complete,"real refusal-1 must produce full R02 cut");
    const auto cut=applyPrivateExcision(saved.candidate.index,excision);
    const auto plan=planPrivateAutomaticClosure(
        *saved.candidate.index,cut,excision,saved.contacts);
    require(plan.ready(),"real refusal-1 must reach R06 closure staging");
    MixedPrivateClosureLimits limits;
    limits.requireReflectedGeometry=saved.symmetricSource;
    limits.scaleAwareRefinement=true;
    std::vector<std::string> declined;
    std::size_t splitsBeforeFirstAttempt=0, totalSplits=0;
    limits.staging.operationObserver=[&](
        const MixedClosureStaging&,const std::string& operation,
        const std::string& arguments) {
        if(operation=="splitEdge")++totalSplits;
        if(operation=="TryDiagonalFlip"||
           operation=="TryCapFanCollapse"||
           operation=="TryInteriorFairing"||
           operation=="TryQualityRestoringSplit") {
            if(declined.empty())splitsBeforeFirstAttempt=totalSplits;
            declined.push_back(operation+":"+arguments);
        }
    };
    bool refused=false;
    try {
        const auto closed=applyPrivateMixedClosure(
            cut,excision,plan,limits);
        require(closed.index!=nullptr,
            "accepted private mixed closure must produce a full index");
    }catch(const std::logic_error&) {refused=true;}
    require(!declined.empty()&&splitsBeforeFirstAttempt==0,
        "refusal-1 step 92 must expose shape attempts before any split");
    require(declined[0].starts_with("TryDiagonalFlip:")&&
            declined[0].find("result=NO_ADMISSIBLE")!=std::string::npos,
        "first no-op flip must be visible in source-backed step trace");
    require(std::any_of(declined.begin(),declined.end(),
        [](const std::string& op) {
            return op.starts_with("TryInteriorFairing:");
        }),"fairing attempt must be visible rather than mistaken for a skipped phase");
    require(saved.candidate.index->retiredFaceCount()==initialRetiredFaces,
        "source post-stamp tombstone count remains unchanged even on refusal");
    std::cout<<"[r06-m17c] exact refusal-1 saved case: "
             <<declined.size()<<" no-change repair diagnostics, "
             <<totalSplits<<" staged split vertices, "
             <<(refused?"strict refusal":"complete private closure")
             <<" PASS\n";
}
void budgetIsFiniteAndValidates() {
    const auto f=openTetra();
    MixedPrivateClosureLimits bad;
    bad.scaleAwareRefinement=true;
    bad.maxScaleAwareSplitOperations=0;
    bool refused=false;
    try {(void)applyPrivateMixedClosure(f.cut,f.proposal,f.plan,bad);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"zero budget must be invalid before staging");
    bad.maxScaleAwareSplitOperations=16;
    bad.maxScaleAwareSplitDepth=0;
    refused=false;
    try {(void)applyPrivateMixedClosure(f.cut,f.proposal,f.plan,bad);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"zero ancestry depth must be invalid");
    std::cout<<"[r06-m17c] hard finite iteration/depth constraints PASS\n";
}
}
int main() {
    try {
        ordinaryUnchangedAndEnabledDensity();
        underQualityHandsOffInsteadOfSplitting();
        budgetIsFiniteAndValidates();
        refusalOneRealSavedCaseAttemptTrace();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"[r06-m17c] FAILED "<<e.what()<<"\n";
        return 1;
    }
}

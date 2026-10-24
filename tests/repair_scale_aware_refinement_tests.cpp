#include "mesher/repair_mixed_closure.hpp"
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
    const auto f=openTetra();
    MixedPrivateClosureLimits bounds;
    bounds.scaleAwareRefinement=true;
    bounds.minCapQuality=.95; // original planar right cap q about .866
    std::size_t splits=0;
    std::vector<MixedClosureTerminalEvent> terminal;
    bounds.staging.operationObserver=[&](const auto&,const std::string& op,
                                         const std::string&) {
        if(op=="splitEdge")++splits;
    };
    bounds.terminalObserver=[&](const auto& event) {
        terminal.push_back(event);
    };
    bool refused=false;
    try {(void)applyPrivateMixedClosure(
        f.cut,f.proposal,f.plan,bounds);}
    catch(const std::logic_error& e) {
        refused=std::string(e.what()).find(
            "R06-M17C under-quality at shape scale")!=std::string::npos;
    }
    require(refused&&splits==0&&terminal.size()==1&&
        terminal.front().outcome==MixedClosureTerminalOutcome::Refused,
        "under-quality fixed-rim tip must refuse before a harmful split cascade");
    std::cout<<"[r06-m17c] underquality shape-first deterministic refusal before split PASS\n";
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
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"[r06-m17c] FAILED "<<e.what()<<"\n";
        return 1;
    }
}

#include "mesher/repair_cap_feasibility.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
void diagnose(bool symmetric) {
    const std::vector<Vec3> rim={
        {0,1,0},{.7,.7,0},{1,0,0},{.7,-.7,0},
        {0,-1,0},{-.7,-.7,0},{-1,0,0},{-.7,.7,0}};
    const std::vector<Vec3> normals(rim.size(),{0,0,-1});
    std::vector<SurvivorDirectionalCurvature> references(rim.size());
    SpatialCapSettings settings;
    settings.requireMirrorPlane=symmetric;
    settings.normalGuidance=0;
    auto base=buildSpatialCapFrontPoisson(rim,normals,settings);
    const auto n=base.boundaryCount;
    require(base.positions.size()>=2*n,"M19A expected a first inward ring");
    base.positions[n+1].z=.18;
    if(symmetric)base.positions[n+7].z=.18;
    std::vector<std::size_t> mirror;
    if(symmetric) {
        mirror.resize(base.positions.size());
        for(std::size_t i=0;i<mirror.size();++i)mirror[i]=i;
        for(std::size_t i=0;i<n;++i)
            mirror[n+i]=n+((n-i)%n);
    }
    unsigned inspected=0;
    const auto visitor=[&](SpatialCapPatch& candidate) {
        ++inspected;
        for(std::size_t i=0;i<n;++i) {
            require(candidate.positions[i].x==rim[i].x&&
                    candidate.positions[i].y==rim[i].y&&
                    candidate.positions[i].z==rim[i].z,
                    "M19A trial moved an immutable original boundary");
        }
        if(symmetric) {
            const auto a=candidate.positions[n+1];
            const auto b=candidate.positions[n+7];
            require(a.x==-b.x&&a.y==b.y&&a.z==b.z,
                    "M19A trial broke an exact mirrored position orbit");
        }
        return true;
    };
    SpatialCapFeasibilityLimits limits;
    limits.minimumQuality=.05;
    limits.maxProposals=32;
    const auto original=base;
    const auto report=probeSpatialCapFirstRingFeasibility(
        base,normals,references,mirror,.75,visitor,limits);
    require(report.proposed<=limits.maxProposals&&
            inspected<=limits.maxProposals&&
            report.normalOutwardProposals>0&&
            report.normalInwardProposals>0&&
            report.sampledPositivePlaneSide>0&&
            report.sampledNegativePlaneSide>0&&
            // M19A4: both directions must reach the full 1.0-edge trial
            // extent in the sampled CURRENT-point tranche.
            std::abs(report.normalSweepMaxOverEdge-1.0)<1e-12&&
            report.minSampledNormalDeltaOverEdge<-.95&&
            report.maxSampledNormalDeltaOverEdge>.95&&
            report.bestWorstSeamRadians<=
                report.baselineWorstSeamRadians+1e-10&&
            report.bestWorstCapRimRadians<=
                report.baselineWorstCapRimRadians+1e-10,
            "M19A bounded counterfactual may not worsen either peak");
    require(base.positions.size()==original.positions.size()&&
            base.faces==original.faces,
            "M19A must never alter staged topology");
    for(std::size_t k=0;k<base.positions.size();++k)
        require(base.positions[k].x==original.positions[k].x&&
                base.positions[k].y==original.positions[k].y&&
                base.positions[k].z==original.positions[k].z,
                "M19A must never mutate the accepted initial cap");

    bool rejected=false;
    const auto reject=[&](SpatialCapPatch&) {
        rejected=true;
        return false;
    };
    const auto blocked=probeSpatialCapFirstRingFeasibility(
        base,normals,references,mirror,.75,reject,limits);
    require(!blocked.improved&&
            blocked.baselineWorstSeamRadians==blocked.bestWorstSeamRadians&&
            blocked.baselineWorstCapRimRadians==blocked.bestWorstCapRimRadians,
            "M19A wholly refused proposals cannot be advertised as feasible");
    const auto repeated=probeSpatialCapFirstRingFeasibility(
        base,normals,references,mirror,.75,visitor,limits);
    require(report.proposed==repeated.proposed&&
            report.normalOutwardProposals==repeated.normalOutwardProposals&&
            report.normalInwardProposals==repeated.normalInwardProposals&&
            report.sampledPositivePlaneSide==repeated.sampledPositivePlaneSide&&
            report.sampledNegativePlaneSide==repeated.sampledNegativePlaneSide&&
            report.bestWorstSeamRadians==repeated.bestWorstSeamRadians&&
            report.bestWorstCapRimRadians==repeated.bestWorstCapRimRadians,
            "M19A bounded search must be deterministic");
    std::cout<<"[r06-m19a] reflected="<<symmetric
             <<" probes="<<report.proposed
             <<" passed_local="<<report.locallyAdmissible
             <<" before_seam="<<report.baselineWorstSeamRadians
             <<" best_seam="<<report.bestWorstSeamRadians
             <<" before_cap="<<report.baselineWorstCapRimRadians
             <<" best_cap="<<report.bestWorstCapRimRadians
             <<" outward_trials="<<report.normalOutwardProposals
             <<" inward_trials="<<report.normalInwardProposals
             <<" plane_positive="<<report.sampledPositivePlaneSide
             <<" plane_negative="<<report.sampledNegativePlaneSide
             <<" normal_delta_min="<<report.minSampledNormalDeltaOverEdge
             <<" normal_delta_max="<<report.maxSampledNormalDeltaOverEdge
             <<" all_refused_callback_seen="<<rejected<<"\n";
}
}
int main() {
    try {
        diagnose(false);
        diagnose(true);
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"R06-M19A feasibility probe FAILED: "<<error.what()<<"\n";
        return 1;
    }
}

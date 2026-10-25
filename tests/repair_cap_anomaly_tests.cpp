#include "mesher/repair_cap_anomaly.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* s) {if(!ok)throw std::runtime_error(s);}
void test(bool mirrored) {
    const std::vector<Vec3> rim={
        {0,1,0},{.7,.7,0},{1,0,0},{.7,-.7,0},
        {0,-1,0},{-.7,-.7,0},{-1,0,0},{-.7,.7,0}};
    const std::vector<Vec3> normal(rim.size(),{0,0,-1});
    SpatialCapSettings settings;
    settings.requireMirrorPlane=mirrored;
    settings.normalGuidance=0;
    auto flat=buildSpatialCapFrontPoisson(rim,normal,settings);
    const auto original=flat;
    const auto healthy=diagnoseSpatialCapAnomaly(
        flat,normal,{},flat.reflectedOrbits);
    require(!healthy.hasProposal()&&healthy.suspectFaces.empty(),
            "M20A1 flat source/flat cap must not propose a second hole");
    const auto n=flat.boundaryCount;
    // Deliberately distort only the interior; the original R03 ring is
    // bit-identical, including its source normal reference and identities.
    flat.positions[n+1].z=1.6;
    if(mirrored)flat.positions[n+7].z=1.6;
    const auto before=flat;
    const auto report=diagnoseSpatialCapAnomaly(
        flat,normal,{},flat.reflectedOrbits);
    require(report.measuredInteriorEdges>0&&
            report.suspectedInteriorEdges>0&&
            report.worstUnexplainedJumpRadians>0.30,
            "M20A1 must detect actual localized generated CAP/CAP fold");
    require(report.hasProposal()&&!report.suspectFaces.empty(),
            "M20A1 localized fold must admit a finite potential second rim");
    for(const auto& loop:report.secondaryContours) {
        require(loop.size()>=3,"M20A1 tiny second-rim loop");
        for(const auto id:loop)
            require(id<flat.positions.size(),
                    "M20A1 second-rim references absent local vertex");
    }
    require(flat.faces==before.faces&&flat.positions.size()==before.positions.size(),
            "M20A1 must not change patch topology");
    for(std::size_t i=0;i<flat.positions.size();++i)
        require(flat.positions[i].x==before.positions[i].x&&
                flat.positions[i].y==before.positions[i].y&&
                flat.positions[i].z==before.positions[i].z,
                "M20A1 must never relocate a vertex");
    const auto repeated=diagnoseSpatialCapAnomaly(
        flat,normal,{},flat.reflectedOrbits);
    require(report.suspectFaces==repeated.suspectFaces&&
            report.secondaryContours==repeated.secondaryContours&&
            report.worstUnexplainedJumpRadians==
                repeated.worstUnexplainedJumpRadians,
            "M20A1 bounded source-aware proposal must be deterministic");
    if(mirrored)require(report.mirrored,
        "M20A1 real reflected patch must preserve exact orbit information");
    SpatialCapAnomalyLimits tiny;
    tiny.maxFaces=2;
    bool bounded=false;
    try {(void)diagnoseSpatialCapAnomaly(
        flat,normal,{},flat.reflectedOrbits,tiny);}
    catch(const std::invalid_argument&){bounded=true;}
    require(bounded,"M20A1 finite cap face budget must reject large input");

    // High source-normal discontinuity is not by itself a CAP ripple:
    // no CAP/CAP turn => no proposed excision of an intentional crease.
    auto creaseNormals=normal;
    for(std::size_t k=n/2;k<n;++k)
        creaseNormals[k]={0,1,-.2};
    const auto crease=diagnoseSpatialCapAnomaly(
        original,creaseNormals,{},original.reflectedOrbits);
    require(crease.sourceCreaseEdges>0&&!crease.hasProposal(),
        "M20A1 original source crease is not an invented generated defect");
    auto corrupt=flat;
    corrupt.faces.push_back(flat.faces.front());
    bool rejectedDuplicate=false;
    try {(void)diagnoseSpatialCapAnomaly(
        corrupt,normal,{},corrupt.reflectedOrbits);}
    catch(const std::logic_error&){rejectedDuplicate=true;}
    require(rejectedDuplicate,"M20A1 must refuse duplicate indexed face");
    std::cout<<"[r06-m20a] mirrored="<<mirrored
             <<" observed="<<report.measuredInteriorEdges
             <<" suspect_edges="<<report.suspectedInteriorEdges
             <<" proposed_faces="<<report.suspectFaces.size()
             <<" second_rims="<<report.secondaryContours.size()
             <<" source_unchanged=1\n";
}
}
int main() {
    try {
        test(false);
        test(true);
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M20A cap diagnosis FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

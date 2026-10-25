#include "mesher/repair_cap_whole_fairing.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
void exactPatch(const SpatialCapPatch& a,const SpatialCapPatch& b) {
    require(a.faces==b.faces&&a.positions.size()==b.positions.size(),
            "M21A baseline/candidate must retain topology and count");
    for(std::size_t i=0;i<a.positions.size();++i)
        require(a.positions[i].x==b.positions[i].x&&
                a.positions[i].y==b.positions[i].y&&
                a.positions[i].z==b.positions[i].z,
                "M21A rejected proposal must preserve every position bitwise");
}
void test(bool mirrored) {
    const std::vector<Vec3> rim={
        {0,1,0},{.7,.7,0},{1,0,0},{.7,-.7,0},
        {0,-1,0},{-.7,-.7,0},{-1,0,0},{-.7,.7,0}};
    const std::vector<Vec3> normals(rim.size(),{0,0,-1});
    SpatialCapSettings settings;
    settings.requireMirrorPlane=mirrored;
    settings.normalGuidance=0.;
    const auto healthy=buildSpatialCapFrontPoisson(rim,normals,settings);
    const auto n=healthy.boundaryCount;
    std::vector<std::size_t> reflection;
    if(mirrored) {
        reflection.resize(healthy.positions.size());
        for(std::size_t i=0;i<n;++i) {
            reflection[i]=(n-i)%n;
            reflection[n+i]=n+(n-i)%n;
        }
        reflection[2*n]=2*n;
    }
    const auto allow=[](SpatialCapPatch&){return true;};
    SpatialCapPatch healthSelected;
    const auto noChange=proposeSpatialCapWholeFairing(
        healthy,normals,reflection,.75,allow,{},&healthSelected);
    require(!noChange.improved,"M21A healthy planar cap should not be changed");
    exactPatch(healthy,healthSelected);

    auto rippled=healthy;
    rippled.positions[n+1].z=.25;
    if(mirrored)rippled.positions[n+7].z=.25;
    const auto unchanged=rippled;
    SpatialCapPatch improved;
    const auto result=proposeSpatialCapWholeFairing(
        rippled,normals,reflection,.75,allow,{},&improved);
    require(result.trials>0&&result.trials<=512&&
            result.improved&&result.acceptedRounds>0&&
            result.selected.total+1e-6<result.baseline.total,
            "M21A must measurably fair synthetic local normal ripple");
    require(result.selected.worstSeamRadians<=
            result.baseline.worstSeamRadians+1e-8&&
            result.selected.worstInteriorRadians<=
            result.baseline.worstInteriorRadians+1e-8&&
            result.selected.minimumQuality>=.10,
            "M21A cannot trade increased worst discontinuity or poor q");
    exactPatch(rippled,unchanged);
    require(improved.faces==rippled.faces,
            "M21A currently tests positional fairing only");
    for(std::size_t i=0;i<n;++i) {
        const auto a=rippled.positions[i],b=improved.positions[i];
        require(a.x==b.x&&a.y==b.y&&a.z==b.z,
                "M21A must preserve exact original R03 rim");
    }
    if(mirrored)
        for(std::size_t i=0;i<reflection.size();++i) {
            const auto a=improved.positions[i],
                       b=improved.positions[reflection[i]];
            require(a.x==-b.x&&a.y==b.y&&a.z==b.z,
                    "M21A must retain exact reflected orbits");
        }
    SpatialCapPatch rejected;
    const auto refuse=[](SpatialCapPatch&){return false;};
    const auto blocked=proposeSpatialCapWholeFairing(
        rippled,normals,reflection,.75,refuse,{},&rejected);
    require(!blocked.improved&&blocked.legallyAdmissibleTrials==0,
            "M21A wholly rejected improvement must not be accepted");
    exactPatch(rippled,rejected);
    SpatialCapPatch repeated;
    const auto again=proposeSpatialCapWholeFairing(
        rippled,normals,reflection,.75,allow,{},&repeated);
    require(result.trials==again.trials&&
            result.acceptedRounds==again.acceptedRounds&&
            result.selected.total==again.selected.total,
            "M21A finite optimistic search must be deterministic");
    exactPatch(improved,repeated);
    SpatialCapFairingLimits tiny;
    tiny.maxTrials=1;
    SpatialCapPatch oneTrial;
    const auto budget=proposeSpatialCapWholeFairing(
        rippled,normals,reflection,.75,allow,tiny,&oneTrial);
    require(budget.trials<=1,"M21A finite trial ceiling violated");
    // True validation exceptions (including finite collision work)
    // cannot turn an already valid original into an R06 refusal.
    SpatialCapPatch exhausted;
    const auto overflow=[](SpatialCapPatch&)->bool {
        throw std::length_error("synthetic cap-contact budget exhausted");
    };
    const auto ranOut=proposeSpatialCapWholeFairing(
        rippled,normals,reflection,.75,overflow,{},&exhausted);
    require(!ranOut.improved&&ranOut.contactBudgetExhausted,
            "M21A contact budget fallback must be explicit");
    exactPatch(rippled,exhausted);

    std::cout<<"[r06-m21a] mirrored="<<mirrored
             <<" baseline="<<result.baseline.total
             <<" fair="<<result.selected.total
             <<" accepted_rounds="<<result.acceptedRounds
             <<" valid_fallback=1 rim_fixed=1\n";
}
}
int main() {
    try {test(false);test(true);return 0;}
    catch(const std::exception& e) {
        std::cerr<<"R06-M21A whole cap fairing FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

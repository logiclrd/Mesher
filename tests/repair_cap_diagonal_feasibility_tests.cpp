#include "mesher/repair_cap_diagonal_feasibility.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
void run(bool symmetric) {
    const std::vector<Vec3> rim={
        {0,1,0},{.7,.7,0},{1,0,0},{.7,-.7,0},
        {0,-1,0},{-.7,-.7,0},{-1,0,0},{-.7,.7,0}};
    const std::vector<Vec3> normal(rim.size(),{0,0,-1});
    SpatialCapSettings settings;
    settings.requireMirrorPlane=symmetric;
    settings.normalGuidance=0;
    auto original=buildSpatialCapFrontPoisson(rim,normal,settings);
    const auto baseline=original;
    const auto n=original.boundaryCount;
    require(original.faces.size()==3*n,
            "M19C sample must have exact indexed three-face strip");
    std::vector<std::size_t> mirror;
    if(symmetric) {
        mirror.resize(original.positions.size());
        for(std::size_t i=0;i<n;++i) {
            mirror[i]=(n-i)%n;
            mirror[n+i]=n+(n-i)%n;
        }
        mirror.back()=mirror.size()-1;
    }

    auto reversible=original;
    require(diagonal_detail::flipOne(reversible,1),
            "M19C original indexed quad should have a legal alternate diagonal");
    require(reversible.faces!=original.faces,
            "M19C flipped indexed diagonal must change the two faces");
    require(diagonal_detail::flipOne(reversible,1)&&
            reversible.faces==original.faces,
            "M19C a second flip should restore exact original topology");

    unsigned validated=0;
    const auto validator=[&](SpatialCapPatch& patch) {
        ++validated;
        for(std::size_t i=0;i<n;++i)
            require(patch.positions[i].x==rim[i].x&&
                    patch.positions[i].y==rim[i].y&&
                    patch.positions[i].z==rim[i].z,
                    "M19C must not move any source rim position");
        require(patch.faces.size()==original.faces.size(),
                "M19C local diagonal trial must retain face count");
        return true;
    };
    SpatialCapDiagonalProbeLimits limits;
    limits.minimumQuality=.05;
    limits.maxCandidates=24;
    const auto report=probeSpatialCapDiagonalFeasibility(
        original,normal,mirror,validator,limits);
    require(report.candidates<=limits.maxCandidates&&
            validated<=report.candidates&&
            report.bestWorstSeamRadians<=
                report.baselineWorstSeamRadians+1e-10&&
            report.bestWorstCapRimRadians<=
                report.baselineWorstCapRimRadians+1e-10,
            "M19C finite legal diagonal proposal cannot worsen the worst fold");
    auto again=probeSpatialCapDiagonalFeasibility(
        original,normal,mirror,validator,limits);
    require(report.candidates==again.candidates&&
            report.admissible==again.admissible&&
            report.bestWorstSeamRadians==again.bestWorstSeamRadians&&
            report.bestWorstCapRimRadians==again.bestWorstCapRimRadians,
            "M19C fixed-order mirrored proposals must be deterministic");
    require(original.faces==baseline.faces&&
            original.positions.size()==baseline.positions.size(),
            "M19C source patch must remain unchanged");
    for(std::size_t k=0;k<original.positions.size();++k)
        require(original.positions[k].x==baseline.positions[k].x&&
                original.positions[k].y==baseline.positions[k].y&&
                original.positions[k].z==baseline.positions[k].z,
                "M19C must not modify original source coordinates");
    std::cout<<"[r06-m19c] mirrored="<<symmetric
             <<" attempted="<<report.candidates
             <<" locally_admissible="<<report.admissible
             <<" old_seam="<<report.baselineWorstSeamRadians
             <<" best_seam="<<report.bestWorstSeamRadians
             <<" old_cap="<<report.baselineWorstCapRimRadians
             <<" best_cap="<<report.bestWorstCapRimRadians<<"\n";
}
}
int main(){
    try {
        run(false);
        run(true);
        return 0;
    }catch(const std::exception& error){
        std::cerr<<"R06-M19C alternate-diagonal probe FAILED: "
                 <<error.what()<<"\n";
        return 1;
    }
}

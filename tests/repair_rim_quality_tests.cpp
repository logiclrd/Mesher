#include "mesher/repair_rim_quality.hpp"
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>
using namespace mesher;
namespace {
void check(bool valid,const char* error){if(!valid)throw std::runtime_error(error);}
void proof() {
    constexpr double sharp=.02;
    std::map<VertexId,Vec3> p{
        {1,{0,0,0}},{2,{1,0,0}},
        {3,{std::cos(sharp),std::sin(sharp),0}},
        {4,{.5,0,0}},
        {5,{.5*std::cos(sharp),.5*std::sin(sharp),0}}
    };
    RepairContour loop;
    loop.vertexIds={1,2,3}; // tip 1 sees predecessor 3 and successor 2
    const std::vector<RepairContour> contours{loop};
    MixedClosureFace candidate{
        0,1,{1,4,5},MixedClosureFaceKind::Cap,1};
    const auto certified=certifyPlanarRimTipQuality(
        candidate,p,contours,.1);
    check(certified&&certified->fixedTip==1&&
          certified->upperBound<.1&&
          certified->actualQuality>.65*certified->upperBound,
          "M11A3c must certify the acute fixed original planar wedge");
    candidate.vertices={1,2,5}; // very imbalanced rays: avoid a free bad split
    p.at(5)=p.at(5)*.001;
    check(!certifyPlanarRimTipQuality(candidate,p,contours,.1),
          "M11A3c poor radial length balance is still improvable");
    candidate.vertices={1,4,5};
    p.at(5)={.5*std::cos(sharp),.5*std::sin(sharp),.1};
    check(!certifyPlanarRimTipQuality(candidate,p,contours,.1),
          "M11A3c nonplanar/folded off-ray triangle can escape planar wedge");
    p.at(5)={.5*std::cos(sharp),.5*std::sin(sharp),0};
    candidate.stableId=23;
    check(!certifyPlanarRimTipQuality(candidate,p,contours,.1),
          "M11A3c cannot waive quality on any original survivor face");
    candidate.stableId=0;
    p.at(3)={std::cos(.3),std::sin(.3),0};
    p.at(5)=p.at(3)*.5;
    check(!certifyPlanarRimTipQuality(candidate,p,contours,.1),
          "M11A3c wide rim corners must keep original q=.10 threshold");
    p.at(3)={1,0,0};
    p.at(5)={.5,0,0};
    check(!certifyPlanarRimTipQuality(candidate,p,contours,.1),
          "M11A3c zero-angle degenerate rim is not an acceptable proof");
}
} // namespace
int main() {
    try {
        proof();
        std::cout<<"[r06-m11a3c] immutable planar acute wedge, length balance, nonplanar and survivor refusals PASS\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"R06 M11A3c fixed rim certificate failed: "<<error.what()<<"\n";
        return 1;
    }
    return 0;
}

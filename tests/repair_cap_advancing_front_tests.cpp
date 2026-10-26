#include "mesher/repair_cap_spatial.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
std::vector<Vec3> rim(bool mirrored) {
    std::vector<Vec3> points;
    const auto pi=std::acos(-1.);
    for(unsigned i=0;i<12;++i) {
        const auto a=2*pi*i/12;
        points.push_back({std::cos(a),std::sin(a),0});
    }
    if(mirrored) {
        // X=0 is represented by exact source vertex IDs.
        points[3].x=0;
        points[9].x=0;
    }
    return points;
}
void exercise(bool symmetric) {
    auto boundary=rim(symmetric);
    SpatialCapSettings settings;
    settings.useAdvancingFront=true;
    settings.requireMirrorPlane=symmetric;
    const std::vector<Vec3> guidance(boundary.size(),{0,0,1});
    const auto generated=buildSpatialCapFrontPoisson(
        boundary,guidance,settings);
    require(generated.usedAdaptiveFront,"M22B did not select AFM");
    require(generated.boundaryCount==boundary.size()&&
            !generated.faces.empty()&&
            generated.poissonIterations>0,
            "M22B did not create a closed, Poisson-settled patch");
    for(std::size_t i=0;i<boundary.size();++i)
        require(generated.positions[i].x==boundary[i].x&&
                generated.positions[i].y==boundary[i].y&&
                generated.positions[i].z==boundary[i].z,
                "M22B moved an original R03 boundary vertex");
    if(symmetric) {
        for(const auto& f:generated.faces) {
            const auto a=generated.positions[f[0]];
            const auto b=generated.positions[f[1]];
            const auto c=generated.positions[f[2]];
            require(!(std::min({a.x,b.x,c.x})<0&&
                      std::max({a.x,b.x,c.x})>0),
                "M22B returned a triangle crossing reflection plane");
        }
    }
    const auto repeat=buildSpatialCapFrontPoisson(
        boundary,guidance,settings);
    require(repeat.faces==generated.faces&&
            repeat.positions.size()==generated.positions.size(),
            "M22B must be deterministic");
    std::cout<<"[repair-r06-m22b] mirror="<<symmetric
             <<" vertices="<<generated.positions.size()
             <<" faces="<<generated.faces.size()
             <<" min_q="<<generated.minimumQuality<<" PASS\n";
}
}
int main() {
    try {
        exercise(false);
        exercise(true);
    }catch(const std::exception& e) {
        std::cerr<<"R06-M22B: "<<e.what()<<"\n";
        return 1;
    }
}

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
    if(mirrored) {
        // Use exactly paired coordinates: sin/cos evaluations at distinct
        // angles do not guarantee bit-exact mirror coordinates.
        return {{0,1,0},{.5,.86,0},{.86,.5,0},{1,0,0},
                {.86,-.5,0},{.5,-.86,0},{0,-1,0},
                {-.5,-.86,0},{-.86,-.5,0},{-1,0,0},
                {-.86,.5,0},{-.5,.86,0}};
    }
    std::vector<Vec3> points;
    const auto pi=std::acos(-1.);
    for(unsigned i=0;i<12;++i) {
        const auto a=2*pi*i/12;
        points.push_back({std::cos(a),std::sin(a),0});
    }
    return points;
}
void mirroredTerminalQuadrilaterals() {
    // The actual six-vertex endgame shape reached by refusal-3 before
    // M22B's stale original-rim normal misclassified its convex corners.
    // A desirable mirrored closure uses two ear triangles per half-disk:
    // no extra vertices, no crossing the symmetry plane, no relaxation
    // of contact or triangle-quality gates.
    const std::vector<Vec3> boundary{
        {0,-1.12200992374,.547030597576},
        {.0497079305927,-1.12200992374,.547030597576},
        {.0313056405566,-1.06810312437,.523393101919},
        {0,-1.06632078233,.529270554899},
        {-.0313056405566,-1.06810312437,.523393101919},
        {-.0497079305927,-1.12200992374,.547030597576}};
    SpatialCapSettings settings;
    settings.useAdvancingFront=true;
    settings.requireMirrorPlane=true;
    const std::vector<Vec3> guidance(boundary.size(),{0,0,1});
    const auto cap=buildSpatialCapFrontPoisson(boundary,guidance,settings);
    require(cap.faces.size()==4&&cap.positions.size()==boundary.size(),
        "M22B mirrored six-corner endgame should close without introducing new vertices");
    require(cap.minimumQuality>=settings.desiredMinimumQuality,
        "M22B terminal quadrilaterals must retain the quality floor");
    for(std::size_t i=0;i<boundary.size();++i)
        require(cap.positions[i].x==boundary[i].x&&
                cap.positions[i].y==boundary[i].y&&
                cap.positions[i].z==boundary[i].z,
            "M22B terminal closing must leave original boundary fixed");
    for(const auto& face:cap.faces) {
        const auto a=cap.positions[face[0]];
        const auto b=cap.positions[face[1]];
        const auto c=cap.positions[face[2]];
        require(!(std::min({a.x,b.x,c.x})<0&&
                  std::max({a.x,b.x,c.x})>0),
            "M22B terminal triangles must not cross X=0");
    }
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
        mirroredTerminalQuadrilaterals();
    }catch(const std::exception& e) {
        std::cerr<<"R06-M22B: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}

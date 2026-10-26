#include "mesher/repair_cap_spatial.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}
std::vector<Vec3> rim(bool reflected) {
    if(reflected)
        return {{0,1,0},{.5,.86,0},{.86,.5,0},{1,0,0},
                {.86,-.5,0},{.5,-.86,0},{0,-1,0},
                {-.5,-.86,0},{-.86,-.5,0},{-1,0,0},
                {-.86,.5,0},{-.5,.86,0}};
    std::vector<Vec3> boundary;
    constexpr unsigned n=12;
    for(unsigned i=0;i<n;++i) {
        const double a=2*std::acos(-1.)*i/n;
        boundary.push_back({std::cos(a),std::sin(a),0});
    }
    return boundary;
}
bool identical(const SpatialCapPatch& a,const SpatialCapPatch& b) {
    if(a.boundaryCount!=b.boundaryCount||a.faces!=b.faces||
       a.positions.size()!=b.positions.size()||
       a.poissonIterations!=b.poissonIterations||
       a.poissonResidual!=b.poissonResidual||
       a.minimumQuality!=b.minimumQuality||
       a.usedAdaptiveFront!=b.usedAdaptiveFront||
       a.usedMirroredRibbonFallback!=b.usedMirroredRibbonFallback)
        return false;
    for(std::size_t i=0;i<a.positions.size();++i) {
        const auto x=a.positions[i],y=b.positions[i];
        if(x.x!=y.x||x.y!=y.y||x.z!=y.z)return false;
    }
    return true;
}
void exercise(bool reflected) {
    const auto boundary=rim(reflected);
    const std::vector<Vec3> normals(boundary.size(),{0,0,1});
    SpatialCapSettings settings;
    settings.useAdvancingFront=true;
    settings.requireMirrorPlane=reflected;

    const auto original=buildSpatialCapFrontPoisson(
        boundary,normals,settings,nullptr);
    const auto explicitNull=buildSpatialCapFrontPoisson(
        boundary,normals,settings,nullptr,nullptr);
    require(identical(original,explicitNull),
        "M23A explicit nullptr changed original cap geometry or Poisson");

    std::size_t calls=0,oneVertex=0,twoVertex=0,secondOfTwo=0;
    SpatialCapPlacementAdjustment identity=
        [&](const SpatialCapPlacementProposal& p)->Vec3 {
            ++calls;
            if(p.faceMode==2)++oneVertex;
            if(p.faceMode==3) {
                ++twoVertex;
                if(p.proposalOrdinal==1)++secondOfTwo;
            }
            require(p.faceMode==2||p.faceMode==3,
                "M23A called adjuster for an ear without a new vertex");
            require(p.proposalOrdinal<(p.faceMode==2?1u:2u),
                "M23A invalid generated vertex ordinal");
            require(p.frontVertex!=p.previousVertex&&
                    p.frontVertex!=p.nextVertex,
                "M23A invalid local front identity");
            require(p.localEdgeScale>0&&std::isfinite(p.localEdgeScale),
                "M23A missing finite local projection scale");
            return p.projectedPosition;
        };
    const auto unchanged=buildSpatialCapFrontPoisson(
        boundary,normals,settings,nullptr,&identity);
    require(calls>0,"M23A never called placement callback");
    require(twoVertex>0&&secondOfTwo>0,
        "M23A did not expose both vertices of a three-triangle proposal");
    require(identical(original,unchanged),
        "M23A identity callback must be bit-identical to original cap");

    std::size_t moved=0;
    SpatialCapPlacementAdjustment slight=
        [&](const SpatialCapPlacementProposal& p)->Vec3 {
            ++moved;
            auto q=p.projectedPosition;
            q.z+=p.localEdgeScale*1e-4;
            return q;
        };
    const auto adjusted=buildSpatialCapFrontPoisson(
        boundary,normals,settings,nullptr,&slight);
    require(moved>0&&adjusted.usedAdaptiveFront&&!adjusted.faces.empty(),
        "M23A tiny valid displacement prevented advancing-front closure");
    for(std::size_t i=0;i<boundary.size();++i) {
        const auto p=adjusted.positions[i],q=boundary[i];
        require(p.x==q.x&&p.y==q.y&&p.z==q.z,
            "M23A callback moved a fixed R03 boundary vertex");
    }
    if(reflected) {
        for(const auto& face:adjusted.faces) {
            const auto a=adjusted.positions[face[0]];
            const auto b=adjusted.positions[face[1]];
            const auto c=adjusted.positions[face[2]];
            require(!(std::min({a.x,b.x,c.x})<0&&
                      std::max({a.x,b.x,c.x})>0),
                "M23A callback broke exact X=0 face-side constraint");
        }
        // Every off-plane synthetic point must have its exact mirror.
        for(std::size_t i=boundary.size();i<adjusted.positions.size();++i) {
            const auto p=adjusted.positions[i];
            if(p.x==0)continue;
            bool paired=false;
            for(std::size_t j=boundary.size();j<adjusted.positions.size();++j) {
                const auto q=adjusted.positions[j];
                if(p.x==-q.x&&p.y==q.y&&p.z==q.z) {
                    paired=true;break;
                }
            }
            require(paired,"M23A callback lost a mirrored generated vertex");
        }
    }
    std::cout<<"[repair-r06-m23a] mirror="<<reflected
             <<" identity_calls="<<calls
             <<" mode2="<<oneVertex
             <<" mode3="<<twoVertex
             <<" moved_calls="<<moved<<" PASS\n";
}
void unsafeMustRefuse() {
    const auto boundary=rim(false);
    const std::vector<Vec3> guidance(boundary.size(),{0,0,1});
    SpatialCapSettings settings;
    settings.useAdvancingFront=true;
    std::size_t attempts=0;
    SpatialCapPlacementAdjustment invalid=
        [&](const SpatialCapPlacementProposal& p)->Vec3 {
            ++attempts;
            auto q=p.projectedPosition;
            q.x=std::numeric_limits<double>::quiet_NaN();
            return q;
        };
    bool refused=false;
    try {
        (void)buildSpatialCapFrontPoisson(
            boundary,guidance,settings,nullptr,&invalid);
    }catch(const std::logic_error&) {refused=true;}
    require(attempts>0&&refused,
        "M23A invalid callback proposal must not be committed as a cap");

    settings.useAdvancingFront=false;
    refused=false;
    try {
        (void)buildSpatialCapFrontPoisson(
            boundary,guidance,settings,nullptr,&invalid);
    }catch(const std::invalid_argument&) {refused=true;}
    require(refused,
        "M23A cannot silently ignore a provided callback on legacy ring path");
}
}
int main() {
    try {
        exercise(false);
        exercise(true);
        unsafeMustRefuse();
    }catch(const std::exception& e) {
        std::cerr<<"R06-M23A: "<<e.what()<<"\n";
        return 1;
    }
}

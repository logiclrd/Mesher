#include "mesher/repair_cap_spatial.hpp"
#include "mesher/repair_cap_continuity.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool pass,const char* message) {
    if(!pass)throw std::runtime_error(message);
}
struct Fixture {
    std::vector<Vec3> rim,normals;
    std::vector<SurvivorDirectionalCurvature> tensors;
};
Fixture curved(double curvature) {
    Fixture f;
    const std::vector<Vec3> planar={
        {0,1,0},{.45,.95,0},{.9,.55,0},{.9,0,0},
        {.9,-.55,0},{.45,-.95,0},{0,-1,0},
        {-.45,-.95,0},{-.9,-.55,0},{-.9,0,0},
        {-.9,.55,0},{-.45,.95,0}};
    f.rim=planar;
    for(auto& p:f.rim)p.z=.5*curvature*(p.x*p.x+p.y*p.y);
    f.normals.resize(f.rim.size());
    f.tensors.resize(f.rim.size());
    for(std::size_t i=0;i<f.rim.size();++i) {
        const auto j=(i+1)%f.rim.size();
        const auto midpoint=(f.rim[i]+f.rim[j])*.5;
        const auto n=Vec3{-curvature*midpoint.x,
                          -curvature*midpoint.y,1.};
        f.normals[j]=n/length(n);
        auto& s=f.tensors[j];
        s.hasEstimate=true;
        s.confidence=SurvivorCurvatureConfidence::Reliable;
        s.confidenceScore=1.;
        s.seam=curvature;
        s.mixed=0;
        s.transverse=curvature;
    }
    return f;
}
double score(const SpatialCapContinuity& value) {
    double objective=0.;
    for(const auto& edge:value.edges)
        objective+=edge.boundaryNormalJumpRadians*
            edge.boundaryNormalJumpRadians+
            .25*edge.capInteriorFairnessRadians*
                edge.capInteriorFairnessRadians;
    return objective/value.edges.size();
}
bool different(const SpatialCapPatch& a,const SpatialCapPatch& b) {
    if(a.positions.size()!=b.positions.size()||a.faces!=b.faces)return true;
    for(std::size_t k=0;k<a.positions.size();++k)
        if(length(a.positions[k]-b.positions[k])>1e-13)return true;
    return false;
}
void fixtureTest(double curvature,bool mirrored) {
    const auto f=curved(curvature);
    SpatialCapSettings limits;
    limits.requireMirrorPlane=mirrored;
    limits.normalGuidance=.04;
    const auto base=buildSpatialCapFrontPoisson(f.rim,f.normals,limits);
    limits.curvatureGuidedPoisson=true;
    const auto guided=buildSpatialCapFrontPoisson(
        f.rim,f.normals,limits,&f.tensors);
    require(base.faces==guided.faces&&base.positions.size()==guided.positions.size(),
            "M18E must never change original patch topology");
    for(std::size_t i=0;i<f.rim.size();++i)
        require(guided.positions[i].x==f.rim[i].x&&
                guided.positions[i].y==f.rim[i].y&&
                guided.positions[i].z==f.rim[i].z,
                "M18E fixed Dirichlet contour must remain bit-identical");
    if(mirrored) {
        const auto n=f.rim.size();
        for(std::size_t i=n;i<2*n;++i) {
            bool partner=false;
            for(std::size_t j=n;j<2*n;++j) {
                const auto a=guided.positions[i],b=guided.positions[j];
                if(a.x==-b.x&&a.y==b.y&&a.z==b.z) {
                    partner=true;break;
                }
            }
            require(partner,"M18E must preserve exact reflected ring orbits");
        }
    }
    const auto old=measureSpatialCapContinuity(base,f.normals,f.tensors);
    const auto now=measureSpatialCapContinuity(guided,f.normals,f.tensors);
    require(score(now)<=score(old)+1e-8,
            "M18E must not select higher-scoring guidance");
    require(now.maximumBoundaryJumpRadians<=
            old.maximumBoundaryJumpRadians+1e-8,
            "M18E may not worsen the maximum dihedral");
    if(base.minimumQuality>=limits.desiredMinimumQuality)
        require(guided.minimumQuality+1e-12>=limits.desiredMinimumQuality,
                "M18E must preserve the original cap q floor");
    std::cout<<"[r06-m18e-fixture] curvature="<<curvature
             <<" mirror="<<mirrored<<" changed="<<different(base,guided)
             <<" before_score="<<score(old)<<" after_score="<<score(now)
             <<" before_fold="<<old.maximumBoundaryJumpRadians
             <<" after_fold="<<now.maximumBoundaryJumpRadians
             <<" before_q="<<base.minimumQuality
             <<" after_q="<<guided.minimumQuality<<"\n";

    auto unreliable=f.tensors;
    for(auto& s:unreliable) {
        s.confidence=SurvivorCurvatureConfidence::CreaseSuspected;
        s.confidenceScore=0;
    }
    const auto unchanged=buildSpatialCapFrontPoisson(
        f.rim,f.normals,limits,&unreliable);
    require(!different(base,unchanged),
            "M18E unqualified guidance must be a deterministic no-op");
}
}
int main() {
    try {
        fixtureTest(.5,false);
        fixtureTest(.5,true);
        fixtureTest(0,false);
        fixtureTest(0,true);
        std::cout<<"[r06-m18e] bounded fixed rim, mirror and untrusted no-op PASS\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"R06-M18E fixture FAILED: "<<error.what()<<"\n";
        return 1;
    }
}

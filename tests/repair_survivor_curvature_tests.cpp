#include "mesher/repair_survivor_curvature.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* why) {
    if(!ok)throw std::runtime_error(why);
}
void close(double actual,double expected,double tol,const char* why) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>tol)
        throw std::runtime_error(why);
}
enum class Surface {Flat,Cylinder,Sphere,Saddle,Twist};
SurvivorCurvatureNeighborhood analytic(Surface type,double k) {
    SurvivorCurvatureNeighborhood result;
    result.faces.push_back({1,{0,0,0},{0,0,1}});
    FaceId next=2;
    for(int ix=-3;ix<=3;++ix)
    for(int iy=-3;iy<=3;++iy) {
        if(ix==0&&iy==0)continue;
        const double x=ix*.2,y=iy*.2;
        double z=0,gx=0,gy=0;
        switch(type) {
        case Surface::Flat:break;
        case Surface::Cylinder:z=.5*k*x*x;gx=k*x;break;
        case Surface::Sphere:z=.5*k*(x*x+y*y);gx=k*x;gy=k*y;break;
        case Surface::Saddle:z=.5*k*(x*x-y*y);gx=k*x;gy=-k*y;break;
        case Surface::Twist:z=k*x*y;gx=k*y;gy=k*x;break;
        }
        result.faces.push_back({next++,{x,y,z},{-gx,-gy,1}});
    }
    return result;
}
void syntheticShapeOperators() {
    const auto t=Vec3{1,0,0},o=Vec3{0,0,0};
    SurvivorCurvatureLimits limits;
    limits.maxFaces=64;
    const auto flat=estimateSurvivorDirectionalCurvature(
        o,t,analytic(Surface::Flat,.2),limits);
    require(flat.hasEstimate&&flat.confidence==SurvivorCurvatureConfidence::Reliable,
        "M18B flat region should be reliable");
    close(flat.seam,0,1.e-12,"M18B flat k_tt");
    close(flat.mixed,0,1.e-12,"M18B flat k_tu");
    close(flat.transverse,0,1.e-12,"M18B flat k_uu");

    const auto cylinder=estimateSurvivorDirectionalCurvature(
        o,t,analytic(Surface::Cylinder,.2),limits);
    require(cylinder.hasEstimate&&
        cylinder.confidence==SurvivorCurvatureConfidence::Reliable,
        "M18B cylinder must give reliable directional result");
    close(cylinder.seam,.2,.012,"M18B cylinder seam bend");
    close(cylinder.mixed,0,.01,"M18B cylinder twist");
    close(cylinder.transverse,0,.01,"M18B cylinder cross bend");

    const auto sphere=estimateSurvivorDirectionalCurvature(
        o,t,analytic(Surface::Sphere,.2),limits);
    close(sphere.seam,.2,.02,"M18B sphere seam bend");
    close(sphere.transverse,.2,.02,"M18B sphere across bend");
    close(sphere.mixed,0,.02,"M18B sphere twist");

    const auto saddle=estimateSurvivorDirectionalCurvature(
        o,t,analytic(Surface::Saddle,.2),limits);
    close(saddle.seam,.2,.02,"M18B saddle seam bend");
    close(saddle.transverse,-.2,.02,"M18B saddle across bend");
    close(saddle.mixed,0,.02,"M18B saddle twist");

    const auto twist=estimateSurvivorDirectionalCurvature(
        o,t,analytic(Surface::Twist,.2),limits);
    close(twist.seam,0,.02,"M18B twist k_tt");
    close(twist.transverse,0,.02,"M18B twist k_uu");
    close(twist.mixed,.2,.02,"M18B twist offdiagonal");
    require(twist.confidence==SurvivorCurvatureConfidence::Reliable,
        "M18B smooth twist should not be misclassified as a crease");
    std::cout<<"[r06-m18b] flat cylinder sphere saddle and twist curvature PASS\n";
}
void mirrorInvariantBasis() {
    const auto original=analytic(Surface::Twist,.2);
    const auto a=estimateSurvivorDirectionalCurvature(
        {0,0,0},{1,0,0},original);
    auto reflected=original;
    for(auto& face:reflected.faces) {
        face.centroid.x=-face.centroid.x;
        face.normal.x=-face.normal.x; // mirror plus reversed winding
    }
    const auto b=estimateSurvivorDirectionalCurvature(
        {-1,0,0},{0,0,0},reflected);
    require(a.hasEstimate&&b.hasEstimate,
        "M18B reflected tensor must be observable");
    close(b.seam,a.seam,1.e-10,"M18B reflected longitudinal bend");
    close(b.transverse,a.transverse,1.e-10,
          "M18B reflected transverse bend");
    close(b.mixed,-a.mixed,1.e-10,"M18B reflected twist parity");
    std::cout<<"[r06-m18b] reflected signed mixed curvature PASS\n";
}
void confidenceAndDeterminism() {
    const auto a=Vec3{0,0,0},b=Vec3{1,0,0};
    SurvivorCurvatureNeighborhood missing;
    missing.missingSeed=true;
    const auto noSeed=estimateSurvivorDirectionalCurvature(a,b,missing);
    require(!noSeed.hasEstimate&&
           noSeed.confidence==SurvivorCurvatureConfidence::MissingSeed,
           "M18B missing source cannot invent a curvature tensor");

    auto few=analytic(Surface::Flat,0);
    few.faces.resize(3);
    const auto notEnough=estimateSurvivorDirectionalCurvature(a,b,few);
    require(!notEnough.hasEstimate&&
           notEnough.confidence==SurvivorCurvatureConfidence::InsufficientSamples,
           "M18B insufficient samples must lower confidence");

    SurvivorCurvatureNeighborhood line;
    line.faces.push_back({1,{0,0,0},{0,0,1}});
    for(unsigned i=1;i<=8;++i)line.faces.push_back(
        {i+1,{i*.12,0,0},{0,0,1}});
    const auto rank=estimateSurvivorDirectionalCurvature(a,b,line);
    require(!rank.hasEstimate&&
           rank.confidence==SurvivorCurvatureConfidence::RankDeficient,
           "M18B cannot infer across-seam curvature from collinear samples");

    auto crease=analytic(Surface::Flat,0);
    crease.faces.back().normal={0,1,0};
    const auto c=estimateSurvivorDirectionalCurvature(a,b,crease);
    require(c.hasEstimate&&
           c.confidence==SurvivorCurvatureConfidence::CreaseSuspected&&
           c.confidenceScore<=.25,
           "M18B sudden normal discontinuity is low-confidence, not zero curvature");

    auto reversed=analytic(Surface::Flat,0);
    reversed.faces.back().normal={0,0,-1};
    const auto inv=estimateSurvivorDirectionalCurvature(a,b,reversed);
    require(inv.confidence==SurvivorCurvatureConfidence::InconsistentNormals,
           "M18B inconsistent winding must lower confidence");

    auto truncated=analytic(Surface::Cylinder,.2);
    truncated.truncated=true;
    const auto bounded=estimateSurvivorDirectionalCurvature(a,b,truncated);
    require(bounded.hasEstimate&&
           bounded.confidence==SurvivorCurvatureConfidence::Truncated,
           "M18B bounded incomplete samples must be flagged");

    const auto c2=estimateSurvivorDirectionalCurvature(a,b,crease);
    require(c.confidence==c2.confidence&&c.seam==c2.seam&&
           c.mixed==c2.mixed&&c.transverse==c2.transverse,
           "M18B bounded robust fit must be deterministic");
    std::cout<<"[r06-m18b] insufficient rank crease winding truncated deterministic PASS\n";
}
}
int main() {
    try {
        syntheticShapeOperators();
        mirrorInvariantBasis();
        confidenceAndDeterminism();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M18B survivor curvature diagnostics FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

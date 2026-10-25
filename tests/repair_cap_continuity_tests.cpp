#include "mesher/repair_cap_continuity.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool valid,const char* message) {
    if(!valid)throw std::runtime_error(message);
}
void near(double actual,double expected,double error,const char* message) {
    if(!std::isfinite(actual)||std::abs(actual-expected)>error)
        throw std::runtime_error(message);
}
struct Fixture {
    SpatialCapPatch patch;
    std::vector<Vec3> guide;
    std::vector<SurvivorDirectionalCurvature> references;
};
Fixture flatCircle() {
    Fixture fixture;
    constexpr unsigned n=12;
    const double pi=std::acos(-1.);
    std::vector<Vec3> rim;
    for(unsigned i=0;i<n;++i) {
        const double phi=2*pi*i/n;
        rim.push_back({std::cos(phi),std::sin(phi),0});
    }
    fixture.guide.assign(n,{0,0,1});
    SpatialCapSettings settings;
    settings.normalGuidance=0; // exact planar control, not Poisson bulge
    fixture.patch=buildSpatialCapFrontPoisson(rim,fixture.guide,settings);
    fixture.references.resize(n);
    for(auto& v:fixture.references) {
        v.hasEstimate=true;
        v.confidence=SurvivorCurvatureConfidence::Reliable;
        v.confidenceScore=1;
    }
    return fixture;
}
Fixture reflected(const Fixture& source) {
    Fixture out;
    const auto n=source.patch.boundaryCount;
    out.patch=source.patch;
    std::vector<std::size_t> mapping(source.patch.positions.size());
    for(std::size_t i=0;i<n;++i) {
        mapping[i]=n-1-i;
        mapping[n+i]=2*n-1-i;
    }
    for(std::size_t i=2*n;i<mapping.size();++i)mapping[i]=i;
    for(std::size_t i=0;i<mapping.size();++i) {
        const auto v=source.patch.positions[i];
        out.patch.positions[mapping[i]]={-v.x,v.y,v.z};
    }
    for(auto& tri:out.patch.faces) {
        const auto a=mapping[tri[0]],b=mapping[tri[1]],c=mapping[tri[2]];
        tri={static_cast<std::uint32_t>(a),
             static_cast<std::uint32_t>(c),
             static_cast<std::uint32_t>(b)};
    }
    out.guide.resize(n);
    out.references.resize(n);
    for(std::size_t old=0;old<n;++old) {
        const auto next=(old+1)%n;
        const auto translated=n-1-old;
        const auto g=source.guide[next];
        out.guide[translated]={-g.x,g.y,g.z};
        out.references[translated]=source.references[next];
        // Under reversal + reflection the edge-transverse coordinate
        // is preserved, but the mixed term is odd under seam reflection.
        out.references[translated].mixed=-out.references[translated].mixed;
    }
    return out;
}
void splitNormalAndCurvatureComparison() {
    auto fixture=flatCircle();
    const auto clean=measureSpatialCapContinuity(
        fixture.patch,fixture.guide,fixture.references);
    require(clean.edges.size()==fixture.patch.boundaryCount&&
            clean.measuredBoundaryEdges==clean.edges.size()&&
            clean.maximumBoundaryJumpRadians<1.e-8&&
            clean.meanBoundaryAndFairnessScore<1.e-8,
            "M18C planar matching cap must have negligible first-order jump");
    require(clean.directionalQualifiedEdges>0,
            "M18C full planar cap must permit at least one directional tensor comparison");
    require(clean.meanQualifiedDirectionalMismatch<1.e-8,
            "M18C flat cap and flat reference must agree in all tensor components");

    auto bent=fixture;
    // Move exactly one SYNTHETIC first-ring vertex out of the flat plane.
    // No new topology or survivor geometry: read-only diagnostic ranking.
    const auto n=fixture.patch.boundaryCount;
    bent.patch.positions[n].z+=.22;
    const auto folded=measureSpatialCapContinuity(
        bent.patch,bent.guide,bent.references);
    require(folded.maximumBoundaryJumpRadians>.1&&
            folded.meanBoundaryAndFairnessScore>
                clean.meanBoundaryAndFairnessScore+.01,
            "M18C visibly folded first inward ring must score worse than smooth cap");
    require(folded.maximumBoundaryJumpRadians>
            clean.maximumBoundaryJumpRadians,
            "M18C a q-valid triangle can be a poor boundary continuation");

    // Feed the CAP's actual tensor back as a matched survivor tensor,
    // then perturb independent components to test S_tt, S_tu, S_uu.
    fixture.references.assign(n,{});
    for(std::size_t i=0;i<n;++i) {
        auto tensor=clean.edges[i].cap;
        if(tensor.hasEstimate&&
           tensor.confidence==SurvivorCurvatureConfidence::Reliable) {
            tensor.confidenceScore=1;
            fixture.references[(i+1)%n]=tensor;
        }
    }
    const auto matched=measureSpatialCapContinuity(
        fixture.patch,fixture.guide,fixture.references);
    require(matched.directionalQualifiedEdges>0&&
            matched.meanQualifiedDirectionalMismatch<1.e-8,
            "M18C identical cap/survivor curvature tensors must have zero mismatch");
    for(auto& tensor:fixture.references)
        if(tensor.hasEstimate) {
            tensor.seam+=.4;
            tensor.mixed+=.3;
            tensor.transverse-=.2;
        }
    const auto differing=measureSpatialCapContinuity(
        fixture.patch,fixture.guide,fixture.references);
    require(differing.directionalQualifiedEdges==matched.directionalQualifiedEdges&&
            differing.meanQualifiedDirectionalMismatch>
                matched.meanQualifiedDirectionalMismatch+.01,
            "M18C directional tensor differences must increase normalized curvature error");
    for(const auto& entry:differing.edges)
        if(entry.directionalQualified) {
            near(entry.deltaSeamScaled,-entry.rim.edgeLength*.4,1.e-9,
                 "M18C seam-parallel component mismatch");
            near(entry.deltaMixedScaled,-entry.rim.edgeLength*.3,1.e-9,
                 "M18C mixed twist mismatch");
            near(entry.deltaTransverseScaled,entry.rim.edgeLength*.2,1.e-9,
                 "M18C cross-seam component mismatch");
        }
    std::cout<<"[r06-m18c] read-only flat/folded rank and directional mismatches PASS\n";
}
void parityAndUncertainty() {
    const auto fixture=flatCircle();
    const auto a=measureSpatialCapContinuity(
        fixture.patch,fixture.guide,fixture.references);
    const auto flipped=reflected(fixture);
    const auto b=measureSpatialCapContinuity(
        flipped.patch,flipped.guide,flipped.references);
    near(a.maximumBoundaryJumpRadians,b.maximumBoundaryJumpRadians,1.e-9,
         "M18C reflected absolute seam jumps must agree");
    near(a.meanBoundaryAndFairnessScore,b.meanBoundaryAndFairnessScore,1.e-9,
         "M18C mirrored cap/survivor combined scores must agree");
    near(a.meanQualifiedDirectionalMismatch,b.meanQualifiedDirectionalMismatch,1.e-8,
         "M18C reflected tensor magnitude comparison must agree");

    auto low=fixture;
    for(auto& v:low.references) {
        v.confidence=SurvivorCurvatureConfidence::CreaseSuspected;
        v.confidenceScore=.05;
        v.seam=10000;
        v.mixed=10000;
        v.transverse=-10000;
    }
    const auto uncertain=measureSpatialCapContinuity(
        low.patch,low.guide,low.references);
    require(uncertain.directionalMeasuredEdges>0&&
            uncertain.directionalQualifiedEdges==0&&
            uncertain.lowConfidenceEdges==low.patch.boundaryCount&&
            uncertain.meanBoundaryAndFairnessScore==a.meanBoundaryAndFairnessScore,
            "M18C unreliable or creased survivor reference cannot drive shape ranking");

    auto missing=fixture;
    missing.guide.clear();
    const auto unavailable=measureSpatialCapContinuity(
        missing.patch,missing.guide,missing.references);
    require(unavailable.measuredBoundaryEdges==0&&
            unavailable.directionalQualifiedEdges==0,
            "M18C absent source normals must not claim reliable continuity");

    auto reversed=fixture;
    const auto f=std::size_t{0};
    std::swap(reversed.patch.faces[f][0],reversed.patch.faces[f][1]);
    bool failed=false;
    try {(void)measureSpatialCapContinuity(
        reversed.patch,reversed.guide,reversed.references);}
    catch(const std::logic_error&) {failed=true;}
    require(failed,"M18C refuses a broken indexed directed cap rim");

    SpatialCapContinuityLimits limits;
    limits.maxPatchFaces=1;
    failed=false;
    try {(void)measureSpatialCapContinuity(
        fixture.patch,fixture.guide,fixture.references,limits);}
    catch(const std::length_error&) {failed=true;}
    require(failed,"M18C finite cap inventory work budget must hold");

    const auto repeat=measureSpatialCapContinuity(
        fixture.patch,fixture.guide,fixture.references);
    require(a.meanBoundaryAndFairnessScore==repeat.meanBoundaryAndFairnessScore&&
            a.directionalQualifiedEdges==repeat.directionalQualifiedEdges,
            "M18C pure evaluation must be deterministic");
    std::cout<<"[r06-m18c] mirror parity low confidence missing-source finite-budget PASS\n";
}
}
int main() {
    try {
        splitNormalAndCurvatureComparison();
        parityAndUncertainty();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M18C cap continuity diagnostics FAILED: "
                 <<e.what()<<"\n";
        return 1;
    }
}

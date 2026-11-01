#include "mesher/repair_plan.hpp"
#include "mesher/exact_stage_work.hpp"
#include "mesher/repair_surgery.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <set>
#include <utility>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
constexpr double pi=std::numbers::pi_v<double>;
void check(bool yes,const char* why) {
    if(!yes)throw std::runtime_error(why);
}
struct CapRange {std::size_t left{},right{},count{};};
CapRange prism(Mesh& mesh,double x0,double x1,
               const std::vector<std::array<double,2>>& ring) {
    const std::size_t n=ring.size();
    const auto start=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto x:{x0,x1})
        for(const auto& yz:ring)mesh.vertices.push_back({x,yz[0],yz[1]});
    Vec3 middle{0,0,0};
    for(const auto& yz:ring)
        middle=middle+Vec3{0,yz[0],yz[1]};
    middle=middle/static_cast<double>(n);
    const auto leftCenter=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({x0,middle.y,middle.z});
    const auto rightCenter=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({x1,middle.y,middle.z});
    const auto firstFace=mesh.triangles.size();
    for(std::uint32_t i=0;i<n;++i) {
        const auto next=(i+1)%static_cast<std::uint32_t>(n);
        mesh.triangles.push_back({start+i,start+next,start+next+
                                  static_cast<std::uint32_t>(n)});
        mesh.triangles.push_back({start+i,start+next+
                                  static_cast<std::uint32_t>(n),
                                  start+i+static_cast<std::uint32_t>(n)});
    }
    const auto left=mesh.triangles.size();
    for(std::uint32_t i=0;i<n;++i)
        mesh.triangles.push_back({leftCenter,start+(i+1)%
                                  static_cast<std::uint32_t>(n),start+i});
    const auto right=mesh.triangles.size();
    for(std::uint32_t i=0;i<n;++i)
        mesh.triangles.push_back({rightCenter,start+i+
                                  static_cast<std::uint32_t>(n),
                                  start+(i+1)%static_cast<std::uint32_t>(n)+
                                  static_cast<std::uint32_t>(n)});
    check(left==firstFace+2*n&&right==firstFace+3*n,
          "Prism cap face layout invariant");
    return {left,right,n};
}
std::vector<std::array<double,2>> square(double r) {
    return {{-r,-r},{r,-r},{r,r},{-r,r}};
}
std::vector<std::array<double,2>> asymmetricSquare(double r) {
    return {{-r,-r},{-.95*r,-r},{-.8*r,-r},{-.4*r,-r},
            {0,-r},{r,-r},{r,r},{-r,r}};
}
auto fixture(const Mesh& mesh) {
    return partitionFlatMesh(mesh,1);
}
std::vector<FaceId> capFaces(const PartitionedMeshImport& import,
                             CapRange range,bool left) {
    std::vector<FaceId> out;
    for(std::size_t i=0;i<range.count;++i)
        out.push_back(import.flatIndex.faceIds[
            (left?range.left:range.right)+i]);
    return out;
}
void testMergingWallsAndArcLength() {
    Mesh m;
    const auto a=prism(m,-2,0,square(1));
    const auto b=prism(m,.1,2,asymmetricSquare(1));
    const auto imp=fixture(m);
    const auto index=RegionTopologyIndex::build(imp.revision);
    auto red=capFaces(imp,a,false);
    const auto second=capFaces(imp,b,true);
    red.insert(red.end(),second.begin(),second.end());
    // M43: observe real NON-SYNTHETIC pair-matching, sample and DP
    // cost of the same unequal-density two-ring zipped source fixture.
    const auto untouchedPicker=index->pickingRootIdentity();
    ExactStageWork contourWork;
    const auto plan=[&] {
        const ScopedExactStageWork meter(contourWork);
        return planRemovalContours(*index,red);
    }();
    check(contourWork.contourEdgeProbes>0&&
          contourWork.contourBoundaryEdges>0&&
          contourWork.contourLoopVisits>0&&
          contourWork.closureRingTests==2&&
          contourWork.closurePairCandidates==1&&
          contourWork.closureSampleTests==2*32*32&&
          contourWork.closureDpTransitions>0&&
          !contourWork.overflow,
          "M43 counts all seven true contour traversal and arc-length zipper closure work categories");
    struct WorkGate {
        std::size_t ExactStageWork::*used;
        std::size_t ExactStageWorkLimits::*limit;
        const char* reason;
    };
    constexpr std::array<WorkGate,7> gates{{
        {&ExactStageWork::contourEdgeProbes,
         &ExactStageWorkLimits::maxContourEdgeProbes,"contour edge-probe budget"},
        {&ExactStageWork::contourBoundaryEdges,
         &ExactStageWorkLimits::maxContourBoundaryEdges,
         "contour boundary-edge budget"},
        {&ExactStageWork::contourLoopVisits,
         &ExactStageWorkLimits::maxContourLoopVisits,"contour loop-visit budget"},
        {&ExactStageWork::closureRingTests,
         &ExactStageWorkLimits::maxClosureRingTests,"closure ring-test budget"},
        {&ExactStageWork::closurePairCandidates,
         &ExactStageWorkLimits::maxClosurePairCandidates,"closure pair-candidate budget"},
        {&ExactStageWork::closureSampleTests,
         &ExactStageWorkLimits::maxClosureSampleTests,"closure sample-test budget"},
        {&ExactStageWork::closureDpTransitions,
         &ExactStageWorkLimits::maxClosureDpTransitions,"closure DP-transition budget"}
    }};
    ExactStageWorkLimits exactly;
    for(const auto& gate:gates)
        exactly.*(gate.limit)=contourWork.*(gate.used);
    check(refuseExactStageWork(contourWork,exactly).empty(),
          "M43 all seven individually finite categories accept EXACT complete real unequal-ring closure selection work");
    for(const auto& gate:gates) {
        auto oneUnder=exactly;
        --(oneUnder.*(gate.limit));
        check(refuseExactStageWork(contourWork,oneUnder).find(
                  gate.reason)!=std::string::npos&&
              index->pickingRootIdentity()==untouchedPicker,
              "M43 one-under original 2-ring pair/zipper/contour work refuses named category without any private COW mutation");
    }
    check(plan.ready()&&plan.pairs.size()==1&&
          plan.cappedContours.empty(),"Nearby walls choose a zipper");
    check(plan.contours[0].vertexIds.size()!=
          plan.contours[1].vertexIds.size(),
          "Regression contains a deliberately mismatched vertex distribution");
    const auto& choice=plan.closureChoices.front();
    check(choice.kind==ClosureKind::Zipper&&
          choice.zipperArea<choice.capArea,
          "Smaller annular/frustum surface beats two flat caps");
    check(std::abs(choice.ringAreaFirst-4)<1e-10&&
          std::abs(choice.ringAreaSecond-4)<1e-10,
          "Split collinear boundary edges do not change polygon area");
    check(std::abs(choice.centroidSeparation-.1)<1e-10,
          "Polygon centroids measure geometry, not vertex-index averages");
    check(choice.meanSquaredCorrespondenceDistance<.011,
          "Arc-length correspondence matches unequal tessellation fairly");
    check(plan.estimatedAddedArea>0&&
          std::abs(plan.estimatedAddedArea-choice.zipperArea)<1e-10,
          "Global selector reports the chosen added surface area");
}
void testCapsWinAtLargeSeparation() {
    Mesh m;
    const auto a=prism(m,-4,-2,square(.1));
    const auto b=prism(m,2,4,square(.1));
    const auto imp=fixture(m);
    const auto index=RegionTopologyIndex::build(imp.revision);
    auto red=capFaces(imp,a,false);
    const auto second=capFaces(imp,b,true);
    red.insert(red.end(),second.begin(),second.end());
    const auto plan=planRemovalContours(*index,red);
    check(plan.ready()&&plan.cappedContours.size()==2&&
          plan.pairs.empty(),
          "Small widely separated rings independently cap despite failed zipper span");
    check(std::abs(plan.estimatedAddedArea-.08)<1e-10,
          "Cap area equals total enclosed planar ring areas");
}
void testDifferentRadiiAndTie() {
    Mesh m;
    const auto a=prism(m,-2,0,square(1));
    const auto b=prism(m,0,2,square(2));
    const auto imp=fixture(m);
    const auto index=RegionTopologyIndex::build(imp.revision);
    auto red=capFaces(imp,a,false);
    const auto second=capFaces(imp,b,true);
    red.insert(red.end(),second.begin(),second.end());
    const auto plan=planRemovalContours(*index,red);
    check(plan.ready()&&plan.pairs.size()==1,
          "Unequal coaxial rings still admit a low-area frustum");
    const auto& score=plan.closureChoices.front();
    const auto expected=pi*(score.effectiveRadiusFirst+
                            score.effectiveRadiusSecond)*
                        std::hypot(score.centroidSeparation,
                                   score.effectiveRadiusFirst-
                                   score.effectiveRadiusSecond);
    check(std::abs(score.zipperArea-expected)<1e-10,
          "Area estimate includes radial difference in quadrature");

    Mesh nearTie;
    const double effectiveR=std::sqrt(4/pi); // square area is 4
    const auto aa=prism(nearTie,-2,0,square(1));
    const auto bb=prism(nearTie,effectiveR,effectiveR+2,square(1));
    const auto ti=fixture(nearTie);
    const auto ix=RegionTopologyIndex::build(ti.revision);
    auto faces=capFaces(ti,aa,false);
    const auto more=capFaces(ti,bb,true);
    faces.insert(faces.end(),more.begin(),more.end());
    // Gap of 1.128 exceeds default maxGapInEdges? h=2, so no.
    const auto ambiguous=planRemovalContours(*ix,faces);
    check(ambiguous.status==RepairPlanStatus::AmbiguousClosure,
          "Nearly equal cap and zipper areas cannot select topology by rounding");
}
struct Torus {
    Mesh mesh;
    unsigned around{},tube{};
};
Torus torus(double major,double minor,unsigned around=24,unsigned tube=16) {
    Torus result;
    result.around=around;result.tube=tube;
    for(unsigned i=0;i<around;++i) {
        const double t=2*pi*i/around;
        for(unsigned j=0;j<tube;++j) {
            const double p=2*pi*j/tube;
            const double radius=major+minor*std::cos(p);
            result.mesh.vertices.push_back({radius*std::cos(t),
                radius*std::sin(t),minor*std::sin(p)});
        }
    }
    for(unsigned i=0;i<around;++i)
        for(unsigned j=0;j<tube;++j) {
            const auto a=i*tube+j;
            const auto b=((i+1)%around)*tube+j;
            const auto c=((i+1)%around)*tube+(j+1)%tube;
            const auto d=i*tube+(j+1)%tube;
            result.mesh.triangles.push_back({a,b,c});
            result.mesh.triangles.push_back({a,c,d});
        }
    return result;
}
std::vector<FaceId> throatBand(const PartitionedMeshImport& imp,
                               unsigned around,unsigned tube) {
    std::vector<FaceId> selected;
    // Remove two rings of quads around the inner equator of a torus,
    // producing the two closed contour loops of the neck.
    for(unsigned i=0;i<around;++i)
        for(const auto j:{tube/2-1,tube/2}) {
            selected.push_back(imp.flatIndex.faceIds[2*(i*tube+j)]);
            selected.push_back(imp.flatIndex.faceIds[2*(i*tube+j)+1]);
        }
    return selected;
}
void testPinchedTorusHandle() {
    for(const auto& [major,minor,expected]:{
        std::array<double,3>{1.0,1.07,1},
        std::array<double,3>{3.0,.5,0}
    }) {
        const auto shape=torus(major,minor);
        const auto imp=fixture(shape.mesh);
        const auto index=RegionTopologyIndex::build(imp.revision);
        const auto red=throatBand(imp,shape.around,shape.tube);
        const auto plan=planRemovalContours(*index,red);
        check(plan.ready()&&plan.contours.size()==2,
              "Torus inner throat yields two genuine contour loops");
        if(expected==1) {
            check(plan.cappedContours.size()==2&&plan.pairs.empty(),
                  "Collapsed/inverted torus neck chooses two caps (handle loss)");
            check(plan.estimatedAddedArea<.2,
                  "Near-zero torus throat caps have small area");
            // Validate actual topology surgery, not only the chosen
            // closure scores: the removed neck must destroy the handle.
            const auto repaired=applyRepairSurgery(index,plan);
            std::size_t vertices=0,faces=0;
            std::set<std::pair<VertexId,VertexId>> edges;
            repaired.index->revision().forEachPartition([&](
                PartitionId,const MeshPartition& part) {
                vertices+=part.vertices.size();
                faces+=part.faces.size();
                for(const auto& face:part.faces)
                    for(unsigned edge=0;edge<3;++edge) {
                        auto a=face.vertices[edge],
                             b=face.vertices[(edge+1)%3];
                        if(a>b)std::swap(a,b);
                        edges.emplace(a,b);
                    }
            });
            check(vertices+faces-edges.size()==2,
                  "Actual committed torus closure has genus zero");
            check(repaired.capFaces>0&&repaired.zipperFaces==0,
                  "Actual torus repair caps the inner tunnel, not a zipper");
        }else {
            check(plan.pairs.size()==1&&plan.cappedContours.empty(),
                  "Wide torus tunnel favours zipper over huge separate caps");
        }
    }
}
void testUnsupportedGeometryAndInvalidSettings() {
    Mesh skew;
    // Laterally offset nearby rings can be incompatible with a coaxial
    // frustum; V16 must construct the actual candidate, not guess here.
    const auto a=prism(skew,-2,0,square(.5));
    auto offset=square(.5);
    for(auto& p:offset)p[0]+=.7;
    const auto b=prism(skew,.05,2,offset);
    const auto imp=fixture(skew);
    const auto index=RegionTopologyIndex::build(imp.revision);
    auto red=capFaces(imp,a,false);
    const auto other=capFaces(imp,b,true);
    red.insert(red.end(),other.begin(),other.end());
    const auto plan=planRemovalContours(*index,red);
    check(plan.status==RepairPlanStatus::AmbiguousClosure,
          "Unsupported off-axis contour geometry does not pretend to know fitness");
    RepairPlanSettings invalid;
    invalid.minimumRelativeAreaAdvantage=-.1;
    bool threw=false;
    try{(void)planRemovalContours(*index,red,invalid);}
    catch(const std::invalid_argument&){threw=true;}
    check(threw,"Fitness configuration has bounded valid values");
}
}
int main() {
    try {
        testMergingWallsAndArcLength();
        testCapsWinAtLargeSeparation();
        testDifferentRadiiAndTie();
        testPinchedTorusHandle();
        testUnsupportedGeometryAndInvalidSettings();
        std::cout<<"Topology-aware cap/zipper fitness regression passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Topology-aware closure failure: "<<e.what()<<'\n';
        return 1;
    }
    return 0;
}

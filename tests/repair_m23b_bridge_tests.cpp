#include "mesher/repair_zipper_front.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* why) {
    if(!yes)throw std::runtime_error(why);
}
UnifiedZipperRim circle(double x,std::size_t n,VertexId base) {
    UnifiedZipperRim rim;
    const double pi=std::acos(-1.);
    for(std::size_t i=0;i<n;++i) {
        const double a=2*pi*static_cast<double>(i)/n;
        rim.ids.push_back(base+static_cast<VertexId>(i));
        rim.positions.push_back({x,.4*std::cos(a),.4*std::sin(a)});
        rim.outgoingNormals.push_back({0,std::cos(a),std::sin(a)});
    }
    return rim;
}
void test(bool mirrored) {
    auto first=circle(.6,8,100);
    UnifiedZipperRim second;
    for(std::size_t i=first.ids.size();i-->0;) {
        const auto p=first.positions[i];
        second.positions.push_back({-.6,p.y,p.z});
        second.ids.push_back(static_cast<VertexId>(200+i));
        const auto n=first.outgoingNormals[i];
        second.outgoingNormals.push_back({-n.x,n.y,n.z});
    }
    if(!mirrored) {
        // First and second remain legal oppositely oriented rim loops.
        second.positions[1].y+=.01;
        second.positions[3].z+=.02;
    }
    SpatialCapSettings settings;
    settings.useAdvancingFront=true;
    settings.maxBoundaryVertices=256;
    settings.maxIntersectionTests=500000;
    const auto p=buildUnifiedZipperFront(
        first,second,mirrored,settings);
    require(p.bridgeFaceCount==(mirrored?4u:3u),
        "M23B did not build a complete positive-width finite bridge");
    require(p.bridgeVertexCount==(mirrored?2u:1u),
        "M23B wrong seam vertex count");
    require(p.boundaryIds.size()==16u+(mirrored?2u:1u),
        "M23B spliced boundary contains missing/duplicate source vertices");
    require(p.positions.size()>=p.boundaryIds.size()&&
            p.faces.size()>p.bridgeFaceCount,
        "M23B did not continue filling through the existing cap constructor");
    require(p.minimumSeedQuality>=settings.desiredMinimumQuality,
        "M23B admitted a bad bridge seed");
    std::set<VertexId> sources;
    for(const auto id:p.boundaryIds)
        if(id)require(sources.insert(id).second,
            "M23B duplicated an original source ID in remaining front");
    require(sources.size()==first.ids.size()+second.ids.size(),
        "M23B lost original source IDs");
    // Ablation: the same bridge+AFM with an identity adjustment must
    // differ from the bounded opposing-side field, otherwise gravity
    // would be a nominal callback with no actual geometric effect.
    std::size_t adjustments=0,ancestralAdjustments=0,tangentChecks=0;
    SpatialCapPlacementAdjustment identity=
        [&](const SpatialCapPlacementProposal& proposal) {
            ++adjustments;
            if(proposal.originatingSide==1||proposal.originatingSide==2)
                ++ancestralAdjustments;
            // A real zipper proposal must be tangent to its *current*
            // geometric surface before any gravitational displacement.
            // This was missing in the original 3D-bisector placement.
            const auto n=proposal.projectionNormal;
            const auto d=proposal.projectedPosition-proposal.frontPosition;
            if(length(n)>1e-12&&length(d)>1e-12) {
                ++tangentChecks;
                require(std::abs(dot(n,d))<=1e-8*length(n)*length(d),
                    "M23B AFM proposed a non-tangential point before gravity");
            }
            return proposal.projectedPosition;
        };
    bool different=false;
    double maxDisplacement=0;
    try {
        const auto control=buildUnifiedZipperFront(
            first,second,mirrored,settings,&identity);
        different=p.faces!=control.faces||
                  p.positions.size()!=control.positions.size();
        for(std::size_t i=0;i<std::min(p.positions.size(),
                                     control.positions.size());++i) {
            const auto delta=length(p.positions[i]-control.positions[i]);
            maxDisplacement=std::max(maxDisplacement,delta);
            if(delta>1e-10)different=true;
        }
    }catch(const std::logic_error&) {
        // A valid gravity-guided candidate can succeed where all identity
        // guided topological attempts refuse; still measurable influence.
        different=true;
    }
    require(adjustments>0&&ancestralAdjustments>0,
        "M23B must invoke the adjustment on active sided front vertices");
    require(tangentChecks>0,
        "M23B must supply a real tangent-normal projection to gravity");
    if(!mirrored)
        require(different,
            "M23B gravity had no effect on the asymmetric candidate");
    std::cout<<"[r06-m23b-gravity] reflected="<<mirrored
             <<" callbacks="<<adjustments
             <<" sided="<<ancestralAdjustments
             <<" changed="<<different
             <<" max_shift="<<maxDisplacement<<" PASS\n";
    if(mirrored) {
        for(const auto& f:p.faces) {
            const auto x0=p.positions[f[0]].x;
            const auto x1=p.positions[f[1]].x;
            const auto x2=p.positions[f[2]].x;
            require(!(std::min({x0,x1,x2})<0&&
                      std::max({x0,x1,x2})>0),
                "M23B bridged X=0 with an off-plane triangle");
        }
    }
    std::cout<<"[r06-m23b] reflected="<<mirrored
             <<" faces="<<p.faces.size()
             <<" generated="<<p.positions.size()-p.boundaryIds.size()
             <<" min_seed_q="<<p.minimumSeedQuality<<" PASS\n";
}
void neutralGravityRetainsThreeDimensionalAttraction() {
    // Both sides are reflected in X=0. Horizontal force cancels, but
    // shared +Y/+Z attraction must survive rather than be suppressed.
    std::vector<Vec3> positions{
        {1,0,1.5},{-1,0,1.5},{1,1,2},{-1,1,2},{0,0,100}
    };
    std::vector<std::uint8_t> sides{1,2,1,2,0};
    std::vector<std::uint32_t> front{0,1,2,3,4};
    SpatialCapPlacementProposal proposal;
    proposal.projectedPosition={0,0,0};
    proposal.frontPosition={0,0,0};
    proposal.localEdgeScale=1;
    proposal.originatingSide=0;
    proposal.activePositions=&positions;
    proposal.activeSides=&sides;
    proposal.activeFront=&front;

    const auto both=unifiedZipperGravity(proposal,true);
    require(both.x==0&&both.y>1e-8&&both.z>1e-8,
        "M23B neutral seam must retain out-of-plane pull from both sides");
    require(length(both-proposal.projectedPosition)<=.3+1e-12,
        "M23B neutral attraction must obey the original shift budget");

    // A neutral active-front point is not a source-side mass.
    positions[4]={100,100,100};
    const auto ignoredNeutral=unifiedZipperGravity(proposal,true);
    require(length(ignoredNeutral-both)<1e-12,
        "M23B neutral field must exclude other neutral targets");
    // Neutral vertices must respond to side 1 AS WELL AS side 2.
    positions[0].z+=1;
    const auto changedFirst=unifiedZipperGravity(proposal,true);
    require(length(changedFirst-both)>1e-6,
        "M23B neutral field must include first-side attraction");
    // Sided fronts retain their previous *opposite-only* semantics.
    proposal.originatingSide=1;
    const auto sidedBefore=unifiedZipperGravity(proposal,true);
    positions[0].z+=1;
    const auto sidedAfter=unifiedZipperGravity(proposal,true);
    require(length(sidedAfter-sidedBefore)<1e-12,
        "M23B sided fronts must not attract their own ancestry");
    proposal.originatingSide=2;
    const auto otherSideBefore=unifiedZipperGravity(proposal,true);
    positions[1].z+=1;
    const auto otherSideAfter=unifiedZipperGravity(proposal,true);
    require(length(otherSideAfter-otherSideBefore)<1e-12,
        "M23B both sided ancestries must retain opposite-only attraction");

    proposal.originatingSide=0;
    for(auto& s:sides)s=0;
    const auto empty=unifiedZipperGravity(proposal,true);
    require(length(empty-proposal.projectedPosition)==0,
        "M23B neutral gravity without sided targets must be a no-op");

    // An asymmetric zipper is not subject to a fictitious X=0 seam.
    sides[0]=1;
    const auto asym=unifiedZipperGravity(proposal,false);
    require(asym.x>0,
        "M23B asymmetric neutral fronts must keep horizontal attraction");
}

void reflectedSeamFollowsSourceTangent() {
    // Two source vertices reflected in X=0. The average outward surface
    // normal bends towards +Y. An uncorrected B-B' midpoint would be
    // (0,0,0), creating an immediate artificial 90-degree crease.
    const Vec3 a{.5,0,0},b{-.5,0,0};
    const Vec3 na{.6,.8,0},nb{-.6,.8,0};
    const auto d=projectReflectedBridgeSeamPoint(a,b,na,nb);
    require(d.x==0&&std::abs(d.y-.2)<1e-12&&d.z==0,
        "M23B initial bridge seam must follow survivor tangent before X=0 projection");
    const auto reverse=projectReflectedBridgeSeamPoint(b,a,nb,na);
    require(reverse.x==d.x&&std::abs(reverse.y-d.y)<1e-12&&reverse.z==d.z,
        "M23B seam seed must be independent of reflected rim ordering");
    const auto flat=projectReflectedBridgeSeamPoint(
        a,b,Vec3{0,1,0},Vec3{0,1,0});
    require(flat.x==0&&flat.y==0&&flat.z==0,
        "M23B a flat tangent crossing may legitimately produce the midpoint");
    const auto degenerate=projectReflectedBridgeSeamPoint(
        a,b,Vec3{1,0,0},Vec3{-1,0,0});
    require(degenerate.x==0&&degenerate.y==0&&degenerate.z==0,
        "M23B degenerate tangent should have a finite seam midpoint fallback");
}
void malformedMustRefuse() {
    auto a=circle(.6,8,100);
    auto b=circle(-.6,8,200);
    b.ids[0]=a.ids[0];
    SpatialCapSettings settings;
    settings.useAdvancingFront=true;
    bool refused=false;
    try{(void)buildUnifiedZipperFront(a,b,false,settings);}
    catch(const std::logic_error&) {refused=true;}
    require(refused,"M23B must refuse shared original source IDs");
}
}
int main() {
    try {
        test(false);
        test(true);
        reflectedSeamFollowsSourceTangent();
        neutralGravityRetainsThreeDimensionalAttraction();
        malformedMustRefuse();
    }catch(const std::exception& e) {
        std::cerr<<"R06-M23B: "<<e.what()<<"\n";
        return 1;
    }
}

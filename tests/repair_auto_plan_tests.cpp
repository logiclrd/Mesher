#include "mesher/repair_auto_plan.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/sculpt_fixtures.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void check(bool yes,const char* why) {
    if(!yes)throw std::runtime_error(why);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> mesh;
    FlatMeshIndex flat;
};
Fixture indexed(Mesh m) {
    auto input=partitionFlatMesh(m,64);
    return {RegionTopologyIndex::build(input.revision),input.flatIndex};
}
FaceId nearest(const Fixture& f,Vec3 position) {
    FaceId id=0;double distance=1e99;
    for(const auto face:f.flat.faceIds) {
        const auto& t=f.mesh->face(face).vertices;
        const auto center=(f.mesh->vertex(t[0]).position+
            f.mesh->vertex(t[1]).position+
            f.mesh->vertex(t[2]).position)/3;
        if(const auto d=length(center-position);d<distance) {
            id=face;distance=d;
        }
    }
    return id;
}
RegionTopologyIndex::CollisionPair pair(FaceId a,FaceId b) {
    if(b<a)std::swap(a,b);
    return {a,b,false};
}
void torusCaps() {
    const auto f=indexed(generateTorusFixture(5000,1,5));
    const auto first=nearest(f,{.12,0,0}),second=nearest(f,{-.12,0,0});
    const std::vector<RegionTopologyIndex::CollisionPair> contacts{
        pair(first,second)};
    const auto preview=previewApproximateExcision(*f.mesh,contacts);
    check(preview.complete&&preview.contours.size()==2,
          "R06-M03 tight torus should make two real directed rims");
    const auto cut=applyPrivateExcision(f.mesh,preview);
    const auto origin=f.mesh->pickingRootIdentity();
    const auto result=planPrivateAutomaticClosure(
        *f.mesh,cut,preview,contacts);
    check(result.ready()&&result.cappedContours.size()==2&&
          result.zipperPairs.empty()&&result.geometryRegions>=1,
          "R06-M03 tight torus must prefer independent caps to unwanted long bridge");
    check(!result.regions.empty()&&
          !result.regions.front().selectionReason.empty()&&
          result.regions.front().rimCentroidSeparation>=0&&
          std::isfinite(result.regions.front().rimNearestSeparationInEdges),
          "R06-M06A actual torus must report topology and finite two-rim evidence");
    check(result.survivors.components.size()==1&&
          result.estimatedAddedArea>0,
          "R06-M03 torus must preserve same-shell ownership and real cost");
    check(f.mesh->pickingRootIdentity()==origin &&
          f.mesh->retiredFaceCount()==0 &&
          cut.index->retiredFaceCount()==preview.proposedFaces.size(),
          "R06-M03 must be read-only and retain source/cut identity");
    std::cout<<"[r06-m03] tight torus: two independent caps, no zipper PASS\n";
}
void sphereSeparatedRepairs() {
    SphereOptions options;
    options.vertexCount=320;
    options.symmetric=false;
    options.relaxationIterations=8;
    const auto f=indexed(generateSphere(options));
    const auto sibling=[&](FaceId id) {
        const auto t=f.mesh->face(id).vertices;
        auto a=t[0],b=t[1];
        if(a>b)std::swap(a,b);
        for(const auto& side:f.mesh->edgeSides({a,b}))
            if(side.face!=id)return side.face;
        throw std::logic_error("R06-M03 missing adjacent sphere triangle");
    };
    const auto north=nearest(f,{0,1,0}),south=nearest(f,{0,-1,0});
    const auto contacts=std::vector<RegionTopologyIndex::CollisionPair>{
        pair(north,sibling(north)),pair(south,sibling(south))};
    const auto preview=previewApproximateExcision(*f.mesh,contacts);
    check(preview.complete&&preview.contours.size()==2,
          "R06-M03 sphere fixture must have two separate rim islands");
    const auto cut=applyPrivateExcision(f.mesh,preview);
    const auto planned=planPrivateAutomaticClosure(*f.mesh,cut,preview,contacts);
    check(planned.ready()&&planned.geometryRegions==2&&
          planned.cappedContours.size()==2&&planned.zipperPairs.empty(),
          "R06-M03 distant contacts on one surviving component must remain independent");
    std::cout<<"[r06-m03] two distant sphere repair regions never cross-pair PASS\n";
}
struct PrismCaps {std::size_t left{},right{},size{};};
PrismCaps prism(Mesh& mesh,double x0,double x1,double r) {
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr std::array<std::array<double,2>,4> ring{{
        {-1,-1},{1,-1},{1,1},{-1,1}
    }};
    for(const auto x:{x0,x1})
        for(const auto yz:ring)mesh.vertices.push_back({x,yz[0]*r,yz[1]*r});
    const auto lc=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({x0,0,0});
    const auto rc=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({x1,0,0});
    for(std::uint32_t i=0;i<4;++i) {
        const auto j=(i+1)%4;
        mesh.triangles.push_back({first+i,first+j,first+j+4});
        mesh.triangles.push_back({first+i,first+j+4,first+i+4});
    }
    const auto left=mesh.triangles.size();
    for(std::uint32_t i=0;i<4;++i)
        mesh.triangles.push_back({lc,first+(i+1)%4,first+i});
    const auto right=mesh.triangles.size();
    for(std::uint32_t i=0;i<4;++i)
        mesh.triangles.push_back({rc,first+i+4,first+(i+1)%4+4});
    return {left,right,4};
}
struct SyntheticCut {
    Fixture original;
    RepairExcisionPreview preview;
    PrivateExcisionResult cut;
    std::vector<RegionTopologyIndex::CollisionPair> links;
};
SyntheticCut stagedCaps(Mesh m,
                        std::vector<std::size_t> capTriangleIndices,
                        std::vector<std::pair<std::size_t,std::size_t>> pairPositions) {
    auto f=indexed(std::move(m));
    RepairExcisionPreview proposal;
    proposal.complete=true;
    for(const auto index:capTriangleIndices)
        proposal.proposedFaces.push_back(f.flat.faceIds.at(index));
    std::sort(proposal.proposedFaces.begin(),proposal.proposedFaces.end());
    const std::set<FaceId> removed(proposal.proposedFaces.begin(),
                                    proposal.proposedFaces.end());
    check(removed.size()==proposal.proposedFaces.size(),
          "R06-M03 synthetic removed faces must be unique");
    // Retire exact orphan cap-centre vertices and no surviving rim vertices.
    for(const auto id:f.flat.vertexIds) {
        const auto& incidence=f.mesh->vertex(id).incidentFaces;
        if(!incidence.empty() &&
           std::all_of(incidence.begin(),incidence.end(),
                       [&](FaceId face){return removed.contains(face);}))
            proposal.proposedVertices.push_back(id);
    }
    const auto contourPlan=planRemovalContours(*f.mesh,proposal.proposedFaces);
    proposal.contours=contourPlan.contours;
    for(const auto& ring:proposal.contours)
        proposal.boundaryEdges+=ring.vertexIds.size();
    check(proposal.boundaryEdges>0,"R06-M03 synthetic cut needs real boundary edges");
    const auto cut=applyPrivateExcision(f.mesh,proposal);
    std::vector<RegionTopologyIndex::CollisionPair> contacts;
    for(const auto& [a,b]:pairPositions)
        contacts.push_back(pair(f.flat.faceIds.at(a),f.flat.faceIds.at(b)));
    // Unit-test trusted provenance records connect excised patches. The
    // production caller supplies actual verified R01 triangle contacts.
    return {std::move(f),std::move(proposal),cut,std::move(contacts)};
}
void append(std::vector<std::size_t>& output,PrismCaps c,bool left) {
    for(std::size_t i=0;i<c.size;++i)
        output.push_back((left?c.left:c.right)+i);
}
void closeOpposingMouthsOnOneSurvivor() {
    // One topological tube curved into a wide U. Its two ends face each
    // other at a short gap: two independent disk caps would preserve a
    // sphere, whereas one zipper would intentionally create a handle.
    // The R01 link is a test-only provenance fixture (not a physical
    // intersection); the open R03 rims, original owners and orientation
    // are computed from actual indexed triangles.
    Mesh mesh;
    const std::array<Vec3,6> centers{{
        {-.28,0,0},{-3,0,0},{-3,4,0},
        {3,4,0},{3,0,0},{.28,0,0}
    }};
    constexpr std::array<std::array<double,2>,4> section{{
        {-1,-1},{1,-1},{1,1},{-1,1}
    }};
    for(std::size_t i=0;i<centers.size();++i) {
        const auto tangent=normalized(
            centers[std::min(i+1,centers.size()-1)]-
            centers[i==0?0:i-1]);
        const auto u=normalized(cross(Vec3{0,0,1},tangent));
        for(const auto& yz:section)
            mesh.vertices.push_back(
                centers[i]+u*yz[0]+Vec3{0,0,yz[1]});
    }
    for(std::uint32_t i=0;i+1<centers.size();++i)
        for(std::uint32_t j=0;j<4;++j) {
            const auto k=(j+1)%4;
            const auto a=4*i+j,b=4*i+k;
            const auto c=4*(i+1)+j,d=4*(i+1)+k;
            mesh.triangles.push_back({a,b,d});
            mesh.triangles.push_back({a,d,c});
        }
    const auto capA=mesh.triangles.size();
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(centers.front());
    for(std::uint32_t j=0;j<4;++j)
        mesh.triangles.push_back({first,(j+1)%4,j});
    const auto capB=mesh.triangles.size();
    const auto last=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(centers.back());
    const auto tail=4*static_cast<std::uint32_t>(centers.size()-1);
    for(std::uint32_t j=0;j<4;++j)
        mesh.triangles.push_back({last,tail+j,tail+(j+1)%4});
    std::vector<std::size_t> excise;
    for(std::size_t j=0;j<4;++j) {
        excise.push_back(capA+j);
        excise.push_back(capB+j);
    }
    auto f=stagedCaps(std::move(mesh),excise,{{capA,capB}});
    const auto planned=planPrivateAutomaticClosure(
        *f.original.mesh,f.cut,f.preview,f.links);
    check(planned.ready()&&planned.geometryRegions==1&&
          planned.zipperPairs.size()==1&&planned.cappedContours.empty()&&
          planned.survivors.components.size()==1,
          "R06-M06B close and directly linked opposing SAME-SURVIVOR mouths must zipper");
    const auto& decision=planned.regions.front();
    check(decision.contactLinks==1&&
          decision.excisionPatches.size()==2&&
          decision.rimNormalAlignment<-.95&&
          decision.rimNearestSeparationInEdges<=2.5&&
          decision.estimatedZipperArea<decision.estimatedCapArea*.55&&
          decision.estimatedZipperArea>decision.estimatedCapArea*.45&&
          decision.selectionReason.find("directly linked close opposing rims")!=
              std::string::npos,
          "R06-M06B must require meaningful facing provenance and approximately 50% area saving");
    const auto unlinked=planPrivateAutomaticClosure(
        *f.original.mesh,f.cut,f.preview,{});
    check(unlinked.ready()&&unlinked.cappedContours.size()==2&&
          unlinked.zipperPairs.empty(),
          "R06-M06B spatially close SAME-survivor rims must not zipper without trusted contacts");
    auto conservative=AutoClosurePlanSettings{};
    conservative.minimumFacingContactZipperSavings=.55;
    const auto oldPolicy=planPrivateAutomaticClosure(
        *f.original.mesh,f.cut,f.preview,f.links,conservative);
    check(oldPolicy.ready()&&oldPolicy.cappedContours.size()==2&&
          oldPolicy.zipperPairs.empty(),
          "R06-M06B synthetic fixture must reproduce the former over-conservative 55% decision");
    std::cout<<"[r06-m06b] two opposite mouths on one U tube: 50% area saving now zips, unlinked stays caps PASS\n";
}
void mixedCapAndZipper() {
    Mesh mesh;
    const auto left=prism(mesh,-2,0,1);
    const auto right=prism(mesh,.1,2,1);
    const auto distant=prism(mesh,6,8,.5);
    std::vector<std::size_t> red;
    append(red,left,false);
    append(red,right,true);
    append(red,distant,true);
    auto candidate=stagedCaps(std::move(mesh),red,
                              {{left.right,right.left}});
    const auto plan=planPrivateAutomaticClosure(
        *candidate.original.mesh,candidate.cut,candidate.preview,candidate.links);
    check(plan.ready()&&plan.geometryRegions==2&&
          plan.cappedContours.size()==1&&plan.zipperPairs.size()==1,
          "R06-M03 must plan one near-gap zipper and one independent distant cap");
    for(const auto& region:plan.regions)
        check(!region.selectionReason.empty(),
              "R06-M06A each actual repair region must expose its decision reason");
    const auto& seam=plan.zipperPairs.front();
    check(seam.first!=seam.second&&seam.gapInEdges>0&&
          plan.estimatedAddedArea>0,
          "R06-M03 selected seam uses V15 geometrical correspondence");
    const auto noLinks=planPrivateAutomaticClosure(
        *candidate.original.mesh,candidate.cut,candidate.preview,{});
    check(noLinks.ready()&&noLinks.geometryRegions==3&&
          noLinks.cappedContours.size()==3&&noLinks.zipperPairs.empty(),
          "R06-M03 spatially nearby rings cannot join without certified contact provenance");
    std::cout<<"[r06-m03] mixed cap/zipper plus no-contact no-guess PASS\n";
}
void nearTieMustRefuse() {
    Mesh mesh;
    // Equal square cap areas = 8. A coaxial annular strip of
    // length sqrt(4/pi) has essentially the same V15 estimated area.
    const double gap=std::sqrt(4.0/std::acos(-1.0));
    const auto left=prism(mesh,-2,0,1);
    const auto right=prism(mesh,gap,gap+2,1);
    std::vector<std::size_t> red;
    append(red,left,false);
    append(red,right,true);
    auto test=stagedCaps(std::move(mesh),red,{{left.right,right.left}});
    const auto plan=planPrivateAutomaticClosure(
        *test.original.mesh,test.cut,test.preview,test.links);
    check(!plan.ready()&&
          plan.status==AutoClosurePlanStatus::AmbiguousFitness&&
          plan.zipperPairs.empty()&&plan.cappedContours.empty()&&
          plan.regions.empty(),
          "R06-M03 equal-area alternatives must refuse rather than guess topology");
    std::cout<<"[r06-m03] near-equal cap/zipper costs explicitly refuse PASS\n";
}
void ambiguousManyToMany() {
    Mesh mesh;
    std::vector<PrismCaps> pieces;
    for(int i=0;i<8;++i)
        pieces.push_back(prism(mesh,double(i)*3,double(i)*3+1,.3));
    std::vector<std::size_t> red;
    std::vector<std::pair<std::size_t,std::size_t>> links;
    for(const auto& piece:pieces)append(red,piece,false);
    for(std::size_t i=1;i<pieces.size();++i)
        links.emplace_back(pieces[i-1].right,pieces[i].right);
    auto candidate=stagedCaps(std::move(mesh),red,links);
    const auto plan=planPrivateAutomaticClosure(
        *candidate.original.mesh,candidate.cut,candidate.preview,candidate.links);
    std::cout<<"[r06-m03-many] status="<<static_cast<int>(plan.status)
             <<" patches="<<plan.excisionPatches
             <<" regions="<<plan.geometryRegions
             <<" links="<<plan.trustedContactLinks
             <<" loops="<<candidate.preview.contours.size()
             <<" reason="<<plan.diagnostic<<"\n";
    check(!plan.ready()&&plan.status==AutoClosurePlanStatus::AmbiguousRegion &&
          plan.diagnostic.find("many-to-many")!=std::string::npos&&
          plan.cappedContours.empty()&&plan.zipperPairs.empty(),
          "R06-M03 linked eight-loop (3-vs-5) region must refuse as a WHOLE");
    std::cout<<"[r06-m03] ambiguous 3-vs-5-like region refuses without partial plan PASS\n";
}
void invalidAndBudgetRefusal() {
    Mesh mesh;
    const auto a=prism(mesh,-2,0,1),b=prism(mesh,.1,2,1);
    std::vector<std::size_t> faces;
    append(faces,a,false);append(faces,b,true);
    auto test=stagedCaps(std::move(mesh),faces,{{a.right,b.left}});
    AutoClosurePlanSettings settings;
    settings.maxExcisionFaces=1;
    const auto budget=planPrivateAutomaticClosure(
        *test.original.mesh,test.cut,test.preview,test.links,settings);
    check(budget.status==AutoClosurePlanStatus::BudgetExceeded&&
          budget.zipperPairs.empty()&&budget.cappedContours.empty(),
          "R06-M03 finite excision limit must refuse without partial plan");
    auto fabricated=test.links;
    fabricated.push_back({test.original.flat.faceIds.front(),
                          test.original.flat.faceIds.back(),true});
    const auto invalid=planPrivateAutomaticClosure(
        *test.original.mesh,test.cut,test.preview,fabricated);
    check(invalid.status==AutoClosurePlanStatus::InvalidInput &&
          !invalid.ready()&&invalid.regions.empty(),
          "R06-M03 ambiguous/stale contact provenance must be refused");
    std::cout<<"[r06-m03] finite budget and false provenance refuse PASS\n";
}
void zeroLoopRetirementIsNotDeletion() {
    Mesh mesh;
    const auto cube=[&](Vec3 center) {
        const auto offset=static_cast<std::uint32_t>(mesh.vertices.size());
        for(const auto p:std::array<Vec3,8>{{
            {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
            {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
        }})mesh.vertices.push_back(center+p);
        for(const auto t:std::array<std::array<std::uint32_t,3>,12>{{
            {0,2,1},{0,3,2},{4,5,6},{4,6,7},
            {0,1,5},{0,5,4},{3,7,6},{3,6,2},
            {0,4,7},{0,7,3},{1,2,6},{1,6,5}
        }})mesh.triangles.push_back({offset+t[0],offset+t[1],offset+t[2]});
    };
    cube({-3,0,0});cube({3,0,0});
    const auto source=indexed(std::move(mesh));
    RepairExcisionPreview proposal;
    proposal.complete=true;
    proposal.proposedVertices.assign(source.flat.vertexIds.begin(),
                                      source.flat.vertexIds.begin()+8);
    proposal.proposedFaces.assign(source.flat.faceIds.begin(),
                                   source.flat.faceIds.begin()+12);
    const auto cut=applyPrivateExcision(source.mesh,proposal);
    const auto result=planPrivateAutomaticClosure(*source.mesh,cut,proposal,{});
    check(result.ready()&&result.regions.size()==1&&
          result.regions.front().zeroLoopComponentRetirement&&
          result.cappedContours.empty()&&result.zipperPairs.empty()&&
          source.mesh->retiredFaceCount()==0,
          "R06-M03 zero-loop whole-component excision needs no fake cap or zipper");
    std::cout<<"[r06-m03] zero-loop component retirement remains private/not auto deletion PASS\n";
}
}
int main() {
    try {
        torusCaps();
        sphereSeparatedRepairs();
        closeOpposingMouthsOnOneSurvivor();
        mixedCapAndZipper();
        nearTieMustRefuse();
        ambiguousManyToMany();
        invalidAndBudgetRefusal();
        zeroLoopRetirementIsNotDeletion();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M03 automatic closure plan FAILED: "<<e.what()<<'\n';
        return 1;
    }
}

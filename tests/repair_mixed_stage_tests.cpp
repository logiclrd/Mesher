#include "mesher/repair_mixed_stage.hpp"
#include "mesher/repair_mixed_closure.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/sculpt_fixtures.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* why) {
    if(!yes)throw std::runtime_error(why);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    FlatMeshIndex flat;
};
Fixture fixture(Mesh mesh) {
    auto imp=partitionFlatMesh(mesh,64);
    return {RegionTopologyIndex::build(imp.revision),imp.flatIndex};
}
FaceId nearest(const Fixture& f,Vec3 p) {
    FaceId best=0;
    double distance=1e99;
    for(const auto id:f.flat.faceIds) {
        const auto tri=f.root->face(id).vertices;
        const auto center=(f.root->vertex(tri[0]).position+
            f.root->vertex(tri[1]).position+
            f.root->vertex(tri[2]).position)/3;
        if(const auto d=length(center-p);d<distance) {
            best=id;distance=d;
        }
    }
    return best;
}
struct CapPosition {std::size_t left{},right{};};
CapPosition prism(Mesh& m,double x0,double x1,double radius) {
    constexpr std::array<std::array<double,2>,4> square{{
        {-1,-1},{1,-1},{1,1},{-1,1}
    }};
    const auto base=static_cast<std::uint32_t>(m.vertices.size());
    for(const auto x:{x0,x1})
        for(const auto yz:square)m.vertices.push_back({x,yz[0]*radius,yz[1]*radius});
    const auto lc=static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({x0,0,0});
    const auto rc=static_cast<std::uint32_t>(m.vertices.size());
    m.vertices.push_back({x1,0,0});
    for(std::uint32_t k=0;k<4;++k) {
        const auto j=(k+1)%4;
        m.triangles.push_back({base+k,base+j,base+j+4});
        m.triangles.push_back({base+k,base+j+4,base+k+4});
    }
    const auto left=m.triangles.size();
    for(std::uint32_t k=0;k<4;++k)
        m.triangles.push_back({lc,base+(k+1)%4,base+k});
    const auto right=m.triangles.size();
    for(std::uint32_t k=0;k<4;++k)
        m.triangles.push_back({rc,base+k+4,base+(k+1)%4+4});
    return {left,right};
}
void addCapFaces(std::vector<std::size_t>& indices,CapPosition cap,bool right) {
    const auto start=right?cap.right:cap.left;
    for(std::size_t i=0;i<4;++i)indices.push_back(start+i);
}
RegionTopologyIndex::CollisionPair contact(FaceId a,FaceId b) {
    if(a>b)std::swap(a,b);
    return {a,b,false};
}
void mixedAndTampered() {
    Mesh m;
    const auto left=prism(m,-2,0,1),right=prism(m,.5,2.5,1);
    const auto remote=prism(m,6,8,.5);
    std::vector<std::size_t> removed;
    addCapFaces(removed,left,true);
    addCapFaces(removed,right,false);
    addCapFaces(removed,remote,false);
    const auto f=fixture(std::move(m));
    RepairExcisionPreview proposal;
    proposal.complete=true;
    for(const auto index:removed)
        proposal.proposedFaces.push_back(f.flat.faceIds[index]);
    std::sort(proposal.proposedFaces.begin(),proposal.proposedFaces.end());
    const std::set<FaceId> deleted(proposal.proposedFaces.begin(),proposal.proposedFaces.end());
    for(const auto id:f.flat.vertexIds) {
        const auto& incident=f.root->vertex(id).incidentFaces;
        if(!incident.empty()&&
           std::all_of(incident.begin(),incident.end(),
               [&](FaceId id){return deleted.contains(id);}))
            proposal.proposedVertices.push_back(id);
    }
    const auto rings=planRemovalContours(*f.root,proposal.proposedFaces);
    proposal.contours=rings.contours;
    for(const auto& loop:proposal.contours)
        proposal.boundaryEdges+=loop.vertexIds.size();
    const auto cut=applyPrivateExcision(f.root,proposal);
    const auto contacts=std::vector<RegionTopologyIndex::CollisionPair>{
        contact(f.flat.faceIds[left.right],f.flat.faceIds[right.left])};
    const auto plan=planPrivateAutomaticClosure(*f.root,cut,proposal,contacts);
    require(plan.ready()&&plan.cappedContours.size()==1&&
            plan.zipperPairs.size()==1,"R06-M04A mixed fixture requires an M03 cap+zipper choice");
    const auto staging=stagePrivateMixedClosure(cut,proposal,plan);
    require(staging.capLoops==1&&staging.zipperPairs==1&&
            staging.capTriangles>0&&staging.zipperTriangles>0&&
            staging.boundarySidesCovered==proposal.boundaryEdges&&
            cut.index->retiredFaceCount()==proposal.proposedFaces.size()&&
            f.root->retiredFaceCount()==0,
            "R06-M04A must stage all actual oriented rims without mutation");

    auto invalid=plan;
    invalid.cappedContours.push_back(plan.zipperPairs.front().first);
    bool refused=false;
    try{(void)stagePrivateMixedClosure(cut,proposal,invalid);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R06-M04A must refuse duplicated cap/zipper rim before execution");

    invalid=plan;
    invalid.regions.front().zippers.clear();
    refused=false;
    try{(void)stagePrivateMixedClosure(cut,proposal,invalid);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R06-M04A must refuse forged global cross-region seam");

    auto bounds=MixedClosureStageSettings{};
    bounds.maxNewTriangles=1;
    refused=false;
    try{(void)stagePrivateMixedClosure(cut,proposal,plan,bounds);}
    catch(const std::length_error&){refused=true;}
    require(refused,"R06-M04A must bound combined new geometry");
    const auto closed=applyPrivateMixedClosure(cut,proposal,plan);
    require(closed.index && closed.index!=cut.index &&
            !closed.greenFaces.empty() && !closed.blueFaces.empty() &&
            closed.cappedLoops==1 && closed.zipperPairs==1 &&
            closed.minimumCapQuality>=MixedPrivateClosureLimits{}.minCapQuality &&
            closed.minimumZipperQuality>=MixedPrivateClosureLimits{}.minZipperQuality &&
            f.root->retiredFaceCount()==0 && cut.index->retiredFaceCount()==
                proposal.proposedFaces.size(),
            "R06-M04B must finish a genuinely mixed closed manifold in one private root");
    for(const auto id:closed.greenFaces)
        require(closed.index->maybeFace(id)&&!cut.index->maybeFace(id),
                "R06-M04B green cap faces need fresh IDs");
    for(const auto id:closed.blueFaces)
        require(closed.index->maybeFace(id)&&!cut.index->maybeFace(id),
                "R06-M04B blue seam faces need fresh IDs");
    auto bound=MixedPrivateClosureLimits{};
    bound.maxNewFaces=1;
    refused=false;
    try{(void)applyPrivateMixedClosure(cut,proposal,plan,bound);}
    catch(const std::length_error&){refused=true;}
    require(refused && f.root->retiredFaceCount()==0,
            "R06-M04B exhausted work must not alter original immutable revision");
    std::cout<<"[r06-m04ab] real mixed cap+zipper private closed, stable IDs preserved PASS\n";
}
void tightTorus() {
    const auto f=fixture(generateTorusFixture(5000,1,5));
    const auto contacts=std::vector<RegionTopologyIndex::CollisionPair>{
        contact(nearest(f,{.12,0,0}),nearest(f,{-.12,0,0}))};
    const auto proposal=previewApproximateExcision(*f.root,contacts);
    require(proposal.complete&&proposal.contours.size()==2,
            "R06-M04A torus must produce two real cut rims");
    const auto cut=applyPrivateExcision(f.root,proposal);
    const auto plan=planPrivateAutomaticClosure(*f.root,cut,proposal,contacts);
    require(plan.ready()&&plan.cappedContours.size()==2,
            "R06-M04A tight torus must be capped by M03");
    const auto staged=stagePrivateMixedClosure(cut,proposal,plan);
    require(staged.capLoops==2&&staged.zipperPairs==0&&
            staged.boundarySidesCovered==cut.boundaryEdges,
            "R06-M04A torus must have complete oriented two-cap closure");
    const auto closed=applyPrivateMixedClosure(cut,proposal,plan);
    require(closed.index && closed.greenFaces.size()>2 &&
            closed.blueFaces.empty() && closed.cappedLoops==2,
            "R06-M04B tight torus must close with real caps and no zipper");
    std::set<std::pair<VertexId,VertexId>> edges;
    std::size_t vertices=0,faces=0;
    closed.index->revision().forEachPartition([&](
        PartitionId,const MeshPartition& part) {
        vertices+=part.vertices.size();
        faces+=part.faces.size();
        for(const auto& f:part.faces)
            for(unsigned k=0;k<3;++k) {
                auto a=f.vertices[k],b=f.vertices[(k+1)%3];
                if(a>b)std::swap(a,b);
                edges.insert({a,b});
            }
    });
    require(vertices+faces-edges.size()==0,
            "R06-M04B unmodified torus disk cuts must preserve its genus-one handle");
    std::cout<<"[r06-m04ab] unmodified torus local disk caps preserve Euler 0 PASS\n";
}
}
int main() {
    try{
        mixedAndTampered();
        tightTorus();
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<"R06-M04A mixed staging FAILED: "<<error.what()<<"\n";
        return 1;
    }
}

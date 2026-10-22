#include "mesher/repair_auto_private.hpp"
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
void require(bool ok,const char* reason) {
    if(!ok)throw std::runtime_error(reason);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> mesh;
    FlatMeshIndex ids;
};
Fixture fixture(Mesh mesh) {
    auto imported=partitionFlatMesh(mesh,64);
    return {RegionTopologyIndex::build(imported.revision),imported.flatIndex};
}
FaceId nearest(const Fixture& f,Vec3 p) {
    FaceId answer=0;
    double best=1e100;
    for(const auto id:f.ids.faceIds) {
        const auto t=f.mesh->face(id).vertices;
        const auto center=(f.mesh->vertex(t[0]).position+
                           f.mesh->vertex(t[1]).position+
                           f.mesh->vertex(t[2]).position)/3;
        if(const auto d=length(center-p);d<best) {
            answer=id;best=d;
        }
    }
    return answer;
}
RegionTopologyIndex::CollisionPair pair(FaceId a,FaceId b) {
    if(a>b)std::swap(a,b);
    return {a,b,false};
}
std::int64_t euler(const RegionTopologyIndex& mesh) {
    std::set<std::pair<VertexId,VertexId>> edges;
    std::size_t faces=0,vertices=0;
    mesh.revision().forEachPartition([&](
        PartitionId,const MeshPartition& part) {
        vertices+=part.vertices.size();
        faces+=part.faces.size();
        for(const auto& f:part.faces)
            for(unsigned i=0;i<3;++i) {
                auto a=f.vertices[i],b=f.vertices[(i+1)%3];
                if(a>b)std::swap(a,b);
                edges.insert({a,b});
            }
    });
    return static_cast<std::int64_t>(vertices)-
           static_cast<std::int64_t>(edges.size())+
           static_cast<std::int64_t>(faces);
}
void nestedInwardShellRetired() {
    SphereOptions opts;
    opts.vertexCount=320;
    opts.symmetric=false;
    opts.relaxationIterations=8;
    const auto unit=generateSphere(opts);
    Mesh combined;
    for(const auto& [radius,inverted]:{
        std::pair{1.0,false},std::pair{.55,true}}) {
        const auto offset=static_cast<std::uint32_t>(combined.vertices.size());
        for(const auto p:unit.vertices)
            combined.vertices.push_back(p*radius);
        for(const auto t:unit.triangles) {
            if(inverted)
                combined.triangles.push_back({offset+t.a,offset+t.c,offset+t.b});
            else
                combined.triangles.push_back({offset+t.a,offset+t.b,offset+t.c});
        }
    }
    const auto f=fixture(std::move(combined));
    const auto before=inspectEnclosedComponents(f.mesh);
    require(before.components.size()==2&&
            std::count_if(before.components.begin(),before.components.end(),
                [](const auto& c) {
                    return c.kind==EnclosedComponentKind::RedundantEnclosedCavity;
                })==1,"R06-M04C nested fixture must have one proven inward cavity");
    const auto contacts=std::vector<RegionTopologyIndex::CollisionPair>{
        pair(nearest(f,{0,1,0}),nearest(f,{0,.55,0}))};
    const auto proposal=previewApproximateExcision(*f.mesh,contacts);
    require(proposal.complete&&proposal.contours.size()==2,
            "R06-M04C synthetic nested contact must cut both independent shells");
    const auto cut=applyPrivateExcision(f.mesh,proposal);
    const auto plan=planPrivateAutomaticClosure(*f.mesh,cut,proposal,contacts);
    require(plan.ready()&&plan.cappedContours.size()==2&&
            plan.zipperPairs.empty(),
            "R06-M04C nested cavity fixture must select two independent caps");
    const auto result=applyCertifiedPrivateAutomaticClosure(
        f.mesh,cut,proposal,plan,contacts);
    require(result.index&&result.retiredCavityComponents==1&&
            !result.retiredCavityFaces.empty() &&
            !result.retiredCavityVertices.empty()&&
            result.acceptedExteriorComponents==1&&
            euler(*result.index)==2 &&
            f.mesh->retiredFaceCount()==0,
            "R06-M04C must discard the whole inverted enclosed shell and preserve Euler 2 exterior");
    for(const auto id:result.retiredCavityFaces)
        require(!result.index->maybeFace(id)&&result.index->retiredFace(id),
                "R06-M04C inner component stable face IDs must remain tombstoned");
    std::cout<<"[r06-m04c] enclosed inward shell retired, one exterior Euler 2 PASS\n";
    auto budgets=CertifiedPrivateAutoLimits{};
    budgets.maxRetiredCavityFaces=1;
    bool refused=false;
    try{(void)applyCertifiedPrivateAutomaticClosure(
        f.mesh,cut,proposal,plan,contacts,budgets);}
    catch(const std::length_error&){refused=true;}
    require(refused&&f.mesh->retiredFaceCount()==0&&
            cut.index->retiredFaceCount()==proposal.proposedFaces.size(),
            "R06-M04C exhausted cavity budget must refuse without altering source");
}
void torusAccepted() {
    const auto f=fixture(generateTorusFixture(5000,1,5));
    const auto contacts=std::vector<RegionTopologyIndex::CollisionPair>{
        pair(nearest(f,{.12,0,0}),nearest(f,{-.12,0,0}))};
    const auto preview=previewApproximateExcision(*f.mesh,contacts);
    const auto cut=applyPrivateExcision(f.mesh,preview);
    const auto plan=planPrivateAutomaticClosure(*f.mesh,cut,preview,contacts);
    require(plan.ready()&&plan.cappedContours.size()==2,
            "R06-M04C tight torus needs two automatic caps");
    const auto result=applyCertifiedPrivateAutomaticClosure(
        f.mesh,cut,preview,plan,contacts);
    std::cout<<"[r06-m04c-torus] retired="<<result.retiredCavityComponents
             <<" exterior="<<result.acceptedExteriorComponents
             <<" Euler="<<euler(*result.index)
             <<" green="<<result.greenFaces.size()
             <<" blue="<<result.blueFaces.size()<<"\n";
    require(result.index&&result.retiredCavityComponents==0&&
            result.acceptedExteriorComponents==1&&
            euler(*result.index)==0&&result.greenFaces.size()>2&&
            result.blueFaces.empty(),
            "R06-M04C unmodified torus local disk closures retain Euler 0 and no zipper");
    std::cout<<"[r06-m04c] unmodified torus local caps retain Euler 0 PASS\n";
}
void zeroRimEnclosedInnerAccepted() {
    SphereOptions opts;
    opts.vertexCount=160;
    opts.symmetric=false;
    opts.relaxationIterations=8;
    const auto sphere=generateSphere(opts);
    Mesh mesh;
    for(const auto radius:{1.0,.5}) {
        const auto offset=static_cast<std::uint32_t>(mesh.vertices.size());
        for(const auto v:sphere.vertices)
            mesh.vertices.push_back(v*radius);
        for(const auto t:sphere.triangles)
            if(radius<1)
                mesh.triangles.push_back({offset+t.a,offset+t.c,offset+t.b});
            else
                mesh.triangles.push_back({offset+t.a,offset+t.b,offset+t.c});
    }
    const auto source=fixture(std::move(mesh));
    RepairExcisionPreview preview;
    preview.complete=true;
    const auto innerVertexBegin=sphere.vertices.size();
    const auto innerFaceBegin=sphere.triangles.size();
    preview.proposedVertices.assign(
        source.ids.vertexIds.begin()+static_cast<std::ptrdiff_t>(innerVertexBegin),
        source.ids.vertexIds.end());
    preview.proposedFaces.assign(
        source.ids.faceIds.begin()+static_cast<std::ptrdiff_t>(innerFaceBegin),
        source.ids.faceIds.end());
    const auto cut=applyPrivateExcision(source.mesh,preview);
    require(cut.boundaryLoops==0&&cut.boundaryEdges==0,
            "R06-M04C entire enclosed inward shell must cut with zero open rims");
    const auto plan=planPrivateAutomaticClosure(*source.mesh,cut,preview,{});
    require(plan.ready()&&plan.cappedContours.empty()&&
            plan.zipperPairs.empty()&&
            plan.regions.size()==1&&
            plan.regions.front().zeroLoopComponentRetirement,
            "R06-M04C zero-loop certificate needs no invented closure faces");
    const auto result=applyCertifiedPrivateAutomaticClosure(
        source.mesh,cut,preview,plan,{});
    require(result.index&&result.greenFaces.empty()&&
            result.blueFaces.empty()&&
            result.acceptedExteriorComponents==1&&euler(*result.index)==2 &&
            source.mesh->retiredFaceCount()==0 &&
            result.index->retiredFaceCount()==preview.proposedFaces.size(),
            "R06-M04C zero-rim inner cavity is safely retired with exterior preserved");
    std::cout<<"[r06-m04c] pre-certified whole inward component zero-loop removal PASS\n";
}
void zeroRimExteriorRefused() {
    Mesh mesh;
    const auto cube=[&](Vec3 shift) {
        const auto offset=static_cast<std::uint32_t>(mesh.vertices.size());
        for(const auto v:std::array<Vec3,8>{{
            {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
            {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
        }})mesh.vertices.push_back(v+shift);
        for(const auto t:std::array<std::array<std::uint32_t,3>,12>{{
            {0,2,1},{0,3,2},{4,5,6},{4,6,7},
            {0,1,5},{0,5,4},{3,7,6},{3,6,2},
            {0,4,7},{0,7,3},{1,2,6},{1,6,5}
        }})mesh.triangles.push_back({offset+t[0],offset+t[1],offset+t[2]});
    };
    cube({-3,0,0});cube({3,0,0});
    const auto f=fixture(std::move(mesh));
    RepairExcisionPreview preview;
    preview.complete=true;
    preview.proposedVertices.assign(f.ids.vertexIds.begin(),
                                    f.ids.vertexIds.begin()+8);
    preview.proposedFaces.assign(f.ids.faceIds.begin(),
                                 f.ids.faceIds.begin()+12);
    const auto cut=applyPrivateExcision(f.mesh,preview);
    const auto plan=planPrivateAutomaticClosure(*f.mesh,cut,preview,{});
    require(plan.ready()&&plan.regions.front().zeroLoopComponentRetirement,
            "R06-M04C needs real zero-boundary whole-component proposal");
    bool refused=false;
    try{(void)applyCertifiedPrivateAutomaticClosure(
        f.mesh,cut,preview,plan,{});}
    catch(const std::logic_error&){refused=true;}
    require(refused&&f.mesh->retiredFaceCount()==0 &&
            cut.index->retiredFaceCount()==12,
            "R06-M04C must refuse retirement of entire exterior solid with zero rim");
    std::cout<<"[r06-m04c] whole exterior deletion without material proof refused PASS\n";
}
}
int main() {
    try {
        nestedInwardShellRetired();
        torusAccepted();
        zeroRimEnclosedInnerAccepted();
        zeroRimExteriorRefused();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M04C private automatic closure FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

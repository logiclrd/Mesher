#include "mesher/repair_cap.hpp"
#include "mesher/repair_inspection.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/region_edit.hpp"
#include "mesher/sculpt_fixtures.hpp"
#include "mesher/mesh_partitioning.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> mesh;
    FlatMeshIndex flat;
};
Fixture fixture(Mesh mesh) {
    auto imported=partitionFlatMesh(mesh,64);
    return {RegionTopologyIndex::build(imported.revision),imported.flatIndex};
}
FaceId nearest(const Fixture& f,Vec3 target) {
    FaceId id=0;double distance=1e99;
    for(const auto face:f.flat.faceIds) {
        const auto t=f.mesh->face(face).vertices;
        const auto center=(f.mesh->vertex(t[0]).position+
            f.mesh->vertex(t[1]).position+f.mesh->vertex(t[2]).position)/3;
        const auto d=length(center-target);
        if(d<distance){distance=d;id=face;}
    }
    return id;
}
void exercise(const char* name,const Fixture& source,
              std::vector<RegionTopologyIndex::CollisionPair> contacts,
              std::size_t expected) {
    const auto plan=previewApproximateExcision(*source.mesh,contacts);
    require(plan.complete&&plan.contours.size()==expected,
            "R05 fixture must create planned number of simple oriented contours");
    const auto cut=applyPrivateExcision(source.mesh,plan);
    const auto result=applyForcedPrivateCap(cut,plan);
    require(result.index&&result.index!=cut.index&&
            result.cappedLoops==expected&&
            result.greenFaces.size()>=expected&&
            result.totalNewFaces>=result.greenFaces.size()&&
            result.minimumCapQuality>=ForcedCapSettings{}.minCapQuality,
            "R05 independent caps must be real valid private closed geometry");
    for(const auto id:plan.proposedFaces)
        require(!result.index->maybeFace(id)&&
                result.index->retiredFace(id)&&source.mesh->maybeFace(id),
                "R05 must not resurrect excised or mutate source geometry");
    for(const auto id:result.greenFaces)
        require(result.index->maybeFace(id)&&!cut.index->maybeFace(id),
                "R05 green faces must have new stable private IDs");
    require(source.mesh->retiredFaceCount()==0&&
            cut.index->retiredFaceCount()==plan.proposedFaces.size(),
            "R05 must not modify the published source or R03 cut");
    std::cout<<"[r05] "<<name<<" caps="<<result.cappedLoops
             <<" green_faces="<<result.greenFaces.size()
             <<" vertices="<<result.insertedVertices.size()
             <<" refinement="<<result.refinementSteps
             <<" quality="<<result.minimumCapQuality<<" : PASS\n";
}
void torusPair() {
    const auto f=fixture(generateTorusFixture(5000,1,5));
    FaceId a=nearest(f,{.12,0,0}),b=nearest(f,{-.12,0,0});
    if(a>b)std::swap(a,b);
    exercise("tight torus two caps",f,{{a,b,false}},2);
}
void hollowSingle() {
    const auto f=fixture(generateHollowSphereFixture(5000,1,65));
    FaceId a=nearest(f,{.095,-.96,0}),b=nearest(f,{.095,-.93,0});
    if(a>b)std::swap(a,b);
    const auto plan=previewApproximateExcision(*f.mesh,{{a,b,false}});
    require(plan.complete&&plan.contours.size()==1,
            "R05 hollow mouth fixture should yield one real oriented loop");
    const auto cut=applyPrivateExcision(f.mesh,plan);
    try {
        const auto result=applyForcedPrivateCap(cut,plan);
        require(result.index&&result.cappedLoops==1,
                "R05 successfully generated hollow cap must close the private mesh");
        std::cout<<"[r05] hollow mouth one cap: PASS\n";
    }catch(const std::logic_error& e) {
        // This is a deeply folded synthetic selection: projection may
        // collapse to a self-overlapping polygon despite valid R03 holes.
        // Refusal is explicitly allowed at R05; it must not be worked
        // around by dropping winding or global manifold checks.
        require(cut.index!=f.mesh&&f.mesh->retiredFaceCount()==0,
                "R05 unsupported cap must not alter the original revision");
        std::cout<<"[r05] hollow mouth unsupported projected disk correctly refused: "
                 <<e.what()<<"\n";
    }
}
void sphereSingle() {
    SphereOptions options;
    options.vertexCount=320;options.symmetric=false;options.relaxationIterations=8;
    const auto f=fixture(generateSphere(options));
    const auto face=nearest(f,{0,1,0});
    const auto t=f.mesh->face(face).vertices;
    auto a=t[0],b=t[1];
    if(a>b)std::swap(a,b);
    FaceId partner=0;
    for(const auto& side:f.mesh->edgeSides({a,b}))
        if(side.face!=face)partner=side.face;
    require(partner!=0,"R05 single spherical cap needs an adjacent face");
    exercise("one independent sphere cap",f,
        {{std::min(face,partner),std::max(face,partner),false}},1);
}
void sphereTwoIslands() {
    SphereOptions options;
    options.vertexCount=320;options.symmetric=false;options.relaxationIterations=8;
    const auto f=fixture(generateSphere(options));
    FaceId top=nearest(f,{0,1,0}),bottom=nearest(f,{0,-1,0});
    const auto partner=[&](FaceId id) {
        const auto t=f.mesh->face(id).vertices;
        auto a=t[0],b=t[1];
        if(a>b)std::swap(a,b);
        for(const auto& side:f.mesh->edgeSides({a,b}))
            if(side.face!=id)return side.face;
        throw std::logic_error("Sphere missing adjacent face");
    };
    const auto first=partner(top),second=partner(bottom);
    exercise("two distant independent sphere caps",f,
            {{std::min(first,top),std::max(first,top),false},
             {std::min(second,bottom),std::max(second,bottom),false}},2);
}
void actualSession() {
    Mesh m;
    const auto tetra=[&](Vec3 at) {
        const auto offset=static_cast<std::uint32_t>(m.vertices.size());
        for(const auto point:std::array<Vec3,4>{{
            {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
        }})m.vertices.push_back(at+point*.5);
        for(const auto t:std::array<std::array<std::uint32_t,3>,4>{{
            {0,1,2},{0,2,3},{0,3,1},{1,3,2}
        }})m.triangles.push_back({offset+t[0],offset+t[1],offset+t[2]});
    };
    tetra({-2,0,0});tetra({2,0,0});
    const auto original=fixture(std::move(m));
    std::vector<VertexId> moved(original.flat.vertexIds.begin(),
                                original.flat.vertexIds.begin()+4);
    RegionEdit edit(original.mesh,moved,{},64);
    for(const auto id:moved)
        edit.moveVertex(id,original.mesh->vertex(id).position+Vec3{3.9,0,0});
    (void)edit.commit();
    RepairInspectionSession inspector(true,true,false,true);
    require(!inspector.inspect({original.mesh,edit.updatedIndex(),moved,{}}).empty(),
            "R05 first real contact must freeze the private inspector");
    const auto snapshot=inspector.frozenSnapshot();
    require(snapshot&&snapshot->capEnabled&&snapshot->privateCut,
            "R05 must run only after R03 private excision");
    // This original R01 regression has only four faces per component.
    // Its R02/R03 vertex-star proposal removes one entire tetrahedron,
    // leaving zero open loops. Auto repair will handle this in R06;
    // ALWAYS CAP must refuse instead of manufacturing an arbitrary disk.
    require(snapshot->excision.contours.empty()&&
            !snapshot->privateCap&&
            snapshot->capDiagnostic.find("at least one")!=std::string::npos,
            "R05 must refuse full-component retirement with no hole to cap");
    require(original.mesh->retiredFaceCount()==0&&
            edit.updatedIndex()->retiredFaceCount()==0,
            "R05 real inspector must not publish/mutate prior roots");
    std::cout<<"[r05] full inspector zero-loop component removal correctly refused"
             <<" (no CAS/history publication): PASS\n";
}
void refuseZeroOrMalformed() {
    const auto f=fixture(generateTorusFixture(5000,1,5));
    RepairExcisionPreview plan;
    plan.complete=true;
    PrivateExcisionResult cut;
    cut.index=f.mesh;
    bool refused=false;
    try{(void)applyForcedPrivateCap(cut,plan);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R05 must refuse zero-loop/no-op instead of inventing geometry");
}
}
int main() {
    try {
        sphereTwoIslands();
        sphereSingle();
        torusPair();
        hollowSingle();
        actualSession();
        refuseZeroOrMalformed();
        std::cout<<"R05 frozen cap CPU acceptance: PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R05 cap regression FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

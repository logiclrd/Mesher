#include "mesher/repair_surgery.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool condition,const char* reason) {
    if(!condition)throw std::runtime_error(reason);
}
void cube(Mesh& m,Vec3 center,double half) {
    const auto first=static_cast<std::uint32_t>(m.vertices.size());
    for(const auto offset:std::array<Vec3,8>{{
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
    }})m.vertices.push_back(center+offset*half);
    for(const auto face:std::array<std::array<std::uint32_t,3>,12>{{
        {0,2,1},{0,3,2},{4,5,6},{4,6,7},
        {0,1,5},{0,5,4},{3,7,6},{3,6,2},
        {0,4,7},{0,7,3},{1,2,6},{1,6,5}
    }})m.triangles.push_back({first+face[0],first+face[1],first+face[2]});
}
struct CapIndex {std::size_t left{},right{},count{};};
CapIndex prism(Mesh& mesh,double x0,double x1,
               const std::vector<std::array<double,2>>& ring) {
    const auto n=ring.size();
    const auto start=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto x:{x0,x1})
        for(const auto& yz:ring)mesh.vertices.push_back({x,yz[0],yz[1]});
    Vec3 average{};
    for(const auto& yz:ring)average=average+Vec3{0,yz[0],yz[1]};
    average=average/static_cast<double>(n);
    const auto leftCenter=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({x0,average.y,average.z});
    const auto rightCenter=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({x1,average.y,average.z});
    for(std::uint32_t i=0;i<n;++i) {
        const auto next=(i+1)%static_cast<std::uint32_t>(n);
        mesh.triangles.push_back({start+i,start+next,
                                  start+next+static_cast<std::uint32_t>(n)});
        mesh.triangles.push_back({start+i,
                                  start+next+static_cast<std::uint32_t>(n),
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
    return {left,right,n};
}
std::vector<std::array<double,2>> square(double size) {
    return {{-size,-size},{size,-size},{size,size},{-size,size}};
}
std::vector<std::array<double,2>> unevenSquare(double size) {
    return {{-size,-size},{-.95*size,-size},{-.8*size,-size},
            {-.4*size,-size},{0,-size},{size,-size},
            {size,size},{-size,size}};
}
std::vector<FaceId> capIds(const PartitionedMeshImport& imp,
                           CapIndex range,bool left) {
    std::vector<FaceId> out;
    for(std::size_t i=0;i<range.count;++i)
        out.push_back(imp.flatIndex.faceIds[
            (left?range.left:range.right)+i]);
    return out;
}
std::size_t euler(const RegionTopologyIndex& index) {
    std::size_t vertices=0,faces=0;
    std::set<std::pair<VertexId,VertexId>> edges;
    index.revision().forEachPartition([&](PartitionId,const MeshPartition& part) {
        vertices+=part.vertices.size();
        faces+=part.faces.size();
        for(const auto& f:part.faces)
            for(unsigned i=0;i<3;++i) {
                auto a=f.vertices[i],b=f.vertices[(i+1)%3];
                if(a>b)std::swap(a,b);
                edges.emplace(a,b);
            }
    });
    return vertices+faces-edges.size();
}
void testCapsAndSnapshotPublication() {
    Mesh m;
    cube(m,{},1);
    const auto imp=partitionFlatMesh(m,1);
    const auto baseline=RegionTopologyIndex::build(imp.revision);
    const auto firstRoot=baseline->pickingRootIdentity();
    const auto oldFace=imp.flatIndex.faceIds[2];
    const auto secondFace=imp.flatIndex.faceIds[3];
    const auto plan=planRemovalContours(*baseline,{oldFace,secondFace});
    require(plan.ready()&&plan.cappedContours.size()==1,
            "Single square opening chooses an independent cap");
    IndexedMeshRepository repository(baseline);
    const auto repaired=applyRepairSurgery(baseline,plan);
    require(repaired.capFaces==2&&repaired.zipperFaces==0,
            "Four-sided opening is sealed by two oriented triangles");
    require(repaired.index->retiredFace(oldFace)&&
            repaired.index->retiredFace(secondFace),
            "Original removed IDs remain retired, never recycled");
    require(repaired.index->retiredVertexCount()==0,
            "Original rim vertices preserve identity");
    require(euler(*repaired.index)==2,
            "Capped closed cube remains a sphere topology");
    require(repaired.index->triangleIntersections(
        {},repaired.insertedFaces).empty(),
        "Cap does not cross other faces");
    require(baseline->pickingRootIdentity()==firstRoot&&
            !baseline->retiredFace(oldFace),
            "Old immutable revision remains untouched");
    require(repository.snapshot()==baseline,
            "No publication occurs merely because surgery succeeds");
    require(repository.publishIfCurrent(baseline,repaired.index),
            "One atomic root exchange publishes validated revision");
    require(repository.snapshot()==repaired.index,
            "New repository snapshot owns repaired root");
    require(!repository.publishIfCurrent(baseline,repaired.index),
            "Stale publication cannot overwrite a newer root");
}
void testZipperJoinsSeparatedSolids() {
    Mesh m;
    cube(m,{-1.1,0,0},1);
    cube(m,{1.1,0,0},1);
    const auto imp=partitionFlatMesh(m,1);
    const auto baseline=RegionTopologyIndex::build(imp.revision);
    const auto& f=imp.flatIndex.faceIds;
    const std::vector<FaceId> red{f[10],f[11],f[12+8],f[12+9]};
    const auto plan=planRemovalContours(*baseline,red);
    require(plan.ready()&&plan.pairs.size()==1,
            "Two nearby square rings choose zipper");
    const auto repaired=applyRepairSurgery(baseline,plan);
    require(repaired.zipperFaces>=8&&repaired.capFaces==0,
            "Zipper closes two entire oriented rings");
    require(euler(*baseline)==4&&euler(*repaired.index)==2,
            "Joining distinct closed solids changes total Euler characteristic 4 to 2");
    require(repaired.index->triangleIntersections(
        {},repaired.insertedFaces).empty(),
        "Zipper produces no residual crossing");
    require(repaired.index->updateStatistics().fullMeshTraversals==0,
            "Region commit updates only touched persistent index branches");
}
void testUnequalSpacingSharedEdgeSplits() {
    Mesh m;
    const auto left=prism(m,-2,0,square(1));
    const auto right=prism(m,.1,2,unevenSquare(1));
    const auto imp=partitionFlatMesh(m,1);
    const auto baseline=RegionTopologyIndex::build(imp.revision);
    auto removed=capIds(imp,left,false);
    const auto other=capIds(imp,right,true);
    removed.insert(removed.end(),other.begin(),other.end());
    const auto plan=planRemovalContours(*baseline,removed);
    require(plan.ready()&&plan.pairs.size()==1,
            "Uneven boundary densities select zipper");
    const auto repaired=applyRepairSurgery(baseline,plan);
    require(repaired.splitBoundaryEdges>0&&
            !repaired.insertedVertices.empty(),
            "Long boundary edges are subdivided by physical arc length");
    require(repaired.zipperFaces>=plan.contours[0].vertexIds.size()+
                                   plan.contours[1].vertexIds.size(),
            "Adaptive zipper can insert enough triangles for unequal spacings");
    require(euler(*repaired.index)==2,
            "Surviving-face splits introduce no T-junction or invalid topology");
    require(repaired.index->triangleIntersections(
        {},repaired.insertedFaces).empty(),
        "Refined zipper triangles do not cross any current geometry");
    require(baseline->retiredFaceCount()==0,
            "Old snapshot and old identities remain valid after refinement");
}
struct MirroredCubes {
    PartitionedMeshImport import;
    std::vector<std::pair<VertexId,VertexId>> mirrors;
};
MirroredCubes reflectedCubes(double displacement) {
    Mesh m;
    cube(m,{-displacement,0,0},1);
    // Reflection reverses triangle winding but shares no vertices;
    // the X=0 closure surgery must create ONE common seam ring.
    for(unsigned i=0;i<8;++i) {
        const auto p=m.vertices[i];
        m.vertices.push_back({-p.x,p.y,p.z});
    }
    for(unsigned i=0;i<12;++i) {
        const auto f=m.triangles[i];
        m.triangles.push_back({f.a+8,f.c+8,f.b+8});
    }
    MirroredCubes result;
    result.import=partitionFlatMesh(m,1);
    for(unsigned i=0;i<8;++i)
        result.mirrors.push_back({result.import.flatIndex.vertexIds[i],
                                  result.import.flatIndex.vertexIds[i+8]});
    return result;
}
void testMirroredZipperUsesOnePlaneSeam() {
    const auto test=reflectedCubes(1.1);
    const auto original=RegionTopologyIndex::build(test.import.revision);
    const auto& f=test.import.flatIndex.faceIds;
    const std::vector<FaceId> red{f[10],f[11],f[22],f[23]};
    const auto plan=planRemovalContours(*original,red);
    require(plan.ready()&&plan.pairs.size()==1,
            "Mirrored facing-cube caps select a zipper");
    const auto before=original->pickingRootIdentity();
    const auto surgery=applyMirroredRepairSurgery(original,plan,test.mirrors);
    require(surgery.zipperFaces==16&&surgery.capFaces==0,
            "Both halves of symmetrical zipper use paired triangles");
    require(surgery.insertedVertices.size()==4,
            "One four-vertex seam ring, not duplicate left/right seam vertices");
    for(const auto id:surgery.insertedVertices) {
        const auto& v=surgery.index->vertex(id);
        require(v.position.x==0&&v.incidentFaces.size()==6,
                "One shared plane vertex joins both symmetric half-surfaces");
    }
    require(euler(*original)==4&&euler(*surgery.index)==2,
            "Mirrored zipper connects two components into one sphere-like solid");
    require(surgery.index->triangleIntersections(
        {},surgery.insertedFaces).empty(),
        "Mirrored scar contains no false crossing or overlap");
    require(surgery.verifiedLocalFaces>surgery.insertedFaces.size()&&
            surgery.maximumSurfaceDeviationInEdges<
                RepairSurgerySettings{}.maximumSurfaceDeviationInEdges,
            "Mirrored surgery verifies surviving local geometry and reports bounded drift");
    require(original->pickingRootIdentity()==before&&
            original->retiredFaceCount()==0,
            "Mirrored private edit leaves renderer's old root intact");
}
void testMirroredIndependentCaps() {
    const auto test=reflectedCubes(3.0);
    const auto original=RegionTopologyIndex::build(test.import.revision);
    const auto& f=test.import.flatIndex.faceIds;
    const std::vector<FaceId> red{f[10],f[11],f[22],f[23]};
    const auto plan=planRemovalContours(*original,red);
    require(plan.ready()&&plan.cappedContours.size()==2,
            "Widely separated reflected loops prefer caps");
    const auto surgery=applyMirroredRepairSurgery(original,plan,test.mirrors);
    require(surgery.capFaces==4&&surgery.zipperFaces==0&&
            surgery.insertedVertices.empty(),
            "Reflected caps replace both cut faces without a plane seam");
    require(euler(*original)==4&&euler(*surgery.index)==4,
            "Capping keeps two distinct symmetric solid components");
    require(surgery.index->retiredFace(f[10])&&
            surgery.index->retiredFace(f[22]),
            "Both original reflected face IDs are retired atomically");
}
void testMirroredRejectsIncompleteMapping() {
    const auto test=reflectedCubes(1.1);
    const auto original=RegionTopologyIndex::build(test.import.revision);
    const auto& f=test.import.flatIndex.faceIds;
    const auto plan=planRemovalContours(*original,{f[10],f[11],f[22],f[23]});
    auto incomplete=test.mirrors;
    incomplete.erase(incomplete.begin()+1); // Needed by deleted +X cap ring
    bool failed=false;
    try{(void)applyMirroredRepairSurgery(original,plan,incomplete);}
    catch(const std::exception&) {failed=true;}
    require(failed,"Incomplete mirror mapping refuses surgery");
    require(original->retiredFaceCount()==0,
            "Failed symmetric operation has no published topology");
}

void testMirroredUnevenBoundarySubdivision() {
    Mesh m;
    const auto cut=prism(m,-2.1,-.1,unevenSquare(1));
    const auto vertexCount=static_cast<std::uint32_t>(m.vertices.size());
    const auto faceCount=static_cast<std::uint32_t>(m.triangles.size());
    for(std::uint32_t i=0;i<vertexCount;++i) {
        const auto p=m.vertices[i];
        m.vertices.push_back({-p.x,p.y,p.z});
    }
    for(std::uint32_t i=0;i<faceCount;++i) {
        const auto f=m.triangles[i];
        m.triangles.push_back({f.a+vertexCount,
                               f.c+vertexCount,f.b+vertexCount});
    }
    const auto imported=partitionFlatMesh(m,1);
    const auto base=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> mirrors;
    for(std::uint32_t i=0;i<vertexCount;++i)
        mirrors.push_back({imported.flatIndex.vertexIds[i],
                           imported.flatIndex.vertexIds[i+vertexCount]});
    auto red=capIds(imported,cut,false);
    for(std::size_t i=0;i<cut.count;++i)
        red.push_back(imported.flatIndex.faceIds[faceCount+cut.right+i]);
    const auto plan=planRemovalContours(*base,red);
    require(plan.ready()&&plan.pairs.size()==1,
            "Uneven reflected contour still selects central zipper");
    const auto result=applyMirroredRepairSurgery(base,plan,mirrors);
    require(result.splitBoundaryEdges>0&&
            result.insertedVertices.size()>cut.count,
            "Mirrored long-edge split creates paired vertices and shared plane scar");
    require(euler(*base)==4&&euler(*result.index)==2,
            "Symmetric unequal boundary density stays a closed two-manifold");
    require(result.index->triangleIntersections(
        {},result.insertedFaces).empty(),
        "Mirrored boundary subdivision does not create crossing triangles");
}
void testMirroredWholeComponentRetirement() {
    const auto test=reflectedCubes(3.0);
    const auto base=RegionTopologyIndex::build(test.import.revision);
    const auto all=test.import.flatIndex.faceIds;
    const auto plan=planRemovalContours(*base,all);
    require(plan.ready()&&plan.contours.empty(),
            "Two entire reflected closed shells need no repair seam");
    const auto result=applyMirroredRepairSurgery(base,plan,test.mirrors);
    require(result.insertedFaces.empty()&&
            result.retiredFaces.size()==24&&
            result.retiredVertices.size()==16,
            "Both removed reflected components retire their identities");
    require(result.index->retiredVertexCount()==16&&
            result.index->retiredFaceCount()==24,
            "Atomic symmetric tombstones remain in the private index");
    require(base->retiredVertexCount()==0&&base->retiredFaceCount()==0,
            "Old two-component snapshot remains intact");
}
void testRepeatedMirroredSurgeryKeepsHistory() {
    Mesh mesh;
    for(const double y:{-4.0,4.0}) {
        const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
        const auto faceStart=static_cast<std::uint32_t>(mesh.triangles.size());
        cube(mesh,{-1.1,y,0},1);
        for(std::uint32_t i=0;i<8;++i) {
            const auto p=mesh.vertices[first+i];
            mesh.vertices.push_back({-p.x,p.y,p.z});
        }
        for(std::uint32_t i=0;i<12;++i) {
            const auto f=mesh.triangles[faceStart+i];
            mesh.triangles.push_back({f.a+8,f.c+8,f.b+8});
        }
    }
    const auto imported=partitionFlatMesh(mesh,1);
    const auto initial=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> mirror;
    for(const unsigned base:{0u,16u})
        for(unsigned i=0;i<8;++i)
            mirror.emplace_back(imported.flatIndex.vertexIds[base+i],
                                imported.flatIndex.vertexIds[base+i+8]);
    const auto& f=imported.flatIndex.faceIds;
    const auto plan1=planRemovalContours(*initial,
        {f[10],f[11],f[22],f[23]});
    require(plan1.ready()&&plan1.pairs.size()==1,
            "First reflected repair ring is valid");
    const auto first=applyMirroredRepairSurgery(initial,plan1,mirror);
    const auto firstRoot=first.index->pickingRootIdentity();
    const auto plan2=planRemovalContours(*first.index,
        {f[34],f[35],f[46],f[47]});
    require(plan2.ready()&&plan2.pairs.size()==1,
            "Second reflected repair uses stable old face IDs");
    const auto second=applyMirroredRepairSurgery(first.index,plan2,mirror);
    require(initial->retiredFaceCount()==0&&
            first.index->retiredFaceCount()==4&&
            second.index->retiredFaceCount()==8,
            "Three immutable generations hold independent retirement histories");
    require(first.index->pickingRootIdentity()==firstRoot&&
            second.index->pickingRootIdentity()!=firstRoot,
            "Second independent surgery cannot mutate previous BVH root");
    require(euler(*initial)==8&&euler(*first.index)==6&&
            euler(*second.index)==4,
            "Two sequential mirrored mergers each reduce Euler characteristic by two");
}
void testGeometricValidationBudgets() {
    Mesh m;
    cube(m,{-1.1,0,0},1);
    cube(m,{1.1,0,0},1);
    const auto imported=partitionFlatMesh(m,1);
    const auto base=RegionTopologyIndex::build(imported.revision);
    const auto& f=imported.flatIndex.faceIds;
    const auto plan=planRemovalContours(*base,
        {f[10],f[11],f[20],f[21]});
    require(plan.ready()&&plan.pairs.size()==1,
            "Drift budget uses the nearby zipper fixture");
    const auto standard=applyRepairSurgery(base,plan);
    require(standard.verifiedLocalFaces>standard.insertedFaces.size(),
            "Residual validator checks surviving faces beyond inserted scar");
    require(standard.maximumSurfaceDeviationInEdges>0,
            "Cross-gap zipper has measurable deviation from removed geometry");
    auto strict=RepairSurgerySettings{};
    strict.maximumSurfaceDeviationInEdges=.001;
    bool rejected=false;
    try {(void)applyRepairSurgery(base,plan,strict);}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"Too-strict quantitative drift budget refuses surgery");
    strict={};
    strict.maxResidualFaces=1;
    rejected=false;
    try {(void)applyRepairSurgery(base,plan,strict);}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"Bounded local collision verification rejects over-budget neighborhood");
    require(base->retiredFaceCount()==0,
            "Both geometric validation refusals leave original immutable revision intact");
}
void testRejectionIsTransactional() {
    Mesh m;
    const auto left=prism(m,-2,0,square(1));
    const auto right=prism(m,.1,2,unevenSquare(1));
    const auto imp=partitionFlatMesh(m,1);
    const auto baseline=RegionTopologyIndex::build(imp.revision);
    auto removed=capIds(imp,left,false);
    const auto more=capIds(imp,right,true);
    removed.insert(removed.end(),more.begin(),more.end());
    const auto plan=planRemovalContours(*baseline,removed);
    const auto before=baseline->pickingRootIdentity();
    RepairSurgerySettings constrained;
    constrained.maxNewVertices=1;
    bool failed=false;
    try{(void)applyRepairSurgery(baseline,plan,constrained);}
    catch(const std::exception&){failed=true;}
    require(failed,"Insufficient subdivision budget fails transaction");
    require(baseline->retiredFaceCount()==0&&
            baseline->pickingRootIdentity()==before,
            "Failed surgery never mutates base revision");
    auto tampered=plan;
    tampered.contours[0].vertexIds[0]=
        tampered.contours[0].vertexIds[1];
    failed=false;
    try{(void)applyRepairSurgery(baseline,tampered);}
    catch(const std::exception&){failed=true;}
    require(failed,"A stale or forged contour is rejected before mutations");
    require(baseline->retiredFaceCount()==0,
            "Tampered surgery does not retire stable IDs");
}
}
int main() {
    try {
        std::cout<<"Surgery fixture: cap"<<std::endl;
        testCapsAndSnapshotPublication();
        std::cout<<"Surgery fixture: separated zipper"<<std::endl;
        testZipperJoinsSeparatedSolids();
        std::cout<<"Surgery fixture: uneven zipper"<<std::endl;
        testUnequalSpacingSharedEdgeSplits();
        std::cout<<"Surgery fixture: mirrored zipper"<<std::endl;
        testMirroredZipperUsesOnePlaneSeam();
        std::cout<<"Surgery fixture: mirrored caps"<<std::endl;
        testMirroredIndependentCaps();
        std::cout<<"Surgery fixture: mirrored refusal"<<std::endl;
        testMirroredRejectsIncompleteMapping();
        std::cout<<"Surgery fixture: mirrored uneven boundary"<<std::endl;
        testMirroredUnevenBoundarySubdivision();
        std::cout<<"Surgery fixture: reflected shell retirement"<<std::endl;
        testMirroredWholeComponentRetirement();
        std::cout<<"Surgery fixture: local geometric bounds"<<std::endl;
        testGeometricValidationBudgets();
        std::cout<<"Surgery fixture: repeated mirrored edits"<<std::endl;
        testRepeatedMirroredSurgeryKeepsHistory();
        std::cout<<"Surgery fixture: rejection"<<std::endl;
        testRejectionIsTransactional();
        std::cout<<"Private transactional cap/zipper surgery regression passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Repair surgery failure: "<<error.what()<<'\n';
        return 1;
    }
}

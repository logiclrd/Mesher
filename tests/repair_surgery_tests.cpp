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
        testCapsAndSnapshotPublication();
        testZipperJoinsSeparatedSolids();
        testUnequalSpacingSharedEdgeSplits();
        testRejectionIsTransactional();
        std::cout<<"Private transactional cap/zipper surgery regression passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Repair surgery failure: "<<error.what()<<'\n';
        return 1;
    }
}

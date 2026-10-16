#include "mesher/repair_plan.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void check(bool condition,const char* why) {
    if(!condition)throw std::runtime_error(why);
}
void cube(Mesh& mesh,Vec3 center,double h) {
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto v:std::array<Vec3,8>{{
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
    }})mesh.vertices.push_back(center+v*h);
    for(const auto f:std::array<std::array<std::uint32_t,3>,12>{{
        {0,2,1},{0,3,2},{4,5,6},{4,6,7},
        {0,1,5},{0,5,4},{3,7,6},{3,6,2},
        {0,4,7},{0,7,3},{1,2,6},{1,6,5}
    }})mesh.triangles.push_back(
        {first+f[0],first+f[1],first+f[2]});
}
void tetra(Mesh& mesh,Vec3 c,double h,bool inverted=false) {
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto p:std::array<Vec3,4>{{
        {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
    }})mesh.vertices.push_back(c+p*h);
    for(const auto f:std::array<std::array<std::uint32_t,3>,4>{{
        {0,1,2},{0,2,3},{0,3,1},{1,3,2}
    }})mesh.triangles.push_back(inverted?
        Triangle{first+f[0],first+f[2],first+f[1]}:
        Triangle{first+f[0],first+f[1],first+f[2]});
}
void testTwoContourLadderAndOldRoot() {
    Mesh m;
    cube(m,{-0.45,0,0},.5);
    cube(m,{0.45,0,0},.5);
    const auto imported=partitionFlatMesh(m,1);
    const auto index=RegionTopologyIndex::build(imported.revision);
    const auto oldIdentity=index->pickingRootIdentity();
    const auto oldTombstones=index->retiredFaceCount();
    const auto& faces=imported.flatIndex.faceIds;
    const std::vector<FaceId> red{
        faces[10],faces[11],faces[12+8],faces[12+9]
    };
    const auto plan=planRemovalContours(*index,red);
    check(plan.ready(),"Two facing deleted caps yield a zipper-ready plan");
    check(plan.removedFaces.size()==4,"Exactly four original faces selected");
    check(plan.contours.size()==2&&plan.pairs.size()==1,
          "Two seam contours are matched, not bridged across the buried volume");
    check(plan.boundaryEdges==8,"Two square boundary rings have eight edges");
    check(plan.contours[0].vertexIds.size()==4&&
          plan.contours[1].vertexIds.size()==4,
          "Each seam retains its original four vertices");
    check(plan.maximumSeamGapInEdges>0&&plan.maximumSeamGapInEdges<0.5,
          "Zipper gap is measured in local mesh-edge units");
    const auto& match=plan.pairs.front();
    check(match.first==0&&match.second==1,"Pairing is canonical");
    check(match.maxSampleDistance<.15,"Contour correspondence is locally close");
    for(const auto& loop:plan.contours) {
        check(loop.survivingFaces.size()==loop.vertexIds.size(),
              "Each boundary edge records the retained adjacent face");
        std::set<VertexId> unique(loop.vertexIds.begin(),loop.vertexIds.end());
        check(unique.size()==loop.vertexIds.size(),
              "Loop visits vertices exactly once");
        for(unsigned i=0;i<loop.vertexIds.size();++i) {
            const auto a=loop.vertexIds[i];
            const auto b=loop.vertexIds[(i+1)%loop.vertexIds.size()];
            const auto& survivor=index->face(loop.survivingFaces[i]);
            bool directed=false;
            for(unsigned j=0;j<3;++j)
                if(survivor.vertices[j]==a&&
                   survivor.vertices[(j+1)%3]==b)directed=true;
            check(directed,"Boundary follows surviving triangle winding");
        }
    }
    check(index->pickingRootIdentity()==oldIdentity &&
          index->retiredFaceCount()==oldTombstones,
          "Planning cannot alter immutable root or retirement tombstones");
    check(planRemovalContours(*index,{}).status==RepairPlanStatus::NoChanges,
          "Empty removal yields no-op without topology writes");

    auto damaged=red;
    damaged.resize(2); // Only one of the two required seam boundaries remains
    check(planRemovalContours(*index,damaged).status==
          RepairPlanStatus::UnmatchedContours,
          "Unpaired or odd contour count must be rejected");
    RepairPlanSettings tight;
    tight.maximumGapInEdges=.01;
    const auto separated=planRemovalContours(*index,red,tight);
    check(!separated.ready(),"Excessive seam gap cannot be approved");
    tight={};
    tight.maxRemovedFaces=3;
    check(planRemovalContours(*index,red,tight).status==
          RepairPlanStatus::BudgetExceeded,
          "Hard face budget rejects an otherwise valid plan");
}
void testMultiLoopPairing() {
    Mesh m;
    for(const double y:{-4.0,4.0}) {
        cube(m,{-0.45,y,0},.5);
        cube(m,{0.45,y,0},.5);
    }
    const auto imported=partitionFlatMesh(m,1);
    const auto index=RegionTopologyIndex::build(imported.revision);
    std::vector<FaceId> removal;
    for(const auto offset:{0u,24u}) {
        const auto& f=imported.flatIndex.faceIds;
        removal.insert(removal.end(),{
            f[offset+10],f[offset+11],f[offset+20],f[offset+21]
        });
    }
    const auto plan=planRemovalContours(*index,removal);
    check(plan.ready()&&plan.contours.size()==4&&plan.pairs.size()==2,
          "Two independent zipper pairs, no cross-matching distant rings");
    for(const auto& pair:plan.pairs) {
        const auto a=plan.contours[pair.first].vertexIds.front();
        const auto b=plan.contours[pair.second].vertexIds.front();
        const auto ay=index->vertex(a).position.y;
        const auto by=index->vertex(b).position.y;
        check(std::abs(ay-by)<2,
              "Contour partners belong to the same local collision site");
    }
    std::reverse(removal.begin(),removal.end());
    const auto reordered=planRemovalContours(*index,removal);
    check(reordered.ready()&&reordered.pairs.size()==plan.pairs.size(),
          "Plan does not depend on order of proposed deletions");
}
void testBubbleWithoutZipper() {
    Mesh mesh;
    tetra(mesh,{},3);
    tetra(mesh,{},.3);
    const auto imported=partitionFlatMesh(mesh,1);
    const auto index=RegionTopologyIndex::build(imported.revision);
    const auto id=imported.flatIndex.faceIds[4];
    const auto plan=planApproximateExcision(*index,{},{},{id});
    check(plan.ready()&&plan.removedFaces.size()==4,
          "Fully redundant bubble planned for deletion");
    check(plan.contours.empty()&&plan.pairs.empty()&&plan.boundaryEdges==0,
          "Entire internal closed shell needs no seam");
    check(index->retiredFaceCount()==0,"Read-only plan does not retire faces");
    RepairPlanSettings tight;
    tight.maxRemovedFaces=3;
    check(planApproximateExcision(*index,{},{},{id},tight).status==
          RepairPlanStatus::BudgetExceeded,
          "Bubble classification cannot exceed bounded component traversal");

    Mesh cavity;
    tetra(cavity,{},3);
    tetra(cavity,{},.3,true);
    const auto caveImport=partitionFlatMesh(cavity,1);
    const auto caveIndex=RegionTopologyIndex::build(caveImport.revision);
    const auto preserved=planApproximateExcision(*caveIndex,{},{},
        {caveImport.flatIndex.faceIds[4]});
    check(preserved.status==RepairPlanStatus::NoChanges &&
          preserved.removedFaces.empty(),
          "Intentional or ambiguous enclosed cavity remains untouched");
}
void testCollisionAwarePlanner() {
    // Two cubes interpenetrate with noncoplanar contact surfaces.
    Mesh mesh;
    cube(mesh,{-0.55,-0.1,-0.2},.9);
    cube(mesh,{0.55,0.15,0.2},.9);
    const auto imported=partitionFlatMesh(mesh,1);
    const auto index=RegionTopologyIndex::build(imported.revision);
    const auto& ids=imported.flatIndex.vertexIds;
    RepairPlanSettings limits;
    limits.sampleDepth=2;
    const auto plan=planApproximateExcision(*index,ids,{}, {},limits);
    check(plan.intersections>0,"V13 finds crossing pairs for V15");
    check(plan.classifiedFaces>0||
          plan.status==RepairPlanStatus::AmbiguousClassification,
          "Post-revision classification is consulted for crossing faces");
    // Coarse 12-face cubes need not form stitchable edge-following rings.
    // Rejecting the geometry is preferable to inventing a bridge.
    check(plan.status==RepairPlanStatus::Ready||
          plan.status==RepairPlanStatus::AmbiguousClassification||
          plan.status==RepairPlanStatus::UnmatchedContours||
          plan.status==RepairPlanStatus::InvalidBoundary,
          "Difficult coarse intersections yield an explicit safe result");
    check(index->retiredVertexCount()==0&&index->retiredFaceCount()==0,
          "Collision plan does not touch live mesh topology");
    limits.maxClassifiedFaces=1;
    check(planApproximateExcision(*index,ids,{}, {},limits).status==
          RepairPlanStatus::BudgetExceeded,
          "Complex processing is explicitly capped");

    Mesh sphere;
    SphereOptions options;
    options.vertexCount=320;
    options.relaxationIterations=8;
    sphere=generateSphere(options);
    auto importedSphere=partitionFlatMesh(sphere,16);
    const auto clean=RegionTopologyIndex::build(importedSphere.revision);
    const auto noHit=planApproximateExcision(*clean,
        {importedSphere.flatIndex.vertexIds.front()},{});
    check(noHit.status==RepairPlanStatus::NoChanges,
          "Ordinary valid local brush neighborhood needs no repair");
}
} // namespace
int main() {
    try {
        testTwoContourLadderAndOldRoot();
        testMultiLoopPairing();
        testBubbleWithoutZipper();
        testCollisionAwarePlanner();
        std::cout<<"Density-aware repair planning regression passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Density-aware repair planning failure: "<<e.what()<<'\n';
        return 1;
    }
}

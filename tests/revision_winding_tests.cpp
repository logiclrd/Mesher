#include "mesher/mesh_partitioning.hpp"
#include "mesher/region_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void check(bool valid,const char* message) {
    if(!valid)throw std::runtime_error(message);
}
void tetra(Mesh& mesh,Vec3 center,double half,bool reverse=false) {
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto offset:std::array<Vec3,4>{{
        {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
    }})mesh.vertices.push_back(center+offset*half);
    for(const auto face:std::array<std::array<std::uint32_t,3>,4>{{
        {0,1,2},{0,2,3},{0,3,1},{1,3,2}
    }}) {
        if(reverse)
            mesh.triangles.push_back({first+face[0],first+face[2],first+face[1]});
        else mesh.triangles.push_back({first+face[0],first+face[1],first+face[2]});
    }
}
void cube(Mesh& mesh,Vec3 c,double half) {
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto v:std::array<Vec3,8>{{
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
    }})mesh.vertices.push_back(c+v*half);
    for(const auto face:std::array<std::array<std::uint32_t,3>,12>{{
        {0,2,1},{0,3,2},{4,5,6},{4,6,7},
        {0,1,5},{0,5,4},{3,7,6},{3,6,2},
        {0,4,7},{0,7,3},{1,2,6},{1,6,5}
    }})mesh.triangles.push_back(
        {first+face[0],first+face[1],first+face[2]});
}
auto indexed(const Mesh& mesh) {
    const auto imported=partitionFlatMesh(mesh,1);
    return RegionTopologyIndex::build(imported.revision);
}
void checkWinding(const RegionTopologyIndex& index,Vec3 p,int expected) {
    RegionTopologyIndex::WindingStatistics stats;
    const auto actual=index.positiveWindingAt(p,&stats);
    check(actual.reliable,"Winding must be reliable away from surfaces");
    check(actual.winding==expected,"Signed ray winding is incorrect");
    check(std::abs(index.referenceWindingAt(p)-expected)<1e-9L,
          "Independent signed solid-angle reference disagrees");
    check(stats.raysTested>=2,
          "Independent ray directions are used for classification");
}
void testPositiveNegativeAndOverlap() {
    Mesh one;
    tetra(one,{},1);
    const auto baseline=indexed(one);
    checkWinding(*baseline,{0,0,0},1);
    checkWinding(*baseline,{4,0,0},0);
    check(baseline->positiveWindingAt({0,0,0}).winding>0,
          "One solid has positive occupancy");
    const auto vertex=one.vertices[0];
    check(!baseline->positiveWindingAt(vertex).reliable,
          "On-surface sample must not be declared certain");
    Mesh reversed;
    tetra(reversed,{},1,true);
    const auto inverted=indexed(reversed);
    checkWinding(*inverted,{0,0,0},-1);
    check(inverted->positiveWindingAt({0,0,0}).winding<=0,
          "Negative winding does not represent occupied material");
    Mesh overlap;
    tetra(overlap,{-0.2,0,0},1);
    tetra(overlap,{0.2,0,0},1);
    const auto combined=indexed(overlap);
    checkWinding(*combined,{0,0,0},2);
    checkWinding(*combined,{-1,0,0},1);
    checkWinding(*combined,{4,0,0},0);
}
void testMixedSurfaceFragments() {
    Mesh mesh;
    cube(mesh,{-0.4,-0.4,0},1);
    cube(mesh,{0.4,0.4,0.3},1);
    auto imported=partitionFlatMesh(mesh,1);
    const auto index=RegionTopologyIndex::build(imported.revision);
    checkWinding(*index,{0,0,0},2);
    checkWinding(*index,{-1,0,0},1);
    checkWinding(*index,{3,0,0},0);
    bool mixed=false;
    bool sawBuried=false,sawExposed=false;
    for(const auto id:imported.flatIndex.faceIds) {
        const auto fragments=index->classifyFaceFragments(id,3);
        check(fragments.size()==64,
              "Uniform barycentric subdivision creates 4^depth patches");
        bool buried=false,exposed=false;
        for(const auto& patch:fragments) {
            check(patch.face==id,"Fragment retains its source stable FaceId");
            if(patch.kind==RegionTopologyIndex::FragmentKind::Buried)
                buried=sawBuried=true;
            if(patch.kind==RegionTopologyIndex::FragmentKind::Exposed)
                exposed=sawExposed=true;
            if(patch.kind==RegionTopologyIndex::FragmentKind::Buried||
               patch.kind==RegionTopologyIndex::FragmentKind::Exposed)
                check(patch.negativeWinding-patch.positiveWinding==1,
                      "Oriented triangle has unit winding jump");
        }
        if(buried&&exposed)mixed=true;
    }
    check(sawBuried&&sawExposed,"Both buried and exterior surface samples exist");
    check(mixed,"One intersected face contains BOTH exposed and buried portions");
    bool threw=false;
    try{(void)index->classifyFaceFragments(imported.flatIndex.faceIds[0],5);}
    catch(const std::invalid_argument&){threw=true;}
    check(threw,"Fragment depth is bounded");
}
void testEnclosedComponents() {
    using Kind=RegionTopologyIndex::ComponentKind;
    Mesh outer;
    tetra(outer,{},3);
    const auto standalone=indexed(outer);
    const auto id=partitionFlatMesh(outer,1).flatIndex.faceIds[0];
    check(standalone->classifyClosedComponent(id).kind==Kind::Boundary,
          "Standalone outward shell is a real boundary");

    Mesh positive=outer;
    tetra(positive,{},0.3);
    const auto outerFaces=outer.triangles.size();
    auto imported=partitionFlatMesh(positive,1);
    auto index=RegionTopologyIndex::build(imported.revision);
    const auto bubbleId=imported.flatIndex.faceIds[outerFaces];
    const auto redundant=index->classifyClosedComponent(bubbleId);
    check(redundant.complete&&redundant.facesVisited==4,
          "Isolated bubble is enumerated as a closed local component");
    check(redundant.kind==Kind::Redundant,
          "Nested positive shell has occupancy on both sides");
    check(index->classifyClosedComponent(bubbleId,3).kind==Kind::Ambiguous,
          "Component traversal bound cannot authorize partial deletion");

    Mesh cavity=outer;
    tetra(cavity,{},0.3,true);
    auto caveImport=partitionFlatMesh(cavity,1);
    auto caveIndex=RegionTopologyIndex::build(caveImport.revision);
    checkWinding(*caveIndex,{0,0,0},0);
    checkWinding(*caveIndex,{0.7,0,0},1);
    check(caveIndex->classifyClosedComponent(
        caveImport.flatIndex.faceIds[outerFaces]).kind==Kind::PotentialCavity,
        "Enclosed cavity boundary requires explicit intent/policy");
    check(caveIndex->classifyClosedComponent(
        caveImport.flatIndex.faceIds[0]).kind==Kind::Boundary,
        "Exterior shell remains retained with an interior cavity");

    Mesh negative;
    tetra(negative,{},0.3,true);
    auto negImport=partitionFlatMesh(negative,1);
    auto negIndex=RegionTopologyIndex::build(negImport.revision);
    check(negIndex->classifyClosedComponent(
        negImport.flatIndex.faceIds[0]).kind==Kind::Redundant,
        "Negative-winding bubble in otherwise empty space is imaginary");
}
void testSameRevisionSymmetricMovement() {
    Mesh mesh;
    tetra(mesh,{-2,0,0},0.5);
    for(unsigned i=0;i<4;++i) {
        const auto p=mesh.vertices[i];
        mesh.vertices.push_back({-p.x,p.y,p.z});
    }
    for(unsigned i=0;i<4;++i) {
        const auto f=mesh.triangles[i];
        mesh.triangles.push_back({f.a+4,f.c+4,f.b+4});
    }
    const auto imported=partitionFlatMesh(mesh,1);
    const auto previous=RegionTopologyIndex::build(imported.revision);
    std::vector<VertexId> both=imported.flatIndex.vertexIds;
    checkWinding(*previous,{0,0,0},0);
    RegionEdit edit(previous,both,{},1);
    for(unsigned i=0;i<both.size();++i) {
        const auto old=previous->vertex(both[i]).position;
        edit.moveVertex(both[i],old+Vec3{i<4?1.9:-1.9,0,0});
    }
    (void)edit.commit();
    const auto proposed=edit.updatedIndex();
    checkWinding(*proposed,{0,0,0},2);
    checkWinding(*previous,{0,0,0},0);
    check(proposed->updateStatistics().fullMeshTraversals==0,
          "Post-move index advances without a full mesh traversal");
    check(previous->pickingRootIdentity()!=proposed->pickingRootIdentity(),
          "Old and new states own independent BVH roots");
    check(!proposed->triangleIntersections(both,{}).empty(),
          "Classified post-state includes detected simultaneous intersections");
    for(unsigned i=0;i<4;++i) {
        const auto a=proposed->vertex(both[i]).position;
        const auto b=proposed->vertex(both[i+4]).position;
        check(a.x==-b.x&&a.y==b.y&&a.z==b.z,
              "New revision still exactly symmetric");
    }
}
void testNoFullMeshFallbackOnAmbiguousRays() {
    // Each of the three independent signed-winding rays meets a triangle
    // edge, so ordinary ray evidence cannot establish occupancy. Keep the
    // source geometry away from the query point itself.
    Mesh mesh;
    const std::array<Vec3,3> directions{{
        normalized(Vec3{1,0.193117,0.527463}),
        normalized(Vec3{-0.327151,1,0.169281}),
        normalized(Vec3{0.187397,-0.521311,1})
    }};
    for(const auto direction:directions) {
        const Vec3 up=std::abs(direction.z)<.8?Vec3{0,0,1}:Vec3{0,1,0};
        const auto tangent=normalized(cross(up,direction));
        const auto other=cross(direction,tangent);
        const auto middle=direction*2;
        const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(middle-tangent*.2);
        mesh.vertices.push_back(middle+tangent*.2);
        mesh.vertices.push_back(middle+other*.2);
        mesh.triangles.push_back({first,first+1,first+2});
    }
    const auto index=indexed(mesh);
    RegionTopologyIndex::WindingStatistics bounded;
    const auto uncertain=index->positiveWindingAt({},&bounded,false);
    check(!uncertain.reliable&&bounded.raysTested==3&&
          bounded.referenceFallbacks==0&&
          bounded.referenceFallbackRefusals==1,
          "Strict live policy refuses ambiguous rays without scanning all faces");
    RegionTopologyIndex::WindingStatistics legacy;
    (void)index->positiveWindingAt({},&legacy);
    check(legacy.referenceFallbacks==1&&
          legacy.referenceFallbackRefusals==0,
          "Legacy callers retain the exceptional reference-winding fallback");
}
void testAcceleratedQueriesPrune() {
    SphereOptions options;
    options.vertexCount=900;
    options.relaxationIterations=8;
    const auto imported=partitionFlatMesh(generateSphere(options),16);
    const auto index=RegionTopologyIndex::build(imported.revision);
    RegionTopologyIndex::WindingStatistics stats;
    const auto test=index->positiveWindingAt({0,0,0},&stats,false);
    check(test.reliable&&test.winding==1,
          "Large closed generated sphere has positive winding inside");
    check(stats.trianglesTested<imported.flatIndex.faceIds.size()/3,
          "Accelerated winding prunes most triangles");
    check(stats.referenceFallbacks==0&&stats.referenceFallbackRefusals==0,
          "Normal winding queries do not traverse full mesh");
    check(std::abs(index->referenceWindingAt({0,0,0})-1)<1e-9L,
          "Accelerated winding matches complete solid-angle sum");
}
}
int main() {
    try {
        testPositiveNegativeAndOverlap();
        testMixedSurfaceFragments();
        testEnclosedComponents();
        testSameRevisionSymmetricMovement();
        testNoFullMeshFallbackOnAmbiguousRays();
        testAcceleratedQueriesPrune();
        std::cout<<"Positive winding and surface classification regression passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Positive winding classification failure: "<<error.what()<<'\n';
        return 1;
    }
}

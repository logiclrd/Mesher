#include "mesher/repair_mixed_edges.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace mesher;
namespace {
void check(bool good,const char* reason) {
    if(!good)throw std::runtime_error(reason);
}
Mesh tetrahedra(bool mirror) {
    Mesh mesh;
    const std::array<Vec3,4> p{{{-2,0,0},{-1,0,0},{-2,1,0},{-2,0,1}}};
    const std::array<std::array<unsigned,3>,4> t{{
        {0,2,1},{0,1,3},{0,3,2},{1,2,3}}};
    for(const auto v:p)mesh.vertices.push_back(v);
    for(const auto triangle:t)
        mesh.triangles.push_back({triangle[0],triangle[1],triangle[2]});
    if(mirror) {
        for(auto v:p) {
            v.x=-v.x;
            mesh.vertices.push_back(v);
        }
        for(const auto triangle:t)
            mesh.triangles.push_back({triangle[2]+4,triangle[1]+4,
                                      triangle[0]+4});
    }
    return mesh;
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> source;
    FlatMeshIndex ids;
};
Fixture fixture(bool mirrored) {
    auto imp=partitionFlatMesh(tetrahedra(mirrored),64);
    return {RegionTopologyIndex::build(imp.revision),imp.flatIndex};
}
void testRemoteIncidenceAndNoEscapeHatch() {
    const auto data=fixture(false);
    MixedClosureStaging stage(data.source,{});
    const auto id=data.ids.faceIds.front();
    (void)stage.trackSurvivor(id,16);
    const auto original=data.source->face(id).vertices;
    const auto e=canonicalMixedEdge(original[0],original[1]);
    const auto before=stage.inspectEdge(e.first,e.second);
    check(before.conformingPair&&before.localFaceIndexes.size()==1&&
          before.missingSurvivorFaces.size()==1,
          "M15C3 must discover remote source incident face without mutable halo access");
    auto view=data.source->revision().createMutableView();
    MixedClosureSplitSettings limits;
    limits.maxTrackedFaces=1; // cannot import remote survivor and children
    bool refused=false;
    try {(void)stage.splitEdge(view,e.first,e.second,limits);}
    catch(const std::length_error&) {refused=true;}
    check(refused&&stage.faces().size()==1&&
          stage.sourceView()->pendingFaceEdits()==0,
          "M15C3 finite preflight must not partially mutate staged topology");
    limits.maxTrackedFaces=16;
    const auto newIds=stage.splitEdge(view,e.first,e.second,limits);
    check(newIds.size()==1&&!stage.hasEffectiveEdge(e.first,e.second)&&
          stage.faces().size()==4&&
          stage.sourceView()->pendingFaceEdits()==2&&
          data.source->face(id).vertices==original,
          "M15C3 the only split API must update both source survivors atomically");
    const auto midpoint=newIds.front();
    const auto left=stage.inspectEdge(e.first,midpoint);
    const auto right=stage.inspectEdge(midpoint,e.second);
    check(left.conformingPair&&right.conformingPair&&
          left.sides()==2&&right.sides()==2&&
          left.missingSurvivorFaces.empty()&&right.missingSurvivorFaces.empty(),
          "M15C3 two children must share exactly one new indexed midpoint");
    refused=false;
    try {(void)stage.splitEdge(view,e.first,e.second,limits,1,1);}
    catch(const std::logic_error&) {refused=true;}
    check(refused,"M15C3 old AB edge cannot be split a second time");
    std::cout<<"[r06-m14c] source halo and one-ID two-sided conforming split PASS\n";
}
void testMirroredOrbit() {
    const auto data=fixture(true);
    MixedClosureStaging stage(data.source,{});
    (void)stage.trackSurvivor(data.ids.faceIds[0],32);
    (void)stage.trackSurvivor(data.ids.faceIds[4],32);
    const auto first=canonicalMixedEdge(data.ids.vertexIds[0],data.ids.vertexIds[1]);
    const auto second=canonicalMixedEdge(data.ids.vertexIds[4],data.ids.vertexIds[5]);
    const auto preflight=stage.inspectEdge(first.first,first.second);
    check(preflight.sides()==2&&preflight.missingSurvivorFaces.size()==1,
          "M15C3 mirrored source edge must resolve complete remote incidence");
    auto view=data.source->revision().createMutableView();
    MixedClosureSplitSettings limits;
    limits.requireReflectedGeometry=true;
    limits.maxTrackedFaces=64;
    const auto mids=stage.splitEdge(view,first.first,first.second,limits);
    check(mids.size()==2&&!stage.hasEffectiveEdge(first.first,first.second)&&
          !stage.hasEffectiveEdge(second.first,second.second)&&
          stage.inspectEdge(first.first,mids[0]).conformingPair&&
          stage.inspectEdge(second.first,mids[1]).conformingPair,
          "M15C3 one request must split both original reflected edge orbits");
    std::cout<<"[r06-m14c] paired reflection source-edge topology PASS\n";
}
}
int main() {
    try {
        testRemoteIncidenceAndNoEscapeHatch();
        testMirroredOrbit();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M15C3 staged edge API FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

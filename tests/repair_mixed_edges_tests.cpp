#include "mesher/repair_mixed_edges.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
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
        {0,2,1},{0,1,3},{0,3,2},{1,2,3}
    }};
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
void track(MixedClosureStaging& staged,FaceId id) {
    const auto& face=staged.sourceView->face(id);
    staged.faces.push_back({id,face.owner,{},MixedClosureFaceKind::Survivor,
                            0,staged.sourceView});
    staged.survivingFaceIds.push_back(id);
    for(const auto v:face.vertices) {
        staged.positions.emplace(v,staged.sourceView->position(v));
        staged.vertexOwners.emplace(v,staged.sourceView->owner(v));
    }
}
void testOutsideTrackedHalo() {
    const auto fixtureData=fixture(false);
    const auto& base=*fixtureData.source;
    MixedClosureStaging staged;
    staged.sourceView=std::make_shared<MixedClosureSourceView>(fixtureData.source);
    staged.positions.bind(staged.sourceView);
    const auto id=fixtureData.ids.faceIds.front();
    track(staged,id);
    const auto v=base.face(id).vertices;
    const auto edge=canonicalMixedEdge(v[0],v[1]);
    const auto before=inspectMixedClosureEdge(
        staged.faces,*staged.sourceView,edge);
    check(before.conformingPair&&before.sides()==2&&
          before.localFaceIndexes.size()==1&&
          before.missingSurvivorFaces.size()==1,
          "M14C must discover a second source survivor through the index, outside the initial worklist");
    const auto originallyTracked=staged.faces.size();
    bool refused=false;
    try{importMixedEdgeSurvivors(staged,before,originallyTracked);}
    catch(const std::length_error&) {refused=true;}
    check(refused&&staged.faces.size()==originallyTracked&&
          staged.sourceView->pendingFaceEdits()==0,
          "M14C exhausted finite halo budget must refuse without staging a partial edit");
    importMixedEdgeSurvivors(staged,before,16);
    check(staged.faces.size()==2&&staged.positions.originalHandles()==4&&
          staged.sourceView->sourceRoot()==fixtureData.source,
          "M14C import must preserve precise source identity and load the missing face only");
    const auto middle=std::numeric_limits<VertexId>::max()-17;
    const auto pa=staged.positions.at(edge.first);
    const auto pb=staged.positions.at(edge.second);
    staged.positions.emplace(middle,(pa+pb)*.5);
    applyMixedConformingEdgeSplit(staged.faces,edge,middle,16);
    check(staged.faces.size()==4&&
          staged.sourceView->pendingFaceEdits()==2&&
          staged.sourceView->source().face(id).vertices==v&&
          staged.sourceView->edgeSides({edge.first,edge.second}).empty(),
          "M14C old edge should be replaced by two effective FaceId overrides and never change immutable source");
    const auto left=inspectMixedClosureEdge(staged.faces,*staged.sourceView,
        canonicalMixedEdge(edge.first,middle));
    const auto right=inspectMixedClosureEdge(staged.faces,*staged.sourceView,
        canonicalMixedEdge(middle,edge.second));
    check(left.conformingPair&&right.conformingPair&&
          left.sides()==2&&right.sides()==2&&
          left.missingSurvivorFaces.empty()&&right.missingSurvivorFaces.empty(),
          "M14C both surviving sides must share ONE midpoint VertexId and consistent winding");
    std::size_t withMiddle=0;
    for(const auto& f:staged.faces) {
        const auto triangle=f.triangle();
        if(std::find(triangle.begin(),triangle.end(),middle)!=triangle.end())
            ++withMiddle;
    }
    check(withMiddle==4&&fixtureData.source->face(id).vertices==v,
          "M14C every child must use the same stable new midpoint, with no mutation of source");
    std::cout<<"[r06-m14c] source halo discovery and one-ID two-sided conforming split PASS\n";
}
void testReflectedPairAndRefusal() {
    const auto f=fixture(true);
    MixedClosureStaging staged;
    staged.sourceView=std::make_shared<MixedClosureSourceView>(f.source);
    staged.positions.bind(staged.sourceView);
    const auto left=f.ids.faceIds[0];
    const auto right=f.ids.faceIds[4];
    track(staged,left);
    track(staged,right);
    const auto makeEdge=[&](unsigned offset) {
        return canonicalMixedEdge(f.ids.vertexIds[offset],
                                  f.ids.vertexIds[offset+1]);
    };
    const auto first=makeEdge(0),second=makeEdge(4);
    const auto a=inspectMixedClosureEdge(staged.faces,*staged.sourceView,first);
    const auto b=inspectMixedClosureEdge(staged.faces,*staged.sourceView,second);
    check(a.conformingPair&&b.conformingPair&&
          a.missingSurvivorFaces.size()==1&&b.missingSurvivorFaces.size()==1,
          "M14C both reflected neighborhoods must exist before any split is attempted");
    const auto firstMid=(staged.positions.at(first.first)+
                         staged.positions.at(first.second))*.5;
    const auto secondMid=(staged.positions.at(second.first)+
                          staged.positions.at(second.second))*.5;
    check(firstMid.x==-secondMid.x&&firstMid.y==secondMid.y&&
          firstMid.z==secondMid.z,
          "M14C reflected source edge midpoints must form an exact orbit");
    importMixedEdgeSurvivors(staged,a,32);
    importMixedEdgeSurvivors(staged,b,32);
    const auto m0=std::numeric_limits<VertexId>::max()-37;
    const auto m1=std::numeric_limits<VertexId>::max()-38;
    staged.positions.emplace(m0,firstMid);
    staged.positions.emplace(m1,secondMid);
    applyMixedConformingEdgeSplit(staged.faces,first,m0,32);
    applyMixedConformingEdgeSplit(staged.faces,second,m1,32);
    check(inspectMixedClosureEdge(staged.faces,*staged.sourceView,
          canonicalMixedEdge(first.first,m0)).conformingPair&&
          inspectMixedClosureEdge(staged.faces,*staged.sourceView,
          canonicalMixedEdge(second.first,m1)).conformingPair&&
          f.source->face(left).vertices==staged.sourceView->source().face(left).vertices,
          "M14C complete reflected edit must keep both halves oriented and source immutable");
    std::cout<<"[r06-m14c] paired reflection source-edge topology PASS\n";
}
}
int main() {
    try {testOutsideTrackedHalo();testReflectedPairAndRefusal();return 0;}
    catch(const std::exception& e) {
        std::cerr<<"R06-M14C working edge topology FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

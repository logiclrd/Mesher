#include "mesher/repair_mixed_stage.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void check(bool value,const char* msg) {
    if(!value)throw std::runtime_error(msg);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> source;
    std::array<VertexId,4> v{};
    RepairContour rim;
};
Fixture openTetra() {
    Mesh m;
    m.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    // Remove original face (0,2,1); remaining three define rim
    // 0->1->2->0 in the SURVIVOR triangle orientation.
    m.triangles={{0,1,3},{0,3,2},{1,2,3}};
    const auto flat=partitionFlatMesh(m,64);
    Fixture f;
    f.source=RegionTopologyIndex::build(flat.revision);
    for(unsigned k=0;k<4;++k)f.v[k]=flat.flatIndex.vertexIds[k];
    f.rim.vertexIds={f.v[0],f.v[1],f.v[2]};
    return f;
}
void testLiveRimAndIdempotentFace() {
    auto f=openTetra();
    MixedClosureStaging staged(f.source,{f.rim});
    const auto original=staged.currentBoundaryLoops();
    check(original.size()==1&&original.front().size()==3,
          "M15A starts with exactly the existing R03 yellow rim");
    const auto a=f.v[0],b=f.v[1],c=f.v[2];
    check(staged.hasEffectiveEdge(a,b),"M15A initial survivor rim exists");
    // A->C->B is opposite to the existing surviving A->B rim.
    const auto owner=f.source->vertex(a).owner;
    const auto first=staged.ensureFace(
        a,c,b,MixedClosureFaceKind::Cap,owner,1);
    check(staged.currentBoundaryLoops().empty(),
          "M15A cap face must remove the now-paired entire yellow rim");
    const auto twice=staged.ensureFace(
        c,b,a,MixedClosureFaceKind::Cap,owner,1);
    check(first==twice&&staged.faces.size()==1&&staged.capTriangles==1,
          "M15A cyclic repeat of oriented face is a true no-op");
    bool wrong=false;
    try{(void)staged.ensureFace(
        a,b,c,MixedClosureFaceKind::Cap,owner,1);}
    catch(const std::logic_error&) {wrong=true;}
    check(wrong&&staged.faces.size()==1,
          "M15A reversed duplicate face must fail before mutation");
    check(f.source->maybeEdgeSides({std::min(a,b),std::max(a,b)})->size()==1,
          "M15A source open rim remains immutable");
    std::cout<<"[r06-m15a] R03 yellow ring closes, same winding idempotent, reverse rejected PASS\n";
}
void testNewFaceMovesActiveBoundary() {
    auto f=openTetra();
    MixedClosureStaging staged(f.source,{f.rim});
    auto view=f.source->revision().createMutableView();
    const auto a=f.v[0],b=f.v[1];
    const auto owner=f.source->vertex(a).owner;
    const auto n=staged.addVertex(view,{1,-1,0},owner);
    check(n!=a&&n!=b,"M15A addVertex must allocate an independent indexed identity");
    (void)staged.ensureFace(a,n,b,MixedClosureFaceKind::Cap,owner,1);
    const auto current=staged.currentBoundaryLoops();
    check(current.size()==1&&current.front().size()==4,
          "M15A inserting triangle on open rim extends active boundary walk");
    check(!staged.hasEffectiveEdge(a,0),"M15A query rejects zero vertex");
    check(staged.hasEffectiveEdge(a,n)&&staged.hasEffectiveEdge(n,b),
          "M15A new free edges belong to the evolving staged topology");
    bool overfilled=false;
    const auto another=staged.addVertex(view,{1,0,-1},owner);
    try{(void)staged.ensureFace(
        a,b,another,MixedClosureFaceKind::Cap,owner,1);}
    catch(const std::logic_error&) {overfilled=true;}
    check(overfilled&&staged.faces.size()==1,
          "M15A overfull original rim edge rejected without adding triangle");
    std::cout<<"[r06-m15a] new vertex, incremental front and overfilled face refusal PASS\n";
}
}
int main() {
    try {
        testLiveRimAndIdempotentFace();
        testNewFaceMovesActiveBoundary();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M15A staged topology API FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

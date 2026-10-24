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
    check(first==twice&&staged.faces().size()==1&&staged.capTriangles==1,
          "M15A cyclic repeat of oriented face is a true no-op");
    bool wrong=false;
    try{(void)staged.ensureFace(
        a,b,c,MixedClosureFaceKind::Cap,owner,1);}
    catch(const std::logic_error&) {wrong=true;}
    check(wrong&&staged.faces().size()==1,
          "M15A reversed duplicate face must fail before mutation");
    check(f.source->maybeEdgeSides({std::min(a,b),std::max(a,b)})->size()==1,
          "M15A source open rim remains immutable");
    std::cout<<"[r06-m15a] R03 yellow ring closes, same winding idempotent, reverse rejected PASS\n";
}
void testSplitRimBeforeOrAfterCap() {
    for(const bool capFirst:{false,true}) {
        auto f=openTetra();
        MixedClosureStaging staged(f.source,{f.rim});
        auto view=f.source->revision().createMutableView();
        const auto a=f.v[0],b=f.v[1],c=f.v[2];
        const auto owner=f.source->vertex(a).owner;
        if(capFirst)
            (void)staged.ensureFace(
                a,c,b,MixedClosureFaceKind::Cap,owner,1);
        MixedClosureSplitSettings limits;
        limits.maxTrackedFaces=64;
        const auto mids=staged.splitEdge(view,a,b,limits);
        check(staged.splitEvents().size()==1&&
              staged.splitEvents().front().midpoint==mids.front()&&
              staged.splitEvents().front().originalR03Sides==1,
              "M15D recorded one-sided rim ancestry must preserve exact original edge");
        check(mids.size()==1&&mids.front()!=a&&mids.front()!=b,
              "M15B one indexed midpoint must be reserved by the staging editor");
        const auto middle=mids.front();
        check(!staged.hasEffectiveEdge(a,b)&&
              staged.hasEffectiveEdge(a,middle)&&
              staged.hasEffectiveEdge(middle,b),
              "M15B old AB must vanish and two real child edges must exist");
        bool repeated=false;
        try{(void)staged.splitEdge(view,a,b,limits,1,1);}
        catch(const std::logic_error&) {repeated=true;}
        check(repeated,"M15B second split of nonexistent AB must HARD fail");
        if(!capFirst) {
            const auto boundary=staged.currentBoundaryLoops();
            check(boundary.size()==1&&boundary.front().size()==4,
                  "M15B original yellow path must become A-M-B");
            (void)staged.ensureFace(
                a,c,middle,MixedClosureFaceKind::Cap,owner,1);
            (void)staged.ensureFace(
                middle,c,b,MixedClosureFaceKind::Cap,owner,1);
        }
        check(staged.currentBoundaryLoops().empty(),
              "M15B split-before/after-cap must both yield a closed working front");
        check(staged.sourceView()->sourceRoot()==f.source&&
              f.source->maybeEdgeSides({
                  std::min(a,b),std::max(a,b)})->size()==1,
              "M15B no split changes the immutable R03 source");
    }
    std::cout<<"[r06-m15b] split before/after cap, live rim, repeated AB hard refusal PASS\n";
}
void testRemoteInteriorEdgeSplit() {
    auto f=openTetra();
    MixedClosureStaging staged(f.source,{f.rim});
    auto view=f.source->revision().createMutableView();
    // BC is an R03 one-sided rim; 0-3 instead has two survivor faces
    // and neither is initially tracked.
    const auto a=f.v[0],b=f.v[3];
    MixedClosureSplitSettings limits;
    limits.maxTrackedFaces=64;
    const auto mids=staged.splitEdge(view,a,b,limits);
    check(staged.splitEvents().size()==1&&
          staged.splitEvents().front().originalR03Sides==2&&
          staged.splitEvents().front().incidentSurvivorSides==2,
          "M15D remote source split provenance must record both survivor sides");
    check(mids.size()==1&&staged.faces().size()==4&&
          staged.sourceView()->pendingFaceEdits()==2,
          "M15B interior source edge must import/split BOTH remote survivors");
    check(staged.currentBoundaryLoops().size()==1&&
          staged.currentBoundaryLoops().front().size()==3,
          "M15B splitting nonboundary edge must not change yellow R03 rim");
    std::cout<<"[r06-m15b] remote two-sided survivor split is conforming PASS\n";
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
    check(overfilled&&staged.faces().size()==1,
          "M15A overfull original rim edge rejected without adding triangle");
    std::cout<<"[r06-m15a] new vertex, incremental front and overfilled face refusal PASS\n";
}
}
int main() {
    try {
        testLiveRimAndIdempotentFace();
        testNewFaceMovesActiveBoundary();
        testSplitRimBeforeOrAfterCap();
        testRemoteInteriorEdgeSplit();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M15A staged topology API FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

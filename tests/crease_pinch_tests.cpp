#include "mesher/crease_pinch.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace mesher;
void require(bool okay,const char* why) {
    if(!okay)throw std::runtime_error(why);
}

// Closed thick rectangular slab with a densely triangulated, perfectly flat
// top. The bottom is deliberately outside the brush sphere.
Mesh slab() {
    constexpr int n=17;
    constexpr double spacing=.15;
    Mesh mesh;
    for(int level=0;level<2;++level)
        for(int j=0;j<n;++j)
            for(int i=0;i<n;++i)
                mesh.vertices.push_back({
                    (i-(n-1)*.5)*spacing,
                    (j-(n-1)*.5)*spacing,
                    level? -3.0:0.0});
    const auto top=[](int i,int j)->std::uint32_t {
        return static_cast<std::uint32_t>(j*n+i);
    };
    const auto bottom=[&](int i,int j)->std::uint32_t {
        return top(i,j)+n*n;
    };
    for(int j=0;j<n-1;++j)
        for(int i=0;i<n-1;++i) {
            const auto a=top(i,j),b=top(i+1,j),c=top(i,j+1),d=top(i+1,j+1);
            mesh.triangles.push_back({a,b,c});
            mesh.triangles.push_back({b,d,c});
            const auto e=bottom(i,j),f=bottom(i+1,j),
                       g=bottom(i,j+1),h=bottom(i+1,j+1);
            mesh.triangles.push_back({e,g,f});
            mesh.triangles.push_back({f,g,h});
        }
    const auto side=[&](std::uint32_t a,std::uint32_t b) {
        const auto backA=a+n*n,backB=b+n*n;
        mesh.triangles.push_back({a,backA,b});
        mesh.triangles.push_back({b,backA,backB});
    };
    for(int i=0;i<n-1;++i) {
        side(top(i,0),top(i+1,0));
        side(top(i+1,n-1),top(i,n-1));
    }
    for(int j=0;j<n-1;++j) {
        side(top(n-1,j),top(n-1,j+1));
        side(top(0,j+1),top(0,j));
    }
    return mesh;
}
std::shared_ptr<const RegionTopologyIndex> index() {
    auto imported=partitionFlatMesh(slab(),256);
    return RegionTopologyIndex::build(std::move(imported.revision));
}
void testBootstrap() {
    auto before=index();
    const Vec3 center{0,0,0},camera{0,0,1};
    const auto first=proposeCreasePinch(
        *before,center,.60,.18,.18,.035,camera,SculptBrushMode::Crease);
    require(first.diagnostics.supportVertices>10,
            "Flat slab did not provide local support");
    require(first.diagnostics.confidence<1e-12,
            "One virtual point invented a line on flat geometry");
    require(!first.movements.empty(),
            "First Crease stamp did not dimple the surface");
    bool changedCenter=false;
    for(const auto& movement:first.movements) {
        const auto original=before->vertex(movement.vertex).position;
        require(std::abs(original.x-movement.position.x)<1e-11&&
                std::abs(original.y-movement.position.y)<1e-11,
                "First stamp imposed an arbitrary XY crease direction");
        require(movement.position.z<=original.z+1e-12,
                "Crease moved flat top material toward camera");
        if(std::abs(original.x)<1e-8&&std::abs(original.y)<1e-8)
            changedCenter=movement.position.z<-.01;
    }
    require(changedCenter,"First contact did not create central dimple");
    const auto stamp=versionedCreasePinchStamp(
        before,std::vector<BrushArea>{{center,.60,.3}},
        {SculptBrushMode::Crease,.3,.3,camera},.035,{},256);
    require(stamp.index!=before&&stamp.moved>0,
            "Crease failed to commit a private indexed deformation");
    require(stamp.baseline.before==before&&
            stamp.baseline.movedVertices.size()==stamp.moved,
            "Crease did not preserve a source-backed local stamp baseline");
    const auto second=proposeCreasePinch(
        *stamp.index,{.30,0,0},.60,.18,.18,.035,
        camera,SculptBrushMode::Crease);
    require(second.diagnostics.realEvidence>0,
            "Real previous dimple was not recognized");
    require(second.diagnostics.confidence>first.diagnostics.confidence+1e-5,
            "Previous dimple and virtual contact failed to bootstrap a line");
    const auto again=proposeCreasePinch(
        *stamp.index,{.30,0,0},.60,.18,.18,.035,
        camera,SculptBrushMode::Crease);
    require(again.diagnostics.confidence==second.diagnostics.confidence&&
            again.movements.size()==second.movements.size(),
            "Frozen Crease line fitting must be deterministic");
    for(std::size_t i=0;i<again.movements.size();++i)
        require(again.movements[i].vertex==second.movements[i].vertex&&
                length(again.movements[i].position-
                       second.movements[i].position)<1e-12,
                "Crease proposal depended on nondeterministic processing");
}
void testSignsAndBounds() {
    const auto before=index();
    const Vec3 contact{0,0,0},camera{0,0,1};
    const auto raised=proposeCreasePinch(
        *before,contact,.60,.18,.18,.035,camera,SculptBrushMode::Pinch);
    require(raised.diagnostics.confidence<1e-12,
            "Initial Pinch invented a random axis");
    require(!raised.movements.empty(),"Pinch produced no raised bump");
    for(const auto& m:raised.movements)
        require(m.position.z>=before->vertex(m.vertex).position.z-1e-12,
                "Pinch moved flat surface inward");
    const auto capped=proposeCreasePinch(
        *before,contact,.60,.18,.08,1.0,camera,SculptBrushMode::Crease);
    require(!capped.movements.empty(),"Strong Crease was unexpectedly rejected");
    for(const auto& m:capped.movements)
        require(m.position.z>=-.08-1e-10,
                "Strong Crease exceeded the conditional D clamp");
    const auto shallow=proposeCreasePinch(
        *before,contact,.60,.18,.18,0,camera,SculptBrushMode::Crease);
    require(shallow.movements.empty(),"Zero strength moved mesh vertices");
    bool bounded=false;
    try {
        (void)proposeCreasePinch(
            *before,contact,.60,.18,.18,.035,camera,
            SculptBrushMode::Crease,0,32768,3);
    }catch(const std::length_error&) {bounded=true;}
    require(bounded,"Crease ignored the maximum edge-work budget");
}
void testWorkerTransactionAndUndo() {
    // The actual sculpt brush must use the same immutable-root transaction
    // path as Inflate, while bypassing ordinary flip and collapse passes.
    CollapseSettings collapse{};
    collapse.enabled=true;
    RefineSettings refine{};
    refine.enabled=true;
    refine.optimizeFlips=true;
    VersionedSculptBrush brush(slab(),.025,{},collapse,refine,false,256);
    brush.setDeformation({SculptBrushMode::Crease,.30,.30,{0,0,1}});
    Mesh dummy;
    const auto source=brush.snapshot();
    const std::vector<BrushArea> areas{{{0,0,0},.60,.3}};
    brush.stamp(dummy,areas);
    const auto changed=brush.snapshot();
    require(changed&&changed!=source,
            "Crease worker failed to publish first dimple");
    require(brush.lastDetails().moved>0,
            "Crease worker reported no touched geometry");
    require(brush.lastDetails().collapsedPairs==0&&
            brush.lastDetails().flipPairs==0,
            "Crease worker destroyed intentionally concentrated geometry");
    require(brush.undo(),"Crease published geometry cannot be undone");
    require(brush.snapshot()==source,
            "Undo did not restore original immutable Crease root");
    require(brush.redo(),"Crease published geometry cannot be redone");
    require(brush.snapshot()==changed,
            "Redo did not restore exact accepted Crease revision");
}
}

int main() {
    try {
        testBootstrap();
        testSignsAndBounds();
        testWorkerTransactionAndUndo();
        std::cout<<"Crease/Pinch virtual-dimple, confidence, signs and bounds passed\n";
        return 0;
    }catch(const std::exception& ex) {
        std::cerr<<"Crease/Pinch test failure: "<<ex.what()<<"\n";
        return 1;
    }
}

#include "mesher/repair_step_scene.hpp"
#include "mesher/mesh_partitioning.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void check(bool ok,const char* s) {if(!ok)throw std::runtime_error(s);}
}
int main() {
    try {
        Mesh mesh;
        mesh.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
        mesh.triangles={{0,1,3},{0,3,2},{1,2,3}};
        const auto flat=partitionFlatMesh(mesh,64);
        const auto source=RegionTopologyIndex::build(flat.revision);
        RepairContour rim;
        const auto a=flat.flatIndex.vertexIds[0],
                   b=flat.flatIndex.vertexIds[1],
                   c=flat.flatIndex.vertexIds[2];
        rim.vertexIds={a,b,c};
        MixedClosureStaging stage(source,{rim});
        const auto before=captureRepairStepGeometry(stage,"initial","R03");
        const auto original=buildRepairStepScene(before);
        check(original.survivorFaces==3&&original.generatedFaces==0&&
              original.triangles.size()==3*3+3*4*6&&
              original.boundaryEdges==3,
              "initial scene must display exact three R03 faces and 3 open rims");
        const auto owner=source->vertex(a).owner;
        (void)stage.ensureFace(a,c,b,MixedClosureFaceKind::Cap,owner,1);
        const auto capped=captureRepairStepGeometry(stage,"ensureFace","a,b,c");
        const auto faceScene=buildRepairStepScene(capped,&before,&capped);
        check(faceScene.survivorFaces==3&&faceScene.generatedFaces==1&&
              faceScene.highlightedFaces==1&&faceScene.boundaryEdges==0&&
              faceScene.triangles.size()==12,
              "closed tetra stage must display one new triangle, no ghost/open rim");
        const auto sceneBefore=buildRepairStepScene(before,&before,&capped,true);
        check(sceneBefore.showingPrevious&&sceneBefore.boundaryEdges==3,
              "Previous displays original gold open front without an invented cap");
        auto view=source->revision().createMutableView();
        MixedClosureSplitSettings bounds;
        bounds.maxTrackedFaces=64;
        const auto mids=stage.splitEdge(view,a,b,bounds);
        check(mids.size()==1,"one ordinary non-mirror shared split required");
        const auto split=captureRepairStepGeometry(stage,"splitEdge","a,b");
        const auto result=buildRepairStepScene(split,&capped,&split);
        check(result.survivorFaces==3&&result.generatedFaces==3&&
              result.highlightedFaces>=3&&result.boundaryEdges==0,
              "split must draw edited source triangle AND three new children, not stale source face");
        const auto old=buildRepairStepScene(capped,&capped,&split,true);
        check(old.highlightedFaces>=1&&old.generatedFaces==1,
              "prior frame highlights removed actual faces without undo");
        bool budget=false;
        try {(void)buildRepairStepScene(split,nullptr,nullptr,false,1);}
        catch(const std::length_error&){budget=true;}
        check(budget,"bounded scene must refuse an oversized source");
        std::cout<<"[r06-m16d1] exact source override, cap, split, previous/current, yellow rims and bounds PASS\n";
    }catch(const std::exception& e){
        std::cerr<<"[r06-m16d1] FAIL "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}

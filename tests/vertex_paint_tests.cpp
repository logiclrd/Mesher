#include "mesher/vertex_paint.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/paint_controls.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/durable_checkpoint.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;
namespace {
void check(bool yes,const std::string& message) {
    if(!yes)throw std::runtime_error(message);
}
bool close(double a,double b,double tolerance=1e-5) {
    return std::abs(a-b)<=tolerance;
}
VertexRGBA colorOf(const RegionTopologyIndex& index,VertexId id) {
    auto owner=index.revision().partition(index.vertex(id).owner);
    for(const auto& v:owner->vertices)
        if(v.id==id)return v.color;
    throw std::logic_error("Test lost a vertex ID");
}
void testColorControls() {
    PaintControls picker;
    picker.rgba={0.3f,0.65f,0.92f,0.7f};
    const auto start=picker.rgba;
    picker.channels=PaintChannels::HSV;
    const auto hsv=picker.values();
    const auto converted=hueToRgb(hsv,PaintChannels::HSV,start.a);
    check(close(start.r,converted.r)&&close(start.g,converted.g)&&
          close(start.b,converted.b),"HSV roundtrip must preserve selection");
    picker.channels=PaintChannels::HSL;
    const auto hsl=picker.values();
    const auto convertedHsl=hueToRgb(hsl,PaintChannels::HSL,start.a);
    check(close(start.r,convertedHsl.r)&&close(start.g,convertedHsl.g)&&
          close(start.b,convertedHsl.b),"HSL roundtrip must preserve selection");
    picker.channels=PaintChannels::RGB;
    check(picker.rgba==start,"Changing models must not alter stored RGBA");
    picker.setChannel(0,0.8);
    check(close(picker.rgba.r,0.8)&&close(picker.rgba.a,0.7),
          "Linked RGB slider must preserve unaffected alpha");
    picker.channels=PaintChannels::HSV;
    picker.setWheel(1.0/3.0,1);
    check(picker.rgba.g>=picker.rgba.r&&picker.rgba.g>=picker.rgba.b,
          "Wheel must control selected hue and saturation");
    picker.setAlpha(0.2);
    picker.setHardness(0.75);
    check(close(picker.rgba.a,0.2)&&close(picker.hardness,0.75),
          "Alpha and hardness must be independently adjustable");
}
std::shared_ptr<const RegionTopologyIndex> makeIndex(const Mesh& mesh) {
    return RegionTopologyIndex::build(partitionFlatMesh(mesh,4).revision);
}
void testShadowedSurface() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{.2,0,0},{0,.2,0},
                   {.04,.04,-.035},{.065,.04,-.035},{.04,.065,-.035}};
    mesh.triangles={{0,1,2},{3,4,5}}; // both wound toward +Z
    const auto partitioned=partitionFlatMesh(mesh,4);
    auto initial=RegionTopologyIndex::build(partitioned.revision);
    const PaintDab dab{{.05,.05,0},{0,0,1},.25,.2,
                       {0,0,1,1},1};
    const auto result=applyVertexPaintDabs(initial,{dab});
    check(result.colored!=initial,"Front sheet should receive paint");
    check(result.colored->pickingRootIdentity()==initial->pickingRootIdentity(),
          "Color-only publication must preserve the exact picking BVH");
    for(int i=0;i<3;++i)
        check(colorOf(*result.colored,partitioned.flatIndex.vertexIds[i]).b>0.99f &&
              colorOf(*result.colored,partitioned.flatIndex.vertexIds[i]).r<0.01f,
              "Visible front sheet should be painted blue");
    for(int i=3;i<6;++i)
        check(colorOf(*result.colored,partitioned.flatIndex.vertexIds[i])==VertexRGBA{},
              "Hidden close sheet must remain unpainted");
    for(const auto& id:result.changedVertices)
        check(id==partitioned.flatIndex.vertexIds[0]||
              id==partitioned.flatIndex.vertexIds[1]||
              id==partitioned.flatIndex.vertexIds[2],
              "Paint must only edit visible front stable IDs");
    const auto original=initial->revision().partition(
        initial->vertex(partitioned.flatIndex.vertexIds[0]).owner);
    check(original->vertices[0].color==VertexRGBA{},
          "Published COW colors must never mutate previous revision");
}
void testUndoRedoAndPaintPublication() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{.2,0,0},{0,.2,0}};
    mesh.triangles={{0,1,2}};
    VersionedSculptBrush brush(mesh,.01,{}, {},{},false,4);
    const auto original=brush.snapshot();
    check(original!=nullptr,"Initial immutable publication must exist");
    const PaintDab dab{{.05,.05,0},{0,0,1},.3,.2,
                       {0.1f,0.5f,0.9f,0.6f},1};
    check(brush.paint({dab}),"Visible brush dab should publish RGBA revision");
    const auto painted=brush.snapshot();
    check(painted!=nullptr && painted!=original,
          "Paint must atomically publish a different indexed root");
    check(painted->pickingRootIdentity()==original->pickingRootIdentity(),
          "Paint must share the exact immutable geometric picking index");
    check(!brush.lastDirtyPartitions().empty(),
          "Paint must identify dirty rendered face partitions");
    check(brush.undo() && brush.snapshot()==original,
          "Undo must restore the exact unpainted immutable root");
    check(brush.redo() && brush.snapshot()==painted,
          "Redo must restore the exact painted immutable root");
    const auto checkpoint=brush.checkpoint();
    const auto first=original->revision().partition(
        original->vertex(brush.originalOrder()->vertexIds[0]).owner);
    check(first->vertices[0].color==VertexRGBA{},
          "An undo history root must never be modified in place");
    const auto saved=std::filesystem::temp_directory_path()/
        "mesher-ux04-brush-history.mshr";
    try {
        writeCheckpoint(saved,checkpoint);
        const auto recovered=readCheckpoint(saved);
        const auto id=brush.originalOrder()->vertexIds[0];
        check(colorOf(*recovered.index,id)==colorOf(*painted,id),
              "Painted undo/redo state must persist in durable checkpoint");
        std::filesystem::remove(saved);
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(saved,ec);
        throw;
    }
}
void testCheckpointColorsAndLegacy() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{1,0,0},{0,1,0}};
    mesh.triangles={{0,1,2}};
    mesh.colors={{0.6f,0.1f,0.5f,0.25f},{},{0.25f,0.8f,0.4f,1.f}};
    const auto parts=partitionFlatMesh(mesh,4);
    const auto output=reconstructFlatMesh(*parts.revision,parts.flatIndex);
    check(output.colors==mesh.colors,"Flat import/roundtrip must preserve per-vertex RGBA");
    DurableCheckpoint record;
    record.index=RegionTopologyIndex::build(parts.revision);
    record.initialVertexCount=3;
    auto file=std::filesystem::temp_directory_path()/
              "mesher-ux04-vertex-color-checkpoint.mshr";
    try {
        writeCheckpoint(file,record);
        const auto loaded=readCheckpoint(file);
        for(int i=0;i<3;++i)
            check(colorOf(*loaded.index,parts.flatIndex.vertexIds[i])==
                  mesh.colors[i],"V14 checkpoint must preserve exact RGBA bits");
        std::filesystem::remove(file);
        mesh.colors.clear();
        const auto legacy=partitionFlatMesh(mesh,4);
        record.index=RegionTopologyIndex::build(legacy.revision);
        writeCheckpoint(file,record);
        const auto old=readCheckpoint(file);
        for(const auto id:legacy.flatIndex.vertexIds)
            check(colorOf(*old.index,id)==VertexRGBA{},
                  "Legacy V12 checkpoint must read as unpainted");
        std::filesystem::remove(file);
    }catch(...) {
        std::error_code error;
        std::filesystem::remove(file,error);
        throw;
    }
}
}
int main() {
    try {
        testColorControls();
        testShadowedSurface();
        testUndoRedoAndPaintPublication();
        testCheckpointColorsAndLegacy();
        std::cout<<"RGBA colour models, immutable shadow paint and checkpoint tests passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Vertex paint regression: "<<e.what()<<'\n';
        return 1;
    }
}

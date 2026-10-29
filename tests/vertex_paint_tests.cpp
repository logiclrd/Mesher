#include "mesher/vertex_paint.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/paint_controls.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/durable_checkpoint.hpp"
#include "mesher/repair_intersection.hpp"

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
void testDistinctBatchColors() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{.2,0,0},{0,.2,0},
                   {1,0,0},{1.2,0,0},{1,.2,0}};
    mesh.triangles={{0,1,2},{3,4,5}};
    const auto parts=partitionFlatMesh(mesh,4);
    const auto source=RegionTopologyIndex::build(parts.revision);
    const PaintDab left{{.05,.05,0},{0,0,1},.3,.2,{1,0,0,1},1};
    const PaintDab right{{1.05,.05,0},{0,0,1},.3,.2,{0,0,1,1},1};
    const auto result=applyVertexPaintDabs(source,{left,right});
    check(result.colored!=source &&
          result.colored->pickingRootIdentity()==source->pickingRootIdentity(),
          "Different-color dabs must create one geometry-sharing paint root");
    for(int i=0;i<3;++i) {
        const auto id=parts.flatIndex.vertexIds[i];
        check(colorOf(*result.colored,id)==left.rgba,
              "First visible dab must use its own RGBA");
        check(colorOf(*source,id)==VertexRGBA{},
              "Multi-dab paint must not mutate the source root");
    }
    for(int i=3;i<6;++i) {
        const auto id=parts.flatIndex.vertexIds[i];
        check(colorOf(*result.colored,id)==right.rgba,
              "Later visible dabs must retain their own RGBA");
    }
}
void testMirroredDabsDoNotDoublePaintSeam() {
    Mesh mesh;
    mesh.vertices={{-.1,0,0},{.1,0,0},{0,.1,0}};
    mesh.triangles={{0,1,2}};
    const auto parts=partitionFlatMesh(mesh,4);
    const auto source=RegionTopologyIndex::build(parts.revision);
    const PaintDab left{{-.02,.02,0},{0,0,1},.3,.2,
                        {0.2f,0.4f,0.8f,0.25f},0};
    auto right=left;
    right.point.x=-right.point.x;
    const auto once=applyVertexPaintDabs(source,{left});
    const auto mirrored=applyVertexPaintDabs(source,{left,right});
    const auto seam=parts.flatIndex.vertexIds[2];
    check(colorOf(*mirrored.colored,seam)==colorOf(*once.colored,seam),
          "A vertex visible to both reflected dabs must be blended only once");
    check(colorOf(*mirrored.colored,seam)!=VertexRGBA{},
          "Seam overlap test must exercise a real visible paint influence");
}
void testConformingCutColorProvenance() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{1,0,0},{0,1,0}};
    mesh.triangles={{0,1,2}};
    mesh.colors={{1,0,0,0.2f},{0,1,0,0.6f},{0,0,1,1.f}};
    const auto parts=partitionFlatMesh(mesh,4);
    const auto source=RegionTopologyIndex::build(parts.revision);
    ConformingCutPoint edge;
    edge.position={0.25,0,0};
    edge.boundaryEdge=RegionTopologyIndex::Edge{
        parts.flatIndex.vertexIds[0],parts.flatIndex.vertexIds[1]};
    const auto onEdge=plannedCutPointColor(*source,edge);
    check(close(onEdge.r,0.75)&&close(onEdge.g,0.25)&&
          close(onEdge.b,0)&&close(onEdge.a,0.3),
          "Boundary cut RGBA must be interpolated on its original source edge");
    ConformingCutPoint interior;
    interior.position={0.25,0.25,0};
    interior.owningFace=parts.flatIndex.faceIds[0];
    const auto inside=plannedCutPointColor(*source,interior);
    check(close(inside.r,0.5)&&close(inside.g,0.25)&&
          close(inside.b,0.25)&&close(inside.a,0.5),
          "Interior cut RGBA must follow its original face barycentrics");
    interior.existingVertex=parts.flatIndex.vertexIds[2];
    check(plannedCutPointColor(*source,interior)==mesh.colors[2],
          "Unsplit original cut vertex must retain its exact source RGBA");
}
void testLinearNormalizedFootprint() {
    Mesh mesh;
    for(unsigned y=0;y<3;++y)
        for(unsigned x=0;x<3;++x)
            mesh.vertices.push_back({double(x)*.1,double(y)*.1,0});
    for(std::uint32_t y=0;y<2;++y)
        for(std::uint32_t x=0;x<2;++x) {
            const auto a=y*3+x,b=a+1,c=a+3,d=c+1;
            mesh.triangles.push_back({a,b,d});
            mesh.triangles.push_back({a,d,c});
        }
    const auto parts=partitionFlatMesh(mesh,4);
    const auto source=RegionTopologyIndex::build(parts.revision);
    const PaintDab dab{{.06,.025,0},{0,0,1},.18,.2,
                       {1,0,0,1},0,parts.flatIndex.faceIds[0]};
    const auto output=applyVertexPaintDabs(source,{dab});
    const auto ids=parts.flatIndex.vertexIds;
    const double center=std::hypot(.04,.075); // farthest picked face corner
    const double farther=std::hypot(.14,.025); // vertex beyond centre plateau
    const double expected=1-(farther-center)/(.18-center);
    for(const auto index:{0,1,4}) {
        const auto color=colorOf(*output.colored,ids[std::size_t(index)]);
        check(color.g==0.f&&color.b==0.f,
              "All picked face corners must have full colour for a saturated centre pixel");
    }
    const auto edge=colorOf(*output.colored,ids[2]);
    check(close(edge.g,1.0-expected)&&close(edge.b,1.0-expected),
          "Soft brush must fall off linearly from the picked triangle to its radius");
    check(colorOf(*output.colored,ids[8])==VertexRGBA{},
          "Vertices outside the full surface-radius must remain unpainted");
    auto higherRay=dab;
    higherRay.nominalEdgeLength=.8; // visibility origin is four times higher
    const auto elevated=applyVertexPaintDabs(source,{higherRay});
    for(const auto id:ids)
        check(colorOf(*elevated.colored,id)==colorOf(*output.colored,id),
              "Visibility ray height must never shrink the surface brush radius");
}
void testOneUndoPerPaintStroke() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{.2,0,0},{0,.2,0}};
    mesh.triangles={{0,1,2}};
    VersionedSculptBrush brush(mesh,.01,{}, {},{},false,4);
    const auto original=brush.snapshot();
    PaintDab dab{{.05,.05,0},{0,0,1},.3,.2,{1,0,0,1},1};
    brush.beginPaintStroke();
    check(brush.paint({dab}),"First visible dab must publish");
    const auto intermediate=brush.snapshot();
    check(brush.paint({PaintDab{dab.point,dab.outwardNormal,dab.radius,
                               dab.nominalEdgeLength,{0,0,1,1},dab.hardness}}),
          "Second dab can repaint the same surface without new geometry");
    const auto finished=brush.snapshot();
    brush.endPaintStroke();
    check(brush.historyStatistics().undoDepth==1,
          "Two separately published dabs in one drag must share one history step");
    check(brush.undo()&&brush.snapshot()==original,
          "One Undo must restore the entire prior stroke");
    check(brush.redo()&&brush.snapshot()==finished,
          "One Redo must restore the complete final stroke");
    check(intermediate!=finished&&
          intermediate->pickingRootIdentity()==finished->pickingRootIdentity(),
          "Intermediate paint remains immutable and shares picking geometry");
    brush.beginPaintStroke();
    check(brush.paint({dab}),"A later drag must publish independently");
    brush.endPaintStroke();
    check(brush.historyStatistics().undoDepth==2,
          "A new drag creates a distinct undo step");
    check(brush.undo()&&brush.snapshot()==finished,
          "Undo of the later drag must retain the preceding stroke");
    brush.beginPaintStroke();
    brush.endPaintStroke();
    check(brush.historyStatistics().undoDepth==1,
          "An empty mouse drag must not create a history entry");
}
void testLongPaintDragKeepsOriginalUndoRoot() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{.2,0,0},{0,.2,0}};
    mesh.triangles={{0,1,2}};
    VersionedSculptBrush brush(mesh,.01,{}, {},{},false,4);
    const auto original=brush.snapshot();
    PaintDab dab{{.05,.05,0},{0,0,1},.3,.2,{0,0,1,1},1};
    brush.beginPaintStroke();
    for(unsigned i=0;i<128;++i) {
        dab.rgba={float(i%5)/4.f,float(i%7)/6.f,
                  float(i%11)/10.f,1.f};
        check(brush.paint({dab}),
              "Repeated distinct drag dabs should publish without refusal");
    }
    brush.endPaintStroke();
    const auto painted=brush.snapshot();
    const auto history=brush.historyStatistics();
    check(history.undoDepth==1&&history.retainedRevisions==2,
          "Long paint drag must not evict its original root through temporary COW clones");
    check(brush.undo()&&brush.snapshot()==original,
          "Long paint drag must undo in one immutable history transition");
    check(brush.redo()&&brush.snapshot()==painted,
          "Long paint drag must redo its exact final colour root");
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
        testDistinctBatchColors();
        testMirroredDabsDoNotDoublePaintSeam();
        testConformingCutColorProvenance();
        testLinearNormalizedFootprint();
        testOneUndoPerPaintStroke();
        testLongPaintDragKeepsOriginalUndoRoot();
        testUndoRedoAndPaintPublication();
        testCheckpointColorsAndLegacy();
        std::cout<<"RGBA colour models, immutable shadow paint and checkpoint tests passed\n";
    }catch(const std::exception& e) {
        std::cerr<<"Vertex paint regression: "<<e.what()<<'\n';
        return 1;
    }
}

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
void testConvexSurfaceBrushRadiusGrowth() {
    // Three concentric spherical surface rings. A fixed 1/4-edge ray
    // origin sees only the nearest ring (self-occlusion at the horizon)
    // even when querySphere admits the far ring.
    Mesh mesh;
    mesh.vertices.push_back({0,0,1});
    const double nearAngle=.25,farAngle=.48;
    for(const double angle:{nearAngle,farAngle}) {
        const double r=std::sin(angle),z=std::cos(angle);
        mesh.vertices.push_back({r,0,z});
        mesh.vertices.push_back({0,r,z});
        mesh.vertices.push_back({-r,0,z});
        mesh.vertices.push_back({0,-r,z});
    }
    for(std::uint32_t j=0;j<4;++j) {
        const auto next=(j+1)%4;
        mesh.triangles.push_back({0,1+j,1+next});
        mesh.triangles.push_back({1+j,5+j,5+next});
        mesh.triangles.push_back({1+j,5+next,1+next});
    }
    const auto imported=partitionFlatMesh(mesh,4);
    const auto source=RegionTopologyIndex::build(imported.revision);
    auto dab=PaintDab{{0,0,1},{0,0,1},.12,.06,
                      {1,0,0,1},1,imported.flatIndex.faceIds[0]};
    const auto small=applyVertexPaintDabs(source,{dab});
    dab.radius=.30;
    const auto medium=applyVertexPaintDabs(source,{dab});
    dab.radius=.50;
    const auto large=applyVertexPaintDabs(source,{dab});
    const auto& ids=imported.flatIndex.vertexIds;
    check(colorOf(*small.colored,ids[0])==dab.rgba,
          "Small dab must paint its centre");
    check(colorOf(*small.colored,ids[1])==VertexRGBA{},
          "Small dab must not reach the middle ring");
    check(colorOf(*medium.colored,ids[1])==dab.rgba,
          "Middle brush radius must paint the first curved ring");
    check(colorOf(*medium.colored,ids[5])==VertexRGBA{},
          "Middle radius must not reach the far curved ring");
    for(std::size_t i=5;i<9;++i)
        check(colorOf(*large.colored,ids[i])==dab.rgba,
              "Largest radius must reach far convex vertices despite horizon");
}
void testPaintAlphaCompositesOverExistingColor() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{.1,0,0},{0,.1,0}};
    mesh.triangles={{0,1,2}};
    const auto imported=partitionFlatMesh(mesh,4);
    const auto source=RegionTopologyIndex::build(imported.revision);
    const auto id=imported.flatIndex.vertexIds[0];
    PaintDab dab{{.03,.03,0},{0,0,1},.30,.05,
                 {0,0,1,.25f},1,imported.flatIndex.faceIds[0]};
    const auto first=applyVertexPaintDabs(source,{dab});
    const auto coat=colorOf(*first.colored,id);
    check(close(coat.r,.75)&&close(coat.g,.75)&&
          close(coat.b,1)&&coat.a==1.f,
          "25% blue must mix with opaque white and retain opaque destination");
    const auto second=applyVertexPaintDabs(first.colored,{dab});
    const auto layered=colorOf(*second.colored,id);
    check(close(layered.r,.5625)&&close(layered.g,.5625)&&
          layered.a==1.f,
          "A second translucent dab must accumulate against the existing colour");
    dab.rgba={1,0,0,.5f};
    const auto red=applyVertexPaintDabs(second.colored,{dab});
    const auto mixed=colorOf(*red.colored,id);
    check(close(mixed.r,.78125)&&close(mixed.g,.28125)&&
          close(mixed.b,.5)&&mixed.a==1.f,
          "Brush alpha must mix new hue over the existing painted hue");
    dab.rgba={0,1,0,0};
    const auto transparent=applyVertexPaintDabs(red.colored,{dab});
    check(transparent.colored==red.colored&&
          transparent.changedVertices.empty(),
          "Zero opacity is a complete no-op without a history revision");
    // A pre-existing partial coat is also composited source-over rather
    // than overwriting its alpha with the brush's alpha.
    mesh.colors.assign(3,{1,0,0,.5f});
    const auto partialImport=partitionFlatMesh(mesh,4);
    const auto partial=RegionTopologyIndex::build(partialImport.revision);
    dab.rgba={0,0,1,.5f};
    dab.sourceFace=partialImport.flatIndex.faceIds[0];
    const auto mixedPartial=applyVertexPaintDabs(partial,{dab});
    const auto result=colorOf(*mixedPartial.colored,
                               partialImport.flatIndex.vertexIds[0]);
    check(close(result.a,.75)&&close(result.r,1./3)&&
          close(result.b,2./3),
          "Source-over paint must preserve and combine partial coat coverage");
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
        testConvexSurfaceBrushRadiusGrowth();
        testPaintAlphaCompositesOverExistingColor();
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

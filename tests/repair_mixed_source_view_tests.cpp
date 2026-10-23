#include "mesher/repair_mixed_source_view.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace mesher;

namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}

void sourceBackedOverrides() {
    // Closed tetrahedron: all four source faces are accessible, and every
    // edge has two real incident faces before proposing local overrides.
    Mesh mesh;
    mesh.vertices={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};
    mesh.triangles.push_back({0,2,1});
    mesh.triangles.push_back({0,1,3});
    mesh.triangles.push_back({0,3,2});
    mesh.triangles.push_back({1,2,3});
    auto imported=partitionFlatMesh(std::move(mesh),64);
    auto source=RegionTopologyIndex::build(imported.revision);
    MixedClosureSourceView working(source,2);
    require(working.sourceRoot()==source&&
            &working.source()==source.get()&&
            working.pendingFaceEdits()==0&&
            working.pendingVertexEdits()==0,
            "M14A must retain precise original immutable mesh identity");

    const auto faceId=imported.flatIndex.faceIds.front();
    const auto original=source->face(faceId);
    const auto verts=original.vertices;
    const auto orphan=std::find_if(imported.flatIndex.vertexIds.begin(),
            imported.flatIndex.vertexIds.end(),[&](VertexId v) {
                return v!=verts[0]&&v!=verts[1]&&v!=verts[2];
            });
    require(orphan!=imported.flatIndex.vertexIds.end(),
            "M14A fixture needs one additional source vertex");
    const VertexId other=*orphan;
    const auto edge=[](VertexId a,VertexId b){
        if(b<a)std::swap(a,b);
        return RegionTopologyIndex::Edge{a,b};
    };
    const auto hasFace=[&](const auto& sides){
        return std::any_of(sides.begin(),sides.end(),
            [faceId](const auto& side){return side.face==faceId;});
    };
    const auto oldEdge=edge(verts[0],verts[1]);
    const auto addedEdge=edge(verts[0],other);
    require(hasFace(working.edgeSides(oldEdge))&&
            !hasFace(working.edgeSides(addedEdge))&&
            working.position(other).x==source->vertex(other).position.x,
            "M14A whole-index original face, edge and vertex reads failed");

    working.overrideFace(faceId,{verts[0],verts[2],other});
    const auto initialPosition=source->vertex(verts[0]).position;
    working.overridePosition(verts[0],initialPosition+Vec3{.25,0,0});
    require(working.face(faceId).vertices[2]==other&&
            working.face(faceId).owner==original.owner&&
            !hasFace(working.edgeSides(oldEdge))&&
            hasFace(working.edgeSides(addedEdge))&&
            working.position(verts[0]).x==initialPosition.x+.25&&
            source->face(faceId).vertices==verts&&
            source->vertex(verts[0]).position.x==initialPosition.x&&
            working.pendingFaceEdits()==1&&
            working.pendingVertexEdits()==1,
            "M14A must expose pending changes, preserve FaceId/owner and never mutate original source");

    bool refused=false;
    try{working.overrideFace(faceId,{verts[0],verts[0],other});}
    catch(const std::logic_error&){refused=true;}
    require(refused&&working.face(faceId).vertices[2]==other,
            "M14A repeated vertex must refuse without changing pending face");
    refused=false;
    try{working.overrideFace(std::numeric_limits<FaceId>::max(),verts);}
    catch(const std::out_of_range&){refused=true;}
    require(refused,"M14A unknown FaceId cannot be fabricated");
    refused=false;
    try{working.overridePosition(std::numeric_limits<VertexId>::max(),initialPosition);}
    catch(const std::out_of_range&){refused=true;}
    require(refused,"M14A unknown VertexId cannot be fabricated");
    refused=false;
    try{working.overridePosition(verts[1],
        {std::numeric_limits<double>::infinity(),0,0});}
    catch(const std::logic_error&){refused=true;}
    require(refused,"M14A nonfinite positions must be rejected");

    MixedClosureSourceView bounded(source,1);
    bounded.overrideFace(faceId,{verts[0],verts[2],other});
    refused=false;
    try{bounded.overridePosition(verts[0],initialPosition);}
    catch(const std::length_error&){refused=true;}
    require(refused&&bounded.pendingFaceEdits()==1&&
            bounded.pendingVertexEdits()==0&&
            source->face(faceId).vertices==verts,
            "M14A exhausted override budget must not partially mutate state");
    refused=false;
    try{MixedClosureSourceView missing({},1);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"M14A cannot construct an ownerless mesh view");
    std::cout<<"[r06-m14a] immutable R03 read-through, effective incidences, "
                "override precedence, finite refusal PASS\n";
}
}

int main() {
    try{sourceBackedOverrides();return 0;}
    catch(const std::exception& e) {
        std::cerr<<"R06-M14A source-backed topology FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

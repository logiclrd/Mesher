#include "mesher/mesh_partitioning.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace mesher;
namespace {
void require(bool pass,const char* message) {
    if(!pass)throw std::runtime_error(message);
}
template<typename F>
void mustThrow(F&& action,const char* message) {
    bool thrown=false;
    try {action();}catch(const std::exception&){thrown=true;}
    require(thrown,message);
}
bool sameBits(double a,double b) {
    return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);
}
void compareFlat(const Mesh& before,const Mesh& after) {
    require(before.vertices.size()==after.vertices.size(),"Vertex count changed");
    require(before.triangles.size()==after.triangles.size(),"Face count changed");
    for(std::size_t i=0;i<before.vertices.size();++i) {
        const auto a=before.vertices[i],b=after.vertices[i];
        require(sameBits(a.x,b.x)&&sameBits(a.y,b.y)&&sameBits(a.z,b.z),
                "Flat vertex bits or original vertex order changed");
    }
    for(std::size_t i=0;i<before.triangles.size();++i) {
        const auto a=before.triangles[i],b=after.triangles[i];
        require(a.a==b.a&&a.b==b.b&&a.c==b.c,
                "Original face order, shared indices or winding changed");
    }
}
Mesh unusualMesh() {
    Mesh input;
    // Intentionally nonspatial order, negative zero, shared and orphan vertices.
    input.vertices={{1,0,0},{-1,-0.0,0},{0,0,1},{0,0,-1},
                    {0,1,0},{0,-1,0},{4,4,4},{-4,-4,-4}};
    input.triangles={{1,2,4},{0,4,2},{5,3,1},{0,3,5},{7,6,2}};
    return input;
}
void testLosslessAndOwnership() {
    const Mesh input=unusualMesh();
    const auto imported=partitionFlatMesh(input,2);
    require(imported.revision->partitionCount()==4,"Bounded partition count");
    validatePartitionedMesh(*imported.revision);
    compareFlat(input,reconstructFlatMesh(*imported.revision,imported.flatIndex));
    std::unordered_map<VertexId,PartitionId> owners;
    std::unordered_set<FaceId> faceIds;
    std::size_t totalVertices=0,totalFaces=0,crossPartitionFaces=0;
    imported.revision->forEachPartition([&](PartitionId id,const MeshPartition& part) {
        require(part.vertices.size()<=2&&part.faces.size()<=2,
                "Each partition has bounded independent element streams");
        totalVertices+=part.vertices.size();
        totalFaces+=part.faces.size();
        for(const auto& v:part.vertices) {
            require(v.id!=0,"Zero ID reserved");
            require(owners.emplace(v.id,id).second,"Duplicate vertex ID");
        }
        for(const auto& f:part.faces)
            require(f.id!=0&&faceIds.insert(f.id).second,"Unique nonzero face ID");
    });
    imported.revision->forEachPartition([&](PartitionId id,const MeshPartition& part) {
        for(const auto& face:part.faces)
            for(const auto vertex:face.vertices)
                if(owners.at(vertex)!=id)++crossPartitionFaces;
    });
    require(totalVertices==input.vertices.size()&&totalFaces==input.triangles.size(),
            "No vertex or face may be lost or duplicated");
    require(crossPartitionFaces>0,"Cross-partition face references work");
    require(imported.flatIndex.vertexIds[0]!=imported.flatIndex.vertexIds[1],
            "Distinct vertices require distinct IDs");
    const auto previous=imported.revision;
    const auto untouched=previous->partition(0);
    auto mutableView=previous->createMutableView();
    mutableView.editPartition(2).vertices[0].position.x=123;
    const auto updated=mutableView.commit();
    require(updated.statistics.modifiedPartitions==1&&
            updated.statistics.copiedDirectoryNodes==9,
            "Local edit requires one fixed-depth directory path");
    require(previous->partition(0)==updated.revision->partition(0),
            "Unchanged partition pointer identity retained");
    compareFlat(input,reconstructFlatMesh(*previous,imported.flatIndex));
    require(reconstructFlatMesh(*updated.revision,imported.flatIndex).vertices.size()
            ==input.vertices.size(),"Mapping stays useful after vertex edit");
}
void testBranchNonReuse() {
    const auto base=MeshRevision::empty();
    auto a=base->createMutableView();
    const VertexId used=a.addVertex(1,{1,2,3});
    const FaceId oldFace=a.addFace(2,{used,used,used});
    const auto first=a.commit().revision;
    auto abandoned=first->createMutableView();
    const VertexId discarded=abandoned.addVertex(1,{4,5,6});
    const FaceId discardedFace=abandoned.addFace(2,{used,used,used});
    auto b=first->createMutableView();
    const VertexId later=b.addVertex(1,{7,8,9});
    const FaceId laterFace=b.addFace(2,{used,later,used});
    const auto branch=b.commit().revision;
    require(used<discarded&&discarded<later,"Aborted vertex IDs not recycled");
    require(oldFace<discardedFace&&discardedFace<laterFace,
            "Aborted face IDs not recycled");
    auto remove=branch->createMutableView();
    remove.erasePartition(2);
    const auto withoutFaces=remove.commit().revision;
    auto afterDelete=withoutFaces->createMutableView();
    require(afterDelete.addFace(2,{used,later,used})>laterFace,
            "Deleted face IDs not recycled");
    require(first->partition(1)->vertices.size()==1,
            "Branch leaves old geometry untouched");
}
void testValidationAndFailures() {
    auto initial=unusualMesh();
    mustThrow([&] { (void)partitionFlatMesh(initial,0); },
              "Zero capacity rejected");
    initial.triangles[0].a=999;
    mustThrow([&] { (void)partitionFlatMesh(initial,2); },
              "Out-of-range flat face reference rejected");
    auto imported=partitionFlatMesh(unusualMesh(),2);
    auto invalid=imported.revision->createMutableView();
    invalid.editPartition(0).faces[0].vertices[0]=9999999;
    auto bad=invalid.commit().revision;
    mustThrow([&] { validatePartitionedMesh(*bad); },
              "Dangling cross-partition ID rejected");
    mustThrow([&] { (void)reconstructFlatMesh(*bad,imported.flatIndex); },
              "Invalid graph cannot export");
    auto repeated=imported.flatIndex;
    repeated.vertexIds[0]=repeated.vertexIds[1];
    mustThrow([&] { (void)reconstructFlatMesh(*imported.revision,repeated); },
              "Repeated flat map vertex rejected");
    auto missing=imported.flatIndex;
    missing.faceIds.pop_back();
    mustThrow([&] { (void)reconstructFlatMesh(*imported.revision,missing); },
              "Export cannot silently omit faces");
    auto duplicate=imported.revision->createMutableView();
    duplicate.editPartition(1).vertices[0].id=
        imported.revision->partition(0)->vertices[0].id;
    const auto duplicated=duplicate.commit().revision;
    mustThrow([&] { validatePartitionedMesh(*duplicated); },
              "Duplicate IDs across partitions rejected");
    // Extreme finite values: avoid overflow even when MSVC long double
    // has the same exponent range as double.
    Mesh extreme;
    extreme.vertices={{1e308,0,0},{-1e308,0,0},{0,0,0}};
    extreme.triangles={{0,1,2}};
    auto extremeImport=partitionFlatMesh(extreme,1);
    compareFlat(extreme,reconstructFlatMesh(*extremeImport.revision,
                                            extremeImport.flatIndex));
}
void testSymmetricSphere() {
    SphereOptions options;
    options.vertexCount=800;
    options.relaxationIterations=8;
    const auto sphere=generateSphere(options);
    require(validateSphere(sphere),"Generated symmetric sphere valid");
    const auto imported=partitionFlatMesh(sphere,64);
    compareFlat(sphere,reconstructFlatMesh(*imported.revision,imported.flatIndex));
    std::unordered_map<VertexId,unsigned> incident;
    imported.revision->forEachPartition([&](PartitionId,const MeshPartition& part) {
        for(const auto& face:part.faces)
            for(const auto id:face.vertices)++incident[id];
    });
    unsigned seam=0;
    for(std::size_t i=0;i<sphere.vertices.size();++i)
        if(sphere.vertices[i].x==0) {
            ++seam;
            require(incident[imported.flatIndex.vertexIds[i]]>=3,
                    "Seam vertex has one shared identity for incident faces");
        }
    require(seam>0,"Symmetric sphere must have seam vertices");
}
}
int main() {
    try {
        testLosslessAndOwnership();
        testBranchNonReuse();
        testValidationAndFailures();
        testSymmetricSphere();
        std::cout<<"Mesh partitioning and stable identity tests passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Partitioning test failed: "<<error.what()<<'\n';
        return 1;
    }
    return 0;
}

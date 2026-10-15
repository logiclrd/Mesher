#include "mesher/mesh_partitioning.hpp"
#include "mesher/region_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* message) {
    if(!yes)throw std::runtime_error(message);
}
template<typename F>
void rejected(F&& function,const char* message) {
    bool threw=false;
    try { function(); }catch(const std::exception&) { threw=true; }
    require(threw,message);
}
Mesh octahedron() {
    Mesh mesh;
    mesh.vertices={{1,0,0},{0,1,0},{-1,0,0},{0,-1,0},
                   {0,0,1},{0,0,-1}};
    mesh.triangles={{4,0,1},{4,1,2},{4,2,3},{4,3,0},
                    {5,1,0},{5,2,1},{5,3,2},{5,0,3}};
    return mesh;
}
bool equalMesh(const Mesh& a,const Mesh& b) {
    if(a.vertices.size()!=b.vertices.size() ||
       a.triangles.size()!=b.triangles.size())return false;
    for(std::size_t i=0;i<a.vertices.size();++i) {
        const auto x=a.vertices[i],y=b.vertices[i];
        if(x.x!=y.x||x.y!=y.y||x.z!=y.z)return false;
    }
    for(std::size_t i=0;i<a.triangles.size();++i) {
        const auto x=a.triangles[i],y=b.triangles[i];
        if(x.a!=y.a||x.b!=y.b||x.c!=y.c)return false;
    }
    return true;
}
void testCrossPartitionEdgeFlip() {
    const auto input=octahedron();
    const auto imported=partitionFlatMesh(input,1);
    auto index=RegionTopologyIndex::build(imported.revision);
    const auto& v=imported.flatIndex.vertexIds;
    const auto& f=imported.flatIndex.faceIds;
    RegionEdit edit(index,{v[0]});
    const auto& fp=edit.footprint();
    require(fp.writePartitions.size()>1,"Halo spans multiple partitions");
    require(fp.readPartitions.size()>=fp.writePartitions.size(),
            "Immutable read halo covers the write set");
    require(std::find(fp.editableFaces.begin(),fp.editableFaces.end(),f[0])!=
            fp.editableFaces.end(),"First incident face writable");
    require(std::find(fp.editableFaces.begin(),fp.editableFaces.end(),f[3])!=
            fp.editableFaces.end(),"Opposite incident face writable");
    std::vector<std::pair<PartitionId,std::shared_ptr<const MeshPartition>>> untouched;
    imported.revision->forEachPartition([&](PartitionId owner,
                                             const MeshPartition&) {
        if(std::find(fp.writePartitions.begin(),fp.writePartitions.end(),owner)==
           fp.writePartitions.end())
            untouched.push_back({owner,imported.revision->partition(owner)});
    });
    require(!untouched.empty(),"Some partitions must remain shared");
    // Flip the edge 4-0, replacing both incident faces together.
    edit.replaceFace(f[0],{v[4],v[3],v[1]});
    edit.replaceFace(f[3],{v[0],v[1],v[3]});
    const auto committed=edit.commit();
    require(committed.statistics.modifiedPartitions==fp.writePartitions.size(),
            "All prepared writable partitions are in the transaction");
    require(committed.statistics.copiedDirectoryNodes==9*fp.writePartitions.size(),
            "Commit copies only modified fixed-depth paths");
    for(const auto& [owner,pointer]:untouched)
        require(committed.revision->partition(owner)==pointer,
                "Unchanged partition retains the exact same pointer");
    require(equalMesh(input,reconstructFlatMesh(*imported.revision,
                                                 imported.flatIndex)),
            "Base revision remains unchanged");
    const auto changed=reconstructFlatMesh(*committed.revision,imported.flatIndex);
    require(changed.triangles[0].a==4&&changed.triangles[0].b==3&&
            changed.triangles[0].c==1,"First face flipped correctly");
    require(changed.triangles[3].a==0&&changed.triangles[3].b==1&&
            changed.triangles[3].c==3,"Second face flipped correctly");
    validatePartitionedMesh(*committed.revision);
    rejected([&] { (void)edit.commit(); },"Region transaction cannot commit twice");
}

void testRollbackAndScopedAccess() {
    const auto imported=partitionFlatMesh(octahedron(),1);
    auto index=RegionTopologyIndex::build(imported.revision);
    const auto& v=imported.flatIndex.vertexIds;
    const auto& f=imported.flatIndex.faceIds;
    MeshRepository published(imported.revision);
    auto reader=published.snapshot();
    RegionEdit invalid(index,{v[0]});
    invalid.replaceFace(f[0],{v[4],v[3],v[1]});
    rejected([&] { published.publish(invalid.commit().revision); },
             "A half-edge flip must fail edge-incidence validation");
    require(published.snapshot()==reader,
            "Failed edit never publishes a partial revision");
    require(equalMesh(reconstructFlatMesh(*reader,imported.flatIndex),
                      octahedron()),"Renderer's retained version unchanged");
    RegionEdit invalidScope(index,{v[0]});
    rejected([&] { invalidScope.moveVertex(v[2],{2,0,0}); },
             "Cannot move non-selected vertex");
    rejected([&] { invalidScope.replaceFace(f[1],{v[4],v[2],v[1]}); },
             "Cannot modify non-selected face");
    rejected([&] { invalidScope.addFace(99999,{v[0],v[1],v[4]}); },
             "Cannot insert into partition outside write set");
    rejected([&] { invalidScope.replaceFace(f[0],{v[0],v[1],999999}); },
             "Cannot reference vertex outside read halo");
    invalidScope.removeFace(f[0]);
    rejected([&] { (void)invalidScope.commit(); },
             "Cannot remove a face without repairing its topology");
    require(published.snapshot()==reader,"All failures leave publication intact");
    auto good=RegionEdit(index,{v[0]});
    good.moveVertex(v[0],{1.1,0,0});
    published.publish(good.commit().revision);
    require(reader->partition(index->vertex(v[0]).owner)->
                vertices[0].position.x==1,
            "Held old revision remains immutable after publication");
    require(published.snapshot()!=reader,"New revision published as one root");
}

void testLocalTriangleSubdivision() {
    const auto imported=partitionFlatMesh(octahedron(),1);
    auto index=RegionTopologyIndex::build(imported.revision);
    const auto& v=imported.flatIndex.vertexIds;
    const auto& f=imported.flatIndex.faceIds;
    RegionEdit edit(index,{v[0]});
    const auto owner=index->vertex(v[0]).owner;
    const auto center=Vec3{1.0/3.0,1.0/3.0,1.0/3.0};
    const auto midpoint=edit.addVertex(owner,center);
    edit.replaceFace(f[0],{v[4],v[0],midpoint});
    edit.addFace(owner,{v[0],v[1],midpoint});
    edit.addFace(owner,{v[1],v[4],midpoint});
    auto revised=edit.commit().revision;
    auto revisedIndex=RegionTopologyIndex::build(revised);
    require(revisedIndex->vertex(midpoint).incidentFaces.size()==3,
            "New vertex has a closed, three-face link");
    require(equalMesh(reconstructFlatMesh(*imported.revision,imported.flatIndex),
                      octahedron()),"Old topology untouched by subdivision");
    rejected([&] {
        (void)reconstructFlatMesh(*revised,imported.flatIndex);
    },"Flat compatibility map must reject new topology until dynamic export");
    // A dangling topology edit must fail without scanning the entire revision.
    RegionEdit bad(index,{v[0]});
    bad.removeVertex(v[0]);
    rejected([&] { (void)bad.commit(); },"Referenced vertex removal rejected");
}

void testMirrorAndSeam() {
    const auto imported=partitionFlatMesh(octahedron(),1);
    auto index=RegionTopologyIndex::build(imported.revision);
    const auto& v=imported.flatIndex.vertexIds;
    const auto& f=imported.flatIndex.faceIds;
    const std::vector<std::pair<VertexId,VertexId>> symmetry={
        {v[0],v[2]},{v[1],v[1]},{v[3],v[3]},
        {v[4],v[4]},{v[5],v[5]}};
    RegionEdit edit(index,{v[0],v[4]},symmetry);
    rejected([&] { edit.moveVertex(v[4],{.1,0,1}); },
             "Self-mirrored seam vertex cannot depart plane");
    rejected([&] { edit.replaceFace(f[0],{v[0],v[1],v[4]}); },
             "Symmetric edit cannot replace just one face");
    edit.moveVertex(v[0],{1.2,0,0});
    edit.moveVertex(v[4],{0,0,1.1});
    edit.replaceMirroredFaces(f[0],f[1],{v[0],v[1],v[4]});
    auto committed=edit.commit().revision;
    const auto flat=reconstructFlatMesh(*committed,imported.flatIndex);
    require(flat.vertices[0].x==1.2&&flat.vertices[2].x==-1.2,
            "Mirrored vertices move atomically");
    require(flat.vertices[4].x==0&&flat.vertices[4].z==1.1,
            "Shared seam vertex remains one ID on X=0");
    require(flat.triangles[0].a==0&&flat.triangles[0].b==1&&
            flat.triangles[0].c==4,"Primary mirrored face rewritten");
    require(flat.triangles[1].a==2&&flat.triangles[1].b==4&&
            flat.triangles[1].c==1,"Counterpart receives reversed mirrored winding");
    RegionEdit broken(index,{v[0]},symmetry);
    rejected([&] {
        broken.replaceMirroredFaces(f[0],f[3],{v[0],v[1],v[4]});
    },"Incorrect mirrored face pairing rejected");
    rejected([&] {
        RegionEdit missing(index,{v[0]},{{v[1],v[1]}});
    },"Unpaired symmetric seed rejected");
}
}
int main() {
    try {
        const auto run=[](const char* name,auto test) {
            try {test();}
            catch(...) {std::cerr<<"Failed scenario: "<<name<<'\\n';throw;}
        };
        run("cross-partition edge flip",testCrossPartitionEdgeFlip);
        run("rollback and scoped access",testRollbackAndScopedAccess);
        run("local subdivision",testLocalTriangleSubdivision);
        run("mirror and seam",testMirrorAndSeam);
        std::cout<<"Region edit transaction tests passed\n";
    }catch(const std::exception& ex) {
        std::cerr<<"Region edit transaction failure: "<<ex.what()<<'\n';
        return 1;
    }
}

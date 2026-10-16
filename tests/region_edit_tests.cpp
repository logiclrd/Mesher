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
    const auto current=good.commit().revision;
    require(published.publishIfCurrent(reader,current),
            "Current-base transaction publishes atomically");
    RegionEdit stale(index,{v[0]});
    stale.moveVertex(v[0],{1.3,0,0});
    require(!published.publishIfCurrent(reader,stale.commit().revision),
            "Stale-base transaction cannot overwrite newer publication");
    require(published.snapshot()==current,
            "Rejected stale transaction leaves published revision intact");
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
    RegionEdit capped(index,{v[0]},{},1);
    rejected([&] { (void)capped.addVertex(owner,center); },
             "Partition capacity prevents unbounded local append");
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

void testMirroredTopologyInsertionAndSharedSeam() {
    const auto imported=partitionFlatMesh(octahedron(),1);
    const auto index=RegionTopologyIndex::build(imported.revision);
    const auto& v=imported.flatIndex.vertexIds;
    const auto& f=imported.flatIndex.faceIds;
    const std::vector<std::pair<VertexId,VertexId>> mirror={
        {v[0],v[2]},{v[1],v[1]},{v[3],v[3]},
        {v[4],v[4]},{v[5],v[5]}};
    {
        RegionEdit edit(index,{v[0],v[4]},mirror,1);
        const auto primary=Vec3{.35,.3,.35};
        const auto pair=edit.appendMirroredVertices(
            index->vertex(v[0]).owner,index->vertex(v[2]).owner,primary);
        require(pair.first!=pair.second&&
                edit.mirroredVertex(pair.first)==pair.second,
                "Off-plane insertion produces an exact mirrored ID pair");
        edit.replaceMirroredFaces(f[0],f[1],{v[4],v[0],pair.first});
        edit.appendMirroredFaces(index->face(f[0]).owner,
                                 index->face(f[1]).owner,
                                 {v[0],v[1],pair.first});
        edit.appendMirroredFaces(index->face(f[0]).owner,
                                 index->face(f[1]).owner,
                                 {v[1],v[4],pair.first});
        const auto result=edit.commit();
        const auto next=edit.updatedIndex();
        require(next!=nullptr&&
                next->vertex(pair.second).position.x==-primary.x&&
                next->vertex(pair.second).position.y==primary.y,
                "Paired interior split uses reflected coordinates");
        require(next->vertex(pair.first).incidentFaces.size()==3&&
                next->vertex(pair.second).incidentFaces.size()==3,
                "Both mirrored triangle fans are closed manifolds");
        require(index->maybeVertex(pair.first)==nullptr&&
                result.statistics.modifiedPartitions>1,
                "Base immutable index preserved across partitioned splits");
    }
    {
        RegionEdit edit(index,{v[0],v[4]},mirror,1);
        const auto seam=edit.appendSeamVertex(
            index->vertex(v[4]).owner,{0,.5,.5});
        require(edit.mirroredVertex(seam)==seam,
                "A new X=0 seam midpoint is represented by one shared ID");
        edit.replaceMirroredFaces(f[0],f[1],{v[4],v[0],seam});
        edit.appendMirroredFaces(index->face(f[0]).owner,
                                 index->face(f[1]).owner,{seam,v[0],v[1]});
        (void)edit.commit();
        const auto next=edit.updatedIndex();
        require(next->vertex(seam).incidentFaces.size()==4,
                "One seam midpoint belongs to both mirrored half-surfaces");
        require(next->vertex(seam).position.x==0,
                "Symmetry seam remains exactly on X=0");
    }
    {
        RegionEdit bad(index,{v[0],v[4]},mirror,1);
        rejected([&] {
            (void)bad.appendMirroredFaces(index->face(f[0]).owner,
                index->face(f[1]).owner,{v[4],v[1],v[3]});
        },"Mirrored triangle entirely on X=0 cannot duplicate coincident faces");
        rejected([&] {(void)bad.commit();},
                 "Atomic symmetric insertion failure poisons transaction");
    }
}

void testSelfReflectedFaceCrossingEdge() {
    // A closed orientable tetrahedron symmetric under X reflection.
    // Its edge +X--X is itself reflected. The two incident triangles
    // are individually self-reflected, not distinct mirrored face IDs.
    Mesh tetra;
    tetra.vertices={{1,0,0},{-1,0,0},{0,1,0},{0,0,1}};
    tetra.triangles={{0,1,2},{1,0,3},{0,2,3},{1,3,2}};
    const auto imp=partitionFlatMesh(tetra,1);
    const auto index=RegionTopologyIndex::build(imp.revision);
    const auto& v=imp.flatIndex.vertexIds;
    const auto& f=imp.flatIndex.faceIds;
    const std::vector<std::pair<VertexId,VertexId>> mirrors={
        {v[0],v[1]},{v[2],v[2]},{v[3],v[3]}};
    const auto root=index->pickingRootIdentity();
    {
        RegionEdit edit(index,{v[0],v[1]},mirrors,1);
        const auto seam=edit.appendSeamVertex(index->vertex(v[0]).owner,
                                               {0,0,0});
        const auto first=edit.splitSelfMirroredFace(f[0],v[0],v[1],seam);
        const auto second=edit.splitSelfMirroredFace(f[1],v[1],v[0],seam);
        require(first.first==f[0]&&second.first==f[1]&&
                first.second!=second.second,
                "Both old self-mirrored face IDs survive and gain independent partners");
        require(edit.mirroredVertex(seam)==seam,
                "Crossing creates one self-mirrored X=0 midpoint");
        (void)edit.commit();
        const auto result=edit.updatedIndex();
        require(result&&result->vertex(seam).position.x==0&&
                result->vertex(seam).incidentFaces.size()==4,
                "Conforming split creates one manifold four-triangle seam fan");
        require(result->vertex(v[0]).incidentFaces.size()==3&&
                result->vertex(v[1]).incidentFaces.size()==3,
                "Both off-plane vertices retain cyclic one-rings");
        require(result->triangleIntersections(
            {},{f[0],f[1],first.second,second.second}).empty(),
            "Conforming X=0 split creates no accidental intersections");
        require(result->retiredFaceCount()==0&&
                index->retiredFaceCount()==0&&
                index->pickingRootIdentity()==root,
                "Pure crossing normalization has no tombstones or old-root mutation");
    }
    {
        RegionEdit invalid(index,{v[0],v[1]},mirrors,1);
        const auto wrong=invalid.appendSeamVertex(
            index->vertex(v[0]).owner,{0,.1,0});
        rejected([&] {
            (void)invalid.splitSelfMirroredFace(f[0],v[0],v[1],wrong);
        },"An approximate or displaced shared seam midpoint is not accepted");
        // A rejected precondition must not make the old revision publish.
        require(index->retiredFaceCount()==0&&
                index->pickingRootIdentity()==root,
                "Malformed attempted normalization leaves the original revision intact");
    }
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
            catch(...) {std::cerr<<"Failed scenario: "<<name<<std::endl;throw;}
        };
        run("cross-partition edge flip",testCrossPartitionEdgeFlip);
        run("rollback and scoped access",testRollbackAndScopedAccess);
        run("local subdivision",testLocalTriangleSubdivision);
        run("mirror and seam",testMirrorAndSeam);
        run("mirrored insertion and shared seam",testMirroredTopologyInsertionAndSharedSeam);
        run("self-reflected crossing-edge split",testSelfReflectedFaceCrossingEdge);
        std::cout<<"Region edit transaction tests passed\n";
    }catch(const std::exception& ex) {
        std::cerr<<"Region edit transaction failure: "<<ex.what()<<'\n';
        return 1;
    }
}

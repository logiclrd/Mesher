#include "mesher/repair_survivors.hpp"
#include "mesher/sculpt_fixtures.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* message) {
    if(!yes)throw std::runtime_error(message);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> index;
    FlatMeshIndex ids;
};
Fixture fixture(Mesh mesh) {
    auto imported=partitionFlatMesh(mesh,64);
    return {RegionTopologyIndex::build(imported.revision),imported.flatIndex};
}
FaceId nearest(const Fixture& source,Vec3 position) {
    FaceId best=0;
    double bestDistance=1e100;
    for(const auto id:source.ids.faceIds) {
        const auto& tri=source.index->face(id).vertices;
        const auto midpoint=(source.index->vertex(tri[0]).position+
                             source.index->vertex(tri[1]).position+
                             source.index->vertex(tri[2]).position)/3;
        const auto distance=length(midpoint-position);
        if(distance<bestDistance){best=id;bestDistance=distance;}
    }
    return best;
}
RegionTopologyIndex::CollisionPair pair(FaceId a,FaceId b) {
    if(b<a)std::swap(a,b);
    return {a,b,false};
}
void inspectFixture(const char* label,const Fixture& source,
                    std::vector<RegionTopologyIndex::CollisionPair> contacts,
                    std::size_t minimumLoops) {
    const auto plan=previewApproximateExcision(*source.index,contacts);
    require(plan.complete&&plan.contours.size()>=minimumLoops,
            "R06-M01 test requires an R02 complete cut with real contours");
    const auto cut=applyPrivateExcision(source.index,plan);
    const auto inventory=inventoryPrivateExcision(cut,plan);
    require(inventory.faces==cut.survivingFaces &&
            inventory.boundaryEdges==plan.boundaryEdges &&
            inventory.boundaryLoops==plan.contours.size() &&
            !inventory.components.empty(),
            "R06-M01 inventory must cover each actual surviving face and edge");
    std::size_t assigned=0,faces=0,edges=0;
    for(const auto& component:inventory.components) {
        assigned+=component.contourIndices.size();
        faces+=component.faces.size();
        edges+=component.edges;
        require(component.closed==component.contourIndices.empty(),
                "R06-M01 closed components must not have contour owners");
    }
    require(assigned==inventory.boundaryLoops&&faces==inventory.faces&&
            edges==inventory.edges,
            "R06-M01 component partition must be exact");
    require(source.index->retiredFaceCount()==0 &&
            source.index->retiredVertexCount()==0,
            "R06-M01 read-only traversal must not retire source stable IDs");

    auto corrupted=plan;
    corrupted.contours.front().survivingFaces.front()=plan.proposedFaces.front();
    bool refused=false;
    try{(void)inventoryPrivateExcision(cut,corrupted);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R06-M01 must reject wrong surviving face associated with rim");

    corrupted=plan;
    corrupted.contours.push_back(plan.contours.front());
    refused=false;
    try{(void)inventoryPrivateExcision(cut,corrupted);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R06-M01 must reject duplicated rim contour");
    auto limits=RepairSurvivorInventoryLimits{};
    limits.maxFaces=inventory.faces-1;
    refused=false;
    try{(void)inventoryPrivateExcision(cut,plan,limits);}
    catch(const std::length_error&){refused=true;}
    require(refused,"R06-M01 must reject bounded face inventory exhaustion");
    std::cout<<"[r06-m01] "<<label<<" faces="<<faces
             <<" components="<<inventory.components.size()
             <<" edges="<<edges<<" loops="<<assigned<<": PASS\n";
}
void realFixtureCuts() {
    SphereOptions options;
    options.vertexCount=320;
    options.symmetric=false;
    options.relaxationIterations=8;
    const auto sphere=fixture(generateSphere(options));
    const auto north=nearest(sphere,{0,1,0}),south=nearest(sphere,{0,-1,0});
    const auto sibling=[&](FaceId face) {
        auto t=sphere.index->face(face).vertices;
        if(t[1]<t[0])std::swap(t[0],t[1]);
        for(const auto& side:sphere.index->edgeSides({t[0],t[1]}))
            if(side.face!=face)return side.face;
        throw std::logic_error("R06-M01 sphere has no neighboring triangle");
    };
    inspectFixture("sphere",sphere,
                   {pair(north,sibling(north)),pair(south,sibling(south))},2);
    const auto torus=fixture(generateTorusFixture(5000,1,5));
    inspectFixture("torus",torus,
                   {pair(nearest(torus,{.12,0,0}),nearest(torus,{-.12,0,0}))},1);
    const auto hollow=fixture(generateHollowSphereFixture(5000,1,65));
    inspectFixture("hollow",hollow,
                   {pair(nearest(hollow,{.095,-.96,0}),
                         nearest(hollow,{.095,-.93,0}))},1);
}
void twoDisconnectedOpenComponents() {
    SphereOptions options;
    options.vertexCount=160;
    options.symmetric=false;
    options.relaxationIterations=8;
    Mesh merged;
    for(const auto center:std::array<Vec3,2>{{{-3,0,0},{3,0,0}}}) {
        const auto sphere=generateSphere(options);
        const auto offset=static_cast<std::uint32_t>(merged.vertices.size());
        for(const auto p:sphere.vertices)merged.vertices.push_back(p+center);
        for(const auto t:sphere.triangles)
            merged.triangles.push_back({
                offset+t[0],offset+t[1],offset+t[2]});
    }
    const auto source=fixture(std::move(merged));
    const auto sibling=[&](FaceId id) {
        auto t=source.index->face(id).vertices;
        if(t[1]<t[0])std::swap(t[0],t[1]);
        for(const auto& side:source.index->edgeSides({t[0],t[1]}))
            if(side.face!=id)return side.face;
        throw std::logic_error("R06-M01 separate sphere face has no adjacent face");
    };
    const auto left=nearest(source,{-3,1,0});
    const auto right=nearest(source,{3,1,0});
    const auto plan=previewApproximateExcision(
        *source.index,{pair(left,sibling(left)),pair(right,sibling(right))});
    require(plan.complete&&plan.contours.size()==2,
            "R06-M01 two disconnected spheres must produce two separate cut rims");
    const auto cut=applyPrivateExcision(source.index,plan);
    const auto inventory=inventoryPrivateExcision(cut,plan);
    require(inventory.components.size()==2&&inventory.boundaryLoops==2,
            "R06-M01 distinct disconnected shells must not have merged ownership");
    require(inventory.components[0].contourIndices.size()==1&&
            inventory.components[1].contourIndices.size()==1&&
            inventory.components[0].contourIndices.front()!=
                inventory.components[1].contourIndices.front(),
            "R06-M01 each of two independent open shells must own its own rim");
    for(const auto& component:inventory.components)
        require(!component.closed&&component.eulerCharacteristic==1,
                "R06-M01 sphere with one boundary disk removed has Euler 1");
    std::cout<<"[r06-m01] two independent open spheres, two owners, Euler 1 each: PASS\n";
}
void zeroRimWholeComponentDeletion() {
    Mesh mesh;
    const auto cube=[&](Vec3 center) {
        const auto offset=static_cast<std::uint32_t>(mesh.vertices.size());
        for(const auto v:std::array<Vec3,8>{{
            {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
            {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
        }})mesh.vertices.push_back(center+v);
        for(const auto t:std::array<std::array<std::uint32_t,3>,12>{{
            {0,2,1},{0,3,2},{4,5,6},{4,6,7},
            {0,1,5},{0,5,4},{3,7,6},{3,6,2},
            {0,4,7},{0,7,3},{1,2,6},{1,6,5}
        }})mesh.triangles.push_back({offset+t[0],offset+t[1],offset+t[2]});
    };
    cube({-3,0,0});
    cube({3,0,0});
    const auto source=fixture(std::move(mesh));
    RepairExcisionPreview plan;
    plan.complete=true;
    plan.proposedVertices.assign(source.ids.vertexIds.begin(),
                                 source.ids.vertexIds.begin()+8);
    plan.proposedFaces.assign(source.ids.faceIds.begin(),
                              source.ids.faceIds.begin()+12);
    const auto cut=applyPrivateExcision(source.index,plan);
    const auto inventory=inventoryPrivateExcision(cut,plan);
    require(inventory.components.size()==1&&
            inventory.components.front().closed &&
            inventory.components.front().faces.size()==12 &&
            inventory.components.front().vertices.size()==8 &&
            inventory.components.front().edges==18 &&
            inventory.components.front().eulerCharacteristic==2 &&
            inventory.boundaryLoops==0&&inventory.boundaryEdges==0,
            "R06-M01 must recognize a closed survivor after zero-loop deletion");
    require(source.index->retiredFaceCount()==0 &&
            cut.index->retiredFaceCount()==12,
            "R06-M01 must not alter original or cut tombstones");
    std::cout<<"[r06-m01] full component retirement: closed survivor Euler 2, zero loops PASS\n";
}
}
int main() {
    try {
        realFixtureCuts();
        twoDisconnectedOpenComponents();
        zeroRimWholeComponentDeletion();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R06-M01 survivor inventory test FAILED: "<<e.what()<<"\n";
        return 1;
    }
}

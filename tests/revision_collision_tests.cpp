#include "mesher/triangle_collision.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/region_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace mesher;

namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
using Points=std::array<Vec3,3>;
using Ids=std::array<VertexId,3>;

void testNarrowPhase() {
    const Points a{{{0,0,0},{1,0,0},{0,1,0}}};
    const Ids ai{1,2,3};
    const auto contact=[&](Points b,Ids ids) {
        return triangleContact(a,ai,b,ids);
    };
    check(contact({{{2,2,0},{3,2,0},{2,3,0}}},{4,5,6})==
          TriangleContact::Disjoint,"Disjoint coplanar triangles");
    check(contact({{{0.2,0.2,-1},{0.2,0.2,1},{0.8,0.8,0}}},{4,5,6})==
          TriangleContact::Intersecting,"Independent noncoplanar crossing");
    check(contact({{{.1,.1,0},{.7,.1,0},{.1,.7,0}}},{4,5,6})==
          TriangleContact::Intersecting,"Independent coplanar overlap");
    check(contact({{{0,0,0},{-1,0,0},{0,-1,0}}},{4,5,6})==
          TriangleContact::Intersecting,"Independent tangential point contact");
    check(contact({{{0,0,1},{1,0,1},{0,1,1}}},{4,5,6})==
          TriangleContact::Disjoint,"Parallel non-contact separated faces");

    check(contact({{{1,0,0},{0,0,0},{1,-1,0}}},{2,1,4})==
          TriangleContact::ExpectedAdjacency,
          "Common edge with coplanar opposite-side triangles is legitimate");
    check(contact({{{1,0,0},{0,0,0},{0,.3,0}}},{2,1,4})==
          TriangleContact::Intersecting,
          "Common edge with coplanar same-side triangles overlaps");
    check(contact({{{1,0,0},{0,0,0},{0,0,1}}},{2,1,4})==
          TriangleContact::ExpectedAdjacency,
          "Normal noncoplanar edge fan is not a collision");
    check(contact({{{0,0,0},{0,0,1},{-1,0,1}}},{1,4,5})==
          TriangleContact::ExpectedAdjacency,
          "Noncoplanar shared vertex without penetration is legitimate");
    // Regression from two correctly sewn cube walls. Their new scar
    // triangle meets the old horizontal face only at a real shared
    // vertex; the other scar edge is coplanar with the horizontal
    // face yet projects entirely OUTSIDE it. Tolerance-expanded edge
    // clipping used to invent an infinitesimal positive-length overlap.
    const double xLeft=-1.1+1,xRight=1.1-1;
    check(triangleContact(
        Points{{{-2.1,-1,-1},{xLeft,1,-1},{xLeft,-1,-1}}},
        Ids{1,6,2},
        Points{{{xLeft,-1,-1},{xRight,1,1},{xRight,1,-1}}},
        Ids{2,15,7})==TriangleContact::ExpectedAdjacency,
        "No phantom coplanar edge penetration at a shared scar vertex");
    check(contact({{{0,0,0},{.4,.4,1},{.4,.4,-1}}},{1,4,5})==
          TriangleContact::Intersecting,
          "Shared vertex does not mask a crossing farther from that vertex");
    check(contact({{{0,0,0},{1000000,0,0},{0,0,1}}},{1,4,5})==
          TriangleContact::Intersecting,
          "A short coplanar edge overlap is detected along a very long edge");
    check(contact({{{0,0,0},{-.5,0,0},{0,-.5,0}}},{1,4,5})==
          TriangleContact::ExpectedAdjacency,
          "Coplanar triangles touching only at one shared vertex are legitimate");
    check(contact({{{0,0,0},{.3,.1,0},{.1,.3,0}}},{1,4,5})==
          TriangleContact::Intersecting,
          "Coplanar shared-vertex triangles overlapping in their interiors");
    check(contact(a,{11,12,13})==TriangleContact::Intersecting,
          "Duplicate coincident triangles are not legitimate adjacency");
    check(contact({{{0,0,0},{1,0,0},{2,0,0}}},{4,5,6})==
          TriangleContact::Ambiguous,
          "Degenerate triangle requires explicit uncertainty classification");
}

void addTetrahedron(Mesh& mesh,Vec3 center,double half) {
    const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto offset:std::array<Vec3,4>{{
        {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
    }})mesh.vertices.push_back(center+offset*half);
    for(const auto face:std::array<std::array<std::uint32_t,3>,4>{{
        {0,1,2},{0,2,3},{0,3,1},{1,3,2}
    }})mesh.triangles.push_back({first+face[0],first+face[1],first+face[2]});
}
std::set<std::pair<FaceId,FaceId>> allPairs(
    const RegionTopologyIndex& index) {
    std::vector<FaceId> faces;
    index.revision().forEachPartition([&](PartitionId,const MeshPartition& part) {
        for(const auto& face:part.faces)faces.push_back(face.id);
    });
    std::set<std::pair<FaceId,FaceId>> found;
    for(std::size_t i=0;i<faces.size();++i) {
        const auto& a=index.face(faces[i]);
        const auto points=[&](const RegionTopologyIndex::Face& f) {
            return Points{{index.vertex(f.vertices[0]).position,
                           index.vertex(f.vertices[1]).position,
                           index.vertex(f.vertices[2]).position}};
        };
        for(std::size_t j=i+1;j<faces.size();++j) {
            const auto& b=index.face(faces[j]);
            const auto kind=triangleContact(points(a),a.vertices,
                                            points(b),b.vertices);
            if(kind==TriangleContact::Intersecting||
               kind==TriangleContact::Ambiguous)
                found.insert(std::minmax(faces[i],faces[j]));
        }
    }
    return found;
}
std::set<std::pair<FaceId,FaceId>> indexedPairs(
    const std::vector<RegionTopologyIndex::CollisionPair>& collisions) {
    std::set<std::pair<FaceId,FaceId>> found;
    for(const auto& pair:collisions) {
        check(pair.first<pair.second,"Pairs must be canonical and ordered");
        check(found.emplace(pair.first,pair.second).second,
              "No duplicate pair even when both faces moved");
    }
    return found;
}

void testSimultaneouslyMovingSolids() {
    Mesh mesh;
    addTetrahedron(mesh,{-2,0,0},.5);
    // Construct the second closed solid by exact X=0 reflection, with
    // opposite triangle winding and stable original-order mirror pairs.
    for(unsigned i=0;i<4;++i) {
        const auto p=mesh.vertices[i];
        mesh.vertices.push_back({-p.x,p.y,p.z});
    }
    for(unsigned i=0;i<4;++i) {
        const auto face=mesh.triangles[i];
        mesh.triangles.push_back({face.a+4,face.c+4,face.b+4});
    }
    const auto imported=partitionFlatMesh(mesh,1);
    const auto initial=RegionTopologyIndex::build(imported.revision);
    check(allPairs(*initial).empty(),"Two separate closed tetrahedra start valid");
    std::vector<VertexId> both=imported.flatIndex.vertexIds;
    RegionEdit edit(initial,both,{},1);
    for(unsigned i=0;i<both.size();++i) {
        const auto old=initial->vertex(both[i]).position;
        // Neither changed tetrahedron intersects the *old* position of the
        // other, but the two post-stamp tetrahedra intersect each other.
        edit.moveVertex(both[i],old+Vec3{i<4?1.9:-1.9,0,0});
    }
    (void)edit.commit();
    const auto proposed=edit.updatedIndex();
    for(unsigned i=0;i<4;++i) {
        const auto left=proposed->vertex(both[i]).position;
        const auto right=proposed->vertex(both[i+4]).position;
        check(left.x==-right.x&&left.y==right.y&&left.z==right.z,
              "Simultaneously moved surfaces preserve exact reflection");
    }
    check(proposed->updateStatistics().fullMeshTraversals==0,
          "Simultaneous movement does not perform a full index traversal");
    RegionTopologyIndex::CollisionStatistics stats;
    const auto found=proposed->triangleIntersections(both,{},&stats);
    check(!found.empty(),"Post-state detects two surfaces moving into each other");
    check(indexedPairs(found)==allPairs(*proposed),
          "BVH post-state results equal independent all-face candidate traversal");
    check(stats.seedFaces==8,"Each moved face appears exactly once as seed");
    check(stats.narrowPhaseTests<=stats.candidateTriangles,
          "Deduplicated narrow-phase tests bounded by BVH candidates");
    check(allPairs(*initial).empty(),"Old immutable snapshot remains unchanged");
    check(initial->pickingRootIdentity()!=proposed->pickingRootIdentity(),
          "Proposed revision owns distinct updated BVH root");
    RegionTopologyIndex::CollisionStatistics beforeStats;
    check(initial->triangleIntersections(both,{},&beforeStats).empty(),
          "Old index queries still refer to old geometry");
    const auto onlyOne=proposed->triangleIntersections(
        std::vector<VertexId>(both.begin(),both.begin()+4),{});
    check(!onlyOne.empty(),"Changed/unchanged faces are both queried");
    for(const auto& pair:onlyOne)
        check(pair.first!=pair.second,"No self-pair in returned intersections");
    const auto empty=proposed->triangleIntersections({},{});
    check(empty.empty(),"No changed inputs produce no work/results");
    bool invalid=false;
    try{(void)proposed->triangleIntersections({9999999},{});}
    catch(const std::out_of_range&){invalid=true;}
    check(invalid,"Unknown vertex is not silently accepted as a clean query");
}

void testPruningAndNoOp() {
    SphereOptions options;
    options.vertexCount=700;
    options.relaxationIterations=8;
    auto imported=partitionFlatMesh(generateSphere(options),16);
    const auto index=RegionTopologyIndex::build(imported.revision);
    const auto candidate=index->querySphere({0,0,1},.2);
    check(!candidate.empty(),"Local sphere seed vertices exist");
    RegionTopologyIndex::CollisionStatistics before;
    check(index->triangleIntersections({candidate.front()},{},&before).empty(),
          "Ordinary incident closed sphere faces must not self-intersect");
    check(before.candidateTriangles<imported.flatIndex.faceIds.size()/3,
          "Local triangle BVH queries reject most distant faces");
    RegionEdit edit(index,{candidate.front()});
    (void)edit.commit();
    const auto noOp=edit.updatedIndex();
    check(noOp->pickingRootIdentity()==index->pickingRootIdentity(),
          "No-op shares the existing immutable collision BVH");
    check(noOp->triangleIntersections({candidate.front()},{}).empty(),
          "No-op revision reuses clean local geometry");
}
} // namespace

int main() {
    try {
        testNarrowPhase();
        testSimultaneouslyMovingSolids();
        testPruningAndNoOp();
        std::cout<<"Revision triangle collision regression passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Revision triangle collision failure: "<<error.what()<<'\n';
        return 1;
    }
}

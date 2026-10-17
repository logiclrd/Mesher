#include "mesher/triangle_collision.hpp"
#include "mesher/repair_intersection.hpp"
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
    check(triangleContact(
          Points{{{-2.1,-1,-1},{-.1,-.95,-1},{-.1,-1,-1}}},
          Ids{1,5,4},
          Points{{{-.1,-.95,-1},{-.1,-.8,-1},{0,-.8,-1}}},
          Ids{5,6,52})==TriangleContact::ExpectedAdjacency,
          "Coplanar edge subdivisions may meet neighboring zipper triangles only at a vertex");
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

void testBoundedCollisionDiscovery() {
    // Two identical, distinct tetrahedron shells guarantee overlapping
    // post-state triangles, including duplicate candidate visits.
    Mesh mesh;
    addTetrahedron(mesh,{},1);
    addTetrahedron(mesh,{},1);
    const auto imported=partitionFlatMesh(mesh,1);
    const auto index=RegionTopologyIndex::build(imported.revision);
    const auto originalRoot=index->pickingRootIdentity();
    const auto& faces=imported.flatIndex.faceIds;
    using Limits=RegionTopologyIndex::CollisionQueryLimits;
    using Statistics=RegionTopologyIndex::CollisionStatistics;
    Statistics full{};
    const auto expected=index->triangleIntersections({},faces,&full);
    check(!expected.empty()&&full.seedFaces==8&&
          full.boxesTested>0&&full.partitionsTested>0&&
          full.candidateTriangles>0&&full.narrowPhaseTests>0&&
          full.reportedContacts==expected.size(),
          "Overlapping shells generate measurable collision work and contacts");
    Limits exact;
    exact.maxSeedFaces=full.seedFaces;
    exact.maxBoxTests=full.boxesTested;
    exact.maxPartitionTests=full.partitionsTested;
    exact.maxCandidateTriangles=full.candidateTriangles;
    exact.maxNarrowPhaseTests=full.narrowPhaseTests;
    exact.maxReportedContacts=full.reportedContacts;
    Statistics measured{};
    check(indexedPairs(index->triangleIntersections({},faces,&measured,exact))==
          indexedPairs(expected),
          "Exact observed work limits still return every collision");
    check(measured.seedFaces==full.seedFaces&&
          measured.boxesTested==full.boxesTested&&
          measured.partitionsTested==full.partitionsTested&&
          measured.candidateTriangles==full.candidateTriangles&&
          measured.narrowPhaseTests==full.narrowPhaseTests&&
          measured.reportedContacts==full.reportedContacts,
          "Limits count real BVH and narrow-phase work exactly");

    // Each dimension is independently restrictive. Never return the
    // partial discoveries as if they were a complete clean result.
    for(unsigned dimension=0;dimension<6;++dimension) {
        auto limited=exact;
        std::size_t budget=0;
        switch(dimension) {
            case 0: budget=--limited.maxSeedFaces;break;
            case 1: budget=--limited.maxBoxTests;break;
            case 2: budget=--limited.maxPartitionTests;break;
            case 3: budget=--limited.maxCandidateTriangles;break;
            case 4: budget=--limited.maxNarrowPhaseTests;break;
            default: budget=--limited.maxReportedContacts;break;
        }
        Statistics partial{};
        bool refused=false;
        try {(void)index->triangleIntersections({},faces,&partial,limited);}
        catch(const std::length_error&){refused=true;}
        check(refused,"Exhausting any collision-work dimension refuses the query");
        const std::array<std::size_t,6> counters{{
            partial.seedFaces,partial.boxesTested,partial.partitionsTested,
            partial.candidateTriangles,partial.narrowPhaseTests,
            partial.reportedContacts
        }};
        check(counters[dimension]==budget,
              "Refusal never performs more than the permitted measured work");
    }
    auto zero=exact;
    zero.maxSeedFaces=0;
    check(index->triangleIntersections({},{},nullptr,zero).empty(),
          "A zero budget still permits genuinely empty queries");
    check(index->pickingRootIdentity()==originalRoot&&
          index->retiredFaceCount()==0,
          "All bounded queries preserve held immutable roots");
}
void testTransverseIntersectionSectionsAndPatches() {
    using Status=IntersectionSectionStatus;
    using Points=std::array<Vec3,3>;
    const Points a{{{0,0,0},{2,0,0},{0,2,0}}};
    const std::array<VertexId,3> ai{1,2,3},bi{4,5,6};
    // Both segment endpoints are in the INTERIOR of A, but on two
    // different edges of B. The section must become an edge in both.
    const Points b{{{.7,-.5,-1},{.7,1.7,1},{.7,.2,-1}}};
    auto interior=splitTransverseTriangleContact(a,ai,b,bi);
    check(interior.status==Status::TransverseSegment,
          "Transverse independent triangles produce one bounded segment");
    check(interior.patches[0].interior[0]&&
          interior.patches[0].interior[1]&&
          !interior.patches[1].interior[0]&&
          !interior.patches[1].interior[1],
          "The same segment can be interior to one face, edge-clipped on the other");
    for(unsigned side=0;side<2;++side) {
        const auto& patch=interior.patches[side];
        check(!patch.triangles.empty()&&patch.triangles.size()<=7,
              "Per-triangle cut remains within fixed local complexity");
        unsigned sectionEdges=0;
        double totalArea=0;
        const auto normal=cross(patch.positions[1]-patch.positions[0],
                                patch.positions[2]-patch.positions[0]);
        for(const auto& tri:patch.triangles) {
            const auto p=patch.positions[tri[0]],q=patch.positions[tri[1]],
                       r=patch.positions[tri[2]];
            const auto signedArea=dot(cross(q-p,r-p),normal)/length(normal);
            check(signedArea>0,"Subdivided triangle retains original winding");
            totalArea+=signedArea;
            for(unsigned j=0;j<3;++j)
                if((tri[j]==3&&tri[(j+1)%3]==4)||
                   (tri[j]==4&&tri[(j+1)%3]==3))++sectionEdges;
        }
        check(sectionEdges==2,
              "Shared segment is a conforming internal edge of EACH tessellated face");
        check(std::abs(totalArea-length(normal))<1e-8,
              "Subdivision conserves exactly one original triangle area");
        for(unsigned k=0;k<2;++k) {
            const auto bary=patch.endpointBarycentrics[k];
            check(std::abs(bary[0]+bary[1]+bary[2]-1)<1e-8,
                  "Intersection endpoint retains valid barycentric provenance");
            check(length(patch.positions[3+k]-interior.endpoints[k])<1e-12,
                  "Both faces share one identical 3D section endpoint");
        }
    }
    const Points wide{{{.7,-2,-1},{.7,3,-1},{.7,.5,2}}};
    const auto clipped=splitTransverseTriangleContact(a,ai,wide,bi);
    check(clipped.status==Status::TransverseSegment&&
          !clipped.patches[0].interior[0]&&
          !clipped.patches[0].interior[1]&&
          clipped.patches[1].interior[0]&&
          clipped.patches[1].interior[1],
          "Two boundary intersections split a face into chord-separated polygons");
    check(clipped.patches[0].triangles.size()==3,
          "Boundary-to-boundary chord triangulates its two complementary polygons");

    const Points coplanar{{{.3,.3,0},{1.0,.3,0},{.3,1.0,0}}};
    check(splitTransverseTriangleContact(a,ai,coplanar,bi).status==
          Status::UnsupportedContact,
          "Coplanar overlap must not masquerade as a transverse cut");
    const Points tangent{{{0,0,0},{0,0,1},{-1,0,1}}};
    check(splitTransverseTriangleContact(a,ai,tangent,bi).status==
          Status::UnsupportedContact,
          "Single-point grazing contact has no authorizing cut segment");
    const Points away{{{3,3,-1},{4,3,1},{4,4,-1}}};
    check(splitTransverseTriangleContact(a,ai,away,bi).status==
          Status::Disjoint,"Separated faces require no reconstruction");

    Mesh mesh;
    for(const auto& p:a)mesh.vertices.push_back(p);
    for(const auto& p:b)mesh.vertices.push_back(p);
    mesh.triangles.push_back({0,1,2});
    mesh.triangles.push_back({3,4,5});
    const auto imported=partitionFlatMesh(mesh,1);
    const auto index=RegionTopologyIndex::build(imported.revision);
    const auto originalRoot=index->pickingRootIdentity();
    const auto intersections=index->triangleIntersections(
        imported.flatIndex.vertexIds,{});
    check(intersections.size()==1,"Actual V13 post-state BVH reports one section");
    const auto sections=splitTransverseContactBatch(*index,intersections,1);
    check(sections.status==Status::TransverseSegment&&
          sections.sections.size()==1,
          "Bounded batch converts a complete V13 crossing without editing the root");
    bool exhausted=false;
    try {(void)splitTransverseContactBatch(*index,intersections,0);}
    catch(const std::length_error&){exhausted=true;}
    check(exhausted,"Over-budget intersection sections refuse, never truncate");
    auto mixed=intersections;
    mixed.push_back({intersections.front().first,
                     intersections.front().second,true});
    const auto refused=splitTransverseContactBatch(*index,mixed,2);
    check(refused.status==Status::UnsupportedContact&&
          refused.sections.empty(),
          "One ambiguous contact invalidates an entire section batch");
    check(index->pickingRootIdentity()==originalRoot&&
          index->retiredFaceCount()==0,
          "All derived sections remain private; original BVH and topology unchanged");
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
        testTransverseIntersectionSectionsAndPatches();
        testSimultaneouslyMovingSolids();
        testBoundedCollisionDiscovery();
        testPruningAndNoOp();
        std::cout<<"Revision triangle collision regression passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Revision triangle collision failure: "<<error.what()<<'\n';
        return 1;
    }
}

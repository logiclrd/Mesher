#include "mesher/triangle_collision.hpp"
#include "mesher/repair_intersection.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/region_edit.hpp"
#include "mesher/revision_history.hpp"
#include "mesher/durable_checkpoint.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <vector>

using namespace mesher;

namespace {
void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}
using Points=std::array<Vec3,3>;
using Ids=std::array<VertexId,3>;

// A genuine Boolean join must connect the two original surface
// components; overlapping but independent coincident shells do not pass.
std::size_t connectedClosedSurfaceComponents(const RegionTopologyIndex& index) {
    std::set<FaceId> live;
    index.revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& face:part.faces)live.insert(face.id);
        });
    if(live.size()>16384)
        throw std::length_error("Union fixture exceeds component audit budget");
    std::set<FaceId> visited;
    std::size_t components=0;
    for(const auto seed:live) {
        if(visited.contains(seed))continue;
        ++components;
        std::vector<FaceId> stack{seed};
        visited.insert(seed);
        while(!stack.empty()) {
            const auto id=stack.back();stack.pop_back();
            const auto ids=index.face(id).vertices;
            for(unsigned k=0;k<3;++k) {
                const auto a=std::min(ids[k],ids[(k+1)%3]),
                           b=std::max(ids[k],ids[(k+1)%3]);
                const auto& sides=index.edgeSides({a,b});
                check(sides.size()==2,
                      "A certified joined surface must be closed and edge-manifold");
                for(const auto& other:sides)
                    if(visited.insert(other.face).second)
                        stack.push_back(other.face);
            }
        }
    }
    check(visited.size()==live.size(),
          "Connected component traversal covers every live private face");
    return components;
}

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

    const Points crossingEdge{{{.7,-3,-1},{.7,2,1},{.7,.1,-1}}};
    const auto mixed=splitTransverseTriangleContact(a,ai,crossingEdge,bi);
    check(mixed.status==Status::TransverseSegment&&
          mixed.patches[0].interior[0]!=mixed.patches[0].interior[1]&&
          mixed.patches[1].interior[0]!=mixed.patches[1].interior[1],
          "One edge endpoint and one face-interior endpoint produce a conforming chord");

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
    auto mixedBatch=intersections;
    mixedBatch.push_back({intersections.front().first,
                          intersections.front().second,true});
    const auto refused=splitTransverseContactBatch(*index,mixedBatch,2);
    check(refused.status==Status::UnsupportedContact&&
          refused.sections.empty(),
          "One ambiguous contact invalidates an entire section batch");
    check(index->pickingRootIdentity()==originalRoot&&
          index->retiredFaceCount()==0,
          "All derived sections remain private; original BVH and topology unchanged");
}
struct ConformingFixture {
    PartitionedMeshImport import;
    std::array<FaceId,3> faces{};
    std::vector<std::pair<VertexId,VertexId>> reflections;
};
ConformingFixture conformingFixture(bool mirrored=false) {
    const std::array<Vec3,3> a{{{0,0,0},{2,0,0},{0,2,0}}};
    const std::array<Vec3,3> b{{{.7,-.5,-1},{.7,1.7,1},{.7,.2,-1}}};
    const std::array<VertexId,3> ai{1,2,3},bi{4,5,6};
    const auto cut=splitTransverseTriangleContact(a,ai,b,bi);
    check(cut.status==IntersectionSectionStatus::TransverseSegment,
          "Shared-edge fixture starts with a real independent intersection");
    unsigned zero=3;
    for(unsigned j=0;j<3;++j)
        if(cut.patches[1].endpointBarycentrics[0][j]==0) {
            zero=j;
            break;
        }
    check(zero<3,"B section endpoint lies on one definite original edge");
    Mesh mesh;
    for(const auto p:a)mesh.vertices.push_back(p);
    for(const auto p:b)mesh.vertices.push_back(p);
    // The endpoint on one B edge must also subdivide the *other*
    // original incident face, not only B's intersected face.
    const auto start=(zero+1)%3,end=(zero+2)%3;
    const auto third=b[start]+b[end]-b[zero];
    mesh.vertices.push_back(third);
    mesh.triangles.push_back({0,1,2});
    mesh.triangles.push_back({3,4,5});
    mesh.triangles.push_back({
        static_cast<std::uint32_t>(3+end),
        static_cast<std::uint32_t>(3+start),6});
    const auto baseVertexCount=mesh.vertices.size();
    const auto baseFaceCount=mesh.triangles.size();
    if(mirrored) {
        for(std::size_t i=0;i<baseVertexCount;++i) {
            const auto p=mesh.vertices[i];
            mesh.vertices.push_back({-p.x,p.y,p.z});
        }
        for(std::size_t i=0;i<baseFaceCount;++i) {
            const auto f=mesh.triangles[i];
            mesh.triangles.push_back({
                static_cast<std::uint32_t>(f.a+baseVertexCount),
                static_cast<std::uint32_t>(f.c+baseVertexCount),
                static_cast<std::uint32_t>(f.b+baseVertexCount)});
        }
    }
    auto imported=partitionFlatMesh(mesh,1);
    ConformingFixture fixture;
    fixture.faces={imported.flatIndex.faceIds[0],
                   imported.flatIndex.faceIds[1],
                   imported.flatIndex.faceIds[2]};
    if(mirrored)for(std::size_t i=0;i<baseVertexCount;++i)
        fixture.reflections.emplace_back(imported.flatIndex.vertexIds[i],
            imported.flatIndex.vertexIds[i+baseVertexCount]);
    fixture.import=std::move(imported);
    return fixture;
}
void testConformingSharedEdgeGraphAndBudgets() {
    auto fixture=conformingFixture(false);
    const auto index=RegionTopologyIndex::build(fixture.import.revision);
    const auto originalRoot=index->pickingRootIdentity();
    const auto all=index->triangleIntersections(
        fixture.import.flatIndex.vertexIds,{});
    auto found=std::find_if(all.begin(),all.end(),
        [&](const auto& pair) {
            const auto ordered=std::minmax(fixture.faces[0],fixture.faces[1]);
            return pair.first==ordered.first&&pair.second==ordered.second;
        });
    check(found!=all.end(),"V13 discovers original intersecting pair");
    const std::vector<RegionTopologyIndex::CollisionPair> pairs{*found};
    const auto result=buildConformingCutPlan(*index,pairs);
    check(result.status==IntersectionSectionStatus::TransverseSegment&&
          result.segments.size()==1&&result.faces.size()==3&&
          result.refusal.empty(),
          "Conformance includes an original noncolliding adjacent face");
    const auto& segment=result.segments[0];
    for(unsigned k=0;k<2;++k) {
        check(segment.firstSide[k]!=segment.secondSide[k],
              "Two intersecting sheets MUST NOT pre-weld coincident section endpoints");
        const auto p=result.points[segment.firstSide[k]].position,
                   q=result.points[segment.secondSide[k]].position;
        check(length(p-q)<1e-9,
              "Distinct contact-sheet vertices still have matching geometric section coordinates");
    }
    const auto& bface=index->face(fixture.faces[1]).vertices;
    const auto& bside=segment.firstFace==fixture.faces[1]?
                      segment.firstSide:segment.secondSide;
    std::optional<std::size_t> sharedPoint;
    for(const auto id:bside) {
        const auto edge=result.points[id].boundaryEdge;
        if(edge&&index->edgeSides(*edge).size()==2) {
            sharedPoint=id;
            break;
        }
    }
    check(sharedPoint.has_value(),
          "One B intersection endpoint must split a real two-face source edge");
    const auto edgePoint=*sharedPoint;
    const auto incidentEdge=*result.points[edgePoint].boundaryEdge;
    const auto incident=index->edgeSides(incidentEdge);
    const auto findFace=[&](FaceId id)->const ConformingCutFace& {
        const auto it=std::find_if(result.faces.begin(),result.faces.end(),
            [&](const auto& face){return face.sourceFace==id;});
        check(it!=result.faces.end(),"Conforming neighbor face exists");
        return *it;
    };
    for(const auto& side:incident) {
        bool present=false;
        const auto& split=findFace(side.face);
        for(const auto tri:split.triangles)
            if(std::find(tri.begin(),tri.end(),edgePoint)!=tri.end())
                present=true;
        check(present,"All incident faces share the SAME ephemeral split-point index");
    }
    check(findFace(fixture.faces[2]).triangles.size()>1,
          "Noncolliding neighboring face is conformingly subdivided");
    for(const auto& cut:result.faces) {
        const auto ids=index->face(cut.sourceFace).vertices;
        const auto a=index->vertex(ids[0]).position,
                   b=index->vertex(ids[1]).position,
                   c=index->vertex(ids[2]).position;
        const auto normal=cross(b-a,c-a);
        double total=0;
        for(const auto face:cut.triangles) {
            const auto p=result.points[face[0]].position,
                       q=result.points[face[1]].position,
                       r=result.points[face[2]].position;
            const auto signedArea=dot(cross(q-p,r-p),normal)/length(normal);
            check(signedArea>0,"All globally indexed face patches retain positive winding");
            total+=signedArea;
        }
        check(std::abs(total-length(normal))<1e-8,
              "Each reconstructed source/neighbor face conserves its original area");
    }
    const auto refuses=[&](ConformingCutLimits limits) {
        bool exhausted=false;
        try {(void)buildConformingCutPlan(*index,pairs,limits);}
        catch(const std::length_error&) {exhausted=true;}
        return exhausted;
    };
    ConformingCutLimits limits;
    limits.maxContacts=0;
    check(refuses(limits),"Zero pair budget refuses before private construction");
    limits={};limits.maxAffectedFaces=2;
    check(refuses(limits),"Plan refuses one fewer than needed incident faces");
    limits={};limits.maxNewPoints=3;
    check(refuses(limits),"Plan refuses insufficient newly generated private vertices");
    limits={};limits.maxTriangles=2;
    check(refuses(limits),"Plan refuses insufficient total reconstructed subtriangles");
    auto ambiguous=pairs;ambiguous[0].ambiguous=true;
    const auto refused=buildConformingCutPlan(*index,ambiguous);
    check(refused.status==IntersectionSectionStatus::UnsupportedContact&&
          refused.points.empty()&&refused.faces.empty()&&
          refused.segments.empty(),
          "Ambiguous batch refuses without returning partial topology");
    auto repeated=pairs;repeated.push_back(pairs.front());
    check(buildConformingCutPlan(*index,repeated).status==
          IntersectionSectionStatus::UnsupportedContact,
          "Multiple sections sharing a source face require a later global polygon arrangement");
    check(index->pickingRootIdentity()==originalRoot&&
          index->retiredFaceCount()==0,
          "Building and refusing cut graphs never mutate held immutable revision/index roots");
    (void)bface;
}
void testConformingReflectedCutGraph() {
    const auto fixture=conformingFixture(true);
    const auto index=RegionTopologyIndex::build(fixture.import.revision);
    const auto all=index->triangleIntersections(
        fixture.import.flatIndex.vertexIds,{});
    const auto reflectedFirst=fixture.import.flatIndex.faceIds[3],
               reflectedSecond=fixture.import.flatIndex.faceIds[4];
    const auto select=[&](FaceId a,FaceId b) {
        const auto it=std::find_if(all.begin(),all.end(),
            [&](const auto& p){
                const auto ordered=std::minmax(a,b);
                return p.first==ordered.first&&p.second==ordered.second;
            });
        check(it!=all.end(),"Reflected triangle pair is a real V13 intersection");
        return *it;
    };
    const std::vector<RegionTopologyIndex::CollisionPair> pairs{
        select(fixture.faces[0],fixture.faces[1]),
        select(reflectedFirst,reflectedSecond)};
    std::unordered_map<VertexId,VertexId> reflection;
    for(const auto& [a,b]:fixture.reflections) {
        reflection.emplace(a,b);
        reflection.emplace(b,a);
    }
    const auto result=buildConformingCutPlan(*index,pairs,{},&reflection);
    if(result.status!=IntersectionSectionStatus::TransverseSegment||
       result.faces.size()!=6||result.segments.size()!=2||
       result.mirroredCutPoints.size()!=4)
        throw std::runtime_error(
            "Reflected conformance failure: "+result.refusal+
            " faces="+std::to_string(result.faces.size())+
            " sections="+std::to_string(result.segments.size())+
            " mirrorPairs="+std::to_string(result.mirroredCutPoints.size()));
    std::set<std::size_t> mirrored;
    for(const auto& [a,b]:result.mirroredCutPoints) {
        check(a!=b&&mirrored.insert(a).second&&mirrored.insert(b).second,
              "Reflected off-plane cut points form unique reciprocal pairs");
        const auto p=result.points[a].position,q=result.points[b].position;
        check(p.x==-q.x&&p.y==q.y&&p.z==q.z,
              "Mirrored cut section coordinates are EXACT reflected values");
    }
    // Missing just one original vertex pair cannot authorize a partly
    // reflected cut; every generated partner must be matched.
    reflection.erase(fixture.reflections.front().first);
    const auto refused=buildConformingCutPlan(*index,pairs,{},&reflection);
    check(refused.status==IntersectionSectionStatus::UnsupportedContact&&
          refused.points.empty()&&refused.faces.empty(),
          "Incomplete original reflection map refuses all candidate cut topology");
}
void testPrivateConformingRevisionCommitsAtomically() {
    // Two fully CLOSED tetrahedra, unlike the deliberately open sheet
    // fixtures used for the read-only graph. RegionEdit validates every
    // affected vertex fan and opposing edge orientation at commit time.
    Mesh mesh;
    for(const auto p:std::array<Vec3,8>{{
        {0,0,0},{2,0,0},{0,2,0},
        {.7,-.5,-1},{.7,1.7,1},{.7,.2,-1},
        {.4,.4,-3},{3,.5,0}
    }})mesh.vertices.push_back(p);
    for(const auto tri:std::array<std::array<std::uint32_t,3>,8>{{
        {0,1,2},{1,0,6},{2,1,6},{0,2,6},
        {3,4,5},{4,3,7},{5,4,7},{3,5,7}
    }})mesh.triangles.push_back({tri[0],tri[1],tri[2]});
    const auto imported=partitionFlatMesh(mesh,256);
    const auto baseline=RegionTopologyIndex::build(imported.revision);
    const auto sourceRoot=baseline->pickingRootIdentity();
    const auto counts=[&](const RegionTopologyIndex& index) {
        std::pair<std::size_t,std::size_t> result{};
        index.revision().forEachPartition(
            [&](PartitionId,const MeshPartition& partition) {
                result.first+=partition.vertices.size();
                result.second+=partition.faces.size();
            });
        return result;
    };
    const auto originalCounts=counts(*baseline);
    check(originalCounts==std::make_pair(std::size_t{8},std::size_t{8}),
          "Private candidate fixture has two closed four-face tetrahedra");
    const auto all=baseline->triangleIntersections(
        imported.flatIndex.vertexIds,{});
    const auto& ids=imported.flatIndex.faceIds;
    const auto ordered=std::minmax(ids[0],ids[4]);
    const auto pair=std::find_if(all.begin(),all.end(),
        [&](const auto& p) {
            return p.first==ordered.first&&p.second==ordered.second;
        });
    check(pair!=all.end(),
          "Closed tetrahedra expose an actual transverse original triangle contact");
    const std::vector<RegionTopologyIndex::CollisionPair> contacts{*pair};
    const auto preflight=buildConformingCutPlan(*baseline,contacts);
    check(preflight.status==IntersectionSectionStatus::TransverseSegment,
          "V17e2b2a authorizes private cuts across closed tetrahedron surfaces");
    const auto candidate=materializeTransverseCutCandidate(baseline,contacts);
    check(candidate.index&&candidate.index!=baseline&&
          candidate.commit.revision&&
          candidate.commit.revision.get()==&candidate.index->revision()&&
          candidate.sectionFirst.size()==1&&candidate.sectionSecond.size()==1,
          "One RegionEdit creates exactly one unpublished immutable candidate");
    check(candidate.rewrittenFaces.size()==preflight.faces.size()&&
          candidate.insertedVertices.size()+
              originalCounts.first==counts(*candidate.index).first&&
          candidate.insertedFaces.size()+
              originalCounts.second==counts(*candidate.index).second,
          "Candidate tracks complete newly allocated vertex/face identities");
    for(const auto face:candidate.rewrittenFaces) {
        check(baseline->maybeFace(face)!=nullptr&&
              candidate.index->maybeFace(face)!=nullptr,
              "Original FaceId survives the private triangle subdivision");
    }
    for(const auto vertex:candidate.insertedVertices)
        check(!baseline->maybeVertex(vertex)&&
              candidate.index->maybeVertex(vertex)!=nullptr,
              "Each inserted cut vertex has a unique identity only in new revision");
    for(const auto face:candidate.insertedFaces)
        check(!baseline->maybeFace(face)&&
              candidate.index->maybeFace(face)!=nullptr,
              "Extra child triangles exist only in private revised root");
    for(unsigned end=0;end<2;++end) {
        const auto a=candidate.sectionFirst[0][end],
                   b=candidate.sectionSecond[0][end];
        check(a!=b&&
              length(candidate.index->vertex(a).position-
                     candidate.index->vertex(b).position)<1e-9,
              "Intersecting triangle sheets retain two distinct stable cut VertexIds");
    }
    check(counts(*baseline)==originalCounts&&
          baseline->retiredFaceCount()==0&&
          baseline->pickingRootIdentity()==sourceRoot,
          "Private conforming RegionEdit leaves baseline renderer/picker roots intact");
    // This is subdivision, NOT UNION. It must not be advertised as a
    // collision-free final solid or published by any repository CAS.
    check(!candidate.index->triangleIntersections(
                candidate.insertedVertices,{}).empty(),
          "Unclassified private intersection subdivisions still retain actual collisions");
    check(candidate.originalIndex==baseline&&
          candidate.childSources.size()==candidate.rewrittenFaces.size()+
                                          candidate.insertedFaces.size(),
          "Permanent real child FaceIds retain their exact original source provenance");
    const auto occupancy=assessConformingCutFragments(candidate);
    check((occupancy.complete&&
           occupancy.fragments.size()==candidate.childSources.size())||
          (!occupancy.complete&&occupancy.fragments.empty()&&
           !occupancy.refusal.empty()),
          "Real cut child occupancy is either complete or entirely fail-closed");
    if(occupancy.complete)
        for(const auto& fragment:occupancy.fragments)
            check(fragment.negativeWinding-fragment.positiveWinding==1,
                  "Uniformly sampled true child triangle has a unit winding jump");
    // The previous one-pair test deliberately selects only PART of the
    // original intersection graph: this new independent certificate must
    // refuse rather than treating a correct-looking section as complete.
    const auto coverage=certifyConformingCutCoverage(candidate);
    check(!coverage.complete&&coverage.coveredContacts.empty()&&
          !coverage.refusal.empty(),
          "Missing neighboring original contact refuses entire V17e2b2c2 graph");
    auto forgedCoverage=candidate;
    forgedCoverage.sourceContacts[0].ambiguous=true;
    const auto invalidCoverage=certifyConformingCutCoverage(forgedCoverage);
    check(!invalidCoverage.complete&&invalidCoverage.coveredContacts.empty(),
          "Forged ambiguous contact never permits coverage certification");
    CutCoverageLimits zeroCoverage;
    zeroCoverage.originalCollisionLimits.maxSeedFaces=0;
    bool originalBudget=false;
    try {(void)certifyConformingCutCoverage(candidate,zeroCoverage);}
    catch(const std::length_error&) {originalBudget=true;}
    check(originalBudget&&baseline->pickingRootIdentity()==sourceRoot,
          "Original contact rediscovery has a strict internal work limit");
    zeroCoverage={};zeroCoverage.maxContacts=0;
    bool zeroContacts=false;
    try {(void)certifyConformingCutCoverage(candidate,zeroCoverage);}
    catch(const std::length_error&) {zeroContacts=true;}
    check(zeroContacts,"Declared contact count has a strict coverage limit");
    auto wrongEndpoint=candidate;
    wrongEndpoint.sectionFirst[0][0]=0;
    const auto missingEnd=certifyConformingCutCoverage(wrongEndpoint);
    check(!missingEnd.complete&&missingEnd.coveredContacts.empty(),
          "Missing section endpoint cannot create a valid edge trace");
    // Exercise the full ORIGINAL contact graph, not the selected subset.
    // This is the positive case only if V17's bounded arranger supports
    // the complete transverse graph for this closed two-tetra fixture.
    {
        ConformingCutCandidate whole;
        try {whole=materializeTransverseCutCandidate(baseline,all);}
        catch(const std::logic_error& error) {
            throw std::runtime_error(
                std::string("Complete cut fixture unsupported: ")+error.what()+
                " (original pairs="+std::to_string(all.size())+")");
        }
        const auto checked=certifyConformingCutCoverage(whole);
        if(!checked.complete)
            throw std::runtime_error(
                "Complete cut fixture coverage refused: "+checked.refusal+
                " (original pairs="+std::to_string(all.size())+")");
        check(checked.checkedContacts==all.size()&&
              checked.coveredContacts.size()==all.size()&&
              checked.checkedChildren==whole.childSources.size(),
              "All true original contacts have actual source-paired child edge traces");
        // The default V15 approximate frustum/cap prefilter can refuse
        // strongly nonplanar tetrahedral cut loops. This explicit,
        // finite test-only contour envelope exercises the legitimate
        // positive nomination WITHOUT bypassing the V15 topology checks
        // or changing the conservative production defaults. V16 remains
        // responsible for actually validating/closing the surface.
        CutRemovalSettings supportedContours;
        supportedContours.contours.maximumPlanarityInRadii=4.0;
        supportedContours.contours.maximumSingleCapPlanarityInRadii=4.0;
        supportedContours.contours.maximumIsoperimetricRatio=20.0;
        supportedContours.contours.maximumSingleCapIsoperimetricRatio=20.0;
        supportedContours.contours.maximumPerimeterRatio=20.0;
        supportedContours.contours.maximumGapInEdges=20.0;
        supportedContours.contours.minimumNormalAlignment=0;
        const auto strict=planExactCutRemoval(whole);
        check(strict.ready()||strict.proposedRemoval.empty(),
              "Default V15 contour-shape refusal returns no removal permission");
        ExactCutJoinLimits joinLimits;
        joinLimits.removal=supportedContours;
        const auto joined=joinCertifiedCutSections(whole,joinLimits);
        std::cout<<"[seam-join] complete="<<joined.complete
                 <<" reason="<<joined.refusal
                 <<" boundaries="<<joined.checkedBoundaryEdges
                 <<" welds="<<joined.weldedPairs.size()<<'\n';
        if(!joined.complete)
            throw std::runtime_error("Real positive cut-seam join refused: "+joined.refusal);
        if(joined.complete) {
            check(joined.commit&&joined.index&&
                  joined.commit->revision.get()==&joined.index->revision()&&
                  joined.index!=whole.index&&joined.index!=baseline&&
                  joined.checkedContacts==all.size(),
                  "Actual provenance-bound closed-shell union commits only one private COW root");
            check(connectedClosedSurfaceComponents(*baseline)==2&&
                  connectedClosedSurfaceComponents(*whole.index)==2&&
                  connectedClosedSurfaceComponents(*joined.index)==1,
                  "Two distinct original closed shells become ONE connected manifold union");
            check(joined.retiredFaces.size()==16&&
                  joined.checkedBoundaryEdges>0&&
                  !joined.weldedPairs.empty(),
                  "True cut-curve join retires exact hidden fragments and welds independent sheets");
            check(baseline->pickingRootIdentity()==sourceRoot,
                  "Genuine unpublished union leaves historical picking root untouched");
        }else
            check(!joined.index&&!joined.commit&&
                  joined.retiredFaces.empty()&&joined.weldedPairs.empty(),
                  "Unsupported section-chain geometry refuses without exposing partial union");
        auto missingSection=whole;
        missingSection.sectionFirst.pop_back();
        const auto missingJoin=joinCertifiedCutSections(missingSection,joinLimits);
        check(!missingJoin.complete&&!missingJoin.index&&
              !missingJoin.commit,
              "Missing contact-edge provenance forbids a partial weld");
        auto capped=joinLimits;
        capped.maxBoundaryEdges=1;
        const auto budgetJoin=joinCertifiedCutSections(whole,capped);
        check(!budgetJoin.complete&&!budgetJoin.index&&
              !budgetJoin.commit,
              "Insufficient local seam edge budget returns no partial joined revision");
        // The public V17e2b2d3 preparation returns the ORIGINAL expected
        // pointer and ONE terminal root; intermediate cut and join roots
        // never enter publication, history, checkpoints or render picking.
        ExactCutStepLimits stepLimits;
        stepLimits.join=joinLimits;
        const auto prepared=prepareExactCutStep(baseline,all,stepLimits);
        if(!prepared.complete)
            throw std::runtime_error(
                "Original-root net Boolean Step refused: "+prepared.refusal);
        check(prepared.expected==baseline&&prepared.final&&
              prepared.final!=whole.index&&prepared.final!=baseline&&
              prepared.commit.revision.get()==&prepared.final->revision()&&
              prepared.addedMirrors.empty()&&prepared.removedMirrors.empty()&&
              prepared.dirtyPartitions.size()>0&&
              prepared.finalCollisionSeeds>0&&
              prepared.checkedProtectedFaces>0,
              "One Step has original CAS expectation and final immutable root only");
        check(connectedClosedSurfaceComponents(*prepared.final)==1,
              "Composed original-root Step is a truly connected closed union");
        const auto countLive=[&](const RegionTopologyIndex& index) {
            std::pair<std::size_t,std::size_t> result{};
            index.revision().forEachPartition(
                [&](PartitionId,const MeshPartition& p) {
                    result.first+=p.vertices.size();result.second+=p.faces.size();
                });
            return result;
        };
        const auto beforeCounts=countLive(*baseline),
                   afterCounts=countLive(*prepared.final);
        check(static_cast<std::ptrdiff_t>(afterCounts.first)-
                  static_cast<std::ptrdiff_t>(beforeCounts.first)==
                  prepared.vertexCountDelta&&
              static_cast<std::ptrdiff_t>(afterCounts.second)-
                  static_cast<std::ptrdiff_t>(beforeCounts.second)==
                  prepared.faceCountDelta,
              "Net journals balance actual root-level live vertex/face counts");
        for(const auto id:prepared.addedVertices)
            check(!baseline->maybeVertex(id)&&prepared.final->maybeVertex(id),
                  "Net inserted vertex exists ONLY at committed final root");
        for(const auto id:prepared.addedFaces)
            check(!baseline->maybeFace(id)&&prepared.final->maybeFace(id),
                  "Net inserted face exists ONLY at committed final root");
        for(const auto id:prepared.retiredFaces)
            check(baseline->maybeFace(id)&&!prepared.final->maybeFace(id)&&
                  prepared.final->retiredFace(id),
                  "Net retired original face maintains permanent ID tombstone");
        for(const auto id:prepared.rewrittenFaces)
            check(baseline->maybeFace(id)&&prepared.final->maybeFace(id)&&
                  baseline->face(id).vertices!=prepared.final->face(id).vertices,
                  "A net rewritten FaceId differs from its original geometry");
        for(const auto id:prepared.retiredVertices)
            check(baseline->maybeVertex(id)&&!prepared.final->maybeVertex(id)&&
                  prepared.final->retiredVertex(id),
                  "Net retired original vertex maintains permanent ID tombstone");
        baseline->revision().forEachPartition(
            [&](PartitionId owner,const MeshPartition&) {
                if(!std::binary_search(prepared.dirtyPartitions.begin(),
                                        prepared.dirtyPartitions.end(),owner))
                    check(baseline->revision().partition(owner)==
                          prepared.final->revision().partition(owner),
                          "Untouched original COW partition has identical pointer");
            });
        IndexedMeshRepository publication(baseline);
        const auto historical=publication.snapshot();
        check(historical==baseline&&
              publication.publishIfCurrent(prepared.expected,prepared.final)&&
              publication.snapshot()==prepared.final&&
              historical->pickingRootIdentity()==sourceRoot,
              "ONE original-root CAS publishes final Boolean without intermediate root");
        check(!publication.publishIfCurrent(prepared.expected,whole.index)&&
              publication.snapshot()==prepared.final,
              "Stale original-root CAS cannot restore a cut-only intermediate");
        IndexedMeshRepository competed(baseline);
        RegionEdit competingEdit(baseline,{imported.flatIndex.vertexIds.front()});
        const auto movedId=imported.flatIndex.vertexIds.front();
        const auto beforePoint=baseline->vertex(movedId).position;
        competingEdit.moveVertex(movedId,
            {beforePoint.x,beforePoint.y,beforePoint.z+.01});
        (void)competingEdit.commit();
        const auto competingIndex=competingEdit.updatedIndex();
        check(competed.publishIfCurrent(baseline,competingIndex)&&
              !competed.publishIfCurrent(prepared.expected,prepared.final)&&
              competed.snapshot()==competingIndex,
              "Concurrent winner makes entire prepared union refuse CAS atomically");
        RevisionHistory history(baseline,8,32*1024*1024,nullptr,
                                beforeCounts.first,beforeCounts.second);
        history.append(prepared.final,prepared.dirtyPartitions,
                       prepared.addedMirrors,prepared.removedMirrors,
                       prepared.copiedPayloadBytes,
                       afterCounts.first,afterCounts.second);
        check(history.current()==prepared.final&&
              history.statistics().undoDepth==1,
              "Composed cut plus join creates exactly ONE undo history Step");
        const auto undo=history.undo();
        check(undo.index==baseline&&undo.vertices==beforeCounts.first&&
              undo.faces==beforeCounts.second&&
              undo.dirtyPartitions==prepared.dirtyPartitions,
              "ONE undo returns to exact original picking and geometric revision");
        const auto redo=history.redo();
        check(redo.index==prepared.final&&
              redo.vertices==afterCounts.first&&
              redo.faces==afterCounts.second,
              "ONE redo returns to exact connected Boolean union revision");
        const auto path=std::filesystem::temp_directory_path()/
            "mesher_v17_exact_cut_step_unmirrored.mshr";
        writeCheckpoint(path,{prepared.final,{},beforeCounts.first});
        const auto restored=readCheckpoint(path);
        std::filesystem::remove(path);
        check(countLive(*restored.index)==afterCounts&&
              restored.index->retiredFaceIds()==prepared.final->retiredFaceIds()&&
              restored.index->retiredVertexIds()==prepared.final->retiredVertexIds()&&
              connectedClosedSurfaceComponents(*restored.index)==1,
              "One published net union survives durable checkpoint/restore");
        // A valid private contact pair subset still is NOT authority to
        // publish an incomplete cut of the original intersection graph.
        const std::vector<RegionTopologyIndex::CollisionPair> partial{
            all.front()};
        const auto refusedPartial=prepareExactCutStep(
            baseline,partial,stepLimits);
        check(!refusedPartial.complete&&!refusedPartial.final&&
              !refusedPartial.commit.revision&&
              refusedPartial.addedVertices.empty(),
              "Partial original contact graph cannot be published as one Step");
        auto workCap=stepLimits;
        workCap.maxDirtyPartitions=0;
        const auto refused=prepareExactCutStep(baseline,all,workCap);
        check(!refused.complete&&!refused.final&&!refused.commit.revision&&
              refused.dirtyPartitions.empty()&&refused.addedFaces.empty()&&
              publication.snapshot()==prepared.final,
              "Budget refusal cannot leak net journal or replace published union");
        const auto nomination=planExactCutRemoval(whole,supportedContours);
        std::cout<<"[exact-removal-probe] status="
                 <<static_cast<int>(nomination.status)
                 <<" reason="<<nomination.refusal
                 <<" removed="<<nomination.proposedRemoval.size()
                 <<" protected="<<nomination.protectedChildren.size()
                 <<'\n';
        if(!nomination.ready())
            throw std::runtime_error(
                "Full-contact exact-child nomination refused: "+
                nomination.refusal+" (status="+
                std::to_string(static_cast<int>(nomination.status))+")");
        if(nomination.ready()) {
            check(!nomination.proposedRemoval.empty()&&
                  !nomination.protectedChildren.empty(),
                  "Exact-child policy must both remove redundant material and preserve exposed boundaries");
            check(nomination.contourPlan.ready()&&
                  nomination.contourPlan.removedFaces==
                      nomination.proposedRemoval,
                  "Accepted face nominations exactly match the V15 private contour set");
            for(const auto id:nomination.protectedChildren)
                check(!std::binary_search(
                    nomination.proposedRemoval.begin(),
                    nomination.proposedRemoval.end(),id),
                    "Cavity and exterior boundary FaceIds are never nominated");
        }else {
            check(nomination.proposedRemoval.empty()||
                  nomination.status==CutRemovalStatus::NoChanges,
                  "Any refusal leaves no partial deletion list");
        }
        auto stale=whole;
        stale.sourceContacts.pop_back();
        stale.sectionFirst.pop_back();
        stale.sectionSecond.pop_back();
        const auto rejected=planExactCutRemoval(stale);
        check(rejected.status==CutRemovalStatus::Refused&&
              rejected.proposedRemoval.empty()&&
              rejected.protectedChildren.empty(),
              "Incomplete original contact graph refuses without partial face nomination");
        // V17e2b2d1 never turns V15's Ready into live publication. It
        // performs the existing V16 closure and all final audits PRIVATELY,
        // refusing unsupported independent-sheet seam topology.
        ExactCutSurgerySettings gated;
        gated.removal=supportedContours;
        gated.surgery.planning=supportedContours.contours;
        const auto surgery=attemptPrivateExactCutSurgery(whole,gated);
        std::cout<<"[exact-surgery-probe] status="
                 <<static_cast<int>(surgery.status)
                 <<" reason="<<surgery.refusal
                 <<" selected="<<surgery.selectedFaces<<'\n';
        if(surgery.status==ExactCutSurgeryStatus::PrivateValidated) {
            check(surgery.surgery.has_value()&&
                  surgery.surgery->index!=baseline&&
                  surgery.surgery->index!=whole.index&&
                  surgery.checkedFinalContacts==0&&
                  surgery.selectedFaces==nomination.proposedRemoval.size()&&
                  surgery.protectedFaces==nomination.protectedChildren.size(),
                  "Private final repair preserves original and subdivided readers");
        }else
            check(surgery.status==ExactCutSurgeryStatus::Refused&&
                  !surgery.surgery&&
                  !surgery.refusal.empty(),
                  "Unsupported V16 seam repair refuses with NO private root");
        auto counterfeit=whole;
        counterfeit.sourceContacts.pop_back();
        counterfeit.sectionFirst.pop_back();
        counterfeit.sectionSecond.pop_back();
        auto mismatchedPlanning=gated;
        mismatchedPlanning.surgery.planning.maximumGapInEdges+=.25;
        const auto inconsistent=attemptPrivateExactCutSurgery(
            whole,mismatchedPlanning);
        check(inconsistent.status==ExactCutSurgeryStatus::Refused&&
              !inconsistent.surgery,
              "V16 cannot re-plan surgery with different V15 contour thresholds");
        const auto staleSurgery=attemptPrivateExactCutSurgery(
            counterfeit,gated);
        check(staleSurgery.status==ExactCutSurgeryStatus::Refused&&
              !staleSurgery.surgery,
              "Missing original contact cannot bypass c3 through V16 adapter");
        gated.maxFinalCollisionFaces=0;
        const auto invalidBudget=attemptPrivateExactCutSurgery(whole,gated);
        check(invalidBudget.status==ExactCutSurgeryStatus::Refused&&
              !invalidBudget.surgery,
              "Invalid final query budget cannot expose an intermediate root");
        CutRemovalSettings tight;
        tight.coverage.originalCollisionLimits.maxSeedFaces=0;
        const auto limited=planExactCutRemoval(whole,tight);
        check(limited.status==CutRemovalStatus::BudgetExceeded&&
              limited.proposedRemoval.empty(),
              "Exhausted V13 work limit refuses the complete proposed removal");

        check(baseline->pickingRootIdentity()==sourceRoot,
              "Complete full-contact coverage preserves the immutable original root");
        check(checked.discoveryWork.reportedContacts==all.size()&&
              checked.checkedContacts==checked.coveredContacts.size()&&
              checked.edgeTraceTests>0&&checked.childPairTests>0,
              "Coverage independently visits every original collision and both edge traces");
        auto truncated=whole;
        truncated.sourceContacts.pop_back();
        truncated.sectionFirst.pop_back();
        truncated.sectionSecond.pop_back();
        const auto missing=certifyConformingCutCoverage(truncated);
        check(!missing.complete&&missing.coveredContacts.empty(),
              "Dropping one actual original contact invalidates complete certification");
        auto wrong=whole;
        wrong.sectionFirst.front()[0]=0;
        const auto missingPoint=certifyConformingCutCoverage(wrong);
        check(!missingPoint.complete&&missingPoint.coveredContacts.empty(),
              "Malformed permanent section endpoint cannot produce certification");
        CutCoverageLimits hard;
        hard.maxEdgeTraceTests=0;
        bool edgeCap=false;
        try {(void)certifyConformingCutCoverage(whole,hard);}
        catch(const std::length_error&) {edgeCap=true;}
        check(edgeCap&&baseline->pickingRootIdentity()==sourceRoot,
              "Zero child edge trace budget refuses without publishing or truncating");
        hard={};hard.maxChildPairTests=0;
        bool childCap=false;
        try {(void)certifyConformingCutCoverage(whole,hard);}
        catch(const std::length_error&) {childCap=true;}
        check(childCap,"Child plane and overlap predicate work is bounded");
        hard={};hard.originalCollisionLimits.maxReportedContacts=0;
        bool collisionCap=false;
        try {(void)certifyConformingCutCoverage(whole,hard);}
        catch(const std::length_error&) {collisionCap=true;}
        check(collisionCap,"Original contact count is capped inside BVH discovery");
    }

    // A forged source FaceId cannot influence deletion through classification.
    auto forged=candidate;
    forged.childSources.front().second=0;
    const auto invalid=assessConformingCutFragments(forged);
    check(!invalid.complete&&invalid.fragments.empty(),
          "A forged or stale original face provenance refuses all fragment labels");

    bool budgetFailed=false;
    try {(void)materializeTransverseCutCandidate(
        baseline,contacts,{},1);}
    catch(const std::length_error&) {budgetFailed=true;}
    check(budgetFailed&&counts(*baseline)==originalCounts,
          "Insufficient writable partition capacity never leaks partial topology");
    auto refused=contacts;
    refused[0].ambiguous=true;
    bool ambiguousFailed=false;
    try {(void)materializeTransverseCutCandidate(baseline,refused);}
    catch(const std::logic_error&) {ambiguousFailed=true;}
    check(ambiguousFailed&&counts(*baseline)==originalCounts,
          "Ambiguous input refuses private topology without changing published root");
    const std::unordered_map<VertexId,VertexId> mirrored;
    bool mirroredFailed=false;
    try {(void)materializeTransverseCutCandidate(
        baseline,contacts,{},256,&mirrored);}
    catch(const std::logic_error&) {mirroredFailed=true;}
    check(mirroredFailed,
          "Mirrored persistent face publication refuses until matched-pair staging exists");
    check(baseline->pickingRootIdentity()==sourceRoot,
          "All failures preserve exact original picking index object graph");
}

void testCrossingMultipleSectionsPerOriginalTriangle() {
    Mesh mesh;
    // A's original horizontal triangle is traversed by two genuinely
    // transverse V13 sections, one at X=.7, one at Y=.7. They cross
    // strictly within the face at (.7,.7,0). Each crossing solid
    // remains topologically distinct until classified Boolean repair.
    for(const auto p:std::array<Vec3,12>{{
        {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
        {.7,-2,-1},{.7,3,-1},{.7,.5,2},{2.5,.5,3},
        {-2,.7,-1},{3,.7,-1},{.5,.7,2},{.5,2.5,3}
    }})mesh.vertices.push_back(p);
    for(unsigned solid=0;solid<3;++solid) {
        const auto v=4u*solid;
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})mesh.triangles.push_back({
            v+t[0],v+t[1],v+t[2]});
    }
    const auto imported=partitionFlatMesh(mesh,256);
    const auto baseline=RegionTopologyIndex::build(imported.revision);
    const auto initialRoot=baseline->pickingRootIdentity();
    const auto all=baseline->triangleIntersections(
        imported.flatIndex.vertexIds,{});
    const auto lookup=[&](FaceId a,FaceId b) {
        const auto pair=std::minmax(a,b);
        const auto found=std::find_if(all.begin(),all.end(),
            [&](const auto& item) {
                return item.first==pair.first&&item.second==pair.second;
            });
        check(found!=all.end(),"V13 reports both true original transverse contacts");
        return *found;
    };
    const auto& faces=imported.flatIndex.faceIds;
    const std::vector<RegionTopologyIndex::CollisionPair> contacts{
        lookup(faces[0],faces[4]),lookup(faces[0],faces[8])};
    const auto plan=buildConformingCutPlan(*baseline,contacts);
    if(plan.status!=IntersectionSectionStatus::TransverseSegment)
        throw std::runtime_error("Multi-section arrangement: "+plan.refusal);
    check(plan.segments.size()==2&&plan.faces.size()>3,
          "Crossing curves include both source sheets and incident face neighbors");
    std::optional<std::size_t> junction;
    for(std::size_t i=0;i<plan.points.size();++i) {
        const auto& point=plan.points[i];
        if(point.owningFace==faces[0]&&!point.boundaryEdge&&
           !point.existingVertex&&
           length(point.position-Vec3{.7,.7,0})<1e-8) {
            check(!junction,"Crossing section junction must have ONE local index");
            junction=i;
        }
    }
    check(junction.has_value(),
          "Actual interior crossing becomes a shared constrained node");
    const auto cutA=std::find_if(plan.faces.begin(),plan.faces.end(),
        [&](const auto& part){return part.sourceFace==faces[0];});
    check(cutA!=plan.faces.end(),"Original twice-cut triangle is triangulated");
    unsigned junctionTriangles=0;
    for(const auto t:cutA->triangles)
        if(std::find(t.begin(),t.end(),*junction)!=t.end())
            ++junctionTriangles;
    check(junctionTriangles>=4,
          "Intersection-curve junction has a complete nonoverlapping oriented face fan");

    const auto privateRevision=materializeTransverseCutCandidate(
        baseline,contacts);
    check(privateRevision.index&&privateRevision.index!=baseline&&
          privateRevision.sectionFirst.size()==2&&
          privateRevision.sectionSecond.size()==2,
          "Multiple real contact pairs materialize in ONE unpublished RegionEdit");
    check(privateRevision.insertedVertices.size()>4&&
          privateRevision.insertedFaces.size()>8,
          "All face boundaries, intersections and adjacent incident triangles are subdivided");
    const auto released=baseline->maybeFace(faces[0]);
    check(released&&privateRevision.index->maybeFace(faces[0])&&
          baseline->pickingRootIdentity()==initialRoot&&
          baseline->retiredFaceCount()==0,
          "Old immutable original face and picking-root identity survive multi-curve staging");
    check(!privateRevision.index->triangleIntersections(
        privateRevision.insertedVertices,{}).empty(),
        "Multiple-curve subdivision retains collision until classified removal");
    const auto multiCoverage=certifyConformingCutCoverage(privateRevision);
    check(!multiCoverage.complete&&multiCoverage.coveredContacts.empty()&&
          !multiCoverage.refusal.empty(),
          "Two listed contacts on triple sheets are not a complete global cut graph");
    const auto emptyTrace=[&] {
        auto tampered=privateRevision;
        tampered.sectionSecond[0][0]=0;
        return certifyConformingCutCoverage(tampered);
    }();
    check(!emptyTrace.complete&&emptyTrace.coveredContacts.empty(),
          "Multi-section cut cannot certify an erased second-sheet trace");

    ConformingCutLimits limits;
    limits.maxArrangementPairTests=0;
    bool testsExceeded=false;
    try {(void)materializeTransverseCutCandidate(
        baseline,contacts,limits);}
    catch(const std::length_error&) {testsExceeded=true;}
    check(testsExceeded&&baseline->pickingRootIdentity()==initialRoot,
          "Zero arrangement-pair budget fails without publishing partial changes");
    limits={};
    limits.maxArrangementEdges=2;
    bool edgesExceeded=false;
    try {(void)materializeTransverseCutCandidate(
        baseline,contacts,limits);}
    catch(const std::length_error&) {edgesExceeded=true;}
    check(edgesExceeded,"Bounded arrangement graph edges refuse over-budget work");
    limits={};
    limits.maxArrangementPredicates=0;
    bool predicatesExceeded=false;
    try {(void)materializeTransverseCutCandidate(
        baseline,contacts,limits);}
    catch(const std::length_error&) {predicatesExceeded=true;}
    check(predicatesExceeded,
          "Zero visibility/ear predicate budget refuses before an incomplete revision");

    const auto malformed=std::vector<RegionTopologyIndex::CollisionPair>{
        contacts[0],contacts[0]};
    const auto refused=buildConformingCutPlan(*baseline,malformed);
    check(refused.status==IntersectionSectionStatus::UnsupportedContact&&
          refused.points.empty()&&refused.faces.empty(),
          "Duplicate transverse constraint cannot leak a partial arrangement");
    const std::unordered_map<VertexId,VertexId> fakeMirror;
    const auto unsupported=buildConformingCutPlan(
        *baseline,contacts,{},&fakeMirror);
    check(unsupported.status==IntersectionSectionStatus::UnsupportedContact&&
          unsupported.points.empty(),
          "Unsupported multi-curve mirror staging remains explicitly fail-closed");
}

void testSectionsMeetAtInteriorTjunction() {
    Mesh mesh;
    // B crosses all of A at X=.7, while C's transverse section
    // terminates exactly on B's section at (.7,.7,0).
    for(const auto p:std::array<Vec3,12>{{
        {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
        {.7,-2,-1},{.7,3,-1},{.7,.5,2},{2.5,.5,3},
        {-1,.7,-1},{.7,.7,-1},{.7,.7,1},{.5,2.5,3}
    }})mesh.vertices.push_back(p);
    for(unsigned solid=0;solid<3;++solid) {
        const auto base=solid*4;
        for(const auto tri:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})mesh.triangles.push_back({
            base+tri[0],base+tri[1],base+tri[2]});
    }
    auto import=partitionFlatMesh(mesh,256);
    auto root=RegionTopologyIndex::build(import.revision);
    const auto contacts=root->triangleIntersections(
        import.flatIndex.vertexIds,{});
    const auto& f=import.flatIndex.faceIds;
    const auto lookup=[&](FaceId a,FaceId b) {
        const auto ordered=std::minmax(a,b);
        const auto it=std::find_if(contacts.begin(),contacts.end(),
            [&](const auto& p){return p.first==ordered.first&&
                                     p.second==ordered.second;});
        check(it!=contacts.end(),
              "T-junction is a real V13 transverse original-face contact");
        return *it;
    };
    const std::vector<RegionTopologyIndex::CollisionPair> selected{
        lookup(f[0],f[4]),lookup(f[0],f[8])};
    const auto plan=buildConformingCutPlan(*root,selected);
    if(plan.status!=IntersectionSectionStatus::TransverseSegment)
        throw std::runtime_error("Interior T-junction: "+plan.refusal);
    const auto center=std::find_if(plan.points.begin(),plan.points.end(),
        [&](const auto& point) {
            return point.owningFace==f[0]&&!point.boundaryEdge&&
                   !point.existingVertex&&
                   length(point.position-Vec3{.7,.7,0})<1e-8;
        });
    check(center!=plan.points.end(),
          "T-junction merges a face-local curve endpoint with a crossing node");
    const auto id=static_cast<std::size_t>(center-plan.points.begin());
    const auto face=std::find_if(plan.faces.begin(),plan.faces.end(),
        [&](const auto& p){return p.sourceFace==f[0];});
    check(face!=plan.faces.end(),"Original source face has a complete T arrangement");
    unsigned incidence=0;
    for(const auto tri:face->triangles)
        if(std::find(tri.begin(),tri.end(),id)!=tri.end())++incidence;
    check(incidence>=3,
          "Interior meeting point has a single shared fan, not a hanging vertex");
    const auto candidate=materializeTransverseCutCandidate(root,selected);
    check(candidate.index&&candidate.index!=root&&
          candidate.rewrittenFaces.size()==plan.faces.size(),
          "T-arranged sections materialize as one complete unpublished manifold revision");
}


// V17e2b2d4: admit COMPLETE original V13 contact graphs in the same
// closed three-solid crossing / T-junction fixtures previously used only
// with two hand-selected contacts. Never infer union readiness from the
// selected-face subdivision alone: the complete graph may contain triple
// intersections or topology which the current seam matcher must refuse.
// Two small independent outward protrusions cut separate, complete
// section loops in the SAME original triangle. Unlike a triple point,
// every V13 section is singly owned, so this is the first bounded d4
// multi-section full-union candidate.
void testSeparateCompleteSectionsOnOneOriginalFace() {
    Mesh mesh;
    for(const auto p:std::array<Vec3,12>{{
        {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
        // Each protrusion has two vertices above and two below A's
        // top face, so ALL FOUR of its original triangles are cut.
        // Nothing fully buried remains unclassified and unchanged.
        {.375,.375,.25},{.625,.375,.25},
        {.625,.625,-.25},{.375,.25,-.25},
        {1,.25,.25},{1.25,.25,.25},
        {1.25,.5,-.25},{1,.125,-.25}
    }})mesh.vertices.push_back(p);
    for(unsigned solid=0;solid<3;++solid) {
        const auto base=4u*solid;
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})mesh.triangles.push_back({
            base+t[0],base+t[1],base+t[2]});
    }
    const auto imported=partitionFlatMesh(mesh,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    const auto oldPicking=root->pickingRootIdentity();
    check(connectedClosedSurfaceComponents(*root)==3,
          "Three independent closed solids precede multiple-section union");
    const auto contacts=root->triangleIntersections(
        imported.flatIndex.vertexIds,{});
    std::cout<<"[separate-sections] original-contact-count="
             <<contacts.size()<<'\n';
    check(contacts.size()==8,
          "Two disjoint protrusions each have four full transverse top-face contact pairs");
    const auto top=imported.flatIndex.faceIds[0];
    for(const auto& pair:contacts)
        check(!pair.ambiguous&&(pair.first==top||pair.second==top),
              "Every full original contact is transverse on the common source face");
    const auto plan=buildConformingCutPlan(*root,contacts);
    if(plan.status!=IntersectionSectionStatus::TransverseSegment)
        throw std::runtime_error(
            "Separate complete sections cannot be arranged: "+plan.refusal);
    check(plan.segments.size()==contacts.size(),
          "No incomplete original contact is omitted from separate loop arrangements");
    CutRemovalSettings contours;
    contours.contours.maximumPlanarityInRadii=4.0;
    contours.contours.maximumSingleCapPlanarityInRadii=4.0;
    contours.contours.maximumIsoperimetricRatio=20.0;
    contours.contours.maximumSingleCapIsoperimetricRatio=20.0;
    contours.contours.maximumPerimeterRatio=20.0;
    // Restrict V15's hypothetical contour pairing to the genuinely
    // coincident loops; d2 later proves exact partner edges via V13.
    contours.contours.maximumGapInEdges=0.1;
    contours.contours.minimumNormalAlignment=0;
    ExactCutStepLimits limits;
    limits.join.removal=contours;
    const auto step=prepareExactCutStep(root,contacts,limits);
    std::cout<<"[separate-sections] accepted="<<step.complete
             <<" refusal="<<step.refusal<<'\n';
    if(!step.complete)
        throw std::runtime_error(
            "Disjoint multi-section closed union refused: "+step.refusal);
    check(step.expected==root&&step.final&&
          step.commit.revision.get()==&step.final->revision()&&
          connectedClosedSurfaceComponents(*step.final)==1&&
          !step.retiredFaces.empty()&&step.checkedProtectedFaces>0,
          "Two separate protrusions form one genuine manifold on one private Step");
    const auto partial=std::vector<RegionTopologyIndex::CollisionPair>(
        contacts.begin(),contacts.end()-1);
    const auto missing=prepareExactCutStep(root,partial,limits);
    check(!missing.complete&&!missing.final&&!missing.commit.revision,
          "Omitting one finite section cannot produce a partially welded Step");
    auto cap=limits;
    cap.join.maxTraceTests=1;
    const auto refused=prepareExactCutStep(root,contacts,cap);
    check(!refused.complete&&!refused.final&&!refused.commit.revision,
          "Multi-section tracing respects a hard work cap and refuses atomically");
    check(root->pickingRootIdentity()==oldPicking&&
          root->retiredFaceCount()==0&&
          connectedClosedSurfaceComponents(*root)==3,
          "Private multiple-seam joins never alter the original immutable root");
}

void testFullThreeBodyContactAdmission() {
    const auto exercise=[](bool tJunction) {
        Mesh mesh;
        const std::array<Vec3,12> positions=tJunction
            ?std::array<Vec3,12>{{
                {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
                {.7,-2,-1},{.7,3,-1},{.7,.5,2},{2.5,.5,3},
                {-1,.7,-1},{.7,.7,-1},{.7,.7,1},{.5,2.5,3}
            }}
            :std::array<Vec3,12>{{
                {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
                {.7,-2,-1},{.7,3,-1},{.7,.5,2},{2.5,.5,3},
                {-2,.7,-1},{3,.7,-1},{.5,.7,2},{.5,2.5,3}
            }};
        for(const auto p:positions)mesh.vertices.push_back(p);
        for(unsigned solid=0;solid<3;++solid) {
            const auto v=4u*solid;
            for(const auto t:std::array<std::array<unsigned,3>,4>{{
                {0,1,2},{1,0,3},{2,1,3},{0,2,3}
            }})mesh.triangles.push_back({
                v+t[0],v+t[1],v+t[2]});
        }
        const auto imported=partitionFlatMesh(mesh,256);
        const auto root=RegionTopologyIndex::build(imported.revision);
        const auto oldPicking=root->pickingRootIdentity();
        check(connectedClosedSurfaceComponents(*root)==3,
              "Triple-contact acceptance begins with three independent closed shells");
        const auto all=root->triangleIntersections(
            imported.flatIndex.vertexIds,{});
        check(all.size()>2,
              "Three-solid contact graph must contain more than two original pairs");
        const auto shared=imported.flatIndex.faceIds[0];
        check(std::count_if(all.begin(),all.end(),
            [&](const auto& p){return p.first==shared||p.second==shared;})>=2,
              "One original triangle meets both other closed shells");
        std::set<std::pair<FaceId,FaceId>> unique;
        for(const auto& pair:all)
            check(unique.emplace(pair.first,pair.second).second,
                  "Complete V13 query must not repeat original contact identities");
        std::cout<<"[three-body-contacts] kind="
                 <<(tJunction?"T-junction":"crossing")
                 <<" complete-pairs="<<all.size()<<'\n';

        CutRemovalSettings contours;
        contours.contours.maximumPlanarityInRadii=4.0;
        contours.contours.maximumSingleCapPlanarityInRadii=4.0;
        contours.contours.maximumIsoperimetricRatio=20.0;
        contours.contours.maximumSingleCapIsoperimetricRatio=20.0;
        contours.contours.maximumPerimeterRatio=20.0;
        contours.contours.maximumGapInEdges=20.0;
        contours.contours.minimumNormalAlignment=0;
        ExactCutStepLimits limits;
        limits.join.removal=contours;
        try {
            const auto cut=materializeTransverseCutCandidate(root,all);
            check(cut.originalIndex==root&&cut.index&&
                  cut.index!=root&&cut.sourceContacts.size()==all.size(),
                  "Full original graph remains private and retains every section");
            const auto c2=certifyConformingCutCoverage(cut);
            const auto c1=assessConformingCutFragments(cut);
            const auto c3=planExactCutRemoval(cut,contours);
            std::cout<<"[three-body-cut] kind="
                     <<(tJunction?"T-junction":"crossing")
                     <<" coverage="<<c2.complete
                     <<" coverage-reason="<<c2.refusal
                     <<" winding="<<c1.complete
                     <<" winding-reason="<<c1.refusal
                     <<" removal="<<c3.ready()
                     <<" removal-reason="<<c3.refusal<<'\n';
        }catch(const std::exception& error) {
            std::cout<<"[three-body-cut] kind="
                     <<(tJunction?"T-junction":"crossing")
                     <<" private-refusal="<<error.what()<<'\n';
        }
        // The ONLY publication authority is the composed original-root
        // Step. A full graph which cannot prove all prerequisites must
        // return no terminal root, net IDs, or CAS/history permission.
        const auto final=prepareExactCutStep(root,all,limits);
        std::cout<<"[three-body-step] kind="
                 <<(tJunction?"T-junction":"crossing")
                 <<" accepted="<<final.complete
                 <<" reason="<<final.refusal<<'\n';
        if(final.complete) {
            check(final.expected==root&&final.final&&
                  final.commit.revision.get()==&final.final->revision()&&
                  connectedClosedSurfaceComponents(*final.final)==1,
                  "Complete certified triple union is one closed connected root");
        }else
            check(!final.expected&&!final.final&&!final.commit.revision&&
                  final.addedVertices.empty()&&final.retiredVertices.empty()&&
                  final.addedFaces.empty()&&final.retiredFaces.empty()&&
                  final.rewrittenFaces.empty()&&final.dirtyPartitions.empty(),
                  "Unsupported complete triple graph refuses with no publishable Step");
        check(root->pickingRootIdentity()==oldPicking&&
              root->retiredFaceCount()==0&&
              connectedClosedSurfaceComponents(*root)==3,
              "Multi-section acceptance does not mutate old readers or original shells");
    };
    exercise(false);
    exercise(true);
}

// Unlike the historic three-tetrahedron crossing/T fixtures (which have
// grazing or otherwise unsupported original triangle pairs), this dyadic,
// generic full-solid arrangement separates all vertices/edges and tests
// three pairwise overlapping closed tetrahedra. A/B and A/C each make
// four transverse sections on A's original top triangle; B/C also meet.
void assertCanonicalTripleJunctions(
    const RegionTopologyIndex& root,
    const std::vector<RegionTopologyIndex::CollisionPair>& complete,
    const char* fixture) {
    const auto plan=buildConformingCutPlan(root,complete);
    if(plan.status!=IntersectionSectionStatus::TransverseSegment)
        throw std::runtime_error(
            std::string(fixture)+" canonical junction cut refused: "+plan.refusal);
    check(!plan.junctions.empty(),
          "Every actual triple-contact fixture has a source-proven junction");
    std::map<std::array<FaceId,3>,Vec3> positions;
    std::set<std::size_t> used;
    for(const auto& junction:plan.junctions) {
        check(std::is_sorted(junction.sourceFaces.begin(),
                             junction.sourceFaces.end())&&
              junction.sourceFaces[0]!=junction.sourceFaces[1]&&
              junction.sourceFaces[1]!=junction.sourceFaces[2],
              "Canonical junction has three distinct sorted original face identities");
        check(positions.emplace(junction.sourceFaces,junction.position).second,
              "A three-face source orbit has one physical point");
        for(std::size_t side=0;side<3;++side) {
            const auto id=junction.facePoints[side];
            check(id<plan.points.size()&&used.insert(id).second&&
                  !plan.points[id].existingVertex&&
                  !plan.points[id].boundaryEdge&&
                  plan.points[id].owningFace==junction.sourceFaces[side],
                  "Every independently intersecting sheet retains one distinct face-local node");
            const auto p=plan.points[id].position;
            check(p.x==junction.position.x&&p.y==junction.position.y&&
                  p.z==junction.position.z,
                  "All three independently owned junction points have exactly equal coordinates");
            const auto f=root.face(junction.sourceFaces[side]).vertices;
            const auto a=root.vertex(f[0]).position,
                       b=root.vertex(f[1]).position,
                       c=root.vertex(f[2]).position;
            const auto n=cross(b-a,c-a);
            const auto scale=std::max({length(b-a),length(c-a),length(c-b)});
            check(length(n)>0&&
                  std::abs(dot(junction.position-a,n))/length(n)<
                    1e-9*scale,
                  "Canonical source-proven point remains on each original supporting plane");
            const auto face=std::find_if(plan.faces.begin(),plan.faces.end(),
                [&](const ConformingCutFace& part){
                    return part.sourceFace==junction.sourceFaces[side];});
            check(face!=plan.faces.end(),"Canonical source face has complete cut children");
            unsigned incident=0;
            for(const auto tri:face->triangles)
                if(std::find(tri.begin(),tri.end(),id)!=tri.end())
                    ++incident;
            check(incident>=3,"Canonical interior junction has a complete oriented fan");
        }
        const auto pairs=std::array<std::pair<FaceId,FaceId>,3>{{
            {junction.sourceFaces[0],junction.sourceFaces[1]},
            {junction.sourceFaces[0],junction.sourceFaces[2]},
            {junction.sourceFaces[1],junction.sourceFaces[2]}}};
        for(const auto& [a,b]:pairs)
            check(std::any_of(plan.segments.begin(),plan.segments.end(),
               [&](const ConformingCutSegment& segment) {
                   return (segment.firstFace==a&&segment.secondFace==b)||
                          (segment.firstFace==b&&segment.secondFace==a);
               }),"Canonical triple point has all three finite V13 source pairs");
    }
    // The original V13 pair order cannot change source-plane identity
    // or the physical position of any junction.
    auto reversed=complete;
    std::reverse(reversed.begin(),reversed.end());
    const auto again=buildConformingCutPlan(root,reversed);
    if(again.status!=IntersectionSectionStatus::TransverseSegment||
       again.junctions.size()!=plan.junctions.size())
        throw std::runtime_error(
            std::string(fixture)+" reverse contact order refused/differed: "+
            again.refusal+" triples="+std::to_string(again.junctions.size())+
            " expected="+std::to_string(plan.junctions.size()));
    for(const auto& junction:again.junctions) {
        const auto found=positions.find(junction.sourceFaces);
        check(found!=positions.end()&&
              found->second.x==junction.position.x&&
              found->second.y==junction.position.y&&
              found->second.z==junction.position.z,
              "The three original source planes define one reproducible coordinate");
    }
    ConformingCutLimits tiny;
    tiny.maxJunctionCandidates=0;
    bool exhausted=false;
    try {
        const auto invalid=buildConformingCutPlan(root,complete,tiny);
        exhausted=invalid.status!=IntersectionSectionStatus::TransverseSegment;
    }catch(const std::length_error&) {exhausted=true;}
    check(exhausted,
          "Triple-junction enumeration exhausts its hard preflight budget before exposing a plan");
    std::cout<<"[canonical-junction] fixture="<<fixture
             <<" triples="<<plan.junctions.size()<<'\n';
}

// Construct a deliberately unevenly subdivided, still closed private
// cut source shell. The extra boundary midpoint is a REAL child-edge
// vertex on only one V13 sheet, introduced without editing the original
// solid or relying on a caller-forged section correspondence.
ConformingCutCandidate splitSingleCertifiedBoundaryForTest(
    const ConformingCutCandidate& cut,const CutRemovalSettings& contours) {
    const auto decision=planExactCutRemoval(cut,contours);
    check(decision.ready(),"Unequal-density fixture needs original c3 authorization");
    const std::set<FaceId> deleted(decision.proposedRemoval.begin(),
                                   decision.proposedRemoval.end());
    std::map<FaceId,FaceId> source;
    for(const auto& [id,origin]:cut.childSources)source[id]=origin;
    FaceId first{},second{};
    RegionTopologyIndex::Edge selected{};
    for(const auto removed:deleted) {
        const auto tri=cut.index->face(removed).vertices;
        for(unsigned j=0;j<3&&!first;++j) {
            const auto a=tri[j],b=tri[(j+1)%3];
            const RegionTopologyIndex::Edge edge{
                std::min(a,b),std::max(a,b)};
            const auto sides=cut.index->maybeEdgeSides(edge);
            if(!sides||sides->size()!=2)continue;
            for(const auto& side:*sides)
                if(!deleted.contains(side.face)&&source.contains(side.face)) {
                    first=removed;second=side.face;selected=edge;break;
                }
        }
        if(first)break;
    }
    check(first&&second,"Unequal-density fixture needs source-owned paired triangles");
    const auto p=cut.index->vertex(selected.a).position,
               q=cut.index->vertex(selected.b).position;
    const auto middle=(p+q)*0.5;
    std::set<VertexId> seeds;
    for(const auto face:{first,second}) {
        const auto tri=cut.index->face(face).vertices;
        seeds.insert(tri.begin(),tri.end());
    }
    RegionEdit edit(cut.index,{seeds.begin(),seeds.end()}, {},256);
    auto result=cut;
    const auto id=edit.appendLocalVertex(cut.index->vertex(selected.a).owner,middle);
    result.insertedVertices.push_back(id);
    for(const auto face:{first,second}) {
        const auto tri=cut.index->face(face).vertices;
        bool found=false;
        for(unsigned j=0;j<3;++j) {
            if(std::min(tri[j],tri[(j+1)%3])!=selected.a||
               std::max(tri[j],tri[(j+1)%3])!=selected.b)continue;
            const auto from=tri[j],to=tri[(j+1)%3],
                       opposite=tri[(j+2)%3];
            edit.replaceFace(face,{from,id,opposite});
            const auto child=edit.appendLocalFace(
                cut.index->face(face).owner,{id,to,opposite});
            result.insertedFaces.push_back(child);
            result.childSources.emplace_back(child,source.at(face));
            found=true;break;
        }
        check(found,"Unequal-density fixture must split both source-edge incident faces");
    }
    result.commit=edit.commit();
    result.index=edit.updatedIndex();
    check(result.index&&
          result.index->pickingRootIdentity()!=cut.index->pickingRootIdentity(),
          "Asymmetric private test cut is a new immutable picking root");
    return result;
}

void assertThreeBodyOneRootHistory(
    const std::shared_ptr<const RegionTopologyIndex>& original,
    const ExactCutStep& step,
    std::size_t expectedComponents,
    const std::string& fixture) {
    if(!step.complete)
        throw std::runtime_error(fixture+" original-root union refused: "+step.refusal);
    check(step.expected==original&&step.final&&
          step.commit.revision&&
          step.commit.revision.get()==&step.final->revision()&&
          step.final!=original&&
          connectedClosedSurfaceComponents(*step.final)==expectedComponents,
          "A three-body Step has exactly one complete final immutable revision");
    check(!step.dirtyPartitions.empty()&&
          step.finalCollisionSeeds>0&&step.checkedProtectedFaces>0&&
          !step.retiredFaces.empty()&&!step.addedFaces.empty()&&
          !step.rewrittenFaces.empty(),
          "Original-root Step carries true protected, dirty and net face journals");
    const auto counts=[](const RegionTopologyIndex& index) {
        std::pair<std::size_t,std::size_t> amount{};
        index.revision().forEachPartition(
            [&](PartitionId,const MeshPartition& p){
                amount.first+=p.vertices.size();
                amount.second+=p.faces.size();
            });
        return amount;
    };
    const auto before=counts(*original),after=counts(*step.final);
    check(static_cast<std::ptrdiff_t>(after.first)-
              static_cast<std::ptrdiff_t>(before.first)==
              step.vertexCountDelta&&
          static_cast<std::ptrdiff_t>(after.second)-
              static_cast<std::ptrdiff_t>(before.second)==
              step.faceCountDelta,
          "Net permanent journals match actual original-root element count changes");
    for(const auto id:step.addedVertices)
        check(!original->maybeVertex(id)&&step.final->maybeVertex(id),
              "Added VertexIds exist only in the one published final revision");
    for(const auto id:step.retiredVertices)
        check(original->maybeVertex(id)&&!step.final->maybeVertex(id),
              "Retired original VertexIds are absent from the final revision");
    for(const auto id:step.addedFaces)
        check(!original->maybeFace(id)&&step.final->maybeFace(id),
              "Added FaceIds are final live IDs, not intermediate-only revisions");
    for(const auto id:step.retiredFaces)
        check(original->maybeFace(id)&&!step.final->maybeFace(id),
              "Retired original FaceIds have no surviving final face");
    for(const auto id:step.rewrittenFaces)
        check(original->maybeFace(id)&&step.final->maybeFace(id)&&
              original->face(id).vertices!=step.final->face(id).vertices,
              "Net rewritten FaceId survives but has different final topology");

    std::vector<FaceId> faces;
    step.final->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& p) {
            for(const auto& f:p.faces)faces.push_back(f.id);
        });
    check(step.final->triangleIntersections({},faces).empty(),
          "Every face of a complete three-body union is globally collision-free");
    IndexedMeshRepository published(original);
    check(published.snapshot()==original&&
          published.publishIfCurrent(step.expected,step.final)&&
          published.snapshot()==step.final&&
          !published.publishIfCurrent(step.expected,step.final)&&
          published.snapshot()==step.final,
          "Single CAS from captured original root rejects stale replay");
    check(original->maybeFace(step.retiredFaces.front())&&
          original->retiredFaceCount()==0,
          "Historical readers continue to see their untouched original FaceIds");
    RevisionHistory history(original,8,32*1024*1024,nullptr,
                            before.first,before.second);
    history.append(step.final,step.dirtyPartitions,
                   step.addedMirrors,step.removedMirrors,
                   step.copiedPayloadBytes,after.first,after.second);
    check(history.statistics().undoDepth==1&&history.current()==step.final,
          "Entire three-body cut, refinement and join append ONE undoable Step");
    const auto undone=history.undo();
    check(undone.index==original&&undone.vertices==before.first&&
          undone.faces==before.second&&
          undone.dirtyPartitions==step.dirtyPartitions,
          "Exactly one undo restores original counts, root and dirty partitions");
    const auto redone=history.redo();
    check(redone.index==step.final&&redone.vertices==after.first&&
          redone.faces==after.second,
          "Exactly one redo restores terminal three-body union");
    const auto path=std::filesystem::temp_directory_path()/
        ("mesher_v17_m04_"+fixture+".mshr");
    writeCheckpoint(path,{step.final,{},before.first});
    const auto reload=readCheckpoint(path);
    std::filesystem::remove(path);
    check(counts(*reload.index)==after&&
          connectedClosedSurfaceComponents(*reload.index)==expectedComponents&&
          reload.index->retiredFaceIds()==step.final->retiredFaceIds()&&
          reload.index->retiredVertexIds()==step.final->retiredVertexIds(),
          "Durable checkpoint/restore retains one fully accepted mesh and tombstones");
    std::cout<<"[three-body-m04] "<<fixture
             <<" final-components="<<expectedComponents
             <<" deleted="<<step.retiredFaces.size()
             <<" added="<<step.addedFaces.size()
             <<" rewritten="<<step.rewrittenFaces.size()
             <<" dirty="<<step.dirtyPartitions.size()<<'\n';
}

void testWhollyBuriedUncutOriginalFace() {
    Mesh mesh;
    // A large closed tetra is pierced by the three sloping sides of B.
    // All three B base vertices are BELOW A's top plane and B's apex is
    // above it. B's own base triangle is fully inside A, but receives
    // ZERO original finite V13 sections and therefore NO cut children.
    for(const auto p:std::array<Vec3,12>{{
        {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
        {.4,.4,-.25},{.4,.6,-.25},{.6,.4,-.25},{.5,.5,.25},
        // A separate REVERSED closed shell entirely inside A represents
        // an intentional disconnected internal cavity. It must never
        // enter the source-adjacent uncut removal scope.
        {.12,.12,-1.1},{.12,.22,-1.1},
        {.22,.12,-1.1},{.15,.15,-.9}
    }})mesh.vertices.push_back(p);
    for(unsigned solid=0;solid<3;++solid) {
        const auto start=4u*solid;
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})mesh.triangles.push_back({
            start+t[0],start+t[solid<2?1:2],start+t[solid<2?2:1]});
    }
    const auto imported=partitionFlatMesh(mesh,256);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto rootIdentity=original->pickingRootIdentity();
    const auto contacts=original->triangleIntersections(
        imported.flatIndex.vertexIds,{});
    check(contacts.size()==3,
          "One-above/three-below tetra has exactly three original transverse contacts");
    const auto sourceBase=imported.flatIndex.faceIds[4];
    for(const auto c:contacts)
        check(!c.ambiguous&&c.first!=sourceBase&&c.second!=sourceBase,
              "Fully buried original B base has no finite V13 intersection section");
    const auto cut=materializeTransverseCutCandidate(original,contacts);
    check(certifyConformingCutCoverage(cut).complete&&
          assessConformingCutFragments(cut).complete,
          "Uncut-face positive fixture has complete original cut coverage and c1 children");
    check(cut.index->face(sourceBase).vertices==
          original->face(sourceBase).vertices,
          "Uncut buried B base retains the same old FaceId and vertex identities");
    for(const auto& [child,source]:cut.childSources)
        check(source!=sourceBase,
              "Original buried B base has NO fake child provenance");

    CutRemovalSettings contours;
    contours.contours.maximumPlanarityInRadii=4.0;
    contours.contours.maximumSingleCapPlanarityInRadii=4.0;
    contours.contours.maximumIsoperimetricRatio=20.0;
    contours.contours.maximumSingleCapIsoperimetricRatio=20.0;
    contours.contours.maximumPerimeterRatio=20.0;
    contours.contours.maximumGapInEdges=.1;
    contours.contours.minimumNormalAlignment=0;
    const auto oldChildOnly=planExactCutRemoval(cut,contours);
    check(std::find(oldChildOnly.proposedRemoval.begin(),
                    oldChildOnly.proposedRemoval.end(),sourceBase)==
                    oldChildOnly.proposedRemoval.end(),
          "The original c3 child-only planner never nominated an uncut original FaceId");
    const auto plan=planUncutFaceRetirement(cut,contours);
    if(!plan.ready())
        throw std::runtime_error(
            "Wholly buried unchanged original refusal: "+plan.refusal);
    check(std::find(plan.authorizedUncut.begin(),plan.authorizedUncut.end(),
              sourceBase)!=plan.authorizedUncut.end(),
          "Previously uncut buried original face is independently authorized");
    check(std::find(plan.combinedRemoval.begin(),plan.combinedRemoval.end(),
              sourceBase)!=plan.combinedRemoval.end()&&
          plan.contourPlan.ready()&&
          plan.contourPlan.removedFaces==plan.combinedRemoval,
          "Complete c1/c2 plus uncut FaceIds have one newly checked combined contour");
    for(unsigned f=1;f<4;++f) {
        const auto outer=imported.flatIndex.faceIds[f];
        check(std::find(plan.protectedUncut.begin(),plan.protectedUncut.end(),
                  outer)!=plan.protectedUncut.end(),
              "Original exposed A side must be protected, never silently retired");
    }
    for(unsigned f=8;f<12;++f) {
        const auto cavity=imported.flatIndex.faceIds[f];
        check(std::find(plan.combinedRemoval.begin(),plan.combinedRemoval.end(),
                        cavity)==plan.combinedRemoval.end()&&
              original->maybeFace(cavity)&&cut.index->maybeFace(cavity),
              "Disconnected reversed cavity component must NEVER be retired by adjacency inference");
    }
    ExactCutStepLimits stepLimits;
    stepLimits.join.removal=contours;
    const auto full=prepareExactCutStep(original,contacts,stepLimits);
    std::cout<<"[uncut-m04] accepted="<<full.complete
             <<" reason="<<full.refusal<<'\n';
    if(!full.complete)
        throw std::runtime_error(
            "Certified fully buried unchanged base could not form a final union: "+
            full.refusal);
    check(!full.final->maybeFace(sourceBase)&&
          std::binary_search(full.retiredFaces.begin(),
                             full.retiredFaces.end(),sourceBase)&&
          full.final->maybeFace(imported.flatIndex.faceIds[8])&&
          connectedClosedSurfaceComponents(*full.final)==2,
          "One root retires uncut buried original base but retains the true reversed cavity");
    assertThreeBodyOneRootHistory(original,full,2,"buried-uncut-cavity");
    check(original->pickingRootIdentity()==rootIdentity&&
          original->retiredFaceCount()==0&&
          original->maybeFace(sourceBase)&&cut.index->maybeFace(sourceBase),
          "M03 is read-only for BOTH original and unpublished private roots");

    auto incomplete=cut;
    incomplete.sourceContacts.pop_back();
    incomplete.sectionFirst.pop_back();
    incomplete.sectionSecond.pop_back();
    const auto missing=planUncutFaceRetirement(incomplete,contours);
    check(!missing.ready()&&missing.authorizedUncut.empty()&&
          missing.combinedRemoval.empty(),
          "Incomplete original V13 pairs cannot authorize a buried unchanged face");
    UncutFaceRetirementLimits hard;
    hard.maxOriginalFaces=0;
    const auto bounded=planUncutFaceRetirement(cut,contours,hard);
    check(bounded.status==CutRemovalStatus::BudgetExceeded&&
          bounded.authorizedUncut.empty()&&bounded.combinedRemoval.empty(),
          "Uncut region component budget fails atomically");
    hard={};
    hard.maxWindingQueries=0;
    const auto noWinding=planUncutFaceRetirement(cut,contours,hard);
    check(noWinding.status==CutRemovalStatus::BudgetExceeded&&
          noWinding.authorizedUncut.empty()&&
          noWinding.combinedRemoval.empty(),
          "Uncut component cannot decide burial without bounded winding evidence");
    hard={};
    hard.maxCollisionNarrowPhaseTests=0;
    const auto noDiscovery=planUncutFaceRetirement(cut,contours,hard);
    check(!noDiscovery.ready()&&noDiscovery.authorizedUncut.empty(),
          "Uncut original V13 discovery exhaustion never becomes accepted deletion");
    std::cout<<"[uncut-burial] source="<<sourceBase
             <<" uncut-approved="<<plan.authorizedUncut.size()
             <<" uncut-protected="<<plan.protectedUncut.size()
             <<" total-removed="<<plan.combinedRemoval.size()<<'\n';
}

void testGenericCompleteThreeBodyTransverseGraph() {
    Mesh mesh;
    for(const auto p:std::array<Vec3,12>{{
        {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
        {.375,.375,.25},{.625,.375,.25},
        {.625,.625,-.25},{.375,.25,-.25},
        {.21875,.15625,.34375},{.46875,.125,.34375},
        {.53125,.40625,-.1875},{.28125,.09375,-.125}
    }})mesh.vertices.push_back(p);
    for(unsigned solid=0;solid<3;++solid) {
        const auto start=solid*4;
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})mesh.triangles.push_back({
            start+t[0],start+t[1],start+t[2]});
    }
    const auto imported=partitionFlatMesh(mesh,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    const auto originalRoot=root->pickingRootIdentity();
    const auto all=root->triangleIntersections(imported.flatIndex.vertexIds,{});
    std::cout<<"[generic-triple] original-pairs="<<all.size()<<'\n';
    for(const auto& pair:all) {
        const auto a=root->face(pair.first).vertices,
                   b=root->face(pair.second).vertices;
        const auto positions=[&](std::array<VertexId,3> ids) {
            return std::array<Vec3,3>{{
                root->vertex(ids[0]).position,
                root->vertex(ids[1]).position,
                root->vertex(ids[2]).position
            }};
        };
        const auto section=splitTransverseTriangleContact(
            positions(a),a,positions(b),b);
        if(section.status!=IntersectionSectionStatus::TransverseSegment)
            std::cout<<"[generic-triple] unsupported original pair "
                     <<pair.first<<" / "<<pair.second
                     <<" ambiguous="<<pair.ambiguous<<'\n';
    }
    check(all.size()==12,"Generic triple fixture has exactly twelve original transverse contacts");
    assertCanonicalTripleJunctions(*root,all,"generic-12");
    CutRemovalSettings contours;
    contours.contours.maximumPlanarityInRadii=4.0;
    contours.contours.maximumSingleCapPlanarityInRadii=4.0;
    contours.contours.maximumIsoperimetricRatio=20.0;
    contours.contours.maximumSingleCapIsoperimetricRatio=20.0;
    contours.contours.maximumPerimeterRatio=20.0;
    contours.contours.maximumGapInEdges=0.1;
    contours.contours.minimumNormalAlignment=0;
    try {
        const auto cut=materializeTransverseCutCandidate(root,all);
        const auto coverage=certifyConformingCutCoverage(cut);
        const auto occupancy=assessConformingCutFragments(cut);
        const auto proposal=planExactCutRemoval(cut,contours);
        check(coverage.complete&&occupancy.complete&&proposal.ready()&&
              coverage.checkedContacts==all.size()&&
              occupancy.fragments.size()==cut.childSources.size(),
              "All generic triple junction original contacts have full c2/c1/c3 certified private children");
        ExactCutJoinLimits joinLimits;
        joinLimits.removal=contours;
        const auto unchangedMatch=reconcileCertifiedSeamSubdivisions(
            cut,ExactSeamRefinementLimits{.join=joinLimits});
        check(unchangedMatch.complete&&unchangedMatch.refined&&
              unchangedMatch.insertedVertices==0&&
              unchangedMatch.insertedFaces==0&&
              unchangedMatch.refined->index==cut.index,
              "Matching true three-body source intervals require no speculative refinement");
        const auto joined=joinCertifiedCutSections(cut,joinLimits);
        check(joined.complete||(joined.refusal.size()>0&&!joined.index&&
              !joined.commit&&joined.weldedPairs.empty()),
              "Unmatched triple seam subintervals must not expose a partial joined root");
        // Exact same ORIGINAL V13 graph but unequal PRIVATE child
        // subdivision: one sheet is split at a source-proven half-edge
        // position while its opposite sheet has no such vertex.
        const auto uneven=splitSingleCertifiedBoundaryForTest(cut,contours);
        const auto unsupported=joinCertifiedCutSections(uneven,joinLimits);
        check(!unsupported.complete&&!unsupported.index&&!unsupported.commit,
              "Raw mismatched physical seam density must never accidentally weld");
        ExactSeamRefinementLimits refineLimits;
        refineLimits.join=joinLimits;
        const auto aligned=reconcileCertifiedSeamSubdivisions(uneven,refineLimits);
        if(!aligned.complete)
            throw std::runtime_error(
                "Source-proven unequal seam density did not reconcile: "+
                aligned.refusal);
        check(aligned.refined&&aligned.insertedVertices>=1&&
              aligned.insertedFaces>=2&&aligned.matchedIntervals>0,
              "Unequal physical section chains are conservatively split in both incident fans");
        const auto coverage2=certifyConformingCutCoverage(*aligned.refined);
        const auto removal2=planExactCutRemoval(*aligned.refined,contours);
        check(coverage2.complete&&removal2.ready(),
              "Every new source-child FaceId is independently c1/c2/c3 certified");
        check(uneven.index->pickingRootIdentity()!=
                  aligned.refined->index->pickingRootIdentity()&&
              cut.index->pickingRootIdentity()!=
                  aligned.refined->index->pickingRootIdentity(),
              "Pre-refinement original readers retain their immutable picking roots");
        const auto accepted=joinCertifiedCutSections(*aligned.refined,joinLimits);
        check(accepted.complete&&accepted.index&&
              connectedClosedSurfaceComponents(*accepted.index)==1,
              "Reconciled provenance-only section endpoints produce an exact 3-to-1 union");
        auto zero=refineLimits;
        zero.maxNewVertices=0;
        const auto exhausted=reconcileCertifiedSeamSubdivisions(uneven,zero);
        check(!exhausted.complete&&!exhausted.refined,
              "Refinement vertex budget failure cannot return a partial private root");
        auto forged=uneven;
        forged.sourceContacts.pop_back();
        forged.sectionFirst.pop_back();
        forged.sectionSecond.pop_back();
        const auto incomplete=reconcileCertifiedSeamSubdivisions(forged,refineLimits);
        check(!incomplete.complete&&!incomplete.refined,
              "Incomplete original source-contact graphs cannot authorize seam refinement");
        auto unpaired=uneven;
        unpaired.mirroredFaces.emplace_back(cut.childSources.front().first,
                                           cut.childSources.back().first);
        const auto mirrorRefusal=
            reconcileCertifiedSeamSubdivisions(unpaired,refineLimits);
        check(!mirrorRefusal.complete&&!mirrorRefusal.refined,
              "Mirrored new seam split orbits remain fail-closed until M05");
        std::cout<<"[generic-triple] cut=1 coverage="<<coverage.complete
                 <<" coverage-refusal="<<coverage.refusal
                 <<" occupancy="<<occupancy.complete
                 <<" occupancy-refusal="<<occupancy.refusal
                 <<" removal="<<proposal.ready()
                 <<" removal-refusal="<<proposal.refusal
                 <<" joined="<<joined.complete
                 <<" joined-refusal="<<joined.refusal<<'\n';
    }catch(const std::exception& error) {
        throw std::runtime_error(
            std::string("Certified generic three-solid cut was rejected: ")+error.what());
    }
    ExactCutStepLimits limits;
    limits.join.removal=contours;
    const auto final=prepareExactCutStep(root,all,limits);
    std::cout<<"[generic-triple] final="<<final.complete
             <<" final-refusal="<<final.refusal<<'\n';
    assertThreeBodyOneRootHistory(root,final,1,"generic-12");
    auto partial=all;
    partial.pop_back();
    const auto missing=prepareExactCutStep(root,partial,limits);
    check(!missing.complete&&!missing.expected&&!missing.final&&
          !missing.commit.revision&&missing.retiredFaces.empty(),
          "Missing one of twelve original V13 contacts refuses without an original-root journal");
    auto budget=limits;
    budget.maxNetChangedIds=1;
    const auto exceeded=prepareExactCutStep(root,all,budget);
    check(!exceeded.complete&&!exceeded.expected&&!exceeded.final&&
          !exceeded.commit.revision&&exceeded.addedFaces.empty(),
          "Terminal original-root net stable-ID budget refuses without publishing a private cut");
    check(root->pickingRootIdentity()==originalRoot&&
          connectedClosedSurfaceComponents(*root)==3,
          "Triple-contact attempts never alter historical immutable pointers");
}

// Orthogonal plane intersection at exactly dyadic (0.5,0.5,0):
// B's source face lies in X=.5, C's in Y=.5 and A's top face
// lies in Z=0. Other tetra faces have independent transverse contacts.
void testOrthogonalTripleJunctionProbe() {
    Mesh mesh;
    for(const auto p:std::array<Vec3,12>{{
        {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
        {.5,.25,.5},{.5,.75,.5},{.5,.625,-.5},{.875,.375,-.25},
        {.25,.5,.625},{.75,.5,.625},{.625,.5,-.625},{.8125,.125,-.3125}
    }})mesh.vertices.push_back(p);
    for(unsigned solid=0;solid<3;++solid) {
        const auto start=solid*4;
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})mesh.triangles.push_back({
            start+t[0],start+t[1],start+t[2]});
    }
    const auto imported=partitionFlatMesh(mesh,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    const auto all=root->triangleIntersections(imported.flatIndex.vertexIds,{});
    std::size_t unsupported=0;
    for(const auto& pair:all) {
        const auto a=root->face(pair.first).vertices,
                   b=root->face(pair.second).vertices;
        const auto coordinates=[&](std::array<VertexId,3> f) {
            return std::array<Vec3,3>{{
                root->vertex(f[0]).position,root->vertex(f[1]).position,
                root->vertex(f[2]).position
            }};
        };
        if(splitTransverseTriangleContact(coordinates(a),a,
            coordinates(b),b).status!=IntersectionSectionStatus::TransverseSegment)
            ++unsupported;
    }
    std::cout<<"[orthogonal-triple] pairs="<<all.size()
             <<" unsupported="<<unsupported<<'\n';
    check(all.size()==16&&unsupported==0,
          "Orthogonal dyadic triple junction requires sixteen strictly transverse original contacts");
    assertCanonicalTripleJunctions(*root,all,"orthogonal-16");
    CutRemovalSettings contours;
    contours.contours.maximumPlanarityInRadii=4.0;
    contours.contours.maximumSingleCapPlanarityInRadii=4.0;
    contours.contours.maximumIsoperimetricRatio=20.0;
    contours.contours.maximumSingleCapIsoperimetricRatio=20.0;
    contours.contours.maximumPerimeterRatio=20.0;
    contours.contours.maximumGapInEdges=0.1;
    contours.contours.minimumNormalAlignment=0;
    try {
        const auto cut=materializeTransverseCutCandidate(root,all);
        const auto coverage=certifyConformingCutCoverage(cut);
        const auto occupancy=assessConformingCutFragments(cut);
        const auto removal=planExactCutRemoval(cut,contours);
        check(coverage.complete&&occupancy.complete&&removal.ready()&&
              coverage.checkedContacts==all.size(),
              "Orthogonal triple-junction graph has complete private coverage, winding and c3 authorization");
        ExactCutJoinLimits limits;
        limits.removal=contours;
        const auto joined=joinCertifiedCutSections(cut,limits);
        check(joined.complete||(joined.refusal.size()>0&&!joined.index&&
              !joined.commit&&joined.weldedPairs.empty()),
              "Orthogonal junction mismatch refuses without a publishable seam or journal");
        ExactSeamRefinementLimits refinementLimits;
        refinementLimits.join=limits;
        const auto refined=reconcileCertifiedSeamSubdivisions(cut,refinementLimits);
        std::cout<<"[orthogonal-m04] refine="<<refined.complete
                 <<" reason="<<refined.refusal
                 <<" vertices="<<refined.insertedVertices
                 <<" faces="<<refined.insertedFaces<<'\n';
        if(refined.complete&&refined.refined) {
            const auto paired=joinCertifiedCutSections(*refined.refined,limits);
            std::cout<<"[orthogonal-m04] refined-join="<<paired.complete
                     <<" reason="<<paired.refusal<<'\n';
        }
        ExactCutStepLimits stepLimits;
        stepLimits.join.removal=contours;
        const auto full=prepareExactCutStep(root,all,stepLimits);
        std::cout<<"[orthogonal-m04] final="<<full.complete
                 <<" reason="<<full.refusal<<'\n';
        if(!full.complete)
            throw std::runtime_error(
                "Complete original-root orthogonal three-body union: "+full.refusal);
        assertThreeBodyOneRootHistory(root,full,1,"orthogonal-16");
        auto partial=all;
        partial.pop_back();
        const auto omitted=prepareExactCutStep(root,partial,stepLimits);
        check(!omitted.complete&&!omitted.expected&&!omitted.final&&
              !omitted.commit.revision&&omitted.retiredFaces.empty(),
              "Missing one of sixteen original source contacts refuses entire orthogonal Step");
        auto exhausted=stepLimits;
        exhausted.refinement.maxCandidateTests=0;
        const auto noWork=prepareExactCutStep(root,all,exhausted);
        check(!noWork.complete&&!noWork.expected&&!noWork.final&&
              !noWork.commit.revision,
              "Zero seam interval work budget does not emit an incomplete triple-union root");
        std::cout<<"[orthogonal-triple] cut=1 coverage="<<coverage.complete
                 <<" reason="<<coverage.refusal
                 <<" occupancy="<<occupancy.complete
                 <<" reason="<<occupancy.refusal
                 <<" removal="<<removal.ready()
                 <<" reason="<<removal.refusal
                 <<" join="<<joined.complete
                 <<" reason="<<joined.refusal<<'\n';
    }catch(const std::exception& e) {
        throw std::runtime_error(
            std::string("Orthogonal triple-junction cut was rejected: ")+e.what());
    }
}

void verifyMirroredOriginalRootStep(
    std::shared_ptr<const RegionTopologyIndex> original,
    const std::unordered_map<VertexId,VertexId>& historical,
    const ExactCutStep& step,
    const std::string& fixture,
    std::size_t expectedComponents) {
    check(step.complete&&step.expected==original&&step.final&&
          step.commit.revision.get()==&step.final->revision()&&
          !step.dirtyPartitions.empty()&&
          connectedClosedSurfaceComponents(*step.final)==expectedComponents&&
          !step.addedMirrors.empty()&&
          !step.survivingMirroredFaces.empty(),
          "Reflected original-root Step must be one complete manifold net journal");
    std::map<VertexId,VertexId> map;
    std::vector<std::pair<VertexId,VertexId>> historyPairs;
    for(const auto& [a,b]:historical) {
        if(a>b)continue;
        const bool liveA=step.final->maybeVertex(a)!=nullptr,
                   liveB=step.final->maybeVertex(b)!=nullptr;
        check(liveA==liveB,"Reflected old original vertex pair retires together");
        if(liveA) {
            map.emplace(a,b);map.emplace(b,a);
            historyPairs.emplace_back(a,b);
        }else
            check(std::binary_search(step.removedMirrors.begin(),
                                     step.removedMirrors.end(),
                                     std::pair<VertexId,VertexId>{a,b}),
                  "Retired original stable mirror pair appears in ONE net mirror delta");
    }
    for(const auto& [a,b]:step.addedMirrors) {
        check(a!=b&&!historical.contains(a)&&!historical.contains(b)&&
              !original->maybeVertex(a)&&!original->maybeVertex(b)&&
              step.final->maybeVertex(a)&&step.final->maybeVertex(b),
              "New paired VertexIds are both genuinely new live off-plane vertices");
        check(map.emplace(a,b).second&&map.emplace(b,a).second,
              "Each new stable reflected vertex has exactly one partner");
        historyPairs.emplace_back(a,b);
    }
    std::sort(historyPairs.begin(),historyPairs.end());
    std::set<FaceId> liveFaces;
    std::map<std::array<VertexId,3>,FaceId> byCorners;
    std::pair<std::size_t,std::size_t> finalCounts{};
    step.final->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            finalCounts.first+=part.vertices.size();
            for(const auto& v:part.vertices) {
                const auto id=v.id;
                check(map.contains(id)&&map.contains(map.at(id))&&
                      map.at(map.at(id))==id&&
                      map.at(id)!=id,
                      "Both new and existing live mirror maps are exact involutions");
                const auto p=step.final->vertex(id).position,
                           q=step.final->vertex(map.at(id)).position;
                check(p.x!=0&&q.x==-p.x&&q.y==p.y&&q.z==p.z,
                      "Off-plane reflected vertex coordinates are exactly reciprocal");
            }
            for(const auto& face:part.faces) {
                ++finalCounts.second;
                liveFaces.insert(face.id);
                auto ids=face.vertices;
                std::sort(ids.begin(),ids.end());
                check(byCorners.emplace(ids,face.id).second,
                      "Final mirrored surface cannot contain duplicate coincident source faces");
            }
        });
    for(const auto face:liveFaces) {
        const auto tri=step.final->face(face).vertices;
        std::array<VertexId,3> mapped{
            map.at(tri[0]),map.at(tri[2]),map.at(tri[1])};
        auto corners=mapped;
        std::sort(corners.begin(),corners.end());
        const auto it=byCorners.find(corners);
        check(it!=byCorners.end()&&it->second!=face,
              "Every terminal face has exactly one distinct reflected FaceId");
        const auto partner=step.final->face(it->second).vertices;
        bool reversed=false;
        for(unsigned shift=0;shift<3;++shift)
            if(partner[0]==mapped[shift]&&
               partner[1]==mapped[(shift+1)%3]&&
               partner[2]==mapped[(shift+2)%3])reversed=true;
        check(reversed,"Reflected final face always has opposite winding");
    }
    std::pair<std::size_t,std::size_t> initialCounts{};
    original->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            initialCounts.first+=part.vertices.size();
            initialCounts.second+=part.faces.size();
        });
    check(static_cast<std::ptrdiff_t>(finalCounts.first)-
              static_cast<std::ptrdiff_t>(initialCounts.first)==
              step.vertexCountDelta&&
          static_cast<std::ptrdiff_t>(finalCounts.second)-
              static_cast<std::ptrdiff_t>(initialCounts.second)==
              step.faceCountDelta,
          "Mirrored net stable ID accounting matches original/terminal counts");
    std::vector<FaceId> seeds(liveFaces.begin(),liveFaces.end());
    check(step.final->triangleIntersections({},seeds).empty(),
          "Both complete reflected unions have no final nonadjacent intersections");
    IndexedMeshRepository published(original);
    check(published.publishIfCurrent(step.expected,step.final)&&
          !published.publishIfCurrent(step.expected,step.final),
          "Both reflected halves publish in one original-root CAS, stale replay refuses");
    RevisionHistory history(original,8,32*1024*1024,nullptr,
                            initialCounts.first,initialCounts.second);
    history.append(step.final,step.dirtyPartitions,
                   step.addedMirrors,step.removedMirrors,
                   step.copiedPayloadBytes,finalCounts.first,finalCounts.second);
    check(history.statistics().undoDepth==1&&history.current()==step.final,
          "Complete reflected contact graphs have one undo history Step");
    const auto undo=history.undo();
    check(undo.index==original&&undo.vertices==initialCounts.first&&
          undo.faces==initialCounts.second&&
          undo.removeMirrors==step.addedMirrors&&
          undo.addMirrors==step.removedMirrors,
          "One undo restores original geometry and exactly reverses net mirror deltas");
    const auto redo=history.redo();
    check(redo.index==step.final&&redo.vertices==finalCounts.first&&
          redo.faces==finalCounts.second&&
          redo.addMirrors==step.addedMirrors&&
          redo.removeMirrors==step.removedMirrors,
          "One redo restores both mirrored unions and net pair deltas");
    const auto path=std::filesystem::temp_directory_path()/
        ("mesher_v17_m05_"+fixture+".mshr");
    writeCheckpoint(path,{step.final,historyPairs,initialCounts.first});
    const auto loaded=readCheckpoint(path);
    std::filesystem::remove(path);
    check(loaded.index&&loaded.mirrorPairs==historyPairs&&
          connectedClosedSurfaceComponents(*loaded.index)==expectedComponents&&
          loaded.index->retiredFaceIds()==step.final->retiredFaceIds()&&
          loaded.index->retiredVertexIds()==step.final->retiredVertexIds(),
          "Reflected full-three-body history checkpoint restores both stable ID orbits");
    std::cout<<"[mirrored-m05] fixture="<<fixture
             <<" faces="<<finalCounts.second
             <<" mirrored-new-pairs="<<step.addedMirrors.size()
             <<" reflected-live-components="<<expectedComponents<<'\n';
}

void testReflectedThreeBodyCompleteOriginalContacts() {
    for(const bool orthogonal:{false,true}) {
        Mesh mesh;
        const std::array<Vec3,12> generic{{
            {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
            {.375,.375,.25},{.625,.375,.25},
            {.625,.625,-.25},{.375,.25,-.25},
            {.21875,.15625,.34375},{.46875,.125,.34375},
            {.53125,.40625,-.1875},{.28125,.09375,-.125}
        }};
        const std::array<Vec3,12> rightAngle{{
            {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
            {.5,.25,.5},{.5,.75,.5},{.5,.625,-.5},{.875,.375,-.25},
            {.25,.5,.625},{.75,.5,.625},{.625,.5,-.625},{.8125,.125,-.3125}
        }};
        const auto positions=orthogonal?rightAngle:generic;
        for(const auto p:positions)
            mesh.vertices.push_back({p.x+5,p.y,p.z});
        for(const auto p:positions)
            mesh.vertices.push_back({-p.x-5,p.y,p.z});
        for(unsigned half=0;half<2;++half)
            for(unsigned solid=0;solid<3;++solid)
                for(const auto tri:std::array<std::array<unsigned,3>,4>{{
                    {0,1,2},{1,0,3},{2,1,3},{0,2,3}
                }}) {
                    const auto start=half*12+solid*4;
                    mesh.triangles.push_back({
                        start+tri[0],
                        start+tri[half?2:1],
                        start+tri[half?1:2]});
                }
        const auto imported=partitionFlatMesh(mesh,256);
        const auto original=RegionTopologyIndex::build(imported.revision);
        const auto identity=original->pickingRootIdentity();
        std::unordered_map<VertexId,VertexId> mirrorMap;
        for(std::size_t i=0;i<12;++i) {
            const auto a=imported.flatIndex.vertexIds[i],
                       b=imported.flatIndex.vertexIds[i+12];
            mirrorMap.emplace(a,b);mirrorMap.emplace(b,a);
        }
        const auto all=original->triangleIntersections(
            imported.flatIndex.vertexIds,{});
        const auto expectedPairs=orthogonal?32u:24u;
        check(all.size()==expectedPairs,
              "Complete reflected three-body graph doubles its genuine original V13 source contacts");
        CutRemovalSettings contours;
        contours.contours.maximumPlanarityInRadii=4.0;
        contours.contours.maximumSingleCapPlanarityInRadii=4.0;
        contours.contours.maximumIsoperimetricRatio=20.0;
        contours.contours.maximumSingleCapIsoperimetricRatio=20.0;
        contours.contours.maximumPerimeterRatio=20.0;
        contours.contours.maximumGapInEdges=.1;
        contours.contours.minimumNormalAlignment=0;
        ExactCutStepLimits limits;
        limits.join.removal=contours;
        limits.requireMirrors=true;
        const auto result=prepareExactCutStep(original,all,limits,&mirrorMap);
        std::cout<<"[mirrored-m05] orthogonal="<<orthogonal
                 <<" contacts="<<all.size()
                 <<" complete="<<result.complete
                 <<" refusal="<<result.refusal<<'\n';
        if(!result.complete)
            throw std::runtime_error(
                "Complete reflected triple intersection failed: "+result.refusal);
        check(result.expected==original&&result.final&&
              connectedClosedSurfaceComponents(*result.final)==2,
              "Two complete reflected three-body groups form two separate closed unions");
        verifyMirroredOriginalRootStep(original,mirrorMap,result,
                                      orthogonal?"orthogonal-32":"generic-24",2);
        auto partial=all;
        partial.pop_back();
        const auto missing=prepareExactCutStep(original,partial,limits,&mirrorMap);
        check(!missing.complete&&!missing.expected&&!missing.final&&
              !missing.commit.revision&&missing.addedMirrors.empty(),
              "Missing one of the mirrored original three-body V13 contact pairs refuses the whole source root");
        auto omitted=mirrorMap;
        omitted.erase(imported.flatIndex.vertexIds[0]);
        const auto noMirror=prepareExactCutStep(original,all,limits,&omitted);
        check(!noMirror.complete&&!noMirror.expected&&!noMirror.final&&
              noMirror.addedMirrors.empty()&&noMirror.removedMirrors.empty(),
              "Incomplete historical vertex involution refuses complete reflected source graph");
        auto mispaired=mirrorMap;
        const auto r0=imported.flatIndex.vertexIds[0],
                   r1=imported.flatIndex.vertexIds[1],
                   l0=imported.flatIndex.vertexIds[12],
                   l1=imported.flatIndex.vertexIds[13];
        mispaired[r0]=l1;mispaired[l1]=r0;
        mispaired[r1]=l0;mispaired[l0]=r1;
        const auto wrongFace=prepareExactCutStep(original,all,limits,&mispaired);
        check(!wrongFace.complete&&!wrongFace.expected&&!wrongFace.final&&
              wrongFace.addedMirrors.empty()&&wrongFace.retiredFaces.empty(),
              "Reciprocal but geometrically WRONG source mirror-face mapping refuses both halves");
        auto tight=limits;
        tight.cut.maxMirroredPairTests=0;
        const auto noPairWork=prepareExactCutStep(original,all,tight,&mirrorMap);
        check(!noPairWork.complete&&!noPairWork.expected&&!noPairWork.final&&
              noPairWork.addedMirrors.empty(),
              "Reflected source point pairing work limit never leaks partial mirror IDs");
        tight=limits;
        tight.maxNetChangedIds=1;
        const auto noNet=prepareExactCutStep(original,all,tight,&mirrorMap);
        check(!noNet.complete&&!noNet.expected&&!noNet.final&&
              !noNet.commit.revision&&!noNet.addedMirrors.size(),
              "Reflected net stable-ID budget refuses without original-root history mutation");
        const auto cut=materializeTransverseCutCandidate(
            original,all,{},256,&mirrorMap);
        auto mismatched=cut;
        mismatched.mirroredFaces.pop_back();
        const auto invalid=joinCertifiedCutSections(
            mismatched,limits.join,&mirrorMap,true);
        check(!invalid.complete&&!invalid.index&&!invalid.commit&&
              invalid.weldedPairs.empty()&&invalid.retiredFaces.empty(),
              "A missing reflected source FaceId cut orbit cannot authorize an asymmetric terminal weld");
        check(original->pickingRootIdentity()==identity&&
              connectedClosedSurfaceComponents(*original)==6,
              "Mirrored three-body original readers remain unaffected");
    }
}

void testReflectedUncutBurialCavityOrbits() {
    Mesh mesh;
    // Duplicate the M03 one-above/three-below closed tetra contact and
    // independent reversed cavity into TWO exact off-plane reflected
    // three-body components, without any invariant X=0 seam point.
    const std::array<Vec3,12> positions{{
        {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
        {.4,.4,-.25},{.4,.6,-.25},{.6,.4,-.25},{.5,.5,.25},
        {.12,.12,-1.1},{.12,.22,-1.1},
        {.22,.12,-1.1},{.15,.15,-.9}
    }};
    for(const auto p:positions)mesh.vertices.push_back({p.x+5,p.y,p.z});
    for(const auto p:positions)mesh.vertices.push_back({-p.x-5,p.y,p.z});
    for(unsigned half=0;half<2;++half)
        for(unsigned solid=0;solid<3;++solid)
            for(const auto tri:std::array<std::array<unsigned,3>,4>{{
                {0,1,2},{1,0,3},{2,1,3},{0,2,3}
            }}) {
                const unsigned start=half*12+solid*4;
                const bool reverse=(half!=0) != (solid==2);
                mesh.triangles.push_back({
                    start+tri[0],
                    start+tri[reverse?2:1],
                    start+tri[reverse?1:2]});
            }
    const auto imported=partitionFlatMesh(mesh,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    std::unordered_map<VertexId,VertexId> mirrors;
    for(std::size_t k=0;k<12;++k) {
        const auto a=imported.flatIndex.vertexIds[k],
                   b=imported.flatIndex.vertexIds[k+12];
        mirrors.emplace(a,b);mirrors.emplace(b,a);
    }
    const auto contacts=root->triangleIntersections(
        imported.flatIndex.vertexIds,{});
    check(contacts.size()==6,
          "Two mirrored one-above-three-below protrusions yield six complete original transverse V13 source pairs");
    CutRemovalSettings contours;
    contours.contours.maximumPlanarityInRadii=4.0;
    contours.contours.maximumSingleCapPlanarityInRadii=4.0;
    contours.contours.maximumIsoperimetricRatio=20.0;
    contours.contours.maximumSingleCapIsoperimetricRatio=20.0;
    contours.contours.maximumPerimeterRatio=20.0;
    contours.contours.maximumGapInEdges=.1;
    contours.contours.minimumNormalAlignment=0;
    const auto cut=materializeTransverseCutCandidate(
        root,contacts,{},256,&mirrors);
    const auto uncut=planUncutFaceRetirement(cut,contours,{},&mirrors);
    if(!uncut.ready())
        throw std::runtime_error("Reflected uncut originals refused: "+
                                 uncut.refusal);
    check(uncut.authorizedUncut.size()==2&&
          uncut.mirroredAuthorizedUncut.size()==1&&
          !uncut.mirroredProtectedUncut.empty(),
          "Two source-proven wholly buried uncut originals form ONE paired retirement orbit");
    const auto baseRight=imported.flatIndex.faceIds[4],
               baseLeft=imported.flatIndex.faceIds[16];
    check(std::binary_search(uncut.authorizedUncut.begin(),
                             uncut.authorizedUncut.end(),baseRight)&&
          std::binary_search(uncut.authorizedUncut.begin(),
                             uncut.authorizedUncut.end(),baseLeft),
          "Both mirror counterparts of the untouched buried original base are authorized");
    ExactCutStepLimits limits;
    limits.requireMirrors=true;
    limits.join.removal=contours;
    const auto full=prepareExactCutStep(root,contacts,limits,&mirrors);
    std::cout<<"[mirrored-uncut-m05] complete="<<full.complete
             <<" reason="<<full.refusal<<'\n';
    if(!full.complete)
        throw std::runtime_error(
            "Both buried uncut mirrored originals must close atomically: "+
            full.refusal);
    check(!full.final->maybeFace(baseRight)&&
          !full.final->maybeFace(baseLeft)&&
          std::binary_search(full.retiredFaces.begin(),
                             full.retiredFaces.end(),baseRight)&&
          std::binary_search(full.retiredFaces.begin(),
                             full.retiredFaces.end(),baseLeft),
          "Both original reflected buried FaceIds enter ONE net retirement journal");
    for(const auto i:{8u,9u,10u,11u,20u,21u,22u,23u}) {
        const auto cavity=imported.flatIndex.faceIds[i];
        check(full.final->maybeFace(cavity)&&root->maybeFace(cavity),
              "Reflected independently enclosed reversed cavity faces remain permanent");
    }
    verifyMirroredOriginalRootStep(root,mirrors,full,"uncut-cavity-orbits",4);

    auto incomplete=contacts;
    incomplete.pop_back();
    const auto absent=prepareExactCutStep(root,incomplete,limits,&mirrors);
    check(!absent.complete&&!absent.expected&&!absent.final&&
          !absent.commit.revision&&absent.addedMirrors.empty(),
          "One missing reflected original V13 pair refuses BOTH halves");
    auto exhausted=limits;
    exhausted.uncut.maxOriginalFaces=0;
    const auto budget=prepareExactCutStep(root,contacts,exhausted,&mirrors);
    check(!budget.complete&&!budget.final&&!budget.expected&&
          budget.addedMirrors.empty()&&budget.retiredFaces.empty(),
          "Reflected uncut original component budget refuses whole original-root Step");
    auto broken=mirrors;
    broken.erase(imported.flatIndex.vertexIds[0]);
    const auto badMap=prepareExactCutStep(root,contacts,limits,&broken);
    check(!badMap.complete&&!badMap.final&&!badMap.expected&&
          badMap.addedMirrors.empty()&&badMap.retiredFaces.empty(),
          "Missing original mirror involution refuses paired classified face retirement");
}

void testX0InvariantExactCutProbe() {
    Mesh mesh;
    for(const auto p:std::array<Vec3,8>{{
        {-1,0,0},{1,0,0},{0,1.2,0},{0,.2,-1.3},
        {-.65,.1,.35},{.65,.1,.35},{0,.9,.35},{0,.3125,-.65}
    }})mesh.vertices.push_back(p);
    for(unsigned tetra=0;tetra<2;++tetra)
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}
        }})
            mesh.triangles.push_back({tetra*4+t[0],
                                      tetra*4+t[1],tetra*4+t[2]});
    const auto imported=partitionFlatMesh(mesh,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    std::unordered_map<VertexId,VertexId> mirror;
    for(unsigned tetra=0;tetra<2;++tetra) {
        const auto start=tetra*4u;
        const auto left=imported.flatIndex.vertexIds[start],
                   right=imported.flatIndex.vertexIds[start+1],
                   axisC=imported.flatIndex.vertexIds[start+2],
                   axisD=imported.flatIndex.vertexIds[start+3];
        mirror[left]=right;mirror[right]=left;
        mirror[axisC]=axisC;mirror[axisD]=axisD;
    }
    const auto contacts=root->triangleIntersections(
        imported.flatIndex.vertexIds,{});
    std::cout<<"[x0-m06] original-pairs="<<contacts.size()<<'\n';
    for(const auto& c:contacts) {
        const auto a=root->face(c.first).vertices,
                   b=root->face(c.second).vertices;
        std::array<Vec3,3> pa{},pb{};
        for(unsigned i=0;i<3;++i) {
            pa[i]=root->vertex(a[i]).position;
            pb[i]=root->vertex(b[i]).position;
        }
        const auto section=splitTransverseTriangleContact(pa,a,pb,b);
        std::cout<<"[x0-m06] pair="<<c.first<<","<<c.second
                 <<" ambiguous="<<c.ambiguous
                 <<" section="<<static_cast<int>(section.status)
                 <<" start=("<<section.endpoints[0].x<<","
                 <<section.endpoints[0].y<<","<<section.endpoints[0].z<<")"
                 <<" end=("<<section.endpoints[1].x<<","
                 <<section.endpoints[1].y<<","<<section.endpoints[1].z<<")"
                 <<'\n';
    }
    check(contacts.size()==3,
          "X=0 regression uses exactly three genuinely transverse ORIGINAL V13 contacts");
    const auto previousIdentity=root->pickingRootIdentity();
    const auto plan=buildConformingCutPlan(*root,contacts,{},&mirror);
    if(plan.status!=IntersectionSectionStatus::TransverseSegment)
        throw std::runtime_error(
            "X=0 source-proven conforming arrangement refused: "+plan.refusal);
    check(!plan.mirroredSeamCutPoints.empty()&&
          !plan.mirroredCutPoints.empty(),
          "Source-complete invariant cut has genuine fixed points and off-plane paired points");
    std::set<std::size_t> fixedPointIndices(
        plan.mirroredSeamCutPoints.begin(),
        plan.mirroredSeamCutPoints.end());
    check(fixedPointIndices.size()==plan.mirroredSeamCutPoints.size(),
          "Each physical source fixed point appears once in the plan");
    for(const auto i:fixedPointIndices)
        check(i<plan.points.size()&&
              plan.points[i].position.x==0&&
              !plan.points[i].existingVertex,
              "X=0 source seam has exactly one independent new fixed-point plan ID");

    const auto cut=materializeTransverseCutCandidate(
        root,contacts,{},256,&mirror);
    if(!cut.index||!cut.commit.revision)
        throw std::runtime_error("Invariant original contact cut did not materialize");
    check(cut.originalIndex==root&&cut.index!=root&&
          cut.commit.revision.get()==&cut.index->revision()&&
          cut.index->pickingRootIdentity()!=previousIdentity&&
          !cut.mirroredSeamVertices.empty(),
          "Complete source cut uses ONE unpublished COW root with real X=0 vertices");
    const auto coverage=certifyConformingCutCoverage(cut);
    if(!coverage.complete)
        throw std::runtime_error(
            "Invariant conforming source-cut c2 coverage refused: "+coverage.refusal);
    check(cut.sourceContacts.size()==contacts.size()&&
          cut.sectionFirst.size()==contacts.size()&&
          cut.sectionSecond.size()==contacts.size(),
          "Every complete source V13 section has exact independently traced sheet endpoints");
    check(cut.insertedVertices.size()==
              cut.mirroredVertices.size()*2+cut.mirroredSeamVertices.size(),
          "Each inserted vertex is accounted for by one distinct or fixed reflection orbit");
    std::map<VertexId,VertexId> partners(mirror.begin(),mirror.end());
    for(const auto& [a,b]:cut.mirroredVertices) {
        check(a!=b&&!root->maybeVertex(a)&&!root->maybeVertex(b)&&
              partners.emplace(a,b).second&&partners.emplace(b,a).second,
              "New off-plane vertex IDs form real reciprocal reflection orbits");
    }
    for(const auto id:cut.mirroredSeamVertices)
        check(!root->maybeVertex(id)&&cut.index->maybeVertex(id)&&
              partners.emplace(id,id).second&&
              cut.index->vertex(id).position.x==0,
              "One permanent self-mirrored VertexId represents each new source fixed point");
    std::set<FaceId> owned;
    for(const auto& [child,source]:cut.childSources) {
        check(owned.insert(child).second&&root->maybeFace(source)&&
              cut.index->maybeFace(child),
              "Every conforming child has unique FaceId and true original source provenance");
    }
    check(cut.mirroredFaces.size()>0,
          "Invariant and off-plane source children are explicitly journaled");
    std::set<FaceId> paired;
    for(const auto& [a,b]:cut.mirroredFaces) {
        check(owned.contains(a)&&owned.contains(b)&&
              paired.insert(a).second&&
              (a==b||paired.insert(b).second),
              "Every conforming source child is in one exact reciprocal FaceId orbit");
        const auto first=cut.index->face(a).vertices,
                   second=cut.index->face(b).vertices;
        bool reversed=false;
        for(unsigned shift=0;shift<3;++shift) {
            bool match=true;
            for(unsigned j=0;j<3;++j)
                if(!partners.contains(first[j])||
                   partners.at(first[j])!=second[(shift+3-j)%3])
                    match=false;
            if(match)reversed=true;
        }
        check(reversed,
              "Every invariant or reflected child has the exact expected reversed cycle");
    }
    check(paired.size()==owned.size(),
          "No invariant child FaceId was silently omitted or duplicated");
    for(const auto& [id,other]:mirror) {
        check(root->vertex(id).position.x==
              cut.index->vertex(id).position.x&&
              root->vertex(id).position.y==
              cut.index->vertex(id).position.y&&
              root->vertex(id).position.z==
              cut.index->vertex(id).position.z&&
              partners.at(other)==id,
              "Old protected X=0 and off-plane original corners stay byte-exact");
    }
    check(root->pickingRootIdentity()==previousIdentity&&
          root->retiredFaceCount()==0&&root->retiredVertexIds().empty(),
          "The source-cut alone cannot retire any original or mutate historical readers");
    auto missing=contacts;
    missing.pop_back();
    bool incompleteRefused=false;
    try {
        const auto partial=materializeTransverseCutCandidate(
            root,missing,{},256,&mirror);
        incompleteRefused=!certifyConformingCutCoverage(partial).complete;
    }catch(const std::exception&) {
        incompleteRefused=true;
    }
    check(incompleteRefused,
          "One missing ORIGINAL contact is never certified as complete X=0 coverage");
    auto broken=mirror;
    broken.erase(imported.flatIndex.vertexIds[0]);
    const auto rejected=buildConformingCutPlan(*root,contacts,{},&broken);
    check(rejected.status!=IntersectionSectionStatus::TransverseSegment,
          "Absent original mirror map partner cannot produce X=0 source topology");
    ConformingCutLimits tight;
    tight.maxMirroredPairTests=0;
    bool exhausted=false;
    try {
        const auto refused=buildConformingCutPlan(*root,contacts,tight,&mirror);
        exhausted=refused.status!=IntersectionSectionStatus::TransverseSegment;
    }catch(const std::length_error&) {exhausted=true;}
    check(exhausted,
          "Insufficient invariant orbit work never produces a consumable source cut");
    std::cout<<"[x0-m06] cut=1 contacts="<<contacts.size()
             <<" fixed="<<cut.mirroredSeamVertices.size()
             <<" paired="<<cut.mirroredVertices.size()
             <<" children="<<cut.childSources.size()<<'\n';
}
void testMirroredCompleteContactCoverage() {
    // Two real closed intersecting tetrahedra translated to X>0 plus
    // their exact reverse-winding reflection. Unlike the selective four
    // intersection fixtures, this hands the planner the COMPLETE V13
    // original contact graph of BOTH independent mirrored sheet groups.
    Mesh mesh;
    const std::array<Vec3,8> original{{
        {0,0,0},{2,0,0},{0,2,0},
        {.75,-.5,-1},{.75,1.75,1},{.75,.25,-1},
        {.5,.5,-3},{3,.5,0}
    }};
    const std::array<std::array<std::uint32_t,3>,8> faces{{
        {0,1,2},{1,0,6},{2,1,6},{0,2,6},
        {3,4,5},{4,3,7},{5,4,7},{3,5,7}
    }};
    for(const auto p:original)mesh.vertices.push_back({p.x+5,p.y,p.z});
    for(const auto p:original)mesh.vertices.push_back({-p.x-5,p.y,p.z});
    for(const auto t:faces)mesh.triangles.push_back({t[0],t[1],t[2]});
    for(const auto t:faces)
        mesh.triangles.push_back({t[0]+8,t[2]+8,t[1]+8});
    const auto imported=partitionFlatMesh(mesh,256);
    const auto root=RegionTopologyIndex::build(imported.revision);
    const auto historical=root->pickingRootIdentity();
    std::unordered_map<VertexId,VertexId> mirrors;
    for(std::size_t i=0;i<8;++i) {
        const auto a=imported.flatIndex.vertexIds[i],
                   b=imported.flatIndex.vertexIds[i+8];
        mirrors.emplace(a,b);mirrors.emplace(b,a);
    }
    const auto originalContacts=root->triangleIntersections(
        imported.flatIndex.vertexIds,{});
    check(originalContacts.size()>=8&&originalContacts.size()%2==0,
          "Both reflected closed tetra pairs have complete reciprocal original contacts");
    const auto cut=materializeTransverseCutCandidate(
        root,originalContacts,{},256,&mirrors);
    const auto checked=certifyConformingCutCoverage(cut);
    if(!checked.complete)
        throw std::runtime_error(
            "Complete mirrored original contact coverage: "+checked.refusal);
    check(checked.checkedContacts==originalContacts.size()&&
          checked.coveredContacts.size()==originalContacts.size()&&
          !cut.mirroredFaces.empty()&&
          !cut.mirroredVertices.empty(),
          "Both mirrored sheets have complete reciprocal persistent curve coverage");
    CutRemovalSettings supportedContours;
    supportedContours.contours.maximumPlanarityInRadii=4.0;
    supportedContours.contours.maximumSingleCapPlanarityInRadii=4.0;
    supportedContours.contours.maximumIsoperimetricRatio=20.0;
    supportedContours.contours.maximumSingleCapIsoperimetricRatio=20.0;
    supportedContours.contours.maximumPerimeterRatio=20.0;
    // Keep geometrically distant reflected components OUT of the same
    // preliminary V15 zipper candidate window.
    supportedContours.contours.maximumGapInEdges=2.5;
    supportedContours.contours.minimumNormalAlignment=0;
    ExactCutJoinLimits joinLimits;
    joinLimits.removal=supportedContours;
    const auto withoutMap=joinCertifiedCutSections(cut,joinLimits);
    check(!withoutMap.complete&&!withoutMap.index,
          "Reflected weld requires explicit original vertex orbits");
    const auto joined=joinCertifiedCutSections(cut,joinLimits,&mirrors);
    std::cout<<"[seam-join-mirror] complete="<<joined.complete
             <<" reason="<<joined.refusal
             <<" boundaries="<<joined.checkedBoundaryEdges
             <<" welds="<<joined.weldedPairs.size()<<'\n';
    if(!joined.complete)
        throw std::runtime_error("Full mirrored positive seam join refused: "+joined.refusal);
    if(joined.complete) {
        check(connectedClosedSurfaceComponents(*root)==4&&
              connectedClosedSurfaceComponents(*cut.index)==4&&
              connectedClosedSurfaceComponents(*joined.index)==2,
              "Both mirrored pairs each become a single closed connected union");
        check(joined.commit&&joined.index&&
              joined.checkedBoundaryEdges>0&&
              !joined.weldedPairs.empty()&&
              root->pickingRootIdentity()==historical,
              "Both mirrored closed intersections join in ONE unpublished stable-ID RegionEdit");
    }else
        check(!joined.commit&&!joined.index&&
              joined.retiredFaces.empty()&&joined.weldedPairs.empty(),
              "Unsupported mirrored seam geometry safely refuses whole revision");
    ExactCutStepLimits stepLimits;
    stepLimits.join=joinLimits;
    stepLimits.requireMirrors=true;
    const auto net=prepareExactCutStep(root,originalContacts,stepLimits,&mirrors);
    if(!net.complete)
        throw std::runtime_error("Mirrored original-root union Step refused: "+net.refusal);
    check(net.expected==root&&net.final&&
          net.commit.revision.get()==&net.final->revision()&&
          connectedClosedSurfaceComponents(*net.final)==2&&
          !net.addedMirrors.empty()&&
          !net.survivingMirroredFaces.empty()&&
          net.checkedProtectedFaces>0,
          "Two reflected unions yield ONE original-root net Step and mirror journals");
    for(const auto& [a,b]:net.addedMirrors)
        check(!root->maybeVertex(a)&&!root->maybeVertex(b)&&
              net.final->maybeVertex(a)&&net.final->maybeVertex(b)&&
              net.final->vertex(a).position.x==-net.final->vertex(b).position.x,
              "Only surviving NEW reflected VertexIds enter the history delta");
    for(const auto& [a,b]:net.survivingMirroredFaces)
        check(net.final->maybeFace(a)&&net.final->maybeFace(b),
              "Only surviving reflected child faces enter the composed face journal");
    std::vector<std::pair<VertexId,VertexId>> checkpointMirrors;
    for(const auto& [a,b]:mirrors)
        if(a<b) {
            const bool aliveA=net.final->maybeVertex(a)!=nullptr,
                       aliveB=net.final->maybeVertex(b)!=nullptr;
            check(aliveA==aliveB,
                  "Every retired ORIGINAL mirror vertex has a retired reflected partner");
            if(aliveA)checkpointMirrors.emplace_back(a,b);
            else check(std::binary_search(
                net.removedMirrors.begin(),net.removedMirrors.end(),
                std::pair<VertexId,VertexId>{a,b}),
                "Retired historical mirror pair must appear in NET removed delta");
        }
    checkpointMirrors.insert(checkpointMirrors.end(),
                             net.addedMirrors.begin(),net.addedMirrors.end());
    std::sort(checkpointMirrors.begin(),checkpointMirrors.end());
    const auto checkpointPath=std::filesystem::temp_directory_path()/
        "mesher_v17_exact_cut_step_reflected.mshr";
    writeCheckpoint(checkpointPath,{net.final,checkpointMirrors,16});
    const auto recovered=readCheckpoint(checkpointPath);
    std::filesystem::remove(checkpointPath);
    check(connectedClosedSurfaceComponents(*recovered.index)==2&&
          recovered.mirrorPairs==checkpointMirrors&&
          recovered.index->retiredFaceIds()==net.final->retiredFaceIds(),
          "Mirrored original-root union checkpoint restores stable IDs and orbits");
    RevisionHistory mirrorHistory(root,8,32*1024*1024,nullptr,16,16);
    mirrorHistory.append(net.final,net.dirtyPartitions,
                         net.addedMirrors,net.removedMirrors,
                         net.copiedPayloadBytes);
    const auto rewind=mirrorHistory.undo();
    check(rewind.index==root&&
          rewind.removeMirrors==net.addedMirrors&&
          rewind.addMirrors==net.removedMirrors,
          "Single undo restores retired historical mirrors and removes all new pairs");
    const auto replay=mirrorHistory.redo();
    check(replay.index==net.final&&
          replay.addMirrors==net.addedMirrors&&
          replay.removeMirrors==net.removedMirrors,
          "Single redo reapplies NET removed historical and surviving new mirror pairs");
    const auto noMirror=prepareExactCutStep(root,originalContacts,stepLimits);
    check(!noMirror.complete&&!noMirror.final&&noMirror.addedMirrors.empty(),
          "Mirrored original-root Step refuses missing historic involution");
    const auto mirroredProposal=planExactCutRemoval(cut,supportedContours);
    std::cout<<"[exact-removal-mirror-probe] status="
             <<static_cast<int>(mirroredProposal.status)
             <<" reason="<<mirroredProposal.refusal
             <<" removed="<<mirroredProposal.proposedRemoval.size()
             <<" mirrorPairs="<<mirroredProposal.mirroredRemoval.size()
             <<'\n';
    if(!mirroredProposal.ready())
        throw std::runtime_error(
            "Full mirrored exact-child nomination refused: "+
            mirroredProposal.refusal+" (status="+
            std::to_string(static_cast<int>(mirroredProposal.status))+")");
    if(mirroredProposal.ready()) {
        check(!mirroredProposal.proposedRemoval.empty()&&
              !mirroredProposal.protectedChildren.empty(),
              "Mirrored exact-child policy preserves both live boundary halves");
        check(mirroredProposal.mirroredRemoval.size()*2==
                  mirroredProposal.proposedRemoval.size()&&
              mirroredProposal.contourPlan.ready()&&
              mirroredProposal.mirroredContours.size()*2==
                  mirroredProposal.contourPlan.contours.size(),
              "Mirror FaceIds, contour loops and V15 repair choices form complete orbits");
    }else
        check(mirroredProposal.proposedRemoval.empty()||
              mirroredProposal.status==CutRemovalStatus::NoChanges,
              "Unaccepted mirrored removal cannot expose partial face retirements");
    ExactCutSurgerySettings gated;
    gated.removal=supportedContours;
    gated.surgery.planning=supportedContours.contours;
    const auto missingMirrors=attemptPrivateExactCutSurgery(cut,gated);
    check(missingMirrors.status==ExactCutSurgeryStatus::Refused&&
          !missingMirrors.surgery,
          "Reflected V16 must have explicit historical mirror-map provenance");
    const auto mirroredSurgery=attemptPrivateExactCutSurgery(
        cut,gated,&mirrors);
    std::cout<<"[exact-surgery-mirror-probe] status="
             <<static_cast<int>(mirroredSurgery.status)
             <<" reason="<<mirroredSurgery.refusal
             <<" selected="<<mirroredSurgery.selectedFaces<<'\n';
    if(mirroredSurgery.status==ExactCutSurgeryStatus::PrivateValidated)
        check(mirroredSurgery.surgery.has_value()&&
              mirroredSurgery.checkedFinalContacts==0,
              "Reflected V16 returns only full-candidate collision-free root");
    else
        check(mirroredSurgery.status==ExactCutSurgeryStatus::Refused&&
              !mirroredSurgery.surgery&&
              !mirroredSurgery.refusal.empty(),
              "Unsupported reflected scar/seam topology refuses all publication");
    auto damagedOriginalMirrors=mirrors;
    damagedOriginalMirrors.erase(imported.flatIndex.vertexIds.front());
    const auto badMap=attemptPrivateExactCutSurgery(
        cut,gated,&damagedOriginalMirrors);
    check(badMap.status==ExactCutSurgeryStatus::Refused&&
          !badMap.surgery,
          "Noninvolutive historical mirror mapping cannot reach RegionEdit");
    auto incompleteMirror=cut;
    incompleteMirror.mirroredFaces.pop_back();
    const auto badOrbit=planExactCutRemoval(incompleteMirror);
    check(badOrbit.status==CutRemovalStatus::Refused&&
          badOrbit.proposedRemoval.empty(),
          "A missing mirrored child face pair vetoes both paired removal halves");

    check(root->pickingRootIdentity()==historical&&
          root->retiredFaceCount()==0,
          "Mirrored full-contact certification preserves all source roots");
}

void testMirroredMultipleSectionsReadOnly() {
    const auto exercise=[&](bool tJunction) {
        Mesh mesh;
        const std::array<Vec3,12> positions=tJunction
            ?std::array<Vec3,12>{{
                {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
                {.7,-2,-1},{.7,3,-1},{.7,.5,2},{2.5,.5,3},
                {-1,.7,-1},{.7,.7,-1},{.7,.7,1},{.5,2.5,3}
            }}
            :std::array<Vec3,12>{{
                {0,0,0},{2,0,0},{0,2,0},{0,0,-3},
                {.7,-2,-1},{.7,3,-1},{.7,.5,2},{2.5,.5,3},
                {-2,.7,-1},{3,.7,-1},{.5,.7,2},{.5,2.5,3}
            }};
        // Both groups consist of three independently closed tetrahedra.
        // Offsetting the first group to X>0 leaves their genuine multi-
        // section contact intact, and the other group is its exact mirror.
        for(const auto p:positions)
            mesh.vertices.push_back({5+p.x,p.y,p.z});
        for(const auto p:positions)
            mesh.vertices.push_back({-5-p.x,p.y,p.z});
        for(unsigned half=0;half<2;++half)
            for(unsigned solid=0;solid<3;++solid) {
                const auto v=12u*half+4u*solid;
                for(const auto t:std::array<std::array<unsigned,3>,4>{{
                    {0,1,2},{1,0,3},{2,1,3},{0,2,3}
                }}) {
                    if(half==0)mesh.triangles.push_back({
                        v+t[0],v+t[1],v+t[2]});
                    else mesh.triangles.push_back({
                        v+t[0],v+t[2],v+t[1]});
                }
            }
        const auto imported=partitionFlatMesh(mesh,256);
        const auto index=RegionTopologyIndex::build(imported.revision);
        const auto oldRoot=index->pickingRootIdentity();
        std::unordered_map<VertexId,VertexId> reflection;
        for(std::size_t i=0;i<12;++i) {
            const auto a=imported.flatIndex.vertexIds[i],
                       b=imported.flatIndex.vertexIds[i+12];
            reflection.emplace(a,b);
            reflection.emplace(b,a);
        }
        const auto all=index->triangleIntersections(
            imported.flatIndex.vertexIds,{});
        const auto& faces=imported.flatIndex.faceIds;
        const auto lookup=[&](FaceId a,FaceId b) {
            const auto ordered=std::minmax(a,b);
            const auto found=std::find_if(all.begin(),all.end(),
                [&](const auto& pair) {
                    return pair.first==ordered.first&&
                           pair.second==ordered.second;
                });
            check(found!=all.end(),"Mirror fixture requires actual V13 transverse pairs");
            return *found;
        };
        const std::vector<RegionTopologyIndex::CollisionPair> contacts{
            lookup(faces[0],faces[4]),lookup(faces[0],faces[8]),
            lookup(faces[12],faces[16]),lookup(faces[12],faces[20])};
        const auto plan=buildConformingCutPlan(*index,contacts,{},&reflection);
        if(plan.status!=IntersectionSectionStatus::TransverseSegment)
            throw std::runtime_error("Mirrored multi-section arrangement: "+plan.refusal);
        check(plan.segments.size()==4&&plan.mirroredCutPoints.size()>6,
              "Both reflected three-tetrahedron intersection graphs pair completely");
        std::vector<std::size_t> pointOrbit(plan.points.size(),plan.points.size());
        for(const auto& [a,b]:plan.mirroredCutPoints) {
            check(a!=b&&pointOrbit[a]==plan.points.size()&&
                  pointOrbit[b]==plan.points.size(),
                  "Mirrored arrangement has distinct, unique cut-point orbits");
            pointOrbit[a]=b;pointOrbit[b]=a;
            const auto p=plan.points[a].position,q=plan.points[b].position;
            check(p.x==-q.x&&p.y==q.y&&p.z==q.z,
                  "Mirror reconciliation uses EXACT reflected cut coordinates");
        }
        for(std::size_t i=0;i<plan.points.size();++i)
            if(plan.points[i].existingVertex) {
                const auto counterpart=reflection.at(*plan.points[i].existingVertex);
                const auto found=std::find_if(plan.points.begin(),plan.points.end(),
                    [&](const auto& p) {
                        return p.existingVertex&&*p.existingVertex==counterpart;
                    });
                check(found!=plan.points.end(),
                      "Original point also has a mirrored orbit in the plan");
                pointOrbit[i]=static_cast<std::size_t>(found-plan.points.begin());
            }
        for(const auto i:pointOrbit)
            check(i<plan.points.size(),
                  "Every original and generated arrangement point is explicitly paired");
        for(const auto& part:plan.faces) {
            if(std::find(faces.begin(),faces.begin()+12,
                         part.sourceFace)==faces.begin()+12)continue;
            auto reflectedVertices=index->face(part.sourceFace).vertices;
            for(auto& id:reflectedVertices)id=reflection.at(id);
            std::sort(reflectedVertices.begin(),reflectedVertices.end());
            const auto mirror=std::find_if(plan.faces.begin(),plan.faces.end(),
                [&](const auto& candidate) {
                    auto corners=index->face(candidate.sourceFace).vertices;
                    std::sort(corners.begin(),corners.end());
                    return corners==reflectedVertices;
                });
            check(mirror!=plan.faces.end()&&
                  part.triangles.size()==mirror->triangles.size(),
                  "Each changed face has the same number of reflected child triangles");
            for(std::size_t j=0;j<part.triangles.size();++j) {
                const auto t=part.triangles[j],m=mirror->triangles[j];
                check(m[0]==pointOrbit[t[0]]&&
                      m[1]==pointOrbit[t[2]]&&
                      m[2]==pointOrbit[t[1]],
                      "Each reflected child triangle has exact paired topology and winding");
            }
        }
        check(index->pickingRootIdentity()==oldRoot&&
              index->retiredFaceCount()==0,
              "Mirrored arrangements leave original revision and BVH pointer untouched");
        // A complete paired RegionEdit makes new permanent IDs but returns
        // only ONE unpublished revision; the old reader stays pointer-stable.
        const auto cut=materializeTransverseCutCandidate(
            index,contacts,{},256,&reflection);
        check(cut.index&&cut.index!=index&&cut.commit.revision&&
              cut.commit.revision.get()==&cut.index->revision()&&
              cut.sectionFirst.size()==4&&cut.sectionSecond.size()==4,
              "Mirrored intersection curves become one private RegionEdit candidate");
        check(cut.rewrittenFaces.size()==plan.faces.size()&&
              cut.mirroredVertices.size()*2==cut.insertedVertices.size()&&
              cut.mirroredFaces.size()*2==
                  cut.rewrittenFaces.size()+cut.insertedFaces.size()&&
              !cut.insertedFaces.empty(),
              "Stable face and vertex allocation provides full paired net journal deltas");
        std::unordered_map<VertexId,VertexId> complete=reflection;
        std::set<VertexId> newIds;
        for(const auto& [a,b]:cut.mirroredVertices) {
            const auto p=cut.index->vertex(a).position,
                       q=cut.index->vertex(b).position;
            check(a!=b&&newIds.insert(a).second&&newIds.insert(b).second&&
                  !index->maybeVertex(a)&&!index->maybeVertex(b)&&
                  p.x==-q.x&&p.y==q.y&&p.z==q.z,
                  "Each new cut point has a unique persistent reflected vertex ID");
            check(complete.emplace(a,b).second&&complete.emplace(b,a).second,
                  "New vertices extend only the staged local mirror correspondence");
        }
        std::set<FaceId> pairedFaces;
        for(const auto& [a,b]:cut.mirroredFaces) {
            check(a!=b&&pairedFaces.insert(a).second&&
                  pairedFaces.insert(b).second&&
                  cut.index->maybeFace(a)&&cut.index->maybeFace(b),
                  "Every retained or new changed face has one permanent reflected FaceId");
            const auto one=cut.index->face(a).vertices,
                       two=cut.index->face(b).vertices;
            const std::array<VertexId,3> expected{
                complete.at(one[0]),complete.at(one[2]),complete.at(one[1])};
            bool same=false;
            for(unsigned shift=0;shift<3;++shift)
                if(expected[0]==two[shift]&&
                   expected[1]==two[(shift+1)%3]&&
                   expected[2]==two[(shift+2)%3])same=true;
            check(same,"Staged child faces have exactly reversed reflected winding");
        }
        for(const auto id:cut.rewrittenFaces)
            check(index->maybeFace(id)&&cut.index->maybeFace(id),
                  "Rewritten original face retains its permanent stable FaceId");
        for(const auto id:cut.insertedFaces)
            check(!index->maybeFace(id)&&cut.index->maybeFace(id),
                  "Mirrored inserted child FaceIds exist only in candidate revision");
        for(std::size_t k=0;k<cut.sectionFirst.size();++k)
            for(unsigned endpoint=0;endpoint<2;++endpoint) {
                const auto a=cut.sectionFirst[k][endpoint],
                           b=cut.sectionSecond[k][endpoint];
                check(a!=b&&
                      length(cut.index->vertex(a).position-
                             cut.index->vertex(b).position)<1e-8,
                      "Distinct intersecting sheet identities survive permanent staging");
            }
        check(!cut.index->triangleIntersections(cut.insertedVertices,{}).empty(),
              "A private symmetric subdivision is NOT yet a Boolean union");
        check(cut.originalIndex==index&&
              cut.childSources.size()==cut.rewrittenFaces.size()+
                                       cut.insertedFaces.size(),
              "Mirrored persistent cut returns provenance for both child face orbits");
        const auto assessed=assessConformingCutFragments(cut);
        check((assessed.complete&&
               assessed.fragments.size()==cut.childSources.size())||
              (!assessed.complete&&assessed.fragments.empty()&&
               !assessed.refusal.empty()),
              "Mirrored true-child assessment never leaks partial authoritative labels");
        check(index->pickingRootIdentity()==oldRoot&&
              index->retiredFaceCount()==0,
              "One mirrored commit preserves all original immutable reader objects");

        // The same paired RegionEdit path also handles the traditional
        // mirrored single-section conformance graph, not only arrangements.
        const std::vector<RegionTopologyIndex::CollisionPair> single{
            contacts[0],contacts[2]};
        const auto singleCut=materializeTransverseCutCandidate(
            index,single,{},256,&reflection);
        check(singleCut.index&&singleCut.sectionFirst.size()==2&&
              !singleCut.mirroredFaces.empty()&&
              !singleCut.mirroredVertices.empty(),
              "Mirrored single-section contact also materializes privately");
        bool limited=false;
        try {(void)materializeTransverseCutCandidate(
            index,contacts,{},1,&reflection);}
        catch(const std::length_error&) {limited=true;}
        check(limited&&index->pickingRootIdentity()==oldRoot,
              "Writable partition capacity refusal leaks no partial revision");
        auto incomplete=reflection;
        incomplete.erase(imported.flatIndex.vertexIds[0]);
        bool missingPair=false;
        try {(void)materializeTransverseCutCandidate(
            index,contacts,{},256,&incomplete);}
        catch(const std::logic_error&) {missingPair=true;}
        check(missingPair&&index->pickingRootIdentity()==oldRoot,
              "Missing mirrored original vertex refuses persistent staging");
        auto partialContacts=contacts;
        partialContacts.pop_back();
        bool incompleteOrbit=false;
        try {(void)materializeTransverseCutCandidate(
            index,partialContacts,{},256,&reflection);}
        catch(const std::logic_error&) {incompleteOrbit=true;}
        check(incompleteOrbit,"Missing reflected section refuses the entire private edit");
        ConformingCutLimits permanentLimits;
        permanentLimits.maxMirroredPairTests=0;
        bool capped=false;
        try {(void)materializeTransverseCutCandidate(
            index,contacts,permanentLimits,256,&reflection);}
        catch(const std::length_error&) {capped=true;}
        check(capped&&index->pickingRootIdentity()==oldRoot,
              "Mirrored pairing limit refuses before permanent ID staging");
        auto missing=reflection;
        missing.erase(imported.flatIndex.vertexIds[0]);
        const auto refused=buildConformingCutPlan(*index,contacts,{},&missing);
        check(refused.status==IntersectionSectionStatus::UnsupportedContact&&
              refused.points.empty()&&refused.faces.empty(),
              "Missing mirror vertex refuses the entire multiple-section cut graph");
        auto partial=contacts;
        partial.pop_back();
        const auto unmatched=buildConformingCutPlan(*index,partial,{},&reflection);
        check(unmatched.status==IntersectionSectionStatus::UnsupportedContact&&
              unmatched.points.empty(),
              "Missing reflected contact curve cannot leave a partial plan");
        ConformingCutLimits budgets;
        budgets.maxMirroredPairTests=0;
        bool overBudget=false;
        try {(void)buildConformingCutPlan(*index,contacts,budgets,&reflection);}
        catch(const std::length_error&) {overBudget=true;}
        check(overBudget&&index->pickingRootIdentity()==oldRoot,
              "Mirrored pairing has a separate hard work ceiling and no published effects");
    };
    exercise(false); // proper crossing inside original face
    exercise(true);  // interior T-junction
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
        testConformingSharedEdgeGraphAndBudgets();
        testConformingReflectedCutGraph();
        testPrivateConformingRevisionCommitsAtomically();
        testCrossingMultipleSectionsPerOriginalTriangle();
        testSectionsMeetAtInteriorTjunction();
        testSeparateCompleteSectionsOnOneOriginalFace();
        testFullThreeBodyContactAdmission();
        testWhollyBuriedUncutOriginalFace();
        testGenericCompleteThreeBodyTransverseGraph();
        testOrthogonalTripleJunctionProbe();
        testMirroredMultipleSectionsReadOnly();
        testX0InvariantExactCutProbe();
        testReflectedThreeBodyCompleteOriginalContacts();
        testReflectedUncutBurialCavityOrbits();
        testMirroredCompleteContactCoverage();
        testSimultaneouslyMovingSolids();
        testBoundedCollisionDiscovery();
        testPruningAndNoOp();
        std::cout<<"Revision triangle collision regression passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Revision triangle collision failure: "<<error.what()<<'\n';
        return 1;
    }
}

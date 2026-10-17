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
#include <unordered_map>
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
    if(joined.complete)
        check(joined.commit&&joined.index&&
              joined.checkedBoundaryEdges>0&&
              !joined.weldedPairs.empty()&&
              root->pickingRootIdentity()==historical,
              "Both mirrored closed intersections join in ONE unpublished stable-ID RegionEdit");
    else
        check(!joined.commit&&!joined.index&&
              joined.retiredFaces.empty()&&joined.weldedPairs.empty(),
              "Unsupported mirrored seam geometry safely refuses whole revision");
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
        testMirroredMultipleSectionsReadOnly();
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

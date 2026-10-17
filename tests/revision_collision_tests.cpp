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
    auto edgePoint=bside[0];
    auto incidentEdge=result.points[edgePoint].boundaryEdge;
    if(!incidentEdge) {
        edgePoint=bside[1];
        incidentEdge=result.points[edgePoint].boundaryEdge;
    }
    check(incidentEdge.has_value(),
          "At least one B intersection endpoint lies on a shared source edge");
    const auto incident=index->edgeSides(*incidentEdge);
    check(incident.size()==2,
          "The shared cut edge belongs to exactly two original incident faces");
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
    check(result.status==IntersectionSectionStatus::TransverseSegment&&
          result.faces.size()==6&&result.segments.size()==2&&
          result.mirroredCutPoints.size()==4,
          "Two independent exact reflections pair every cut node without welding solids");
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
        testSimultaneouslyMovingSolids();
        testBoundedCollisionDiscovery();
        testPruningAndNoOp();
        std::cout<<"Revision triangle collision regression passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Revision triangle collision failure: "<<error.what()<<'\n';
        return 1;
    }
}

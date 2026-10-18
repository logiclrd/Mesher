#include "mesher/repair_surgery.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "../src/repair_surgery_detail.hpp"
#include "../src/repair_plane_internal.hpp"
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <numbers>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace mesher;
namespace {
void require(bool condition,const char* reason) {
    if(!condition)throw std::runtime_error(reason);
}
void cube(Mesh& m,Vec3 center,double half) {
    const auto first=static_cast<std::uint32_t>(m.vertices.size());
    for(const auto offset:std::array<Vec3,8>{{
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
    }})m.vertices.push_back(center+offset*half);
    for(const auto face:std::array<std::array<std::uint32_t,3>,12>{{
        {0,2,1},{0,3,2},{4,5,6},{4,6,7},
        {0,1,5},{0,5,4},{3,7,6},{3,6,2},
        {0,4,7},{0,7,3},{1,2,6},{1,6,5}
    }})m.triangles.push_back({first+face[0],first+face[1],first+face[2]});
}
struct CapIndex {std::size_t left{},right{},count{};};
CapIndex prism(Mesh& mesh,double x0,double x1,
               const std::vector<std::array<double,2>>& ring) {
    const auto n=ring.size();
    const auto start=static_cast<std::uint32_t>(mesh.vertices.size());
    for(const auto x:{x0,x1})
        for(const auto& yz:ring)mesh.vertices.push_back({x,yz[0],yz[1]});
    Vec3 average{};
    for(const auto& yz:ring)average=average+Vec3{0,yz[0],yz[1]};
    average=average/static_cast<double>(n);
    const auto leftCenter=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({x0,average.y,average.z});
    const auto rightCenter=static_cast<std::uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back({x1,average.y,average.z});
    for(std::uint32_t i=0;i<n;++i) {
        const auto next=(i+1)%static_cast<std::uint32_t>(n);
        mesh.triangles.push_back({start+i,start+next,
                                  start+next+static_cast<std::uint32_t>(n)});
        mesh.triangles.push_back({start+i,
                                  start+next+static_cast<std::uint32_t>(n),
                                  start+i+static_cast<std::uint32_t>(n)});
    }
    const auto left=mesh.triangles.size();
    for(std::uint32_t i=0;i<n;++i)
        mesh.triangles.push_back({leftCenter,start+(i+1)%
                                  static_cast<std::uint32_t>(n),start+i});
    const auto right=mesh.triangles.size();
    for(std::uint32_t i=0;i<n;++i)
        mesh.triangles.push_back({rightCenter,start+i+
                                  static_cast<std::uint32_t>(n),
                                  start+(i+1)%static_cast<std::uint32_t>(n)+
                                  static_cast<std::uint32_t>(n)});
    return {left,right,n};
}
std::vector<std::array<double,2>> square(double size) {
    return {{-size,-size},{size,-size},{size,size},{-size,size}};
}
std::vector<std::array<double,2>> unevenSquare(double size) {
    return {{-size,-size},{-.95*size,-size},{-.8*size,-size},
            {-.4*size,-size},{0,-size},{size,-size},
            {size,size},{-size,size}};
}
std::vector<FaceId> capIds(const PartitionedMeshImport& imp,
                           CapIndex range,bool left) {
    std::vector<FaceId> out;
    for(std::size_t i=0;i<range.count;++i)
        out.push_back(imp.flatIndex.faceIds[
            (left?range.left:range.right)+i]);
    return out;
}
std::size_t euler(const RegionTopologyIndex& index) {
    std::size_t vertices=0,faces=0;
    std::set<std::pair<VertexId,VertexId>> edges;
    index.revision().forEachPartition([&](PartitionId,const MeshPartition& part) {
        vertices+=part.vertices.size();
        faces+=part.faces.size();
        for(const auto& f:part.faces)
            for(unsigned i=0;i<3;++i) {
                auto a=f.vertices[i],b=f.vertices[(i+1)%3];
                if(a>b)std::swap(a,b);
                edges.emplace(a,b);
            }
    });
    return vertices+faces-edges.size();
}
void testPureMultiEdgeRetainedFaceSplitting() {
    using mesher::detail::SideSplits;
    const std::unordered_map<VertexId,Vec3> p={
        {1,{0,0,0}},{2,{2,0,0}},{3,{0,2,0}},
        {4,{1,0,0}},{5,{1,1,0}},{6,{0,1,0}}};
    const RepairSurgerySettings settings;
    for(const bool threeEdges:{false,true}) {
        SideSplits splits={{{1,2},{4}},{{2,3},{5}}};
        if(threeEdges)splits.emplace(
            std::pair<VertexId,VertexId>{3,1},std::vector<VertexId>{6});
        const auto faces=mesher::detail::subdivideRetainedTriangle(
            {1,2,3},splits,p,settings);
        require(faces.size()==(threeEdges?4u:3u),
                "One triangle is split along two or all three boundary edges");
        double area=0;
        std::map<std::pair<VertexId,VertexId>,unsigned> incidences;
        for(const auto& ids:faces) {
            const auto a=p.at(ids[0]),b=p.at(ids[1]),d=p.at(ids[2]);
            const double orientedArea=cross(b-a,d-a).z*.5;
            require(orientedArea>0,
                    "All subtile faces retain the original triangle winding");
            area+=orientedArea;
            for(unsigned i=0;i<3;++i) {
                auto u=ids[i],v=ids[(i+1)%3];
                if(u>v)std::swap(u,v);
                ++incidences[{u,v}];
            }
        }
        require(std::abs(area-2)<1e-12,
                "Multi-edge subfaces partition the original area exactly");
        for(const auto& [edge,count]:incidences) {
            (void)edge;
            require(count==1||count==2,
                    "No internal edge has a nonmanifold degree");
        }
        require(incidences[{1,4}]==1&&incidences[{2,4}]==1&&
                incidences[{2,5}]==1&&incidences[{3,5}]==1,
                "Both originally long boundary sides are conformingly subdivided");
        if(threeEdges)
            require(incidences[{3,6}]==1&&incidences[{1,6}]==1,
                    "Third side subdivides without hanging endpoints");
    }
}
void testCapsAndSnapshotPublication() {
    Mesh m;
    cube(m,{},1);
    const auto imp=partitionFlatMesh(m,1);
    const auto baseline=RegionTopologyIndex::build(imp.revision);
    const auto firstRoot=baseline->pickingRootIdentity();
    const auto oldFace=imp.flatIndex.faceIds[2];
    const auto secondFace=imp.flatIndex.faceIds[3];
    const auto plan=planRemovalContours(*baseline,{oldFace,secondFace});
    require(plan.ready()&&plan.cappedContours.size()==1,
            "Single square opening chooses an independent cap");
    IndexedMeshRepository repository(baseline);
    const auto repaired=applyRepairSurgery(baseline,plan);
    require(repaired.capFaces==2&&repaired.zipperFaces==0,
            "Four-sided opening is sealed by two oriented triangles");
    RepairSurgerySettings reverse;
    reverse.requireReverseSurfaceFidelity=true;
    reverse.maximumReverseDeviationInEdges=.01;
    const auto exact=applyRepairSurgery(baseline,plan,reverse);
    require(exact.maximumReverseDeviationInEdges<1e-10,
            "Planar replacement preserves both sides of the exposed original surface");
    require(repaired.index->retiredFace(oldFace)&&
            repaired.index->retiredFace(secondFace),
            "Original removed IDs remain retired, never recycled");
    require(repaired.index->retiredVertexCount()==0,
            "Original rim vertices preserve identity");
    require(euler(*repaired.index)==2,
            "Capped closed cube remains a sphere topology");
    require(repaired.index->triangleIntersections(
        {},repaired.insertedFaces).empty(),
        "Cap does not cross other faces");
    require(baseline->pickingRootIdentity()==firstRoot&&
            !baseline->retiredFace(oldFace),
            "Old immutable revision remains untouched");
    require(repository.snapshot()==baseline,
            "No publication occurs merely because surgery succeeds");
    require(repository.publishIfCurrent(baseline,repaired.index),
            "One atomic root exchange publishes validated revision");
    require(repository.snapshot()==repaired.index,
            "New repository snapshot owns repaired root");
    require(!repository.publishIfCurrent(baseline,repaired.index),
            "Stale publication cannot overwrite a newer root");
}
void testZipperJoinsSeparatedSolids() {
    Mesh m;
    cube(m,{-1.1,0,0},1);
    cube(m,{1.1,0,0},1);
    const auto imp=partitionFlatMesh(m,1);
    const auto baseline=RegionTopologyIndex::build(imp.revision);
    const auto& f=imp.flatIndex.faceIds;
    const std::vector<FaceId> red{f[10],f[11],f[12+8],f[12+9]};
    const auto plan=planRemovalContours(*baseline,red);
    require(plan.ready()&&plan.pairs.size()==1,
            "Two nearby square rings choose zipper");
    const auto repaired=applyRepairSurgery(baseline,plan);
    require(repaired.zipperFaces>=8&&repaired.capFaces==0,
            "Zipper closes two entire oriented rings");
    RepairSurgerySettings preserveExposed;
    preserveExposed.requireExposedFragmentFidelity=true;
    preserveExposed.maximumReverseDeviationInEdges=.01;
    bool exposedLossRejected=false;
    try {(void)applyRepairSurgery(baseline,plan,preserveExposed);}
    catch(const std::logic_error&){exposedLossRejected=true;}
    require(exposedLossRejected,
            "Explicit exterior-detail preservation prevents a zipper from erasing exposed cap centers");
    RepairSurgerySettings reverse;
    reverse.requireReverseSurfaceFidelity=true;
    reverse.maximumReverseDeviationInEdges=.01;
    bool rejected=false;
    try {(void)applyRepairSurgery(baseline,plan,reverse);}
    catch(const std::logic_error&) {rejected=true;}
    require(rejected,
            "Intentional material removal cannot pass an inappropriate strict reverse envelope");
    require(euler(*baseline)==4&&euler(*repaired.index)==2,
            "Joining distinct closed solids changes total Euler characteristic 4 to 2");
    require(repaired.index->triangleIntersections(
        {},repaired.insertedFaces).empty(),
        "Zipper produces no residual crossing");
    require(repaired.index->updateStatistics().fullMeshTraversals==0,
            "Region commit updates only touched persistent index branches");
}
void testUnequalSpacingSharedEdgeSplits() {
    Mesh m;
    const auto left=prism(m,-2,0,square(1));
    const auto right=prism(m,.1,2,unevenSquare(1));
    const auto imp=partitionFlatMesh(m,1);
    const auto baseline=RegionTopologyIndex::build(imp.revision);
    auto removed=capIds(imp,left,false);
    const auto other=capIds(imp,right,true);
    removed.insert(removed.end(),other.begin(),other.end());
    const auto plan=planRemovalContours(*baseline,removed);
    require(plan.ready()&&plan.pairs.size()==1,
            "Uneven boundary densities select zipper");
    const auto repaired=applyRepairSurgery(baseline,plan);
    require(repaired.splitBoundaryEdges>0&&
            !repaired.insertedVertices.empty(),
            "Long boundary edges are subdivided by physical arc length");
    require(repaired.zipperFaces>=plan.contours[0].vertexIds.size()+
                                   plan.contours[1].vertexIds.size(),
            "Adaptive zipper can insert enough triangles for unequal spacings");
    require(euler(*repaired.index)==2,
            "Surviving-face splits introduce no T-junction or invalid topology");
    require(repaired.index->triangleIntersections(
        {},repaired.insertedFaces).empty(),
        "Refined zipper triangles do not cross any current geometry");
    require(baseline->retiredFaceCount()==0,
            "Old snapshot and old identities remain valid after refinement");
}
struct MirroredCubes {
    PartitionedMeshImport import;
    std::vector<std::pair<VertexId,VertexId>> mirrors;
};
MirroredCubes reflectedCubes(double displacement) {
    Mesh m;
    cube(m,{-displacement,0,0},1);
    // Reflection reverses triangle winding but shares no vertices;
    // the X=0 closure surgery must create ONE common seam ring.
    for(unsigned i=0;i<8;++i) {
        const auto p=m.vertices[i];
        m.vertices.push_back({-p.x,p.y,p.z});
    }
    for(unsigned i=0;i<12;++i) {
        const auto f=m.triangles[i];
        m.triangles.push_back({f.a+8,f.c+8,f.b+8});
    }
    MirroredCubes result;
    result.import=partitionFlatMesh(m,1);
    for(unsigned i=0;i<8;++i)
        result.mirrors.push_back({result.import.flatIndex.vertexIds[i],
                                  result.import.flatIndex.vertexIds[i+8]});
    return result;
}
struct MirroredNestedSolids {
    PartitionedMeshImport import;
    std::vector<MirrorPair> pairs;
    std::array<FaceId,2> internalSeeds{};
};
MirroredNestedSolids reflectedNestedSolids() {
    Mesh mesh;
    cube(mesh,{-4,0,0},2.5);
    cube(mesh,{-4,0,0},.5);
    const auto count=static_cast<std::uint32_t>(mesh.vertices.size());
    const auto faceCount=mesh.triangles.size();
    for(std::uint32_t i=0;i<count;++i) {
        const auto p=mesh.vertices[i];
        mesh.vertices.push_back({-p.x,p.y,p.z});
    }
    for(std::size_t i=0;i<faceCount;++i) {
        const auto t=mesh.triangles[i];
        mesh.triangles.push_back({t.a+count,t.c+count,t.b+count});
    }
    auto imported=partitionFlatMesh(mesh,1);
    MirroredNestedSolids fixture;
    for(std::uint32_t i=0;i<count;++i)
        fixture.pairs.emplace_back(
            imported.flatIndex.vertexIds[i],
            imported.flatIndex.vertexIds[i+count]);
    fixture.internalSeeds={
        imported.flatIndex.faceIds[12],
        imported.flatIndex.faceIds[faceCount+12]};
    fixture.import=std::move(imported);
    return fixture;
}
std::unordered_map<VertexId,VertexId> mirrorMap(
    const std::vector<MirrorPair>& pairs) {
    std::unordered_map<VertexId,VertexId> mapped;
    for(const auto& [a,b]:pairs) {
        mapped.emplace(a,b);
        mapped.emplace(b,a);
    }
    return mapped;
}
void testMirroredCandidateRetirementAndLiveRollback() {
    auto fixture=reflectedNestedSolids();
    const auto initial=RegionTopologyIndex::build(fixture.import.revision);
    const auto originalRoot=initial->pickingRootIdentity();
    SculptCollisionSettings mode;
    mode.enabled=true;
    mode.isolatedComponentSeeds.assign(
        fixture.internalSeeds.begin(),fixture.internalSeeds.end());
    const ProposedSculptStep proposed{initial,initial,{},{}};
    const auto initialMap=mirrorMap(fixture.pairs);
    const auto delta=evaluateSculptCollisionCandidate(
        proposed,true,mode,mode.isolatedComponentSeeds,&initialMap);
    require(delta.safe()&&delta.plan.ready()&&delta.repair&&
            delta.repair->retiredVertices.size()==16&&
            delta.repair->retiredFaces.size()==24&&
            delta.retiredMirrors.size()==8&&delta.insertedMirrors.empty(),
            "Classified reflected interior shells retire complete mirror orbits");
    auto incompleteRetirement=delta;
    incompleteRetirement.repair->retiredVertices.pop_back();
    bool orphanRejected=false;
    try {deriveMirroredRepairDeltas(incompleteRetirement,*initial,initialMap);}
    catch(const std::logic_error&) {orphanRejected=true;}
    require(orphanRejected,"Deleting only one end of a mirrored vertex pair is refused");
    require(initial->retiredFaceCount()==0&&
            initial->pickingRootIdentity()==originalRoot,
            "Symmetric classified evaluation preserves original index identity");

    DurableCheckpoint recovered;
    recovered.index=initial;
    recovered.mirrorPairs=fixture.pairs;
    recovered.initialVertexCount=fixture.import.flatIndex.vertexIds.size();
    std::size_t called=0;
    VersionedSculptBrush brush(recovered,.001,{}, {},{},256,
        [&](const ProposedSculptStep& p)->std::string {
            ++called;
            require(p.candidate->retiredFaceCount()==24,
                    "Validator sees private symmetric repaired candidate, never partial state");
            return called==1?"Intentional mirrored repair veto":"";
        },mode);
    Mesh dummy;
    const std::array<BrushArea,2> areas{{
        {{-6.5,-2.5,-2.5},.9,.6},{{6.5,-2.5,-2.5},.9,.6}
    }};
    const auto old=brush.snapshot();
    const auto origPairs=brush.checkpoint().mirrorPairs;
    brush.stamp(dummy,areas);
    require(called==1&&brush.rejectedStamps()==1&&
            brush.snapshot()==old&&brush.publishedRevisions()==0&&
            brush.historyStatistics().undoDepth==0&&
            brush.checkpoint().mirrorPairs==origPairs&&
            brush.lastDirtyPartitions().empty(),
            "Mirror retirement veto rolls back both speculative maps and history");
    brush.stamp(dummy,areas);
    const auto accepted=brush.snapshot();
    const auto finalPairs=brush.checkpoint().mirrorPairs;
    require(called==2&&brush.rejectedStamps()==1&&
            brush.collisionRepairedStamps()==1&&
            accepted!=old&&brush.publishedRevisions()==1&&
            brush.historyStatistics().undoDepth==1&&
            finalPairs.size()==origPairs.size()-8&&
            accepted->retiredFaceCount()==24,
            "Accepted symmetric excision retires exactly eight mirror pairs in one CAS");
    require(brush.lastDetails().vertices==16&&
            brush.lastDetails().triangles==24&&
            !brush.lastDirtyPartitions().empty(),
            "Sculpt metrics and dirty renderer partitions describe final symmetric root");
    const auto saved=brush.checkpoint();
    const auto path=std::filesystem::temp_directory_path()/
        "mesher-v17d2-mirrored-acceptance.chk";
    try {
        writeCheckpoint(path,saved);
        const auto restored=readCheckpoint(path);
        require(restored.mirrorPairs==saved.mirrorPairs&&
                restored.index->retiredFaceCount()==24,
                "Round-trip checkpoint retains mirrored repair tombstones and live orbit map");
    }catch(...) {
        std::error_code ec;
        std::filesystem::remove(path,ec);
        throw;
    }
    std::error_code ec;
    std::filesystem::remove(path,ec);
    require(brush.undo()&&brush.snapshot()==old&&
            brush.checkpoint().mirrorPairs==origPairs,
            "Undo restores exact original root and all removed mirror pairs");
    require(brush.redo()&&brush.snapshot()==accepted&&
            brush.checkpoint().mirrorPairs==finalPairs,
            "Redo reinstates accepted private surgery and precise canonical mirror IDs");
}
void testNewMirroredSeamPairsAndMissingOrbits() {
    const auto fixture=reflectedCubes(1.1);
    const auto candidate=RegionTopologyIndex::build(fixture.import.revision);
    const auto& f=fixture.import.flatIndex.faceIds;
    const auto plan=planRemovalContours(
        *candidate,{f[10],f[11],f[22],f[23]});
    require(plan.ready(),"Mirrored plane seam fixture has valid private plan");
    SculptCollisionEvaluation staged;
    staged.repair=applyMirroredRepairSurgery(candidate,plan,fixture.mirrors);
    const auto pairs=mirrorMap(fixture.mirrors);
    deriveMirroredRepairDeltas(staged,*candidate,pairs);
    require(staged.insertedMirrors.size()==4&&
            staged.retiredMirrors.empty(),
            "One shared X=0 four-vertex seam creates four self-mirror pairs");
    for(const auto& [a,b]:staged.insertedMirrors)
        require(a==b&&staged.repair->index->vertex(a).position.x==0,
                "Every inserted seam correspondence is exact self-reflection");
    auto incomplete=staged;
    incomplete.repair->insertedVertices.push_back(
        fixture.import.flatIndex.vertexIds.front());
    bool refused=false;
    try {deriveMirroredRepairDeltas(incomplete,*candidate,pairs);}
    catch(const std::logic_error&) {refused=true;}
    require(refused,"Surgery may not claim a pre-existing ID as a new mirror orbit");
}
void testMirroredZipperUsesOnePlaneSeam() {
    const auto test=reflectedCubes(1.1);
    const auto original=RegionTopologyIndex::build(test.import.revision);
    const auto& f=test.import.flatIndex.faceIds;
    const std::vector<FaceId> red{f[10],f[11],f[22],f[23]};
    const auto plan=planRemovalContours(*original,red);
    require(plan.ready()&&plan.pairs.size()==1,
            "Mirrored facing-cube caps select a zipper");
    const auto before=original->pickingRootIdentity();
    const auto surgery=applyMirroredRepairSurgery(original,plan,test.mirrors);
    require(surgery.zipperFaces==16&&surgery.capFaces==0,
            "Both halves of symmetrical zipper use paired triangles");
    require(surgery.insertedVertices.size()==4,
            "One four-vertex seam ring, not duplicate left/right seam vertices");
    for(const auto id:surgery.insertedVertices) {
        const auto& v=surgery.index->vertex(id);
        require(v.position.x==0&&v.incidentFaces.size()==6,
                "One shared plane vertex joins both symmetric half-surfaces");
    }
    require(euler(*original)==4&&euler(*surgery.index)==2,
            "Mirrored zipper connects two components into one sphere-like solid");
    require(surgery.index->triangleIntersections(
        {},surgery.insertedFaces).empty(),
        "Mirrored scar contains no false crossing or overlap");
    require(surgery.verifiedLocalFaces>surgery.insertedFaces.size()&&
            surgery.maximumSurfaceDeviationInEdges<
                RepairSurgerySettings{}.maximumSurfaceDeviationInEdges,
            "Mirrored surgery verifies surviving local geometry and reports bounded drift");
    require(original->pickingRootIdentity()==before&&
            original->retiredFaceCount()==0,
            "Mirrored private edit leaves renderer's old root intact");
}
void testMirroredIndependentCaps() {
    const auto test=reflectedCubes(3.0);
    const auto original=RegionTopologyIndex::build(test.import.revision);
    const auto& f=test.import.flatIndex.faceIds;
    const std::vector<FaceId> red{f[10],f[11],f[22],f[23]};
    const auto plan=planRemovalContours(*original,red);
    require(plan.ready()&&plan.cappedContours.size()==2,
            "Widely separated reflected loops prefer caps");
    const auto surgery=applyMirroredRepairSurgery(original,plan,test.mirrors);
    require(surgery.capFaces==4&&surgery.zipperFaces==0&&
            surgery.insertedVertices.empty(),
            "Reflected caps replace both cut faces without a plane seam");
    require(euler(*original)==4&&euler(*surgery.index)==4,
            "Capping keeps two distinct symmetric solid components");
    require(surgery.index->retiredFace(f[10])&&
            surgery.index->retiredFace(f[22]),
            "Both original reflected face IDs are retired atomically");
}
void testMirroredRejectsIncompleteMapping() {
    const auto test=reflectedCubes(1.1);
    const auto original=RegionTopologyIndex::build(test.import.revision);
    const auto& f=test.import.flatIndex.faceIds;
    const auto plan=planRemovalContours(*original,{f[10],f[11],f[22],f[23]});
    auto incomplete=test.mirrors;
    incomplete.erase(incomplete.begin()+1); // Needed by deleted +X cap ring
    bool failed=false;
    try{(void)applyMirroredRepairSurgery(original,plan,incomplete);}
    catch(const std::exception&) {failed=true;}
    require(failed,"Incomplete mirror mapping refuses surgery");
    require(original->retiredFaceCount()==0,
            "Failed symmetric operation has no published topology");
}

void testMirroredUnevenBoundarySubdivision() {
    Mesh m;
    const auto cut=prism(m,-2.1,-.1,unevenSquare(1));
    const auto vertexCount=static_cast<std::uint32_t>(m.vertices.size());
    const auto faceCount=static_cast<std::uint32_t>(m.triangles.size());
    for(std::uint32_t i=0;i<vertexCount;++i) {
        const auto p=m.vertices[i];
        m.vertices.push_back({-p.x,p.y,p.z});
    }
    for(std::uint32_t i=0;i<faceCount;++i) {
        const auto f=m.triangles[i];
        m.triangles.push_back({f.a+vertexCount,
                               f.c+vertexCount,f.b+vertexCount});
    }
    const auto imported=partitionFlatMesh(m,1);
    const auto base=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> mirrors;
    for(std::uint32_t i=0;i<vertexCount;++i)
        mirrors.push_back({imported.flatIndex.vertexIds[i],
                           imported.flatIndex.vertexIds[i+vertexCount]});
    auto red=capIds(imported,cut,false);
    for(std::size_t i=0;i<cut.count;++i)
        red.push_back(imported.flatIndex.faceIds[faceCount+cut.right+i]);
    const auto plan=planRemovalContours(*base,red);
    require(plan.ready()&&plan.pairs.size()==1,
            "Uneven reflected contour still selects central zipper");
    const auto result=applyMirroredRepairSurgery(base,plan,mirrors);
    require(result.splitBoundaryEdges>0&&
            result.insertedVertices.size()>cut.count,
            "Mirrored long-edge split creates paired vertices and shared plane scar");
    require(euler(*base)==4&&euler(*result.index)==2,
            "Symmetric unequal boundary density stays a closed two-manifold");
    require(result.index->triangleIntersections(
        {},result.insertedFaces).empty(),
        "Mirrored boundary subdivision does not create crossing triangles");
}
void testIsolatedIrregularAndWarpedCap() {
    // A single boundary has no alternative zipper partner. Its
    // nonplanarity or concavity should not automatically prevent
    // testing a real, bounded cap against the private post-state mesh.
    for(const bool warped:{false,true}) {
        Mesh m;
        const std::vector<std::array<double,2>> concave={
            {-1,-1},{1,-1},{1,1},{.1,.2},{-1,1}};
        const auto end=prism(m,-2,2,warped?square(1):concave);
        if(warped) {
            for(std::size_t i=0;i<4;++i)
                m.vertices[i].x=-2+(i%2==0?-.60:.60);
        }
        const auto imported=partitionFlatMesh(m,1);
        const auto baseline=RegionTopologyIndex::build(imported.revision);
        const auto removed=capIds(imported,end,true);
        const auto plan=planRemovalContours(*baseline,removed);
        if(!plan.ready()||plan.cappedContours.size()!=1)
            throw std::runtime_error(
                std::string("Isolated ")+(warped?"warped":"concave")+
                " ring refused: status="+std::to_string(
                    static_cast<int>(plan.status))+
                ", reason="+plan.reason);
        require(plan.pairs.empty(),"Isolated disk must never invent a zipper partner");
        const auto surgery=applyRepairSurgery(baseline,plan);
        require(surgery.capFaces==end.count-2&&
                surgery.zipperFaces==0,
                "Nonplanar/concave replacement is a complete bounded triangulated disk");
        require(euler(*surgery.index)==2,
                "Irregular isolated cap retains a closed genus-zero manifold");
        require(surgery.verifiedLocalFaces>surgery.capFaces,
                "Private irregular cap includes surviving faces in collision tests");
        require(baseline->retiredFaceCount()==0,
                "Irregular surgery leaves precommit revision unchanged");
        if(warped) {
            RepairPlanSettings strict;
            strict.maximumSingleCapPlanarityInRadii=
                strict.maximumPlanarityInRadii;
            const auto refused=planRemovalContours(*baseline,removed,strict);
            require(refused.status==RepairPlanStatus::AmbiguousClosure,
                    "Legacy strict frustum planar bound rejects saddle-shaped contour");
            Mesh extreme=m;
            for(std::size_t i=0;i<4;++i)
                extreme.vertices[i].x=-2+(i%2==0?-1.0:1.0);
            const auto tooWarped=partitionFlatMesh(extreme,1);
            const auto ix=RegionTopologyIndex::build(tooWarped.revision);
            const auto noPlan=planRemovalContours(*ix,
                capIds(tooWarped,end,true));
            require(noPlan.status==RepairPlanStatus::AmbiguousClosure,
                    "Severely folded isolated cap still rejects beyond its own bound");
        }
    }
}
void testProjectedSelfCrossingCapIsRejected() {
    // A pentagram loop has a nonzero projected area yet is NOT the
    // boundary of a simple disk. The earlier single-ring relaxation
    // must never let ear clipping silently fill its overlapped winding.
    Mesh mesh;
    std::vector<std::array<double,2>> crossing;
    for(unsigned i=0;i<5;++i) {
        const unsigned index=(2*i)%5;
        const double phi=2*std::numbers::pi_v<double>*index/5;
        crossing.push_back({std::cos(phi),std::sin(phi)});
    }
    const auto cap=prism(mesh,-2,2,crossing);
    const auto imported=partitionFlatMesh(mesh,1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    auto relaxed=RepairPlanSettings{};
    relaxed.maximumSingleCapIsoperimetricRatio=100;
    const auto plan=planRemovalContours(
        *original,capIds(imported,cap,true),relaxed);
    require(plan.ready()&&plan.cappedContours.size()==1,
            "Topologically closed but crossing pentagram challenges cap triangulator");
    auto surgerySettings=RepairSurgerySettings{};
    surgerySettings.planning=relaxed;
    bool rejected=false;
    try {(void)applyRepairSurgery(original,plan,surgerySettings);}
    catch(const std::logic_error& e) {
        rejected=std::string(e.what()).find(
            "not simple in its projection")!=std::string::npos;
    }
    require(rejected,
            "Projected nonadjacent ring-edge intersection is rejected before creating cap triangles");
    require(original->retiredFaceCount()==0,
            "Rejected crossing cap leaves original revision and history intact");
}
void testNonplanarMultiContourStaysAmbiguous() {
    Mesh m;
    const auto a=prism(m,-3,-2,square(.7));
    for(std::size_t i=0;i<4;++i)
        m.vertices[i].x=-3+(i%2==0?-.42:.42);
    const auto b=prism(m,2,3,square(.7));
    const auto imported=partitionFlatMesh(m,1);
    const auto baseline=RegionTopologyIndex::build(imported.revision);
    auto removed=capIds(imported,a,true);
    const auto other=capIds(imported,b,true);
    removed.insert(removed.end(),other.begin(),other.end());
    const auto plan=planRemovalContours(*baseline,removed);
    require(plan.status==RepairPlanStatus::AmbiguousClosure,
            "A warped multi-ring option must not trust a circular-frustum topology score");
    require(baseline->retiredFaceCount()==0,
            "Ambiguous multi-ring geometry never publishes surgery");
}
void testSymmetryPlaneClassificationFailsClosed() {
    const auto check=mesher::detail::inspectPlaneContour;
    {
        const auto clean=check({{1,-1,0},{0,0,-1},{-1,-1,0},
                                {0,0,1}},1);
        require(clean.seamVertices==2&&clean.crossingEdges==0&&
                clean.onPlaneEdges==0,
                "Explicit seam endpoints are allowed for one self-mirrored cap");
    }
    {
        const auto crossed=check({{1,0,0},{-1,1,0},{-1,0,1}},1);
        require(crossed.crossingEdges==2&&crossed.seamVertices==0,
                "An X=0 crossing without a topological vertex is detectable");
    }
    {
        const auto crowded=check({{0,0,0},{1,0,0},{0,1,0},
                                   {-1,0,0},{0,-1,0}},1);
        require(crowded.seamVertices==3,
                "A multi-contact self-reflected loop is rejected pending domain decomposition");
    }
    {
        const auto along=check({{0,0,0},{0,1,0},{1,1,0},{1,0,0}},1);
        require(along.onPlaneEdges==1,
                "An edge lying on X=0 cannot be treated as two independent seams");
    }
    {
        const auto uncertain=check({{1,0,0},{1e-12,1,0},
                                    {1,0,1}},1);
        require(uncertain.nearlyOnPlaneVertices==1,
                "Near-zero coordinate is not silently treated as an exact shared seam ID");
    }
}
void testMirroredPlaneOrbitAndDiskFixedArc() {
    using namespace mesher::detail;
    const std::map<VertexId,VertexId> reflected{
        {1,3},{3,1},{2,4},{4,2}};
    const Vec3 a{1,0,0},b{-.5,1,0};
    const Vec3 ma{-1,0,0},mb{.5,1,0};
    require(classifyPlaneCrossingOrbit(1,2,reflected)==
                PlaneEdgeOrbit::DistinctReflected,
            "Two topologically different mirrored crossing edges are a distinct orbit");
    const auto crossing=crossingPointOnPlane(a,b);
    const auto reflectedCrossing=crossingPointOnPlane(ma,mb);
    require(crossing.x==0&&crossing.y==reflectedCrossing.y&&
            crossing.z==reflectedCrossing.z,
            "Different mirrored crossing edges hit the same fixed-plane point");
    require(classifyPlaneCrossingOrbit(1,3,reflected)==
                PlaneEdgeOrbit::SelfReflected,
            "An edge joining a mirror pair has its own single-edge orbit");
    require(classifyPlaneCrossingOrbit(1,5,reflected)==
                PlaneEdgeOrbit::Unmapped,
            "Unknown mirror vertex does not authorize a crossing normalization");
    bool invalid=false;
    try {(void)crossingPointOnPlane({1,0,0},{2,0,0});}
    catch(const std::invalid_argument&){invalid=true;}
    require(invalid,"A noncrossing edge has no plane intersection");

    const auto inspect=inspectPlaneContactTopology;
    {
        const auto normal=inspect(
            {{1,-1,0},{0,0,-1},{-1,-1,0},{0,0,1}},1);
        require(normal.transverseVertices==2&&
                normal.totalTransverseContacts()==2&&
                normal.tangentVertices==0,
                "A valid symmetric cap disk has two transverse fixed-plane endpoints");
    }
    {
        const auto four=inspect(
            {{0,-4,0},{1,-3,0},{0,-2,0},{-1,-1,0},
             {0,0,0},{1,1,0},{0,2,0},{-1,3,0}},1);
        require(four.totalTransverseContacts()==4&&
                four.tangentVertices==0,
                "Four seam contacts must not be paired as two independent cap chords");
    }
    {
        const auto touching=inspect(
            {{0,0,0},{1,0,0},{1,1,0}},1);
        require(touching.totalTransverseContacts()==0&&
                touching.tangentVertices==1,
                "A tangent X=0 touch cannot be counted as a transverse cap endpoint");
    }
    {
        const auto mixed=inspect(
            {{0,0,0},{1,0,0},{-1,1,0},{0,2,0},{1,3,0}},1);
        require(mixed.crossingEdges==1&&
                mixed.totalTransverseContacts()==2,
                "Actual seam points and crossing edges both count toward the same topology limit");
    }
}
void testAtomicSelfReflectedPlaneCrossingNormalization() {
    // Two self-reflected triangles meet across one undivided edge
    // joining a mirrored vertex pair. This is the safe normalization
    // precursor for cut rings that cannot be zipped across X=0 yet.
    Mesh tetra;
    tetra.vertices={{1,0,0},{-1,0,0},{0,1,0},{0,0,1}};
    tetra.triangles={{0,1,2},{1,0,3},{0,2,3},{1,3,2}};
    const auto imported=partitionFlatMesh(tetra,1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto& v=imported.flatIndex.vertexIds;
    const std::vector<std::pair<VertexId,VertexId>> mapping={
        {v[0],v[1]},{v[2],v[2]},{v[3],v[3]}};
    IndexedMeshRepository repository(original);
    const auto oldRoot=original->pickingRootIdentity();
    const auto normalized=normalizeSelfMirroredPlaneEdge(
        original,v[0],v[1],mapping,1);
    require(normalized.index&&normalized.seamVertex!=v[0]&&
            normalized.seamVertex!=v[1]&&
            normalized.index->vertex(normalized.seamVertex).position.x==0,
            "One shared X=0 midpoint normalizes the mirror-pair crossing");
    require(normalized.insertedFaces[0]!=normalized.insertedFaces[1]&&
            normalized.preservedFaces[0]!=normalized.preservedFaces[1],
            "One new partner face for each retained self-mirrored old face");
    require(normalized.index->vertex(
                normalized.seamVertex).incidentFaces.size()==4&&
            euler(*original)==2&&euler(*normalized.index)==2,
            "Entire crossing edge is conforming, genus-preserving and manifold");
    require(normalized.index->retiredFaceCount()==0&&
            original->retiredFaceCount()==0&&
            original->pickingRootIdentity()==oldRoot,
            "Plane-edge normalization preserves old IDs and historical root");
    require(repository.snapshot()==original,
            "Private normalization has not published any topology");
    require(repository.publishIfCurrent(original,normalized.index),
            "Validated normalization publishes by one optimistic root swap");
    require(!repository.publishIfCurrent(original,normalized.index),
            "Stale normalization cannot overwrite current repository revision");
    bool refused=false;
    try {
        (void)normalizeSelfMirroredPlaneEdge(
            original,v[0],v[1],{{v[0],v[1]},{v[2],v[2]}},1);
    }catch(const std::exception&){refused=true;}
    require(refused,"Missing seam-apex reflection cannot normalize an edge");
    refused=false;
    try {
        (void)normalizeSelfMirroredPlaneEdge(
            original,v[0],v[2],mapping,1);
    }catch(const std::exception&){refused=true;}
    require(refused,"Unpaired edge endpoints are rejected before topology changes");
    require(original->pickingRootIdentity()==oldRoot&&
            original->retiredFaceCount()==0,
            "All failed normalizations leave original index immutable");
}
void testNormalizeAndReplanNoCrossingCompatibility() {
    const auto fixture=reflectedCubes(3.0);
    const auto base=RegionTopologyIndex::build(fixture.import.revision);
    const auto& f=fixture.import.flatIndex.faceIds;
    const auto& mirrors=fixture.mirrors;
    const auto plan=planRemovalContours(
        *base,{f[10],f[11],f[22],f[23]});
    require(plan.ready()&&plan.cappedContours.size()==2,
            "Two separated reflected square openings prefer caps");
    const auto wrapped=repairMirroredContoursAcrossPlane(
        base,plan,mirrors);
    require(wrapped.normalizedCrossingEdges==0&&
            wrapped.planeSeamVertices.empty()&&
            wrapped.normalizationInsertedFaces.empty()&&
            wrapped.surgery.capFaces==4,
            "Without a crossing, the wrapper delegates unchanged mirrored surgery");
    require(wrapped.accumulatedStatistics.modifiedPartitions==
                wrapped.surgery.commit.statistics.modifiedPartitions&&
            euler(*wrapped.surgery.index)==4,
            "Ordinary mirrored cap produces no hidden normalization transactions");
}
void testCrossingContourFullNormalizeReplanRepair() {
    // Four-equator-vertex bipyramid: top four triangles are removed.
    // Its cut contour has TWO X=0 crossings and NO seam vertex.
    // Each crossing is shared by one removed and one retained
    // self-mirrored triangle, and they must be normalized together
    // before independently capping the now conforming six-edge rim.
    Mesh m;
    m.vertices={{1,-1,0},{1,1,0},{-1,1,0},{-1,-1,0},
                {0,0,1},{0,0,-1}};
    for(std::uint32_t i=0;i<4;++i) {
        const auto next=(i+1)%4;
        m.triangles.push_back({4,i,next});
        m.triangles.push_back({5,next,i});
    }
    const auto imp=partitionFlatMesh(m,1);
    const auto base=RegionTopologyIndex::build(imp.revision);
    const auto& v=imp.flatIndex.vertexIds;
    const auto& faces=imp.flatIndex.faceIds;
    const std::vector<std::pair<VertexId,VertexId>> mirrors={
        {v[0],v[3]},{v[1],v[2]},{v[4],v[4]},{v[5],v[5]}};
    const std::vector<FaceId> removal={
        faces[0],faces[2],faces[4],faces[6]};
    const auto proposed=planRemovalContours(*base,removal);
    require(proposed.ready()&&proposed.contours.size()==1&&
            proposed.cappedContours.size()==1,
            "Two plane-crossings initially form one valid disconnected-from-plane rim");
    const auto root=base->pickingRootIdentity();
    IndexedMeshRepository repository(base);
    const auto result=repairMirroredContoursAcrossPlane(
        base,proposed,mirrors);
    require(result.normalizedCrossingEdges==2&&
            result.planeSeamVertices.size()==2&&
            result.normalizationInsertedFaces.size()==4,
            "Both self-reflected boundary edges acquire one X=0 seam midpoint");
    for(const auto id:result.planeSeamVertices) {
        const auto& seam=result.surgery.index->vertex(id);
        require(seam.position.x==0&&seam.incidentFaces.size()>=3,
                "Each seam vertex is a unique shared ID with an intact one-ring");
    }
    require(result.normalizedPlan.ready()&&
            result.normalizedPlan.removedFaces.size()==6&&
            result.normalizedPlan.contours.size()==1&&
            result.normalizedPlan.contours.front().vertexIds.size()==6,
            "Replanning expands removed face IDs and preserves all original ordered contour vertices");
    require(result.surgery.capFaces>0&&
            result.surgery.zipperFaces==0&&
            euler(*result.surgery.index)==2,
            "Two-crossing private repair closes a valid genus-zero surface");
    for(const auto id:removal)
        require(result.surgery.index->retiredFace(id),
                "Original deleted FaceIds remain stable tombstones");
    require(base->retiredFaceCount()==0&&base->pickingRootIdentity()==root,
            "Multi-step normalizations never mutate original renderer/picker root");
    require(repository.snapshot()==base,
            "No intermediate normalization revision is published");
    require(result.surgery.commit.revision!=nullptr&&
            repository.publishIfCurrent(base,result.surgery.index),
            "All normalization and closure changes publish with ONE original-root CAS");
    require(!repository.publishIfCurrent(base,result.surgery.index),
            "A stale root cannot overwrite the completed repair");
    require(result.accumulatedStatistics.modifiedPartitions>=
                result.surgery.commit.statistics.modifiedPartitions,
            "Accumulated work accounts for both normalization commits");
    bool rejected=false;
    try {
        (void)repairMirroredContoursAcrossPlane(
            base,proposed,mirrors,{},1);
    }catch(const std::length_error&){rejected=true;}
    require(rejected,"An insufficient plane crossing budget rejects the entire operation");
    auto forged=proposed;
    forged.contours.front().vertexIds.front()=
        forged.contours.front().vertexIds[1];
    rejected=false;
    try {
        (void)repairMirroredContoursAcrossPlane(base,forged,mirrors);
    }catch(const std::logic_error&){rejected=true;}
    require(rejected,"A forged original contour is not authorized to normalize");
    require(base->retiredFaceCount()==0&&base->pickingRootIdentity()==root,
            "All failed normalization/replans preserve original immutable revision");
}
void testDistinctReflectedCrossingEdgesRejectAsSelfIntersection() {
    // Two reflected but individually asymmetric tetrahedra carry
    // DISTINCT crossing edges. Their crossings coincide on X=0,
    // giving pre-existing geometric self-intersections, not a
    // single shared topological edge eligible for normalization.
    Mesh m;
    m.vertices={{1,0,0},{-.5,1,0},{1,0,1},{1,-1,0}};
    m.triangles={{0,1,2},{1,0,3},{0,2,3},{1,3,2}};
    for(unsigned i=0;i<4;++i) {
        const auto p=m.vertices[i];
        m.vertices.push_back({-p.x,p.y,p.z});
    }
    for(unsigned i=0;i<4;++i) {
        const auto face=m.triangles[i];
        m.triangles.push_back({face.a+4,face.c+4,face.b+4});
    }
    const auto imported=partitionFlatMesh(m,1);
    const auto base=RegionTopologyIndex::build(imported.revision);
    const auto& v=imported.flatIndex.vertexIds;
    const auto& f=imported.flatIndex.faceIds;
    const std::vector<std::pair<VertexId,VertexId>> mirror={
        {v[0],v[4]},{v[1],v[5]},{v[2],v[6]},{v[3],v[7]}};
    const auto plan=planRemovalContours(*base,{f[0],f[4]});
    const auto identity=base->pickingRootIdentity();
    bool rejected=!plan.ready();
    if(plan.ready()) {
        // The planner may independently reject intersecting shells
        // before the specialized plane-orbit classifier is reached.
        // If planning permits a candidate, normalization MUST reject.
        try {
            (void)repairMirroredContoursAcrossPlane(base,plan,mirror);
        }catch(const std::logic_error&){rejected=true;}
    }
    require(rejected,
            "Intersecting reflected shells cannot produce a publishable repair");
    require(base->pickingRootIdentity()==identity&&
            base->retiredFaceCount()==0,
            "Refused self-intersecting input cannot change immutable source revision");
}
void testFourCrossingsAcrossTwoIndependentContours() {
    // Four plane contacts are entirely legitimate when distributed as
    // two contacts EACH on two separate reflection-symmetric disks.
    // This differs topologically from four contacts on ONE disk.
    Mesh mesh;
    std::vector<std::pair<unsigned,unsigned>> bases;
    for(const double y:{-12.0,12.0}) {
        const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
        const auto faceStart=static_cast<unsigned>(mesh.triangles.size());
        for(const auto point:std::array<Vec3,6>{{
            {1,y-1,0},{1,y+1,0},{-1,y+1,0},{-1,y-1,0},
            {0,y,1},{0,y,-1}
        }})mesh.vertices.push_back(point);
        for(std::uint32_t i=0;i<4;++i) {
            const auto next=(i+1)%4;
            mesh.triangles.push_back({first+4,first+i,first+next});
            mesh.triangles.push_back({first+5,first+next,first+i});
        }
        bases.emplace_back(first,faceStart);
    }
    const auto imported=partitionFlatMesh(mesh,1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto& v=imported.flatIndex.vertexIds;
    const auto& faces=imported.flatIndex.faceIds;
    std::vector<std::pair<VertexId,VertexId>> mirrors;
    std::vector<FaceId> removed;
    for(const auto& [first,faceStart]:bases) {
        mirrors.insert(mirrors.end(),{
            {v[first],v[first+3]},{v[first+1],v[first+2]},
            {v[first+4],v[first+4]},{v[first+5],v[first+5]}
        });
        for(const unsigned k:{0u,2u,4u,6u})
            removed.push_back(faces[faceStart+k]);
    }
    const auto plan=planRemovalContours(*original,removed);
    require(plan.ready()&&plan.contours.size()==2&&
            plan.cappedContours.size()==2,
            "Two distant self-reflected cut rings independently choose caps");
    const auto oldRoot=original->pickingRootIdentity();
    const auto repaired=repairMirroredContoursAcrossPlane(
        original,plan,mirrors);
    require(repaired.normalizedCrossingEdges==4&&
            repaired.planeSeamVertices.size()==4&&
            repaired.normalizationInsertedFaces.size()==8,
            "Four crossing edges on two different rings normalize independently");
    require(repaired.normalizedPlan.removedFaces.size()==12&&
            repaired.normalizedPlan.contours.size()==2&&
            repaired.normalizedPlan.cappedContours.size()==2,
            "Re-planning retains two separate caps and all divided deleted faces");
    require(euler(*original)==4&&euler(*repaired.surgery.index)==4&&
            repaired.surgery.zipperFaces==0,
            "Repair preserves two disconnected orientable closed shells");
    require(original->pickingRootIdentity()==oldRoot&&
            original->retiredFaceCount()==0,
            "Multiple intermediate commits do not change the historic root");
    bool overBudget=false;
    try {
        (void)repairMirroredContoursAcrossPlane(
            original,plan,mirrors,{},3);
    }catch(const std::length_error&){overBudget=true;}
    require(overBudget,
            "Global crossing budget counts separate valid contours together");
}
void testSingleCrossingContourRepair() {
    Mesh tetra;
    tetra.vertices={{1,0,0},{-1,0,0},{0,1,0},{0,0,1}};
    tetra.triangles={{0,1,2},{1,0,3},{0,2,3},{1,3,2}};
    const auto imp=partitionFlatMesh(tetra,1);
    const auto initial=RegionTopologyIndex::build(imp.revision);
    const auto& v=imp.flatIndex.vertexIds;
    const std::vector<std::pair<VertexId,VertexId>> mapping={
        {v[0],v[1]},{v[2],v[2]},{v[3],v[3]}};
    const auto old=imp.flatIndex.faceIds.front();
    const auto plan=planRemovalContours(*initial,{old});
    require(plan.ready()&&plan.cappedContours.size()==1,
            "Single-crossing triangular rim can be proposed for a cap");
    const auto fixed=repairMirroredContoursAcrossPlane(
        initial,plan,mapping);
    require(fixed.normalizedCrossingEdges==1&&
            fixed.planeSeamVertices.size()==1&&
            fixed.normalizedPlan.removedFaces.size()==2,
            "One crossing edge produces exactly one seam vertex and one extra removed face");
    require(fixed.surgery.index->retiredFace(old)&&
            euler(*fixed.surgery.index)==2,
            "One-crossing cap yields a stable closed manifold");
    require(initial->retiredFaceCount()==0,
            "Initial removal FaceId is never retired on the original snapshot");
}
void testSelfMirroredCapAcrossExistingSeam() {
    for(const double bend:{0.0,.04}) {
    Mesh oct;
    oct.vertices={{1,0,bend},{0,1,0},{-1,0,bend},{0,-1,0},
                  {0,0,1},{0,0,-1}};
    oct.triangles={{4,0,1},{4,1,2},{4,2,3},{4,3,0},
                   {5,1,0},{5,2,1},{5,3,2},{5,0,3}};
    const auto imp=partitionFlatMesh(oct,1);
    const auto base=RegionTopologyIndex::build(imp.revision);
    const auto& v=imp.flatIndex.vertexIds;
    const auto& faces=imp.flatIndex.faceIds;
    const std::vector<std::pair<VertexId,VertexId>> mirrors={
        {v[0],v[2]},{v[1],v[1]},{v[3],v[3]},
        {v[4],v[4]},{v[5],v[5]}};
    const auto plan=planRemovalContours(*base,
        {faces[0],faces[1],faces[2],faces[3]});
    require(plan.ready()&&plan.cappedContours.size()==1&&
            plan.contours.size()==1,
            "Symmetric upper octahedron removal has a single self-reflected rim");
    const auto scar=applyMirroredRepairSurgery(base,plan,mirrors);
    require(scar.capFaces==2&&scar.zipperFaces==0&&
            scar.insertedVertices.empty(),
            "Single self-reflected cap creates exactly two shared-chord triangles");
    require(scar.retiredVertices.size()==1&&
            scar.retiredVertices.front()==v[4],
            "Original pole is retired once despite self-mirror mapping");
    require(euler(*base)==2&&euler(*scar.index)==2,
            "Symmetric single-ring cap preserves genus zero and orientable manifold");
    require(scar.index->vertex(v[1]).incidentFaces.size()==4&&
            scar.index->vertex(v[3]).incidentFaces.size()==4,
            "Both existing X=0 seam endpoints have one continuous four-face fan");
    require(base->retiredFaceCount()==0&&
            scar.index->retiredFaceCount()==4,
            "Old revision remains unchanged after mirrored self-cap");
    }
}
void testMirroredWholeComponentRetirement() {
    const auto test=reflectedCubes(3.0);
    const auto base=RegionTopologyIndex::build(test.import.revision);
    const auto all=test.import.flatIndex.faceIds;
    const auto plan=planRemovalContours(*base,all);
    require(plan.ready()&&plan.contours.empty(),
            "Two entire reflected closed shells need no repair seam");
    const auto result=applyMirroredRepairSurgery(base,plan,test.mirrors);
    require(result.insertedFaces.empty()&&
            result.retiredFaces.size()==24&&
            result.retiredVertices.size()==16,
            "Both removed reflected components retire their identities");
    require(result.index->retiredVertexCount()==16&&
            result.index->retiredFaceCount()==24,
            "Atomic symmetric tombstones remain in the private index");
    require(base->retiredVertexCount()==0&&base->retiredFaceCount()==0,
            "Old two-component snapshot remains intact");
}
void testRepeatedMirroredSurgeryKeepsHistory() {
    Mesh mesh;
    for(const double y:{-4.0,4.0}) {
        const auto first=static_cast<std::uint32_t>(mesh.vertices.size());
        const auto faceStart=static_cast<std::uint32_t>(mesh.triangles.size());
        cube(mesh,{-1.1,y,0},1);
        for(std::uint32_t i=0;i<8;++i) {
            const auto p=mesh.vertices[first+i];
            mesh.vertices.push_back({-p.x,p.y,p.z});
        }
        for(std::uint32_t i=0;i<12;++i) {
            const auto f=mesh.triangles[faceStart+i];
            mesh.triangles.push_back({f.a+8,f.c+8,f.b+8});
        }
    }
    const auto imported=partitionFlatMesh(mesh,1);
    const auto initial=RegionTopologyIndex::build(imported.revision);
    std::vector<std::pair<VertexId,VertexId>> mirror;
    for(const unsigned base:{0u,16u})
        for(unsigned i=0;i<8;++i)
            mirror.emplace_back(imported.flatIndex.vertexIds[base+i],
                                imported.flatIndex.vertexIds[base+i+8]);
    const auto& f=imported.flatIndex.faceIds;
    const auto plan1=planRemovalContours(*initial,
        {f[10],f[11],f[22],f[23]});
    require(plan1.ready()&&plan1.pairs.size()==1,
            "First reflected repair ring is valid");
    const auto first=applyMirroredRepairSurgery(initial,plan1,mirror);
    const auto firstRoot=first.index->pickingRootIdentity();
    const auto plan2=planRemovalContours(*first.index,
        {f[34],f[35],f[46],f[47]});
    require(plan2.ready()&&plan2.pairs.size()==1,
            "Second reflected repair uses stable old face IDs");
    const auto second=applyMirroredRepairSurgery(first.index,plan2,mirror);
    require(initial->retiredFaceCount()==0&&
            first.index->retiredFaceCount()==4&&
            second.index->retiredFaceCount()==8,
            "Three immutable generations hold independent retirement histories");
    require(first.index->pickingRootIdentity()==firstRoot&&
            second.index->pickingRootIdentity()!=firstRoot,
            "Second independent surgery cannot mutate previous BVH root");
    require(euler(*initial)==8&&euler(*first.index)==6&&
            euler(*second.index)==4,
            "Two sequential mirrored mergers each reduce Euler characteristic by two");
}
void testSampledExcisionProvenanceAndSelectiveReverseFidelity() {
    Mesh m;
    cube(m,{},1);
    const auto imported=partitionFlatMesh(m,1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto& f=imported.flatIndex.faceIds;
    const std::vector<FaceId> cap{f[2],f[3]};
    const auto exposed=inspectRemovalSurfaceProvenance(*original,cap);
    const auto strictProvenance=inspectRemovalSurfaceProvenance(
        *original,cap,2,2048,false);
    require(exposed.sampledFaces==2&&exposed.featureFragments()>0&&
            exposed.totalFragments()==32&&
            exposed.ambiguousFragments==0&&
            strictProvenance.exposedFragments==exposed.exposedFragments&&
            strictProvenance.ambiguousFragments==0,
            "V14 records explicit sampled exposed occupancy at a cube surface");
    auto plan=planRemovalContours(*original,cap);
    require(plan.ready()&&plan.cappedContours.size()==1,
            "Exposed original cube cap remains eligible for replacement");
    RepairSurgerySettings guarded;
    guarded.requireExposedFragmentFidelity=true;
    guarded.planning.allowReferenceWindingFallback=false;
    guarded.maximumReverseDeviationInEdges=.01;
    const auto replacement=applyRepairSurgery(original,plan,guarded);
    require(replacement.removalProvenance.exposedFragments==
                exposed.exposedFragments&&
            replacement.checkedReverseFragments==exposed.featureFragments()&&
            replacement.reverseDistanceTests>0&&
            replacement.maximumReverseDeviationInEdges<1e-10,
            "Exposed-fragment reverse sampling preserves exactly replaced cap detail");

    guarded.maxProvenanceFragments=1;
    bool refused=false;
    try {(void)applyRepairSurgery(original,plan,guarded);}
    catch(const std::length_error&){refused=true;}
    require(refused,"Provenance sample budget rejects rather than truncating classification");
    guarded.maxProvenanceFragments=2048;
    guarded.maxReverseDistanceTests=1;
    refused=false;
    try {(void)applyRepairSurgery(original,plan,guarded);}
    catch(const std::length_error&){refused=true;}
    require(refused,"Reverse nearest-face work budget fails closed");
    guarded.maxReverseDistanceTests=131072;
    guarded.requireReverseSurfaceFidelity=true;
    refused=false;
    try {(void)applyRepairSurgery(original,plan,guarded);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,
            "Two incompatible reverse-fidelity policies cannot be silently combined");
    require(original->retiredFaceCount()==0,
            "All sampled classification and failure paths leave old root unchanged");
}
void testEightIndependentNearCoplanarCapRepairs() {
    // A broad edit neighborhood should not turn lawful coplanar scar
    // contacts into phantom intersections, nor should it accidentally
    // zipper distant components. All eight caps remain one PRIVATE step.
    Mesh m;
    for(unsigned i=0;i<8;++i)
        cube(m,{0,static_cast<double>(i)*8,0},1);
    const auto imported=partitionFlatMesh(m,1);
    const auto base=RegionTopologyIndex::build(imported.revision);
    std::vector<FaceId> removed;
    for(unsigned i=0;i<8;++i) {
        removed.push_back(imported.flatIndex.faceIds[12*i+2]);
        removed.push_back(imported.flatIndex.faceIds[12*i+3]);
    }
    const auto plan=planRemovalContours(*base,removed);
    require(plan.ready()&&plan.contours.size()==8&&
            plan.cappedContours.size()==8&&plan.pairs.empty(),
            "Distant near-coplanar cap regions never acquire false zipper partners");
    const auto oldRoot=base->pickingRootIdentity();
    const auto fixed=applyRepairSurgery(base,plan);
    require(fixed.capFaces==16&&fixed.zipperFaces==0&&
            fixed.retiredFaces.size()==16&&euler(*fixed.index)==16,
            "Eight independent caps preserve eight closed genus-zero components");
    require(fixed.verifiedLocalFaces>=fixed.capFaces&&
            fixed.index->triangleIntersections(
                {},fixed.insertedFaces).empty(),
            "Complete local BVH verification accepts valid coplanar scar contacts");
    require(base->retiredFaceCount()==0&&
            base->pickingRootIdentity()==oldRoot,
            "Large multi-component surgery never mutates old reader roots");
    RepairSurgerySettings bounded;
    bounded.maxResidualFaces=8;
    bool rejected=false;
    try {(void)applyRepairSurgery(base,plan,bounded);}
    catch(const std::length_error&){rejected=true;}
    require(rejected,
            "Broad multi-component residual checks refuse over-budget work privately");
}
void testRedundantBuriedShellCanDisappearWithoutReversePenalty() {
    Mesh m;
    cube(m,{},3);
    cube(m,{},.5);
    const auto imported=partitionFlatMesh(m,1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto& ids=imported.flatIndex.faceIds;
    const std::vector<FaceId> inner(ids.begin()+12,ids.end());
    const auto provenance=inspectRemovalSurfaceProvenance(*original,inner);
    require(provenance.sampledFaces==12&&provenance.buriedFragments>0&&
            provenance.featureFragments()==0&&provenance.ambiguousFragments==0,
            "Nested redundant inner shell is positively wound on BOTH sides");
    const auto plan=planRemovalContours(*original,inner);
    require(plan.ready()&&plan.contours.empty(),
            "Entire buried inner shell requires no cap or zipper");
    RepairSurgerySettings settings;
    settings.requireExposedFragmentFidelity=true;
    settings.maximumReverseDeviationInEdges=.001;
    const auto repair=applyRepairSurgery(original,plan,settings);
    require(repair.insertedFaces.empty()&&
            repair.retiredFaces.size()==12&&
            repair.removalProvenance.buriedFragments==provenance.buriedFragments&&
            repair.checkedReverseFragments==0&&
            euler(*repair.index)==2,
            "Buried shell disappears without an impossible reverse detail constraint");
    require(original->retiredFaceCount()==0,
            "Buried shell removal keeps original nested index immutable");
}
void testMixedExposedAndBuriedFaceProvenance() {
    // A geometric intersection divides a SINGLE source triangle into
    // patches that are feature-bearing and patches that are buried.
    // Whole-face reverse sampling cannot represent that distinction.
    Mesh m;
    cube(m,{-0.4,-0.4,0},1);
    cube(m,{0.4,0.4,0.3},1);
    const auto imported=partitionFlatMesh(m,1);
    const auto index=RegionTopologyIndex::build(imported.revision);
    const auto oldRoot=index->pickingRootIdentity();
    bool foundMixed=false;
    for(const auto id:imported.flatIndex.faceIds) {
        const auto classified=index->classifyFaceFragments(id,3);
        bool exposed=false,buried=false;
        for(const auto& fragment:classified) {
            exposed|=fragment.kind==RegionTopologyIndex::FragmentKind::Exposed;
            buried|=fragment.kind==RegionTopologyIndex::FragmentKind::Buried;
        }
        if(!exposed||!buried)continue;
        const auto sampled=inspectRemovalSurfaceProvenance(
            *index,{id},3,64);
        require(sampled.sampledFaces==1&&
                sampled.totalFragments()==64&&
                sampled.exposedFragments>0&&sampled.buriedFragments>0,
                "One intersected face has BOTH exposed and buried original patches");
        bool limited=false;
        try {(void)inspectRemovalSurfaceProvenance(*index,{id},3,63);}
        catch(const std::length_error&){limited=true;}
        require(limited,
                "Provenance cannot truncate a partially sampled mixed face");
        foundMixed=true;
        break;
    }
    require(foundMixed,
            "Overlap fixture contains an actual mixed exposed/buried face");
    require(index->retiredFaceCount()==0&&
            index->pickingRootIdentity()==oldRoot,
            "Mixed-fragment inspection is strictly read-only");
}
void testAnchoredOriginalWitnessesProtectAgainstCumulativeDrift() {
    Mesh mesh;
    cube(mesh,{},1);
    const auto imported=partitionFlatMesh(mesh,1);
    auto root=RegionTopologyIndex::build(imported.revision);
    const std::vector<FaceId> protectedFaces{
        imported.flatIndex.faceIds[2],imported.flatIndex.faceIds[3]};
    const auto witnesses=sampleSurfaceWitnesses(*root,protectedFaces);
    SculptCollisionSettings selected;
    selected.enabled=true;
    selected.anchoredSurface=SculptAnchoredSurfaceGuard{
        witnesses,protectedFaces,2.0,1e-9,256};
    const auto pristine=evaluateSculptCollisionCandidate(
        {root,root,{},{}},false,selected);
    require(pristine.safe()&&pristine.anchoredSurfaceAudit&&
            pristine.anchoredSurfaceAudit->maximumDistanceInEdges<1e-12,
            "Initial reference has zero deviation at original exposed patch");

    const auto f=root->face(protectedFaces.front()).vertices;
    const auto center=root->vertex(f[0]).position;
    const std::array<BrushArea,1> area{{{center,.7,1}}};
    VersionedInflateBrush moved(.35,{});
    const auto displaced=moved.stamp(root,area,1);
    require(displaced.index!=root&&displaced.moved>0,
            "Anchored guard regression applies an actual changed brush vertex");

    auto guarded=evaluateSculptCollisionCandidate(
        {root,displaced.index,displaced.baseline.movedVertices,
         displaced.baseline.affectedFaces},false,selected);
    require(!guarded.safe()&&!guarded.repair&&
            guarded.anchoredSurfaceAudit&&
            guarded.anchoredSurfaceAudit->maximumDistanceInEdges>1e-9&&
            guarded.refusal.find("Cumulative anchored")!=std::string::npos,
            "Small individually lawful deformation refuses because original patch eroded");

    DurableCheckpoint recovered;
    recovered.index=root;
    recovered.initialVertexCount=imported.flatIndex.vertexIds.size();
    VersionedSculptBrush brush(recovered,.35,{}, {},{},1,{},selected);
    Mesh dummy;
    brush.stamp(dummy,area);
    require(brush.rejectedStamps()==1&&brush.snapshot()==root&&
            brush.publishedRevisions()==0&&
            brush.historyStatistics().undoDepth==0&&
            brush.lastDirtyPartitions().empty(),
            "Original witness refusal consumes a Step without any geometry/history delta");

    selected.anchoredSurface->maxDistanceTests=witnesses.size()*
                                                protectedFaces.size()-1;
    bool exhausted=false;
    try {(void)evaluateSculptCollisionCandidate(
        {root,root,{},{}},false,selected);}
    catch(const std::length_error&) {exhausted=true;}
    require(exhausted,"Anchored witness audit refuses work over its exact budget");
    selected.anchoredSurface->maxDistanceTests=witnesses.size()*
                                                protectedFaces.size();
    require(evaluateSculptCollisionCandidate(
        {root,root,{},{}},false,selected).safe(),
        "Exact witness distance-test budget accepts the unchanged original surface");

    // Deliberately calibrate the guard BETWEEN measured first- and second-
    // Step distances. A policy comparing only consecutive revisions would
    // accept both; a reference anchored to root must reject the second.
    VersionedInflateBrush incremental(.025,{});
    const auto once=incremental.stamp(root,area,1);
    const auto twice=incremental.stamp(once.index,area,1);
    const auto firstAudit=auditSurfaceWitnesses(
        *once.index,witnesses,protectedFaces,2.0,256);
    const auto secondAudit=auditSurfaceWitnesses(
        *twice.index,witnesses,protectedFaces,2.0,256);
    require(firstAudit.maximumDistanceInEdges>0&&
            secondAudit.maximumDistanceInEdges>
                firstAudit.maximumDistanceInEdges+1e-6,
            "Two successive small deformations accumulate measurable original-surface drift");
    selected.anchoredSurface->maximumDistanceInEdges=
        (firstAudit.maximumDistanceInEdges+
         secondAudit.maximumDistanceInEdges)/2;
    auto firstCheck=evaluateSculptCollisionCandidate(
        {root,once.index,once.baseline.movedVertices,
         once.baseline.affectedFaces},false,selected);
    auto secondCheck=evaluateSculptCollisionCandidate(
        {root,twice.index,twice.baseline.movedVertices,
         twice.baseline.affectedFaces},false,selected);
    require(firstCheck.safe()&&!secondCheck.safe()&&
            secondCheck.anchoredSurfaceAudit&&
            secondCheck.anchoredSurfaceAudit->maximumDistanceInEdges>
                selected.anchoredSurface->maximumDistanceInEdges,
            "Anchored witness validation permits a small first Step but refuses accumulated erosion");
    VersionedSculptBrush guardedSequence(recovered,.025,{}, {},{},1,{},selected);
    guardedSequence.stamp(dummy,area);
    const auto accepted=guardedSequence.snapshot();
    require(accepted!=root&&guardedSequence.publishedRevisions()==1&&
            guardedSequence.historyStatistics().undoDepth==1,
            "First small protected Step publishes once");
    guardedSequence.stamp(dummy,area);
    require(guardedSequence.snapshot()==accepted&&
            guardedSequence.rejectedStamps()==1&&
            guardedSequence.publishedRevisions()==1&&
            guardedSequence.historyStatistics().undoDepth==1,
            "Second small Step exceeding cumulative original-surface drift is rejected without publication");
}

void testContactEpisodeHysteresisNeverCachesApproval() {
    SculptContactTracker tracker(3);
    tracker.observe(true);
    tracker.observe(true);
    tracker.observe(std::nullopt);
    tracker.observe(false);
    tracker.observe(true); // One clean interval does not reset an episode.
    auto report=tracker.statistics();
    require(report.active&&report.episodes==1&&report.contactChecks==3&&
            report.uncertainChecks==1&&report.quietRun==0&&
            report.longestContactRun==2,
            "Repeated contact and indeterminate checks remain in one episode");
    tracker.observe(false);
    tracker.observe(std::nullopt);
    tracker.observe(false);
    require(tracker.statistics().active&&
            tracker.statistics().quietRun==2,
            "Uncertain BVH/classification result cannot certify contact separation");
    tracker.observe(false);
    require(!tracker.statistics().active&&
            tracker.statistics().episodes==1,
            "Three conclusive clean samples terminate one diagnostic episode");
    tracker.observe(true);
    report=tracker.statistics();
    require(report.active&&report.episodes==2&&report.contactChecks==4&&
            report.cleanChecks==4,
            "New contact after separation starts a new diagnostic episode");
    bool invalid=false;
    try {(void)SculptContactTracker(0);}
    catch(const std::invalid_argument&) {invalid=true;}
    require(invalid,"Contact hysteresis cannot accept a zero-quiet threshold");
}

void testRepeatedRealSymmetricTowerContactAndAnchoredSurface() {
    // Two independent symmetric pairs, each starting with a small physical
    // gap. Their near faces are pushed toward X=0 by exactly reflected
    // Inflate areas; no preselected removal contour or fabricated repair.
    Mesh mesh;
    std::array<std::size_t,2> vertexStarts{},faceStarts{};
    for(unsigned t=0;t<2;++t) {
        const double y=t==0?-4.0:4.0;
        vertexStarts[t]=mesh.vertices.size();
        faceStarts[t]=mesh.triangles.size();
        cube(mesh,{-1.1,y,0},1);
        for(unsigned i=0;i<8;++i) {
            const auto p=mesh.vertices[vertexStarts[t]+i];
            mesh.vertices.push_back({-p.x,p.y,p.z});
        }
        for(unsigned i=0;i<12;++i) {
            const auto f=mesh.triangles[faceStarts[t]+i];
            mesh.triangles.push_back({
                f.a+8,f.c+8,f.b+8
            });
        }
    }
    auto imported=partitionFlatMesh(mesh,1);
    DurableCheckpoint starting;
    starting.index=RegionTopologyIndex::build(imported.revision);
    starting.initialVertexCount=imported.flatIndex.vertexIds.size();
    for(const auto first:vertexStarts)
        for(std::size_t i=0;i<8;++i)
            starting.mirrorPairs.emplace_back(
                imported.flatIndex.vertexIds[first+i],
                imported.flatIndex.vertexIds[first+i+8]);

    // Anchor the *far* exposed walls before sculpting, at the original
    // physical coordinates rather than the previous intermediate Step.
    std::vector<FaceId> exterior;
    for(const auto first:faceStarts)
        for(const unsigned off:{8u,9u,20u,21u})
            exterior.push_back(imported.flatIndex.faceIds[first+off]);
    const auto witnesses=sampleSurfaceWitnesses(*starting.index,exterior);
    require(witnesses.size()==56,
            "Eight initial exterior triangles produce fixed original witnesses");

    const auto zones=[&](unsigned tower) {
        const auto y=tower==0?-4.0:4.0;
        return std::array<BrushArea,2>{{
            {{-.2,y,0},2.1,.8},{{.2,y,0},2.1,.8}
        }};
    };
    // Confirm that the stimulus really CREATES a triangle intersection;
    // otherwise a sequence of innocuous no-contact stamps proves nothing.
    VersionedInflateBrush trial(.48,{},starting.mirrorPairs);
    const auto near=trial.stamp(starting.index,zones(0),1);
    require(!near.index->triangleIntersections(
                near.baseline.movedVertices,near.baseline.affectedFaces).empty(),
            "Mirrored two-tower stroke actually moves opposing walls into contact");

    SculptCollisionSettings policy;
    policy.enabled=true;
    policy.quietChecksToEndContact=3;
    policy.anchoredSurface=SculptAnchoredSurfaceGuard{
        witnesses,exterior,2.0,1e-8,16384};
    VersionedSculptBrush brush(starting,.48,{}, {},{},1,{},policy);
    Mesh dummy;
    std::vector<std::shared_ptr<const RegionTopologyIndex>> held{
        brush.snapshot()};
    std::size_t accepted=0,refused=0,scarResidualRefusals=0;
    for(unsigned step=0;step<16;++step) {
        const auto before=brush.snapshot();
        const auto root=before->pickingRootIdentity();
        const auto pairs=brush.checkpoint().mirrorPairs;
        const auto history=brush.historyStatistics();
        brush.stamp(dummy,zones(step%4<2?0:1));
        require(brush.completedStamps()==step+1&&
                brush.collisionCheckedStamps()==step+1,
                "Every proposed repeat-contact Step is checked, never silently cached");
        const auto current=brush.snapshot();
        if(current==before) {
            ++refused;
            if(brush.lastRejection().find("residual local collision")!=
               std::string::npos)++scarResidualRefusals;
            require(brush.rejectedStamps()==refused&&
                    brush.historyStatistics().undoDepth==history.undoDepth&&
                    brush.checkpoint().mirrorPairs==pairs&&
                    brush.lastDirtyPartitions().empty(),
                    "Refused contact preserves original mirror graph, history and renderer");
        }else {
            ++accepted;
            require(brush.historyStatistics().undoDepth==history.undoDepth+1&&
                    !brush.lastDirtyPartitions().empty(),
                    "Accepted Step publishes one complete immutable revision");
            std::vector<FaceId> currentFaces;
            current->revision().forEachPartition(
                [&](PartitionId,const MeshPartition& p) {
                    for(const auto& face:p.faces)currentFaces.push_back(face.id);
                });
            require(current->triangleIntersections({},currentFaces).empty(),
                    "Every published symmetric contact Step remains globally collision-free");
        }
        require(before->pickingRootIdentity()==root&&
                before->retiredFaceCount()<=current->retiredFaceCount(),
                "All held prior contact roots remain readable and tombstones monotonic");
        // This external acceptance audit is intentionally exhaustive for
        // these small synthetic towers, not a claimed O(1) live path.
        std::vector<FaceId> allFaces;
        current->revision().forEachPartition(
            [&](PartitionId,const MeshPartition& p) {
                for(const auto& face:p.faces)allFaces.push_back(face.id);
            });
        const auto audit=auditSurfaceWitnesses(
            *current,witnesses,allFaces,2.0,16384);
        require(audit.maximumDistanceInEdges<1e-8&&
                audit.rmsDistanceInEdges<1e-8,
                "Fixed outer-wall witnesses detect no cumulative erosion across contact Steps");
        held.push_back(current);
    }
    require(accepted+refused==16&&
            brush.collisionRepairedStamps()<=accepted&&
            brush.publishedRevisions()==accepted&&
            brush.historyStatistics().undoDepth==accepted,
            "Physical repeated-contact run accounts for every complete Step and commit");
    require(brush.contactStatistics().contactChecks+
            brush.contactStatistics().cleanChecks+
            brush.contactStatistics().uncertainChecks==16,
            "Hysteresis observes all assessments even when classification refuses");
    require(refused>0||brush.collisionRepairedStamps()>0,
            "Physical contact demands either classified merger or conservative refusal");
    require(scarResidualRefusals>0||brush.collisionRepairedStamps()>0,
            "Physical tower contact must demonstrate either successful union or a V16 scar-contact blocker");
    // Known current limitation: a fully classified private zipper can still
    // leave intersections; the V16 residual check MUST reject that Step.
    for(const auto& version:held)
        require(version->pickingRootIdentity()!=nullptr,
                "Held immutable reader roots survive all repeated contact attempts");
}

void testCompletePostSculptOriginalContactDiscovery() {
    const auto pairSet=[](
        const std::vector<RegionTopologyIndex::CollisionPair>& pairs) {
        std::set<std::pair<FaceId,FaceId>> ids;
        for(const auto& pair:pairs) {
            require(pair.first<pair.second&&!pair.ambiguous,
                    "M17 complete certified pair is canonical and unambiguous");
            require(ids.emplace(pair.first,pair.second).second,
                    "M17 does not return duplicate source pairs");
        }
        return ids;
    };
    const auto allVertices=[](const RegionTopologyIndex& root) {
        std::vector<VertexId> ids;
        root.revision().forEachPartition(
            [&](PartitionId,const MeshPartition& part) {
                for(const auto& vertex:part.vertices)ids.push_back(vertex.id);
            });
        return ids;
    };
    const auto checkSource=[&](
        const std::shared_ptr<const RegionTopologyIndex>& before,
        const VersionedInflateStamp& physical,
        bool mirrored,
        const std::unordered_map<VertexId,VertexId>* mirrors) {
        require(physical.index!=before&&physical.moved>0&&
                !physical.baseline.movedVertices.empty()&&
                !physical.baseline.affectedFaces.empty(),
                "A real Inflate deformation, not an explicit Merge command, makes the M17 candidate");
        const ProposedSculptStep privateStep{
            before,physical.index,physical.baseline.movedVertices,
            physical.baseline.affectedFaces};
        auto result=assessPostSculptOriginalContacts(
            privateStep,mirrored,{},mirrors);
        require(result.ready()&&result.originalCandidate==physical.index&&
                result.originalCandidate!=before&&
                !result.contacts.empty()&&result.refusal.empty(),
                "M17 returns a genuine complete candidate-root finite V13 contact graph");
        const auto raw=physical.index->triangleIntersections(
            allVertices(*physical.index),{});
        require(pairSet(result.contacts)==pairSet(raw)&&
                result.discoveredPairs==raw.size(),
                "M17 entire candidate-source graph equals independent all-live-face query");
        for(const auto& pair:result.contacts) {
            const auto idsA=physical.index->face(pair.first).vertices;
            const auto idsB=physical.index->face(pair.second).vertices;
            const auto xyz=[&](const std::array<VertexId,3>& ids) {
                return std::array<Vec3,3>{{
                    physical.index->vertex(ids[0]).position,
                    physical.index->vertex(ids[1]).position,
                    physical.index->vertex(ids[2]).position}};
            };
            require(splitTransverseTriangleContact(
                        xyz(idsA),idsA,xyz(idsB),idsB).status==
                        IntersectionSectionStatus::TransverseSegment,
                    "Every M17 claimed original contact has a finite source-proven transverse section");
        }
        auto capped=SculptSourceContactLimits{};
        capped.maxSectionTests=raw.size()-1;
        const auto incomplete=assessPostSculptOriginalContacts(
            privateStep,mirrored,capped,mirrors);
        require(incomplete.status==SculptSourceContactStatus::Refused&&
                incomplete.contacts.empty()&&!incomplete.originalCandidate&&
                !incomplete.refusal.empty(),
                "Too-small aggregate source-section limit cannot return truncated positive graph");
        capped={};
        capped.query.maxReportedContacts=raw.size()-1;
        const auto exhausted=assessPostSculptOriginalContacts(
            privateStep,mirrored,capped,mirrors);
        require(exhausted.status==SculptSourceContactStatus::Refused&&
                exhausted.contacts.empty()&&!exhausted.originalCandidate&&
                !exhausted.refusal.empty(),
                "Exactly one-short V13 query work cap never certifies a partial source list");
        capped={};
        capped.maxFaces=7;
        const auto tooMany=assessPostSculptOriginalContacts(
            privateStep,mirrored,capped,mirrors);
        require(tooMany.status==SculptSourceContactStatus::Refused&&
                tooMany.contacts.empty()&&!tooMany.originalCandidate,
                "Whole source-owner index size is bounded before attempting work");
        require(before->retiredFaceCount()==0&&
                physical.index->retiredFaceCount()==0,
                "M17 read-only contact certificates leave all original stable IDs untouched");
        return raw.size();
    };
    Mesh source;
    // This M16 source fixture has a complete positive three-contact
    // original FaceId graph. It is translated strictly OFF X=0.
    const std::array<Vec3,8> points{{
        {-1,0,0},{1,0,0},{0,1.2,0},{0,.2,-1.3},
        {-.65,.1,.35},{.65,.1,.35},{0,.9,.35},{0,.3125,-.65}}};
    for(const auto p:points)source.vertices.push_back({p.x+5,p.y,p.z});
    for(unsigned tetra=0;tetra<2;++tetra)
        for(const auto t:std::array<std::array<unsigned,3>,4>{{
            {0,1,2},{1,0,3},{2,1,3},{0,2,3}}})
            source.triangles.push_back({
                4*tetra+t[0],4*tetra+t[1],4*tetra+t[2]});
    const auto imported=partitionFlatMesh(source,256);
    const auto before=RegionTopologyIndex::build(imported.revision);
    const auto originalPicking=before->pickingRootIdentity();
    const std::array<BrushArea,1> stroke{{{{4,0,0},.5,.9}}};
    VersionedInflateBrush inflate(.0005,{});
    const auto candidate=inflate.stamp(before,stroke,256);
    const auto count=checkSource(before,candidate,false,nullptr);
    require(count==3,
            "Physical post-sculpt source candidate retains all three true M16 finite contacts");
    require(before->pickingRootIdentity()==originalPicking&&
            before->triangleIntersections(allVertices(*before),{}).size()==3,
            "M17 does not replace or rewrite held pre-stamp BVH readers");

    // Reflection must be certified on every live *candidate* stable ID,
    // never inferred by XYZ from an incomplete historical list.
    Mesh reflected=source;
    const auto firstCount=source.vertices.size();
    for(const auto p:source.vertices)
        reflected.vertices.push_back({-p.x,p.y,p.z});
    const auto firstFaces=source.triangles.size();
    for(std::size_t i=0;i<firstFaces;++i) {
        const auto t=source.triangles[i];
        const auto delta=static_cast<std::uint32_t>(firstCount);
        reflected.triangles.push_back({
            t.a+delta,t.c+delta,t.b+delta});
    }
    auto mirroredImport=partitionFlatMesh(reflected,256);
    const auto symmetricRoot=RegionTopologyIndex::build(mirroredImport.revision);
    std::vector<std::pair<VertexId,VertexId>> mirroredPairs;
    std::unordered_map<VertexId,VertexId> mirroredMap;
    for(std::size_t i=0;i<firstCount;++i) {
        const auto a=mirroredImport.flatIndex.vertexIds[i];
        const auto twin=mirroredImport.flatIndex.vertexIds[firstCount+i];
        mirroredPairs.emplace_back(a,twin);
        mirroredMap.emplace(a,twin);
        mirroredMap.emplace(twin,a);
    }
    const std::array<BrushArea,2> symmetricStroke{{
        {{4,0,0},.5,.9},{{-4,0,0},.5,.9}}};
    VersionedInflateBrush symmetricInflate(.0005,{},mirroredPairs);
    const auto symmetricCandidate=symmetricInflate.stamp(
        symmetricRoot,symmetricStroke,256);
    const auto symmetricCount=checkSource(
        symmetricRoot,symmetricCandidate,true,&mirroredMap);
    require(symmetricCount==6,
            "Reflected physical candidate discovers BOTH three-contact source halves");
    const auto symmetricProposal=ProposedSculptStep{
        symmetricRoot,symmetricCandidate.index,
        symmetricCandidate.baseline.movedVertices,
        symmetricCandidate.baseline.affectedFaces};
    auto missingMirror=mirroredMap;
    missingMirror.erase(mirroredPairs.front().first);
    const auto invalid=assessPostSculptOriginalContacts(
        symmetricProposal,true,{},&missingMirror);
    require(invalid.status==SculptSourceContactStatus::Refused&&
            !invalid.originalCandidate&&invalid.contacts.empty(),
            "A missing historical post-refine mirror ID cannot certify any contact pair");

    // Exercise real motion FROM A GAP to collision. The expected
    // crossing comes from normals/brush influence, not a preset
    // original-contact list, and no chosen dirty subset may hide
    // any other affected original face.
    bool induced=false;
    for(const double halfGap:{.15,.25,.35}) {
        if(induced)break;
        Mesh gap;
        const auto tetra=[&](Vec3 center) {
            const auto offset=static_cast<std::uint32_t>(gap.vertices.size());
            for(const auto p:std::array<Vec3,4>{{
                {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}}})
                gap.vertices.push_back(center+p);
            for(const auto t:std::array<std::array<unsigned,3>,4>{{
                {0,1,2},{0,2,3},{0,3,1},{1,3,2}}})
                gap.triangles.push_back({
                    offset+t[0],offset+t[1],offset+t[2]});
        };
        tetra({-1-halfGap,0,0});
        tetra({1+halfGap,.19,-.11});
        const auto gapImport=partitionFlatMesh(gap,256);
        const auto gapRoot=RegionTopologyIndex::build(gapImport.revision);
        require(gapRoot->triangleIntersections(
                    allVertices(*gapRoot),{}).empty(),
                "Physical M17 candidate begins with two independent closed solids and a gap");
        const auto cleanGap=assessPostSculptOriginalContacts(
            {gapRoot,gapRoot,{},{}},false);
        require(cleanGap.status==SculptSourceContactStatus::Clean&&
                cleanGap.originalCandidate==gapRoot&&
                cleanGap.contacts.empty()&&cleanGap.refusal.empty(),
                "A complete clean candidate returns no false finite source seams");
        for(const double strength:{.35,.55,.8,1.0,1.3}) {
            const std::array<BrushArea,1> area{{{{0,0,0},6,.95}}};
            VersionedInflateBrush moving(strength,{});
            const auto physical=moving.stamp(gapRoot,area,256);
            const ProposedSculptStep trial{
                gapRoot,physical.index,
                physical.baseline.movedVertices,
                physical.baseline.affectedFaces};
            auto observed=assessPostSculptOriginalContacts(trial,false);
            if(!observed.ready())continue;
            const auto ground=physical.index->triangleIntersections(
                allVertices(*physical.index),{});
            require(!ground.empty()&&
                    pairSet(ground)==pairSet(observed.contacts)&&
                    observed.originalCandidate==physical.index,
                    "Physical crossing uses the exact resulting root and all original pairs");
            // The same actual private candidate may still be locally
            // refined before the M18 decision. M17 must classify the
            // final post-remesh root, including any newly owned FaceIds,
            // rather than recycling its old pre-split pair list.
            RefineSettings settings;
            settings.enabled=true;
            settings.maximumEdgeRatio=.1;
            settings.maximumSplitPairsPerStamp=1;
            settings.optimizeFlips=false;
            VersionedLocalRefiner refiner(gap.vertices.size(),settings,{},256);
            const auto afterRefine=refiner.apply(
                physical.index,physical.baseline);
            require(afterRefine.index!=physical.index&&
                    !afterRefine.insertedVertices.empty(),
                    "Physical M17 contact probe actually performs one local post-Inflate edge split");
            std::vector<FaceId> changed=physical.baseline.affectedFaces;
            changed.insert(changed.end(),
                           afterRefine.changedFaces.begin(),
                           afterRefine.changedFaces.end());
            const auto classified=assessPostSculptOriginalContacts(
                {gapRoot,afterRefine.index,physical.baseline.movedVertices,
                 changed},false);
            const auto allAfterSplit=afterRefine.index->triangleIntersections(
                allVertices(*afterRefine.index),{});
            if(classified.ready()) {
                require(classified.originalCandidate==afterRefine.index&&
                        pairSet(classified.contacts)==pairSet(allAfterSplit),
                        "Real split/flip candidate certificates refer only to final post-remesh source FaceIds");
            }else {
                require(classified.status==SculptSourceContactStatus::Refused&&
                        classified.contacts.empty()&&
                        !classified.originalCandidate&&!classified.refusal.empty(),
                        "Ambiguous post-remesh original graph refuses with no partial old-source journal");
            }
            induced=true;
            std::cout<<"[v17-m17] genuine normal-driven gap-crossing: "
                     <<observed.contacts.size()
                     <<" complete source pairs, strength="<<strength<<'\n';
            break;
        }
    }
    require(induced,
            "At least one actual physically induced gap crossing must have complete finite V13 source evidence");

    // A test with a root but no selected changedFaces must never
    // silently truncate the full source graph.
    const auto completeWithoutDirty=assessPostSculptOriginalContacts(
        {before,before,{},{}},false);
    require(completeWithoutDirty.ready()&&
            completeWithoutDirty.originalCandidate==before&&
            completeWithoutDirty.contacts.size()==3,
            "Empty dirty spans do not erase the complete original V13 source graph");

    // A point-only contact is never a finite section. If the V13
    // collision query reports it, this stage refuses; otherwise the
    // read-only result may be clean but may NOT license a weld.
    {
        Mesh vertexTangent;
        cube(vertexTangent,{-1,-1,-1},1);
        cube(vertexTangent,{1,1,1},1);
        const auto importedTouch=partitionFlatMesh(vertexTangent,256);
        const auto touchRoot=RegionTopologyIndex::build(importedTouch.revision);
        const auto touch=assessPostSculptOriginalContacts(
            {touchRoot,touchRoot,{},{}},false);
        require(!touch.ready()&&touch.contacts.empty()&&
                (touch.status==SculptSourceContactStatus::Clean||
                 (!touch.originalCandidate&&!touch.refusal.empty())),
                "A vertex-only tangent cannot be certified as a finite source-paired Boolean seam");
    }
    // Exact coplanar overlap has no independently oriented finite
    // two-sheet interval, even though all source FaceIds exist.
    {
        Mesh coplanar;
        cube(coplanar,{0,0,0},1);
        cube(coplanar,{0,0,0},1);
        const auto importedCoplanar=partitionFlatMesh(coplanar,256);
        const auto planeRoot=RegionTopologyIndex::build(importedCoplanar.revision);
        auto limits=SculptSourceContactLimits{};
        limits.query.maxReportedContacts=1024;
        const auto decision=assessPostSculptOriginalContacts(
            {planeRoot,planeRoot,{},{}},false,limits);
        require(decision.status==SculptSourceContactStatus::Refused&&
                decision.contacts.empty()&&!decision.originalCandidate&&
                !decision.refusal.empty(),
                "Coplanar coincident shells cannot create false finite V13 original-source certificates");
    }
}
void testActualCollisionSculptCandidateNeverLeaksPartialGeometry() {
    Mesh m;
    cube(m,{-0.55,0,0},.9);
    cube(m,{0.55,0,0},.9);
    SculptCollisionSettings mode;
    mode.enabled=true;
    VersionedSculptBrush brush(m,.0001,{}, {},{},false,256,{},mode);
    const auto before=brush.snapshot();
    require(!before->triangleIntersections(
                brush.originalOrder()->vertexIds,{}).empty(),
            "Sculpt fixture begins with genuine intersecting disjoint surfaces");
    Mesh dummy;
    const std::array<BrushArea,1> area{{{{0,0,0},5,.3}}};
    brush.stamp(dummy,area);
    require(brush.completedStamps()==1&&brush.collisionCheckedStamps()==1,
            "Worker policy actually inspects changed collision geometry");
    require(brush.rejectedStamps()+brush.collisionRepairedStamps()==1,
            "Detected candidate either passes fully classified surgery or refuses");
    if(brush.rejectedStamps()) {
        require(brush.snapshot()==before&&
                brush.publishedRevisions()==0&&
                brush.historyStatistics().undoDepth==0&&
                brush.lastDirtyPartitions().empty(),
                "Unrepairable actual contact refuses without any published delta");
    }else {
        require(brush.snapshot()!=before&&
                brush.publishedRevisions()==1&&
                brush.historyStatistics().undoDepth==1,
                "Approved classified surgery atomically publishes one final Step");
        std::vector<FaceId> all;
        brush.snapshot()->revision().forEachPartition(
            [&](PartitionId,const MeshPartition& p) {
                for(const auto& f:p.faces)all.push_back(f.id);
            });
        require(brush.snapshot()->triangleIntersections({},all).empty(),
                "Actual accepted repair has no remaining contact anywhere in fixture");
    }
    require(before->retiredFaceCount()==0,
            "Old retained snapshot still has all original face IDs");
}
void testCompleteSculptCandidateClassifiedRepairGate() {
    Mesh mesh;
    cube(mesh,{},3);
    cube(mesh,{},.5);
    auto imported=partitionFlatMesh(mesh,1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto sourceRoot=original->pickingRootIdentity();
    const auto seed=imported.flatIndex.faceIds[12];
    SculptCollisionSettings selected;
    selected.enabled=true;
    // An ordinary collision-free stamp needs no other geometry preparation.
    const ProposedSculptStep noDirty{original,original,{},{}};
    const auto clean=evaluateSculptCollisionCandidate(noDirty,false,selected);
    require(clean.safe()&&!clean.repair&&
            clean.plan.status==RepairPlanStatus::NoChanges,
            "No-contact private candidate keeps its original immutable root");
    const auto approved=evaluateSculptCollisionCandidate(
        noDirty,false,selected,{seed});
    require(approved.safe()&&approved.plan.ready()&&approved.repair&&
            approved.repair->retiredFaces.size()==12&&
            approved.repair->retiredVertices.size()==8&&
            approved.repair->insertedFaces.empty(),
            "V17d1 runs V13-V16 classified redundant-shell repair privately");
    require(approved.repair->index!=original&&
            approved.repair->index->triangleIntersections(
                {},imported.flatIndex.faceIds).empty()&&
            original->pickingRootIdentity()==sourceRoot&&
            original->retiredFaceCount()==0,
            "Fully verified private replacement never modifies held source root");
    const auto symmetric=evaluateSculptCollisionCandidate(
        noDirty,true,selected,{seed});
    require(!symmetric.safe()&&
            symmetric.plan.ready()&&!symmetric.repair,
            "A symmetric classified cut must refuse until mirror-ID deltas are staged");
    selected.surgery.planning.collisionLimits.maxSeedFaces=0;
    const auto bounded=evaluateSculptCollisionCandidate(
        {original,original,
         std::span<const VertexId>(imported.flatIndex.vertexIds.data(),1),{}},
        false,selected);
    require(!bounded.safe()&&
            bounded.plan.status==RepairPlanStatus::BudgetExceeded&&
            !bounded.repair&&original->retiredFaceCount()==0,
            "Strict finite BVH work-budget exhaustion refuses whole private Step");
}
void testExplicitClassifiedRepairPolicyAndNoChanges() {
    Mesh m;
    cube(m,{},3);
    cube(m,{},.5);
    const auto imported=partitionFlatMesh(m,1);
    const auto original=RegionTopologyIndex::build(imported.revision);
    const auto initialRoot=original->pickingRootIdentity();
    const auto seed=imported.flatIndex.faceIds[12];
    IndexedMeshRepository repository(original);
    const auto noChanges=applyClassifiedExcisionRepair(
        original,{},{},{},ClassifiedFeaturePolicy::TopologyOnly);
    require(noChanges.plan.status==RepairPlanStatus::NoChanges&&
            !noChanges.surgery.has_value(),
            "No collision and no component seed produce no private revision");
    const auto raw=applyClassifiedExcisionRepair(
        original,{},{},{seed},ClassifiedFeaturePolicy::TopologyOnly);
    require(raw.plan.ready()&&raw.surgery.has_value()&&
            raw.plan.removedFaces.size()==12&&
            raw.surgery->retiredFaces.size()==12,
            "Classification-aware path only removes an isolated V14-redundant shell");
    const auto guarded=applyClassifiedExcisionRepair(
        original,{},{},{seed},
        ClassifiedFeaturePolicy::PreserveSampledExposedFeatures);
    require(guarded.plan.ready()&&guarded.surgery.has_value()&&
            guarded.surgery->removalProvenance.featureFragments()==0&&
            guarded.surgery->checkedReverseFragments==0&&
            guarded.surgery->retiredFaces.size()==12,
            "Explicit exposed-detail policy allows the certified redundant shell to disappear");
    require(repository.snapshot()==original&&
            original->pickingRootIdentity()==initialRoot&&
            original->retiredFaceCount()==0,
            "Classification and both private surgeries leave old reader root intact");
    auto forbidden=RepairSurgerySettings{};
    forbidden.requireExposedFragmentFidelity=true;
    bool rejected=false;
    try {
        (void)applyClassifiedExcisionRepair(
            original,{},{},{seed},ClassifiedFeaturePolicy::TopologyOnly,
            forbidden);
    }catch(const std::invalid_argument&){rejected=true;}
    require(rejected,
            "Classified wrapper rejects two conflicting ways of setting feature policy");
}
void testNearTangentAndFoldedClassificationStress() {
    // Three two-shell configurations: a known volumetric overlap, an
    // almost grazing near-coplanar contact and an exact tangent plane.
    // Neither a winding-ambiguity refusal nor a validated private
    // surgery may change the original renderer/picker's immutable root.
    bool sawActualIntersection=false;
    for(const unsigned variant:{0u,1u,2u}) {
        Mesh mesh;
        Vec3 a{-0.55,-0.1,-0.2},b{0.55,0.15,0.2};
        double half=.9;
        if(variant==1) {
            half=1;
            a={-1,0,0};
            b={1-1e-7,.03,.07};
        }else if(variant==2) {
            half=1;
            a={-1,0,0};
            b={1,0,0};
        }
        cube(mesh,a,half);
        cube(mesh,b,half);
        const auto imported=partitionFlatMesh(mesh,1);
        const auto initial=RegionTopologyIndex::build(imported.revision);
        const auto root=initial->pickingRootIdentity();
        RepairSurgerySettings settings;
        settings.planning.sampleDepth=1;
        settings.planning.maxClassifiedFaces=64;
        const auto plan=planApproximateExcision(
            *initial,imported.flatIndex.vertexIds,{},{},settings.planning);
        if(plan.intersections>0)sawActualIntersection=true;
        if(plan.ready()) {
            try {
                const auto repaired=applyClassifiedExcisionRepair(
                    initial,imported.flatIndex.vertexIds,{},{},
                    ClassifiedFeaturePolicy::PreserveSampledExposedFeatures,
                    settings);
                if(repaired.surgery)
                    require(repaired.surgery->index->triangleIntersections(
                            {},repaired.surgery->insertedFaces).empty(),
                            "Accepted near-contact surgery has no residual scar intersections");
            }catch(const std::length_error&) {
                // Bounded geometry-validation work may refuse.
            }catch(const std::logic_error&) {
                // Uncertain winding or invalid private geometry may
                // legitimately refuse this difficult near-contact edit.
                // Do NOT swallow our regression assertions (runtime_error).
            }
        }
        require(initial->pickingRootIdentity()==root&&
                initial->retiredFaceCount()==0,
                "Near-tangent or folded contact never mutates an original snapshot");
    }
    require(sawActualIntersection,
            "Stress fixture exercises actual overlapping triangles, not only harmless gaps");
}
void testSequentialOverlappingHistoryAndStaleCuts() {
    Mesh m;
    for(unsigned i=0;i<5;++i)
        cube(m,{0,static_cast<double>(i)*8,0},1);
    const auto imported=partitionFlatMesh(m,1);
    auto current=RegionTopologyIndex::build(imported.revision);
    IndexedMeshRepository repository(current);
    std::vector<std::shared_ptr<const RegionTopologyIndex>> history{current};
    for(unsigned i=0;i<5;++i) {
        const auto& f=imported.flatIndex.faceIds;
        const std::vector<FaceId> removal{f[12*i+2],f[12*i+3]};
        const auto plan=planRemovalContours(*current,removal);
        require(plan.ready()&&plan.cappedContours.size()==1,
                "Each sequential cap has an independent complete closure");
        const auto oldRoot=current->pickingRootIdentity();
        const auto privateResult=applyRepairSurgery(current,plan);
        require(repository.snapshot()==current&&
                current->pickingRootIdentity()==oldRoot,
                "A candidate revision never mutates the published old root");
        require(repository.publishIfCurrent(current,privateResult.index),
                "Each separate repair step publishes by one original-root CAS");
        const auto previous=current;
        current=privateResult.index;
        history.push_back(current);
        bool staleRejected=false;
        try {(void)applyRepairSurgery(current,plan);}
        catch(const std::exception&){staleRejected=true;}
        require(staleRejected,
                "A prior removal plan cannot be reapplied after its FaceIds retire");
        require(previous->retiredFaceCount()==i*2&&
                current->retiredFaceCount()==(i+1)*2&&
                euler(*current)==10,
                "Repeated repairs preserve five manifold shells and immutable retirement history");
    }
    for(unsigned i=0;i<history.size();++i)
        require(history[i]->retiredFaceCount()==i*2,
                "All held historical snapshots remain individually readable");
}
void testThirtyTwoRepeatedSamePatchCapsNoAccumulatedErosion() {
    // Replacing the SAME exterior patch repeatedly can pass one-step
    // proximity tests yet silently accumulate erosion. Anchor witness
    // points in the FIRST snapshot and compare all later revisions to
    // that original surface, not to the immediately preceding repair.
    Mesh m;
    cube(m,{},1);
    const auto imported=partitionFlatMesh(m,1);
    const auto initial=RegionTopologyIndex::build(imported.revision);
    const std::vector<FaceId> originalCap{
        imported.flatIndex.faceIds[2],imported.flatIndex.faceIds[3]};
    const auto originalWitnesses=sampleSurfaceWitnesses(*initial,originalCap);
    require(originalWitnesses.size()==14,
            "Two original triangles supply fourteen anchored geometric samples");
    bool refused=false;
    try {(void)sampleSurfaceWitnesses(*initial,originalCap,13);}
    catch(const std::length_error&){refused=true;}
    require(refused,"Witness gathering enforces a hard sampling budget");

    IndexedMeshRepository repository(initial);
    auto current=initial;
    auto selected=originalCap;
    std::vector<std::shared_ptr<const RegionTopologyIndex>> history{initial};
    for(unsigned step=0;step<32;++step) {
        const auto plan=planRemovalContours(*current,selected);
        require(plan.ready()&&plan.cappedContours.size()==1&&
                plan.contours.size()==1,
                "Repeated replacement of the same patch still plans one cap");
        const auto oldRoot=current->pickingRootIdentity();
        const auto fixed=applyRepairSurgery(current,plan);
        require(fixed.capFaces==2&&fixed.zipperFaces==0&&
                fixed.insertedFaces.size()==2,
                "Each repeated cap creates a new oriented two-triangle disk");
        const auto measured=auditSurfaceWitnesses(
            *fixed.index,originalWitnesses,fixed.insertedFaces,2);
        require(measured.witnessCount==14&&measured.candidateFaces==2&&
                measured.distanceTests==28&&
                measured.maximumDistanceInEdges<1e-10&&
                measured.meanDistanceInEdges<1e-10&&
                measured.rmsDistanceInEdges<1e-10,
                "Original exterior cap witnesses never accumulate geometric erosion");
        if(step==0) {
            bool overBudget=false;
            try {(void)auditSurfaceWitnesses(
                *fixed.index,originalWitnesses,fixed.insertedFaces,2,27);}
            catch(const std::length_error&){overBudget=true;}
            require(overBudget,"Distance-audit work limit refuses an over-budget query");
            bool stale=false;
            try {(void)auditSurfaceWitnesses(
                *fixed.index,originalWitnesses,originalCap,2);}
            catch(const std::exception&){stale=true;}
            require(stale,
                "Audit never silently reads retired initial cap FaceIds");
            const std::vector<FaceId> opposite{
                imported.flatIndex.faceIds[0],imported.flatIndex.faceIds[1]};
            const auto lost=auditSurfaceWitnesses(
                *fixed.index,originalWitnesses,opposite,2);
            require(lost.maximumDistanceInEdges>.9&&
                    lost.rmsDistanceInEdges>.9,
                    "Wrong-surface witness audit detects substantial apparent erosion");
        }
        require(repository.snapshot()==current&&
                current->pickingRootIdentity()==oldRoot,
                "Replacement remains private until successful CAS publication");
        require(repository.publishIfCurrent(current,fixed.index),
                "Each patch replacement publishes as its own atomic revision");
        current=fixed.index;
        selected=fixed.insertedFaces;
        history.push_back(current);
        require(current->retiredFaceCount()==2*(step+1)&&
                euler(*current)==2,
                "Thirty-two overlapping repairs never change genus or reuse retired faces");
    }
    for(unsigned i=0;i<history.size();++i)
        require(history[i]->retiredFaceCount()==2*i,
                "All thirty-three historical snapshots keep their own tombstones");
}
void testFoldedDeepMultiShellCollisionMatrix() {
    // Rotated/offset intersecting volumes intentionally exceed what
    // triangle-only approximate excision can always safely reconstruct.
    // Their classification must refuse ambiguous/budget cases, and
    // any successfully constructed private scar must pass post-state
    // collision validation without changing the published root.
    bool sawIntersections=false;
    for(const double shift:{-.7,0.0,.7}) {
        Mesh mesh;
        cube(mesh,{-1.25,0,0},1);
        const Vec3 middle{.35,shift,.2};
        cube(mesh,middle,1);
        const Vec3 last{.05,-.35,shift};
        cube(mesh,last,.8);
        // Rotate the second closed solid and shear the third. Both are
        // orientation-preserving affine transforms, so face topology
        // remains valid but the overlap cuts are now strongly oblique.
        const double angle=.37+shift*.2;
        const double sn=std::sin(angle),cs=std::cos(angle);
        for(unsigned i=8;i<16;++i) {
            const auto p=mesh.vertices[i]-middle;
            mesh.vertices[i]=middle+Vec3{
                cs*p.x-sn*p.y,sn*p.x+cs*p.y,p.z};
        }
        for(unsigned i=16;i<24;++i) {
            const auto p=mesh.vertices[i]-last;
            mesh.vertices[i]=last+Vec3{
                p.x+.25*p.z,p.y+.2*p.x,p.z};
        }
        const auto imported=partitionFlatMesh(mesh,1);
        const auto baseline=RegionTopologyIndex::build(imported.revision);
        const auto root=baseline->pickingRootIdentity();
        RepairSurgerySettings limits;
        limits.planning.sampleDepth=2;
        limits.planning.maxClassifiedFaces=128;
        limits.planning.maxRemovedFaces=64;
        const auto plan=planApproximateExcision(
            *baseline,imported.flatIndex.vertexIds,{},{},limits.planning);
        sawIntersections|=plan.intersections>0;
        if(plan.ready()) {
            try {
                const auto result=applyClassifiedExcisionRepair(
                    baseline,imported.flatIndex.vertexIds,{},{},
                    ClassifiedFeaturePolicy::PreserveSampledExposedFeatures,
                    limits);
                if(result.surgery) {
                    require(result.surgery->verifiedLocalFaces>0&&
                            result.surgery->index->triangleIntersections(
                                {},result.surgery->insertedFaces).empty(),
                            "Accepted folded candidate has validated local geometry");
                }
            }catch(const std::length_error&) {
                // A bounded workload is a valid fail-closed outcome.
            }catch(const std::logic_error&) {
                // Complex near-overlap can require unsupported retopology.
            }
        }
        require(baseline->retiredFaceCount()==0&&
                baseline->pickingRootIdentity()==root,
                "Deep multicomponent collisions never alter a retained source root");
    }
    require(sawIntersections,
            "Folded collision stress actually creates intersecting triangles");
}
void testGeometricValidationBudgets() {
    Mesh m;
    cube(m,{-1.1,0,0},1);
    cube(m,{1.1,0,0},1);
    const auto imported=partitionFlatMesh(m,1);
    const auto base=RegionTopologyIndex::build(imported.revision);
    const auto& f=imported.flatIndex.faceIds;
    const auto plan=planRemovalContours(*base,
        {f[10],f[11],f[20],f[21]});
    require(plan.ready()&&plan.pairs.size()==1,
            "Drift budget uses the nearby zipper fixture");
    const auto standard=applyRepairSurgery(base,plan);
    require(standard.verifiedLocalFaces>standard.insertedFaces.size(),
            "Residual validator checks surviving faces beyond inserted scar");
    require(standard.maximumSurfaceDeviationInEdges>0,
            "Cross-gap zipper has measurable deviation from removed geometry");
    auto strict=RepairSurgerySettings{};
    strict.maximumSurfaceDeviationInEdges=.001;
    bool rejected=false;
    try {(void)applyRepairSurgery(base,plan,strict);}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"Too-strict quantitative drift budget refuses surgery");
    strict={};
    strict.maxResidualFaces=1;
    rejected=false;
    try {(void)applyRepairSurgery(base,plan,strict);}
    catch(const std::exception&) {rejected=true;}
    require(rejected,"Bounded local collision verification rejects over-budget neighborhood");
    strict={};
    strict.residualCollisionLimits.maxBoxTests=1;
    rejected=false;
    try {(void)applyRepairSurgery(base,plan,strict);}
    catch(const std::length_error&) {rejected=true;}
    require(rejected,
            "Final private scar refuses when BVH collision validation exceeds its work limit");
    require(base->retiredFaceCount()==0,
            "All geometry budget refusals preserve the original immutable revision");
}
void testRejectionIsTransactional() {
    Mesh m;
    const auto left=prism(m,-2,0,square(1));
    const auto right=prism(m,.1,2,unevenSquare(1));
    const auto imp=partitionFlatMesh(m,1);
    const auto baseline=RegionTopologyIndex::build(imp.revision);
    auto removed=capIds(imp,left,false);
    const auto more=capIds(imp,right,true);
    removed.insert(removed.end(),more.begin(),more.end());
    const auto plan=planRemovalContours(*baseline,removed);
    const auto before=baseline->pickingRootIdentity();
    RepairSurgerySettings constrained;
    constrained.maxNewVertices=1;
    bool failed=false;
    try{(void)applyRepairSurgery(baseline,plan,constrained);}
    catch(const std::exception&){failed=true;}
    require(failed,"Insufficient subdivision budget fails transaction");
    require(baseline->retiredFaceCount()==0&&
            baseline->pickingRootIdentity()==before,
            "Failed surgery never mutates base revision");
    auto tampered=plan;
    tampered.contours[0].vertexIds[0]=
        tampered.contours[0].vertexIds[1];
    failed=false;
    try{(void)applyRepairSurgery(baseline,tampered);}
    catch(const std::exception&){failed=true;}
    require(failed,"A stale or forged contour is rejected before mutations");
    require(baseline->retiredFaceCount()==0,
            "Tampered surgery does not retire stable IDs");
}
}
int main() {
    try {
        std::cout<<"Surgery fixture: pure multi-edge triangle"<<std::endl;
        testPureMultiEdgeRetainedFaceSplitting();
        std::cout<<"Surgery fixture: cap"<<std::endl;
        testCapsAndSnapshotPublication();
        std::cout<<"Surgery fixture: separated zipper"<<std::endl;
        testZipperJoinsSeparatedSolids();
        std::cout<<"Surgery fixture: uneven zipper"<<std::endl;
        testUnequalSpacingSharedEdgeSplits();
        std::cout<<"Surgery fixture: mirrored zipper"<<std::endl;
        testMirroredCandidateRetirementAndLiveRollback();
        testNewMirroredSeamPairsAndMissingOrbits();
        testMirroredZipperUsesOnePlaneSeam();
        std::cout<<"Surgery fixture: mirrored caps"<<std::endl;
        testMirroredIndependentCaps();
        std::cout<<"Surgery fixture: mirrored refusal"<<std::endl;
        testMirroredRejectsIncompleteMapping();
        std::cout<<"Surgery fixture: mirrored uneven boundary"<<std::endl;
        testMirroredUnevenBoundarySubdivision();
        std::cout<<"Surgery fixture: irregular isolated cap"<<std::endl;
        testIsolatedIrregularAndWarpedCap();
        std::cout<<"Surgery fixture: self-crossing projected cap"<<std::endl;
        testProjectedSelfCrossingCapIsRejected();
        std::cout<<"Surgery fixture: ambiguous warped multiple loops"<<std::endl;
        testNonplanarMultiContourStaysAmbiguous();
        std::cout<<"Surgery fixture: symmetry-plane preflight"<<std::endl;
        testSymmetryPlaneClassificationFailsClosed();
        std::cout<<"Surgery fixture: mirrored crossing orbit and fixed-arc topology"<<std::endl;
        testMirroredPlaneOrbitAndDiskFixedArc();
        std::cout<<"Surgery fixture: distinct reflected edge intersection refusal"<<std::endl;
        testDistinctReflectedCrossingEdgesRejectAsSelfIntersection();
        std::cout<<"Surgery fixture: self-reflected plane edge normalization"<<std::endl;
        testAtomicSelfReflectedPlaneCrossingNormalization();
        std::cout<<"Surgery fixture: no crossing wrapper compatibility"<<std::endl;
        testNormalizeAndReplanNoCrossingCompatibility();
        std::cout<<"Surgery fixture: single crossing normalize/replan"<<std::endl;
        testSingleCrossingContourRepair();
        std::cout<<"Surgery fixture: two crossing normalize/replan"<<std::endl;
        testCrossingContourFullNormalizeReplanRepair();
        std::cout<<"Surgery fixture: four crossings across two valid contours"<<std::endl;
        testFourCrossingsAcrossTwoIndependentContours();
        std::cout<<"Surgery fixture: self-mirrored cap"<<std::endl;
        testSelfMirroredCapAcrossExistingSeam();
        std::cout<<"Surgery fixture: reflected shell retirement"<<std::endl;
        testMirroredWholeComponentRetirement();
        std::cout<<"Surgery fixture: exposed V14 feature fidelity"<<std::endl;
        testSampledExcisionProvenanceAndSelectiveReverseFidelity();
        std::cout<<"Surgery fixture: buried shell removal provenance"<<std::endl;
        testRedundantBuriedShellCanDisappearWithoutReversePenalty();
        std::cout<<"Surgery fixture: eight independent near-coplanar caps"<<std::endl;
        testEightIndependentNearCoplanarCapRepairs();
        std::cout<<"Surgery fixture: intersected mixed face provenance"<<std::endl;
        testMixedExposedAndBuriedFaceProvenance();
        std::cout<<"Surgery fixture: explicit classified repair policy"<<std::endl;
        testAnchoredOriginalWitnessesProtectAgainstCumulativeDrift();
        testContactEpisodeHysteresisNeverCachesApproval();
        testRepeatedRealSymmetricTowerContactAndAnchoredSurface();
        testCompletePostSculptOriginalContactDiscovery();
        testActualCollisionSculptCandidateNeverLeaksPartialGeometry();
        testCompleteSculptCandidateClassifiedRepairGate();
        testExplicitClassifiedRepairPolicyAndNoChanges();
        std::cout<<"Surgery fixture: sequential stale overlapping repair plans"<<std::endl;
        testSequentialOverlappingHistoryAndStaleCuts();
        std::cout<<"Surgery fixture: grazing and intersecting classified candidates"<<std::endl;
        testNearTangentAndFoldedClassificationStress();
        std::cout<<"Surgery fixture: 32 overlapping repairs and original-surface erosion"<<std::endl;
        testThirtyTwoRepeatedSamePatchCapsNoAccumulatedErosion();
        std::cout<<"Surgery fixture: deep multicomponent folded collision matrix"<<std::endl;
        testFoldedDeepMultiShellCollisionMatrix();
        std::cout<<"Surgery fixture: local geometric bounds"<<std::endl;
        testGeometricValidationBudgets();
        std::cout<<"Surgery fixture: repeated mirrored edits"<<std::endl;
        testRepeatedMirroredSurgeryKeepsHistory();
        std::cout<<"Surgery fixture: rejection"<<std::endl;
        testRejectionIsTransactional();
        std::cout<<"Private transactional cap/zipper surgery regression passed\n";
    }catch(const std::exception& error) {
        std::cerr<<"Repair surgery failure: "<<error.what()<<'\n';
        return 1;
    }
}

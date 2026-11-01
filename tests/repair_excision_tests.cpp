#include "mesher/repair_excision.hpp"
#include "mesher/region_edit.hpp"
#include "mesher/repair_inspection.hpp"
#include "mesher/sculpt_fixtures.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/partition_render.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/desktop_offmode_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* text) {
    if(!ok)throw std::runtime_error(text);
}
struct Fixture {
    std::shared_ptr<const RegionTopologyIndex> root;
    FlatMeshIndex ids;
};
Fixture fixture(Mesh mesh) {
    auto imported=partitionFlatMesh(mesh,64);
    return {RegionTopologyIndex::build(imported.revision),imported.flatIndex};
}
RegionTopologyIndex::CollisionPair pairOf(FaceId a,FaceId b) {
    if(a>b)std::swap(a,b);
    return {a,b,false};
}
void verifyCut(const Fixture& f,
               const RepairExcisionPreview& preview,
               bool expectHoles) {
    require(preview.complete && !preview.proposedFaces.empty(),
            "R03 input must be complete and nonempty");
    const auto originalFaces=f.root->retiredFaceCount();
    const auto originalVertices=f.root->retiredVertexCount();
    const auto originalIdentity=f.root->pickingRootIdentity();
    const auto originalRevision=&f.root->revision();
    const auto cut=applyPrivateExcision(f.root,preview);
    require(cut.index&&cut.index!=f.root&&
            &cut.index->revision()!=originalRevision,
            "R03 must build a distinct immutable private revision");
    require(cut.removedFaces==preview.proposedFaces.size()&&
            cut.removedVertices==preview.proposedVertices.size()&&
            cut.boundaryEdges==preview.boundaryEdges&&
            cut.boundaryLoops==preview.contours.size(),
            "R03 actual open edges must equal the predicted directed contours");
    require((cut.boundaryEdges>0)==expectHoles,
            "R03 expected holes or closed-component removal as configured");
    for(const auto id:preview.proposedFaces)
        require(!cut.index->maybeFace(id)&&cut.index->retiredFace(id)&&
                f.root->maybeFace(id),
                "Removed private face keeps tombstone but original face remains present");
    for(const auto id:preview.proposedVertices)
        require(!cut.index->maybeVertex(id)&&cut.index->retiredVertex(id)&&
                f.root->maybeVertex(id),
                "Removed private vertex keeps tombstone but original remains");
    // Positive equivalence oracle: R03's incremental deletion MUST
    // describe precisely the same complete mesh, vertex incidences,
    // indexed directed edge sides, and historical tombstones as a fresh
    // full rebuild of this already-audited private revision.
    const auto reference=RegionTopologyIndex::restore(
        std::shared_ptr<const MeshRevision>(cut.index,&cut.index->revision()),
        cut.index->retiredVertexIds(),cut.index->retiredFaceIds());
    require(cut.index->updateStatistics().fullMeshTraversals==0,
            "R03 should update the persistent index rather than rebuild everything");
    require(reference->retiredVertexIds()==cut.index->retiredVertexIds()&&
            reference->retiredFaceIds()==cut.index->retiredFaceIds(),
            "R03 incremental excision lost the exact permanent ID tombstones");
    cut.index->revision().forEachPartition(
        [&](PartitionId owner,const MeshPartition& part) {
            for(const auto& v:part.vertices) {
                const auto& actual=cut.index->vertex(v.id);
                const auto& rebuilt=reference->vertex(v.id);
                require(actual.owner==owner&&rebuilt.owner==owner&&
                        actual.position.x==rebuilt.position.x&&
                        actual.position.y==rebuilt.position.y&&
                        actual.position.z==rebuilt.position.z&&
                        actual.incidentFaces==rebuilt.incidentFaces,
                        "R03 incremental vertex owner/position/incidence differs from full rebuild");
            }
            for(const auto& f:part.faces) {
                const auto& actual=cut.index->face(f.id);
                const auto& rebuilt=reference->face(f.id);
                require(actual.owner==owner&&rebuilt.owner==owner&&
                        actual.vertices==rebuilt.vertices&&
                        actual.vertices==f.vertices,
                        "R03 incremental indexed face differs from full rebuild");
                for(unsigned k=0;k<3;++k) {
                    auto a=f.vertices[k],b=f.vertices[(k+1)%3];
                    if(a>b)std::swap(a,b);
                    const auto real=cut.index->maybeEdgeSides({a,b});
                    const auto oracle=reference->maybeEdgeSides({a,b});
                    require(real&&oracle&&real->size()==oracle->size(),
                            "R03 incremental edge incidence count differs from full rebuild");
                    for(std::size_t i=0;i<real->size();++i)
                        require((*real)[i].face==(*oracle)[i].face&&
                                (*real)[i].forward==(*oracle)[i].forward,
                                "R03 incremental edge winding differs from full rebuild");
                }
            }
        });
    require(f.root->retiredFaceCount()==originalFaces&&
            f.root->retiredVertexCount()==originalVertices&&
            f.root->pickingRootIdentity()==originalIdentity,
            "R03 MUST NOT touch original candidate root or ID ledgers");
    // The ordinary indexed wireframe path must render the PRIVATE holes,
    // not stale partition cache geometry and not ghosts of removed faces.
    PartitionRenderCache cache;
    cache.initialize(f.root);
    std::vector<PartitionId> all;
    cut.index->revision().forEachPartition(
        [&](PartitionId id,const MeshPartition&){all.push_back(id);});
    const auto render=cache.advance(cut.index,all);
    require(render.rebuiltPartitions>0,
            "R03 private cut must invalidate GPU-visible partition geometry");
}
void twoSphereHoles() {
    SphereOptions options;
    options.vertexCount=320;
    options.symmetric=false;
    options.relaxationIterations=8;
    const auto f=fixture(generateSphere(options));
    FaceId up=0,down=0;
    double high=-1e20,low=1e20;
    for(const auto id:f.ids.faceIds) {
        const auto& t=f.root->face(id).vertices;
        const auto center=(f.root->vertex(t[0]).position+
                           f.root->vertex(t[1]).position+
                           f.root->vertex(t[2]).position)/3;
        if(center.y>high){high=center.y;up=id;}
        if(center.y<low){low=center.y;down=id;}
    }
    const auto sibling=[&](FaceId id) {
        const auto tri=f.root->face(id).vertices;
        auto a=tri[0],b=tri[1];
        if(b<a)std::swap(a,b);
        for(const auto& side:f.root->edgeSides({a,b}))
            if(side.face!=id)return side.face;
        throw std::logic_error("No sibling face on a closed sphere");
    };
    const auto preview=previewApproximateExcision(*f.root,{
        pairOf(up,sibling(up)),pairOf(down,sibling(down))});
    require(preview.complete&&preview.contours.size()==2,
            "R03 dual sphere test needs two clean independent open boundaries");
    verifyCut(f,preview,true);
    std::cout<<"[r03] Two independent oriented open sphere holes: PASS\n";
}
void fixtureMouth(Mesh mesh,Vec3 a,Vec3 b,const char* label) {
    const auto f=fixture(std::move(mesh));
    const auto nearest=[&](Vec3 target) {
        FaceId bestId=0;double best=1e100;
        for(const auto id:f.ids.faceIds) {
            const auto& tri=f.root->face(id).vertices;
            const auto center=(f.root->vertex(tri[0]).position+
                               f.root->vertex(tri[1]).position+
                               f.root->vertex(tri[2]).position)/3;
            const double distance=length(center-target);
            if(distance<best){best=distance;bestId=id;}
        }
        return bestId;
    };
    const auto preview=previewApproximateExcision(
        *f.root,{pairOf(nearest(a),nearest(b))});
    require(preview.complete&&preview.contours.size()>0,
            "Tight torus/hollow fixture must permit coherent bounded cut");
    verifyCut(f,preview,true);
    std::cout<<"[r03] "<<label<<" private holes="
             <<preview.contours.size()<<" faces="
             <<preview.proposedFaces.size()<<": PASS\n";
}
void wholeComponentRemovalAndRefusals() {
    Mesh mesh;
    const auto cube=[&](Vec3 center) {
        const auto offset=static_cast<std::uint32_t>(mesh.vertices.size());
        for(const auto point:std::array<Vec3,8>{{
            {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
            {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
        }})mesh.vertices.push_back(center+point);
        for(const auto t:std::array<std::array<std::uint32_t,3>,12>{{
            {0,2,1},{0,3,2},{4,5,6},{4,6,7},
            {0,1,5},{0,5,4},{3,7,6},{3,6,2},
            {0,4,7},{0,7,3},{1,2,6},{1,6,5}
        }})mesh.triangles.push_back({
            offset+t[0],offset+t[1],offset+t[2]});
    };
    cube({-3,0,0});cube({3,0,0});
    const auto f=fixture(std::move(mesh));
    RepairExcisionPreview preview;
    preview.complete=true;
    preview.proposedVertices.assign(f.ids.vertexIds.begin(),
                                    f.ids.vertexIds.begin()+8);
    preview.proposedFaces.assign(f.ids.faceIds.begin(),
                                 f.ids.faceIds.begin()+12);
    verifyCut(f,preview,false);
    const auto result=applyPrivateExcision(f.root,preview);
    require(result.survivingFaces==12&&result.boundaryEdges==0,
            "Entire closed component removal should not invent holes or require caps");
    preview.complete=false;
    bool refused=false;
    try {(void)applyPrivateExcision(f.root,preview);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R03 must refuse an incomplete R02 preview");
    preview.complete=true;
    preview.proposedFaces.pop_back();
    refused=false;
    try {(void)applyPrivateExcision(f.root,preview);}
    catch(const std::logic_error&){refused=true;}
    require(refused,"R03 must refuse a partial incident fan");
    std::cout<<"[r03] Whole closed component deletion with zero loops, fail-closed input: PASS\n";
}
void closedTetraActualSessionFreeze() {
    // Exercise the *same session* that the GUI passes to its async worker,
    // with a real fully closed candidate and a newly intersecting face set.
    // R01's near-sheets regression covers refusal but not successful wiring.
    Mesh m;
    const auto tetra=[&](Vec3 at,double h) {
        const auto base=static_cast<std::uint32_t>(m.vertices.size());
        for(const auto p:std::array<Vec3,4>{{
            {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}
        }})m.vertices.push_back(at+p*h);
        for(const auto f:std::array<std::array<std::uint32_t,3>,4>{{
            {0,1,2},{0,2,3},{0,3,1},{1,3,2}
        }})m.triangles.push_back({base+f[0],base+f[1],base+f[2]});
    };
    tetra({-2,0,0},.5);
    tetra({2,0,0},.5);
    const auto input=fixture(std::move(m));
    const std::vector<VertexId> moving(
        input.ids.vertexIds.begin(),input.ids.vertexIds.begin()+4);
    RegionEdit edit(input.root,moving,{},64);
    for(const auto id:moving)
        edit.moveVertex(id,input.root->vertex(id).position+Vec3{3.9,0,0});
    (void)edit.commit();
    const auto candidate=edit.updatedIndex();

    RepairInspectionSession session(true,true);
    const auto refusal=session.inspect({input.root,candidate,moving,{}});
    require(!refusal.empty()&&session.frozen(),
            "R03 must freeze on a real new collision of closed independent shells");
    const auto observed=session.frozenSnapshot();
    if(observed)std::cout<<"[r03-session] contacts="<<observed->contacts.size()
        <<" preview_complete="<<observed->excision.complete
        <<" proposed_faces="<<observed->excision.proposedFaces.size()
        <<" private_cut="<<observed->privateCut.has_value()
        <<" preview_reason="<<observed->excision.diagnostic
        <<" cut_reason="<<observed->excisionDiagnostic<<"\n";
    require(observed&&observed->excision.complete&&
            observed->privateCut.has_value()&&
            observed->privateCut->index!=candidate,
            "R03 must actually produce a distinct private cut through the live inspector");
    const auto cut=observed->privateCut->index;
    for(const auto id:observed->excision.proposedFaces)
        require(!cut->maybeFace(id)&&candidate->maybeFace(id),
                "R03 freeze must display cut faces only in private revision");
    const auto overlay=buildRepairInspectionOverlay(*observed);
    for(const auto& vertex:overlay)
        require(vertex.material==6.0f,
                "Successfully cut R03 preview cannot retain orange/red ghost triangles");
    require(input.root->retiredFaceCount()==0&&
            candidate->retiredFaceCount()==0,
            "R03 private cut never mutates source or uncut post-stamp candidate");
    std::cout<<"[r03] Real closed-shell collision through inspector creates private excision: PASS\n";
}

void unsupportedOpenFixtureFreezesWithoutExcision() {
    auto session=std::make_shared<RepairInspectionSession>(true,true);
    const auto validator=[session](const ProposedSculptStep& proposal) {
        return session->inspect(proposal);
    };
    VersionedSculptBrush brush(desktopOffModeNearSheetsFixture(),.9,
                               {},{},{},false,256,validator);
    const auto prior=brush.snapshot();
    Mesh unused;
    const std::array<BrushArea,1> stamp{{{{0,0,0},.5,.3}}};
    brush.stamp(unused,stamp);
    require(session->frozen(),"R03 must freeze at first actual post-remesh contact");
    const auto captured=session->frozenSnapshot();
    if(captured)std::cout<<"[r03-live] contacts="<<captured->contacts.size()
        <<" cut_complete="<<captured->excision.complete
        <<" cut_vertices="<<captured->excision.proposedVertices.size()
        <<" cut_faces="<<captured->excision.proposedFaces.size()
        <<" cut_loops="<<captured->excision.contours.size()
        <<" reason="<<captured->excision.diagnostic
        <<" excision_refusal="<<captured->excisionDiagnostic
        <<" private_cut="<<captured->privateCut.has_value()<<"\n";
    // The historical R01 near-sheets fixture consists of independent OPEN
    // triangles, not an original closed 2-manifold. It is valid for R01
    // contact detection but MUST NOT be accepted for R02/R03 topology cuts.
    require(captured&&captured->excisionEnabled&&
            !captured->privateCut.has_value()&&
            !captured->excision.complete&&
            !captured->excisionDiagnostic.empty(),
            "R03 must fail closed when a true stamp hits unsupported OPEN source topology");
    require(brush.snapshot()==prior&&brush.historyStatistics().undoDepth==0,
            "R03 must never publish even the pre-excision candidate to worker/history");
    const auto visible=buildRepairInspectionOverlay(*captured);
    require(!visible.empty()&&
            std::any_of(visible.begin(),visible.end(),[](const auto& marker) {
                return marker.material==2.0f;
            })&&
            std::none_of(visible.begin(),visible.end(),[](const auto& marker) {
                return marker.material==4.0f||marker.material==5.0f;
            }),
            "Rejected R03 cut must show red contact warning but NEVER orange fake-excision overlays");
    brush.stamp(unused,stamp);
    require(brush.snapshot()==prior,
            "R03 further brush stamps must be refused after freeze");
}
}
int main() {
    try {
        twoSphereHoles();
        fixtureMouth(generateTorusFixture(5000,1,5),
                     {.12,0,0},{-.12,0,0},"tight torus");
        fixtureMouth(generateHollowSphereFixture(5000,1,65),
                     {.095,-.96,0},{.095,-.93,0},"hollow-shell mouth");
        wholeComponentRemovalAndRefusals();
        closedTetraActualSessionFreeze();
        unsupportedOpenFixtureFreezesWithoutExcision();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"R03 open excision test FAILED: "<<e.what()<<"\n";
        return 1;
    }
    return 0;
}

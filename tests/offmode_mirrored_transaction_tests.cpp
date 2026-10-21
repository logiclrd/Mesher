#include "mesher/async_sculpt.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/offmode_clearance.hpp"
#include "mesher/offmode_mirror.hpp"
#include "mesher/versioned_sculpt.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* detail) {
    if(!ok)throw std::runtime_error(detail);
}
struct Fixture {
    DurableCheckpoint source;
    std::vector<VertexId> vertexIds;
};
Fixture mirroredSource(bool duplicateRemote=false) {
    Mesh mesh;
    // Independent positive-X bottom/top sheets face toward each other.
    // Reversed winding of the upper sheet points its normal DOWN.
    const std::array<Vec3,6> positive{{
        {4,0,0},{6,0,0},{4,2,0},
        {4,0,.14},{4,2,.14},{6,0,.14}
    }};
    for(const auto p:positive)mesh.vertices.push_back(p);
    for(const auto p:positive)mesh.vertices.push_back({-p.x,p.y,p.z});
    mesh.triangles={{0,1,2},{3,4,5},{6,8,7},{9,11,10}};
    // Real original X=0 source-fixed vertices: two opposite face
    // halves, NOT fictitious duplicated vertices at equal XYZ.
    mesh.vertices.insert(mesh.vertices.end(),{
        {-1,11,0},{0,10,0},{0,12,0},{1,11,0}
    });
    mesh.triangles.push_back({12,13,14});
    mesh.triangles.push_back({15,14,13});
    if(duplicateRemote) {
        // Entirely unrelated coincident *coordinates*, with four
        // independent triangles and distinct permanent stable IDs.
        // These MUST NOT imply any new mirror or welded identity.
        for(unsigned repeat=0;repeat<2;++repeat) {
            const std::array<Vec3,3> p{{
                {40,0,0},{42,0,0},{40,2,0}
            }};
            const auto start=static_cast<std::uint32_t>(mesh.vertices.size());
            for(const auto point:p)mesh.vertices.push_back(point);
            for(const auto point:p)
                mesh.vertices.push_back({-point.x,point.y,point.z});
            mesh.triangles.push_back({start,start+1,start+2});
            mesh.triangles.push_back({start+3,start+5,start+4});
        }
    }
    const auto imported=partitionFlatMesh(mesh,256);
    auto root=RegionTopologyIndex::build(imported.revision);
    std::vector<MirrorPair> pairs;
    for(unsigned i=0;i<6;++i)
        pairs.emplace_back(imported.flatIndex.vertexIds[i],
                           imported.flatIndex.vertexIds[i+6]);
    pairs.emplace_back(imported.flatIndex.vertexIds[12],
                       imported.flatIndex.vertexIds[15]);
    pairs.emplace_back(imported.flatIndex.vertexIds[13],
                       imported.flatIndex.vertexIds[13]);
    pairs.emplace_back(imported.flatIndex.vertexIds[14],
                       imported.flatIndex.vertexIds[14]);
    if(duplicateRemote)for(unsigned repeat=0;repeat<2;++repeat) {
        const unsigned offset=16+6*repeat;
        for(unsigned i=0;i<3;++i)
            pairs.emplace_back(imported.flatIndex.vertexIds[offset+i],
                               imported.flatIndex.vertexIds[offset+i+3]);
    }
    return {DurableCheckpoint(
        root,std::move(pairs),mesh.vertices.size()),
        imported.flatIndex.vertexIds};
}
Fixture closedFixedSeamTetra() {
    Mesh mesh;
    mesh.vertices={
        {-1,0,0},{1,0,0},{0,1.2,0},{0,.2,-1.3}
    };
    mesh.triangles={
        {0,1,2},{1,0,3},{2,1,3},{0,2,3}
    };
    const auto imported=partitionFlatMesh(mesh,256);
    const auto& ids=imported.flatIndex.vertexIds;
    std::vector<MirrorPair> pairs{
        {ids[0],ids[1]},{ids[2],ids[2]},{ids[3],ids[3]}
    };
    return {DurableCheckpoint(
        RegionTopologyIndex::build(imported.revision),std::move(pairs),
        mesh.vertices.size()),ids};
}
SculptCollisionSettings enabled(unsigned maxTrials=5) {
    SculptCollisionSettings cfg;
    cfg.offModeConstraints.enabled=true;
    cfg.offModeConstraints.backtrack.maxTrials=maxTrials;
    return cfg;
}
const std::array<BrushArea,4> bothSides{{
    {{4,0,0},.13,.3},{{4,0,.14},.13,.3},
    {{-4,0,0},.13,.3},{{-4,0,.14},.13,.3}
}};
const std::array<BrushArea,2> seam{{
    {{0,10,0},.13,.3},{{0,10,0},.13,.3}
}};
std::vector<MirrorPair> canonicalPairs(std::vector<MirrorPair> pairs) {
    for(auto& [a,b]:pairs)if(b<a)std::swap(a,b);
    std::sort(pairs.begin(),pairs.end());
    return pairs;
}
bool equalReflection(Vec3 a,Vec3 b) {
    return a.x==-b.x&&a.y==b.y&&a.z==b.z;
}
void verifyAllPairs(const DurableCheckpoint& checkpoint) {
    const auto& root=*checkpoint.index;
    for(const auto& [a,b]:checkpoint.mirrorPairs) {
        require(root.maybeVertex(a)&&root.maybeVertex(b),
                "Mirrored checkpoint cannot reference retired or missing stable ID");
        const auto p=root.vertex(a).position,q=root.vertex(b).position;
        require(equalReflection(p,q)&&((a==b)==(p.x==0)),
                "All source-owned pairs preserve exact X reflection and true X=0 fixed points");
    }
}
void allSourcePairsAndRealReflectedApproach() {
    const auto source=mirroredSource(true);
    auto initial=source.source;
    verifyAllPairs(initial);
    require(initial.mirrorPairs.size()==15,
            "Fixture has reciprocal off-plane orbits and two real X=0 fixed IDs");
    const auto mirrorCheck=auditOffModeMirrors(
        initial.index, [&]{
            std::unordered_map<VertexId,VertexId> map;
            for(const auto& [a,b]:initial.mirrorPairs) {
                map[a]=b;map[b]=a;
            }
            return map;
        }(),{},{},true);
    require(mirrorCheck.complete&&mirrorCheck.fixedPlaneVertices==2&&
            mirrorCheck.facesChecked==10,
            "Whole first source is certified by stable reciprocal ID, correct face orientation and real fixed points");
    std::size_t finalCalls=0;
    const auto original=initial.index;
    SculptStepValidator wholeStage=[&](const ProposedSculptStep& step) {
        ++finalCalls;
        require(step.before==original&&step.candidate!=original,
                "Mirrored multi-sheet validator receives ONE original root and complete final post-remesh candidate");
        const auto& after=*step.candidate;
        const auto& ids=source.vertexIds;
        for(const unsigned k:{0u,3u,6u,9u}) {
            const auto p=after.vertex(ids[k]).position;
            const auto q=original->vertex(ids[k]).position;
            require(p.z!=q.z,
                    "Both inward-moving sheets and BOTH reflected islands move in same immutable Step");
        }
        return std::string{};
    };
    VersionedSculptBrush brush(initial,.12,{},{},{},256,wholeStage,enabled());
    const auto base=brush.snapshot();
    const auto picker=base->pickingRootIdentity();
    Mesh dummy;
    brush.stamp(dummy,bothSides);
    const auto step=brush.lastStepResourceWork();
    require(brush.rejectedStamps()==0&&brush.publishedRevisions()==1&&
            brush.historyStatistics().undoDepth==1&&
            brush.collisionCheckedStamps()==1&&
            finalCalls==1&&step.backtracking.trials>=2&&
            step.backtracking.chosenNormalFraction>0&&
            step.backtracking.chosenNormalFraction<1&&
            step.backtracking.privateCopiedBytes>0&&step.published&&
            step.privateCopiedBytes>step.backtracking.privateCopiedBytes,
            "Both opposing sheets across both mirrored islands backtrack to one accepted private original-root CAS");
    const auto accepted=brush.snapshot();
    require(accepted!=base&&base->pickingRootIdentity()==picker,
            "Earlier immutable original/picker survives all speculative reflected trials");
    verifyAllPairs(brush.checkpoint());
    // Nearby separation derives from SAME accepted post-state for
    // opposing moving faces; no stale opposite-sheet interpolation.
    const auto a=source.vertexIds[0],b=source.vertexIds[3];
    const double gap=accepted->vertex(b).position.z-
                     accepted->vertex(a).position.z;
    require(gap>.1&&gap<.14,
            "Certified both-moving reflected sheets retain required mesh-scale clearance after normal attenuation");
    for(unsigned i=16;i<source.vertexIds.size();++i) {
        const auto v=source.vertexIds[i];
        require(accepted->vertex(v).position.x==
                base->vertex(v).position.x&&
                accepted->vertex(v).position.y==
                base->vertex(v).position.y&&
                accepted->vertex(v).position.z==
                base->vertex(v).position.z,
                "Unrelated independent coincident-XYZ source IDs never move, merge or change mirror owner");
    }
    const auto stored=brush.checkpoint();
    require(brush.undo()&&brush.snapshot()==base&&
            canonicalPairs(brush.checkpoint().mirrorPairs)==
                canonicalPairs(initial.mirrorPairs)&&
            brush.redo()&&brush.snapshot()==accepted&&
            canonicalPairs(brush.checkpoint().mirrorPairs)==
                canonicalPairs(stored.mirrorPairs),
            "Mirrored multi-sheet accepted Step has exactly one pointer-exact history/mirror transition");
}
void explicitRefusalThenFixedPointRecovery() {
    auto fixture=mirroredSource();
    VersionedSculptBrush brush(fixture.source,.12,{},{},{},256,{},enabled(1));
    const auto base=brush.snapshot();
    const auto sourceCheckpoint=brush.checkpoint();
    Mesh dummy;
    brush.stamp(dummy,bothSides);
    require(brush.rejectedStamps()==1&&brush.publishedRevisions()==0&&
            brush.snapshot()==base&&
            brush.historyStatistics().undoDepth==0&&
            brush.lastDirtyPartitions().empty()&&
            brush.checkpoint().mirrorPairs==sourceCheckpoint.mirrorPairs&&
            brush.lastStepResourceWork().backtracking.trials==1&&
            brush.lastStepResourceWork().refused,
            "Symmetric fully staged unsafe both-sheet proposal refuses without CAS, mirror journal or dirty history");
    brush.stamp(dummy,seam);
    const auto next=brush.snapshot();
    require(brush.rejectedStamps()==1&&brush.publishedRevisions()==1&&
            next!=base&&brush.historyStatistics().undoDepth==1,
            "Same worker resumes with valid seam-local mirrored stroke after refusal");
    const auto id=fixture.vertexIds[13];
    require(next->vertex(id).position.x==0&&
            next->vertex(id).position.z>base->vertex(id).position.z,
            "Genuine X=0 fixed stable-ID seam moves tangentially to the mirror plane without leaving it");
    verifyAllPairs(brush.checkpoint());
    require(brush.undo()&&brush.snapshot()==base&&
            brush.checkpoint().mirrorPairs==sourceCheckpoint.mirrorPairs&&
            brush.redo()&&brush.snapshot()==next,
            "Refused mirrored trial does not poison fixed-plane undo/redo or mirrors");
}
void actualMirrored4DWorkerFifoAndCheckpoint() {
    auto fixture=mirroredSource();
    const auto original=fixture.source.index;
    AsyncSculptWorker worker(fixture.source,.02,.12,.2,{},{},{},
                             {},enabled(1));
    worker.begin({4,0,0},.13);
    worker.end({4,0,0},0);
    const auto wait=[&]() {
        const auto deadline=std::chrono::steady_clock::now()+
                            std::chrono::seconds(15);
        while(!worker.canBegin()&&std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        require(worker.canBegin(),"Reflected OFF-mode 4D worker FIFO drained");
    };
    wait();
    const auto denied=worker.status();
    require(denied.rejectedStamps==1&&denied.publishedRevisions==0&&
            denied.history.undoDepth==0&&denied.resources.refused==1&&
            worker.initialRevision()==original&&
            denied.pendingSteps==0&&denied.pendingSegments==0,
            "Mirrored 4D worker rejects unsafe paired input without root/undo/renderer publication");
    const auto noDirty=worker.takeSnapshot();
    if(noDirty)
        require(noDirty->index==original&&
                noDirty->dirtyPartitions.empty(),
                "Mirrored 4D rejected Step never exposes a partial dirty frame");
    worker.begin({0,10,0},.13);
    worker.end({0,10,0},0);
    wait();
    const auto current=worker.status();
    const auto accepted=worker.initialRevision();
    require(accepted!=original&&current.rejectedStamps==1&&
            current.publishedRevisions==1&&current.history.undoDepth==1&&
            current.resources.published==1&&
            current.pendingSteps==0&&current.pendingSegments==0,
            "Next real mirrored 4D seam stroke publishes one complete original-root transaction after refusal");
    const auto seamPoint=fixture.vertexIds[13];
    require(accepted->vertex(seamPoint).position.x==0&&
            accepted->vertex(seamPoint).position.z>
                original->vertex(seamPoint).position.z,
            "Mirrored 4D worker preserves actual X=0 stable fixed-point source");
    const auto published=worker.takeSnapshot();
    require(published&&published->index==accepted&&
            !published->dirtyPartitions.empty(),
            "One valid 4D mirrored Step produces complete renderer dirty owners");
    worker.undo();
    const auto deadline=std::chrono::steady_clock::now()+
                        std::chrono::seconds(10);
    while(std::chrono::steady_clock::now()<deadline) {
        if(worker.status().history.undoDepth==0&&
           worker.initialRevision()==original)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    require(worker.initialRevision()==original&&
            worker.status().history.undoDepth==0,
            "Asynchronous reflected worker undo restores exact accepted source ancestor");
}
void actualFixedPointMirrorJournalSplitAndRollback() {
    const auto fixture=closedFixedSeamTetra();
    const auto initial=fixture.source;
    RefineSettings refine;
    refine.enabled=true;
    refine.maximumEdgeRatio=.1;
    refine.maximumSplitPairsPerStamp=1;
    refine.optimizeFlips=false;
    std::size_t candidates=0;
    bool examinedNewFixed=false;
    SculptStepValidator declineFirst=[&](const ProposedSculptStep& proposal) {
        ++candidates;
        require(proposal.before==initial.index&&proposal.candidate!=initial.index,
                "Refined reflected seam validator sees the complete private source root");
        for(const auto id:proposal.changedVertices) {
            if(proposal.before->maybeVertex(id)||
               !proposal.candidate->maybeVertex(id))continue;
            if(proposal.candidate->vertex(id).position.x==0)
                examinedNewFixed=true;
        }
        return candidates==1?
            std::string("M05 deliberate refusal after staged fixed-point mirror split"):
            std::string{};
    };
    VersionedSculptBrush brush(initial,.12,{},{},refine,256,declineFirst,enabled());
    const auto baseline=brush.snapshot();
    const auto mirrors=canonicalPairs(brush.checkpoint().mirrorPairs);
    Mesh dummy;
    const std::array<BrushArea,1> fixedEdge{{
        {{0,1.2,0},.13,.3}
    }};
    brush.stamp(dummy,fixedEdge);
    if(!(brush.rejectedStamps()==1&&brush.snapshot()==baseline&&
         brush.historyStatistics().undoDepth==0&&
         brush.checkpoint().mirrorPairs.size()==mirrors.size()&&
         canonicalPairs(brush.checkpoint().mirrorPairs)==mirrors&&
         examinedNewFixed))
        throw std::runtime_error(
            "Speculative X=0 mirror split/rollback failure: rejected="+
            std::to_string(brush.rejectedStamps())+
            " callbacks="+std::to_string(candidates)+
            " newFixed="+std::to_string(examinedNewFixed)+
            " remainingMirrors="+std::to_string(brush.checkpoint().mirrorPairs.size())+
            " originalMirrors="+std::to_string(mirrors.size())+
            " reason="+brush.lastRejection());
    brush.stamp(dummy,fixedEdge);
    const auto accepted=brush.snapshot();
    const auto after=canonicalPairs(brush.checkpoint().mirrorPairs);
    require(candidates==2&&brush.rejectedStamps()==1&&
            accepted!=baseline&&after.size()>mirrors.size()&&
            brush.historyStatistics().undoDepth==1&&
            brush.publishedRevisions()==1,
            "Later accepted reflected seam split publishes new self-mirrored stable ID in precisely one history transition");
    verifyAllPairs(brush.checkpoint());
    require(brush.undo()&&brush.snapshot()==baseline&&
            canonicalPairs(brush.checkpoint().mirrorPairs)==mirrors&&
            brush.redo()&&brush.snapshot()==accepted&&
            canonicalPairs(brush.checkpoint().mirrorPairs)==after,
            "Mirror insertion and fixed-point X=0 split roll back through undo and replay through redo");
}
void badMirrorOwnershipIsNotGuessedFromXYZ() {
    auto f=mirroredSource(true);
    auto invalid=f.source;
    // Legitimately equal coordinates are not permission to map one
    // stable VertexId to some different coincident historical source.
    auto& pairs=invalid.mirrorPairs;
    const auto offset=9u; // two positive copies in remote repeat groups
    std::swap(pairs[offset].second,pairs[offset+3].second);
    bool refused=false;
    try {
        VersionedSculptBrush brush(invalid,.12,{},{},{},256,{},enabled());
    }catch(const std::invalid_argument&) {refused=true;}
    require(refused,
            "OFF-mode initial mirror audit refuses non-source-owned mapping even for matching XYZ");
}
void staleExpectedRootRefusalNoPartialHistory() {
    auto source=mirroredSource();
    std::shared_ptr<const RegionTopologyIndex> nominated=source.source.index;
    std::size_t checks=0;
    SculptStepValidator stale=[&](const ProposedSculptStep& proposal) {
        ++checks;
        return proposal.before!=nominated?
            std::string("M05 stale selected root refuses candidate"):
            std::string{};
    };
    VersionedSculptBrush brush(source.source,.012,{},{},{},256,stale,enabled());
    Mesh dummy;
    brush.stamp(dummy,seam);
    const auto accepted=brush.snapshot();
    require(brush.publishedRevisions()==1&&checks==1,
            "Initial selected original-root reference permits one current symmetric commit");
    const auto prior=brush.checkpoint();
    brush.stamp(dummy,seam);
    require(brush.rejectedStamps()==1&&checks==2&&
            brush.snapshot()==accepted&&
            brush.checkpoint().mirrorPairs==prior.mirrorPairs&&
            brush.historyStatistics().undoDepth==1&&
            brush.lastDirtyPartitions().empty()&&
            brush.lastRejection().find("stale selected root")!=std::string::npos,
            "Stale external original-root selection vetoes a complete private candidate before any root/mirror/history side effect");
}
}
int main() {
    try {
        allSourcePairsAndRealReflectedApproach();
        explicitRefusalThenFixedPointRecovery();
        actualFixedPointMirrorJournalSplitAndRollback();
        badMirrorOwnershipIsNotGuessedFromXYZ();
        actualMirrored4DWorkerFifoAndCheckpoint();
        staleExpectedRootRefusalNoPartialHistory();
        std::cout<<"V18-M05 reflected source and opposing-sheet transaction: PASS\n";
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"V18-M05: "<<e.what()<<'\n';
        return 1;
    }
}

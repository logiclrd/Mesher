#include "mesher/repair_auto_publication.hpp"
#include "mesher/repair_auto_private.hpp"
#include "mesher/repair_case.hpp"
#include "mesher/repair_excision.hpp"
#include "mesher/repair_auto_plan.hpp"
#include "mesher/revision_history.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/sculpt_fixtures.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <vector>

using namespace mesher;
namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
std::unordered_map<VertexId,VertexId> fixtureMirrors(
    const RegionTopologyIndex& original) {
    std::map<std::tuple<double,double,double>,VertexId> byXYZ;
    original.revision().forEachPartition(
        [&](PartitionId,const MeshPartition& p) {
            for(const auto& v:p.vertices)
                require(byXYZ.emplace(
                    std::tuple{v.position.x,v.position.y,v.position.z},
                    v.id).second,"mirror test fixture has duplicate XYZ");
        });
    std::unordered_map<VertexId,VertexId> result;
    for(const auto& [pos,id]:byXYZ) {
        const auto& [x,y,z]=pos;
        const auto it=byXYZ.find(std::tuple{-x,y,z});
        require(it!=byXYZ.end(),"mirror test fixture is not actually reflected");
        result.emplace(id,it->second);
    }
    return result;
}
void acceptedPhysicalArchive() {
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/
        "repair-cases"/"refusal-2.r06case";
    const auto saved=readRepairCase(path);
    require(saved.symmetricSource&&saved.candidate.index&&!saved.contacts.empty(),
            "R07 must use a symmetric exact candidate archive");
    const auto old=saved.candidate.index;
    const auto mirrors=fixtureMirrors(*old);
    const auto originalFaceTombstones=old->retiredFaceCount();
    const auto preview=previewApproximateExcision(*old,saved.contacts);
    require(preview.complete,"R07 exact archive preview unavailable");
    const auto cut=applyPrivateExcision(old,preview);
    const auto plan=planPrivateAutomaticClosure(
        *old,cut,preview,saved.contacts);
    require(plan.ready(),"R07 saved AUTO plan unavailable");
    CertifiedPrivateAutoLimits policy;
    policy.closure.requireReflectedGeometry=true;
    policy.closure.staging.useUnifiedZipper=true;
    const auto fixed=applyCertifiedPrivateAutomaticClosure(
        old,cut,preview,plan,saved.contacts,policy);
    require(fixed.index&&fixed.minimumZipperQuality>=.10,
            "R07 publish transition must start from certified private closure");
    const auto delta=inspectCertifiedAutoPublication(
        *old,*fixed.index,&mirrors);
    require(!delta.addedVertices.empty()&&
            !delta.retiredFaces.empty()&&
            !delta.addedFaces.empty()&&
            !delta.changedFaces.empty()&&
            !delta.addedMirrors.empty()&&
            delta.liveFaces>0&&delta.liveVertices>0,
            "R07 exact source-to-certified stable-ID and mirror journal is empty");
    require(delta.addedVertices.size()>0&&
            delta.retiredVertices.size()>0,
            "R07 should record both added and retired source vertices");
    require(old->retiredFaceCount()==originalFaceTombstones,
            "R07 inspecting a candidate cannot mutate its original history root");

    // Exercise the already existing history journal with precisely one
    // SUCCESSFUL certified root, including its mirrored deltas.
    RevisionHistory history(old,16,32*1024*1024,nullptr,
                            mirrors.size(),0);
    require(!history.canUndo(),"R07 history must start at the old root");
    history.append(fixed.index,{},delta.addedMirrors,
                   delta.removedMirrors,0,delta.liveVertices,delta.liveFaces);
    require(history.canUndo()&&history.current()==fixed.index,
            "R07 certified repair must occupy one atomic undoable revision");
    const auto backward=history.undo();
    require(backward.index==old&&!backward.removeMirrors.empty(),
            "R07 undo must restore the old topology and mirror map");
    const auto forward=history.redo();
    require(forward.index==fixed.index&&!forward.addMirrors.empty(),
            "R07 redo must restore the exact certified topology and mirror map");
    std::cout<<"[r07-auto-publication] exact saved refusal-2 certified, "
        <<"one history revision, undo/redo, reflection journal PASS\n";
}
void cleanLiveSculptDoesNotFreeze() {
    // R07 must retain the normal worker path even when its R01 contact
    // discovery is active. Clean ordinary stamps still publish, can be
    // undone/redone, and do not require a frozen R01 viewport.
    SphereOptions options;
    options.vertexCount=600;
    options.relaxationIterations=8;
    options.symmetric=true;
    const auto initial=generateSphere(options);
    SculptCollisionSettings settings;
    settings.certifiedAutoClosure=true;
    settings.certifiedAutoUnifiedZipper=true;
    VersionedSculptBrush brush(initial,.004,CurvatureSettings{},
                               CollapseSettings{},RefineSettings{},
                               true,256,{},settings);
    const auto before=brush.snapshot();
    Mesh ignored;
    const std::array<BrushArea,2> areas{{
        {{-.26,-.67,.66},.28,.3},
        {{ .26,-.67,.66},.28,.3}
    }};
    brush.stamp(ignored,areas);
    require(brush.completedStamps()==1&&brush.rejectedStamps()==0&&
            brush.publishedRevisions()==1&&
            brush.snapshot()!=before&&brush.lastRejection().empty(),
        "R07 collision-clean stamp must publish without freezing or refusing");
    const auto after=brush.snapshot();
    require(brush.undo()&&brush.snapshot()==before,
        "R07 live undo must restore the exact previous published root");
    require(brush.redo()&&brush.snapshot()==after,
        "R07 live redo must restore the exact published root");
    brush.stamp(ignored,areas);
    // A later stamp may have a legitimate private geometric refusal. The
    // required invariant is that it RETURNS, cannot freeze the brush or
    // change the already published previous revision on refusal.
    require(brush.completedStamps()==2&&!brush.terminalFailure()&&
            brush.publishedRevisions()>=1,
        "R07 must process subsequent input without any paused debugger");
    std::cout<<"[r07-live-worker] second_stamp_rejected="
             <<brush.rejectedStamps()
             <<" first_root_published=1"
             <<" clean stamp/undo/redo/continued input PASS\\n";
}
void desktopScaleLiveAutoFirstStamp() {
    // R07 regression: exercise the actual default interactive profile,
    // not only the previous 600-vertex, deliberately tiny stamp fixture.
    SphereOptions options;
    options.vertexCount=5000;
    options.symmetric=true;
    const auto initial=generateSphere(options);
    SculptCollisionSettings settings;
    settings.certifiedAutoClosure=true;
    settings.certifiedAutoUnifiedZipper=true;
    CurvatureSettings curvature;
    CollapseSettings collapse;
    RefineSettings refine;
    curvature.enabled=true;
    collapse.enabled=true;
    refine.enabled=true;
    VersionedSculptBrush brush(initial,.012,curvature,collapse,refine,
                               true,256,{},settings);
    const auto before=brush.snapshot();
    const std::array<BrushArea,2> areas{{
        {{-.26,-.67,.66},.4,.3},
        {{ .26,-.67,.66},.4,.3}
    }};
    Mesh ignored;
    brush.stamp(ignored,areas);
    std::cout<<"[r07-live-desktop] completed="<<brush.completedStamps()
             <<" rejected="<<brush.rejectedStamps()
             <<" published="<<brush.publishedRevisions()
             <<" checked="<<brush.collisionCheckedStamps()
             <<" repaired="<<brush.collisionRepairedStamps()
             <<" reason="<<brush.lastRejection()<<"\n";
    require(brush.completedStamps()==1&&
            brush.rejectedStamps()==0&&
            brush.publishedRevisions()==1&&
            brush.snapshot()!=before,
        "ordinary default-size collision-free AUTO stamp must publish");
}
void invalidSourceMirrorsCannotPublish() {
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/
        "repair-cases"/"refusal-1.r06case";
    const auto saved=readRepairCase(path);
    const auto original=saved.candidate.index;
    auto mirrors=fixtureMirrors(*original);
    // No repair can authorize an incomplete original mirror table.
    mirrors.erase(mirrors.begin());
    bool rejected=false;
    try {
        (void)inspectCertifiedAutoPublication(
            *original,*original,&mirrors);
    }catch(const std::logic_error&) {rejected=true;}
    require(rejected&&original==saved.candidate.index,
        "R07 must reject source mirror mismatch before history publication");
}
}
int main() {
    try {acceptedPhysicalArchive();invalidSourceMirrorsCannotPublish();
         cleanLiveSculptDoesNotFreeze();
         desktopScaleLiveAutoFirstStamp();}
    catch(const std::exception& e) {
        std::cerr<<"[r07-auto-publication] FAILED "<<e.what()<<"\n";
        return 1;
    }
}

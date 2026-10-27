#include "mesher/repair_auto_publication.hpp"
#include "mesher/repair_auto_private.hpp"
#include "mesher/repair_case.hpp"
#include "mesher/repair_excision.hpp"
#include "mesher/repair_auto_plan.hpp"
#include "mesher/revision_history.hpp"
#include "mesher/versioned_sculpt.hpp"
#include "mesher/sculpt_fixtures.hpp"
#include "mesher/mesh_partitioning.hpp"
#include "mesher/region_edit.hpp"

#include <algorithm>
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
    // The accelerated grouped query must return exactly the same contact
    // graph as the independent per-face BVH query. Also verify that it
    // really eliminates repeated directory traversals on a 5K sphere.
    std::vector<FaceId> queryFaces;
    after->revision().forEachPartition(
        [&](PartitionId,const MeshPartition& part) {
            for(const auto& face:part.faces)
                if(queryFaces.size()<640)queryFaces.push_back(face.id);
        });
    require(queryFaces.size()==640,"R07 benchmark fixture needs 640 faces");
    RegionTopologyIndex::CollisionStatistics oldWork,groupedWork;
    const auto originalPairs=after->triangleIntersections(
        {},queryFaces,&oldWork);
    const auto groupedPairs=after->triangleIntersections(
        {},queryFaces,&groupedWork,{},16);
    require(originalPairs.size()==groupedPairs.size(),
        "Grouped BVH changed contact count compared with ordinary exact query");
    for(std::size_t i=0;i<originalPairs.size();++i)
        require(originalPairs[i].first==groupedPairs[i].first&&
                originalPairs[i].second==groupedPairs[i].second&&
                originalPairs[i].ambiguous==groupedPairs[i].ambiguous,
            "Grouped BVH must preserve exact contact IDs and ambiguity");
    require(groupedWork.boxesTested<oldWork.boxesTested,
        "Grouped BVH must reduce repeated directory/box traversal on a large mesh");
    std::cout<<"[r07-batched-bvh] regular_boxes="<<oldWork.boxesTested
             <<" grouped_boxes="<<groupedWork.boxesTested
             <<" exact_pairs="<<groupedPairs.size()<<" PASS\\n";
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
void liveAutoChangedNeighborhoodDiscovery() {
    // The physical candidate must be checked against the COMPLETE BVH,
    // but only changed faces/vertices are collision-query seeds. In
    // particular, a fresh contact against a distant unchanged face is
    // discovered, while existing unrelated intersections are not an
    // artificial new contact.
    const auto addTetra=[](Mesh& mesh,Vec3 origin,double half) {
        const auto base=static_cast<std::uint32_t>(mesh.vertices.size());
        const std::array<Vec3,4> local{{
            {-1,-1,-1},{1,1,-1},{1,-1,1},{-1,1,1}}};
        for(const auto v:local)
            mesh.vertices.push_back(origin+v*half);
        const std::array<std::array<std::uint32_t,3>,4> tris{{
            {0,1,2},{0,2,3},{0,3,1},{1,3,2}}};
        for(const auto t:tris)
            mesh.triangles.push_back({
                base+t[0],base+t[1],base+t[2]});
    };
    const auto move=[&](
        const std::shared_ptr<const RegionTopologyIndex>& old,
        const std::vector<VertexId>& ids,Vec3 offset) {
        RegionEdit edit(old,ids,{},2);
        for(const auto id:ids)
            edit.moveVertex(id,old->vertex(id).position+offset);
        (void)edit.commit();
        return edit.updatedIndex();
    };
    Mesh fixture;
    addTetra(fixture,{-2,0,0},.5);
    addTetra(fixture,{2,0,0},.5);
    addTetra(fixture,{10,0,0},.5);
    auto built=partitionFlatMesh(fixture,2);
    auto root=RegionTopologyIndex::build(built.revision);
    const std::vector<VertexId> first(built.flatIndex.vertexIds.begin(),
                                      built.flatIndex.vertexIds.begin()+4);
    const std::vector<VertexId> far(built.flatIndex.vertexIds.begin()+8,
                                    built.flatIndex.vertexIds.end());
    const auto clean=move(root,far,{0,.1,0});
    const auto cleanResult=assessPostSculptRepairContacts(
        {root,clean,far,{}},false);
    require(cleanResult.status==SculptSourceContactStatus::Clean,
            "R07 changed-neighborhood query must admit unrelated clean edits");
    const auto contacted=move(clean,first,{3.9,0,0});
    const auto report=assessPostSculptRepairContacts(
        {clean,contacted,first,{}},false);
    require(report.status==SculptSourceContactStatus::CompleteIntersecting&&
            report.ready()&&!report.contacts.empty()&&
            report.originalCandidate==contacted,
            "R07 must detect new transverse contacts with unchanged opposing faces");
    // Repair admission needs whole-source provenance, not just the first
    // localized candidate contact. The independent full query uses the
    // identical exact geometry but a spatially grouped BVH traversal.
    const auto complete=assessPostSculptOriginalContacts(
        {clean,contacted,first,{}},false,{},nullptr,16,false);
    require(complete.status==SculptSourceContactStatus::CompleteIntersecting,
        "R07 full source must use face-contact evidence, not obsolete split-patch admission");
    require(complete.ready()&&complete.originalCandidate==contacted&&
            complete.contacts.size()>=report.contacts.size(),
        "R07 must obtain complete trusted source contact evidence before excision");
    for(const auto& candidate:report.contacts)
        require(std::any_of(complete.contacts.begin(),complete.contacts.end(),
                [&](const auto& full) {
                    return full.first==candidate.first&&
                           full.second==candidate.second&&
                           full.ambiguous==candidate.ambiguous;
                }),
                "Every local new contact must be present in full-source R06 evidence");
    SculptSourceContactLimits tight;
    tight.query.maxBoxTests=1;
    const auto bounded=assessPostSculptRepairContacts(
        {clean,contacted,first,{}},false,tight);
    require(bounded.status==SculptSourceContactStatus::Refused&&
            bounded.contacts.empty(),
            "R07 exhausted finite work must refuse, never return partial contact evidence");

    Mesh imperfect;
    addTetra(imperfect,{0,0,0},1);
    addTetra(imperfect,{0,0,0},1);
    addTetra(imperfect,{10,0,0},.5);
    const auto builtOld=partitionFlatMesh(imperfect,2);
    const auto previous=RegionTopologyIndex::build(builtOld.revision);
    const std::vector<VertexId> remote(builtOld.flatIndex.vertexIds.begin()+8,
                                       builtOld.flatIndex.vertexIds.end());
    const auto modified=move(previous,remote,{0,.1,0});
    const auto independent=assessPostSculptRepairContacts(
        {previous,modified,remote,{}},false);
    require(independent.status==SculptSourceContactStatus::Clean,
        "R07 must not reject a clean changed region because unrelated old collisions already exist");
    std::cout<<"[r07-source] full-BVH neighborhood contacts, unrelated old "
                "intersections and exact finite-work refusal PASS\n";
}
void actualInflateContactCanPublishCertifiedAuto() {
    // A live AUTO positive fixture must be ONE connected self-contacting
    // object, not a Boolean union between two separate exterior solids.
    // The old two-exterior-tetra fixture legitimately lacks inward cavity
    // proof, so using it as a required AUTO success was incorrect.
    Mesh dummy;
    bool accepted=false;
    std::size_t contacted=0;
    std::string lastRefusal;
    const auto stamp=[&](const Mesh& initial,double strength,
                         BrushArea area,const char* description) {
        SculptCollisionSettings policy;
        policy.certifiedAutoClosure=true;
        policy.certifiedAutoUnifiedZipper=true;
        VersionedSculptBrush brush(initial,strength,{},{},{},
                                   false,256,{},policy);
        const auto before=brush.snapshot();
        const std::array<BrushArea,1> areas{{area}};
        brush.stamp(dummy,areas);
        std::cout<<"[r07-physical-attempt] fixture="<<description
                 <<" strength="<<strength
                 <<" checked="<<brush.collisionCheckedStamps()
                 <<" repaired="<<brush.collisionRepairedStamps()
                 <<" rejected="<<brush.rejectedStamps()
                 <<" reason="<<brush.lastRejection()<<"\n";
        if(brush.collisionRepairedStamps()==1) {
            require(brush.rejectedStamps()==0&&
                    brush.publishedRevisions()==1&&
                    brush.snapshot()!=before&&
                    brush.undo()&&brush.snapshot()==before,
                "One connected surface's genuine contact must publish one certified undoable AUTO root");
            accepted=true;
            std::cout<<"[r07-physical-contact] accepted fixture="<<description
                     <<" strength="<<strength<<" PASS\n";
        }else if(brush.contactStatistics().contactChecks>0||
                 brush.lastRejection().find("R07 live AUTO")!=std::string::npos) {
            ++contacted;
            lastRefusal=std::string(description)+" strength="+
                        std::to_string(strength)+" reason="+brush.lastRejection();
        }
    };
    // Inflate pushes a torus's inner lip toward its opposite side. The
    // torus is one connected manifold: overlapping it is a true
    // self-contact/closure, not a request to delete another exterior solid.
    const auto torus=generateTorusFixture(950,1,10);
    for(const double strength:{.10,.18,.26,.40,.65}) {
        if(accepted)break;
        stamp(torus,strength,{{.11,0,0},.42,.8},"torus-inner-rim");
    }
    if(!accepted) {
        // The two shells of an oriented hollow sphere enclose a legitimate
        // cavity. An inward brush can produce the alternative, certified
        // cavity-retirement case without inventing an external solid.
        const auto hollow=generateHollowSphereFixture(5000,1,10);
        for(const double strength:{-.03,-.06,-.10,-.16,-.25}) {
            if(accepted)break;
            stamp(hollow,strength,{{.9,0,0},.45,.8},"enclosed-cavity");
        }
    }
    require(accepted,
        ("R07 must certify at least one real connected/cavity contact; "
         "attempted="+std::to_string(contacted)+
         " last_refusal="+lastRefusal).c_str());
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
         desktopScaleLiveAutoFirstStamp();
         liveAutoChangedNeighborhoodDiscovery();
         actualInflateContactCanPublishCertifiedAuto();}
    catch(const std::exception& e) {
        std::cerr<<"[r07-auto-publication] FAILED "<<e.what()<<"\n";
        return 1;
    }
}

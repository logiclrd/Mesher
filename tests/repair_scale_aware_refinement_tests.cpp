#include "mesher/repair_mixed_closure.hpp"
#include "mesher/repair_scale_aware_policy.hpp"
#include "mesher/repair_case.hpp"
#include "mesher/repair_cap_spatial.hpp"
#include "mesher/repair_surface_profile.hpp"
#include "mesher/repair_survivor_curvature.hpp"
#include "mesher/repair_auto_plan.hpp"
#include "mesher/repair_preview.hpp"
#include "mesher/repair_excision.hpp"
#include "mesher/repair_inspection.hpp"
#include <filesystem>
#include "mesher/mesh_partitioning.hpp"

#include <algorithm>
#include <iomanip>
#include <set>
#include <tuple>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mesher;
namespace {
void require(bool yes,const char* message) {
    if(!yes)throw std::runtime_error(message);
}
struct Case {
    PrivateExcisionResult cut;
    RepairExcisionPreview proposal;
    AutoClosurePlan plan;
    std::shared_ptr<const RegionTopologyIndex> source;
};
Case openTetra() {
    Mesh mesh;
    mesh.vertices={{0,0,0},{2,0,0},{0,2,0},{0,0,2}};
    mesh.triangles={{0,1,3},{0,3,2},{1,2,3}};
    const auto flat=partitionFlatMesh(mesh,64);
    Case out;
    out.source=RegionTopologyIndex::build(flat.revision);
    out.cut.index=out.source;
    out.cut.boundaryLoops=1;
    out.cut.boundaryEdges=3;
    out.proposal.complete=true;
    out.proposal.boundaryEdges=3;
    RepairContour rim;
    rim.vertexIds={flat.flatIndex.vertexIds[0],
                   flat.flatIndex.vertexIds[1],
                   flat.flatIndex.vertexIds[2]};
    for(std::size_t i=0;i<rim.vertexIds.size();++i) {
        auto a=rim.vertexIds[i],b=rim.vertexIds[(i+1)%rim.vertexIds.size()];
        if(a>b)std::swap(a,b);
        const auto sides=out.source->maybeEdgeSides({a,b});
        require(sides&&sides->size()==1,
                "synthetic tetra must present one existing side of each cap rim");
        rim.survivingFaces.push_back(sides->front().face);
    }
    out.proposal.contours.push_back(rim);
    out.plan.status=AutoClosurePlanStatus::Ready;
    out.plan.cappedContours={0};
    AutoClosureRegion region;
    region.contours={0};
    region.caps={0};
    out.plan.regions={region};
    out.plan.geometryRegions=1;
    return out;
}
void ordinaryUnchangedAndEnabledDensity() {
    const auto fixture=openTetra();
    MixedPrivateClosureLimits original;
    // Force one oversized but healthy cap edge; the source-side quality
    // check still has to approve both children.
    original.maxCapEdgeFactor=1.02;
    original.spatialCapDensityEdgeFactor=1.01;
    const auto baseline=applyPrivateMixedClosure(
        fixture.cut,fixture.proposal,fixture.plan,original);
    require(baseline.index&&
        baseline.scaleAwareSplitOperations==0,
        "legacy disabled path stays available with no M17C actions");
    auto enabled=original;
    enabled.scaleAwareRefinement=true;
    const auto revised=applyPrivateMixedClosure(
        fixture.cut,fixture.proposal,fixture.plan,enabled);
    require(revised.index&&revised.refinementSteps>=1&&
        revised.scaleAwareSplitOperations>=1&&
        revised.scaleAwareSplitOperations<=enabled.maxScaleAwareSplitOperations&&
        revised.splitEvents.size()>=revised.scaleAwareSplitOperations&&
        revised.minimumCapQuality>=enabled.minCapQuality,
        "enabled density path must produce certified finite stage-owned splits");
    require(fixture.source->retiredFaceCount()==0&&
        fixture.cut.index->retiredFaceCount()==0,
        "neither mode publishes its result to original source");
    std::cout<<"[r06-m17c] enabled finite healthy density and unchanged disabled path PASS\n";
}
void underQualityHandsOffInsteadOfSplitting() {
    require(shouldRepairShapeBeforeDensity(.01,.1,1.5,1.,2.)&&
            !shouldRepairShapeBeforeDensity(.01,.1,3.,1.,2.)&&
            !shouldRepairShapeBeforeDensity(.2,.1,1.5,1.,2.),
            "under-quality within local scale requires shape repair while coarse size allows bounded density");
    const auto f=openTetra();
    MixedPrivateClosureLimits bounds;
    bounds.scaleAwareRefinement=true;
    bounds.minCapQuality=.95; // original planar right cap q about .866
    std::size_t splits=0;
    std::vector<std::string> declinedAttempts;
    std::vector<MixedClosureTerminalEvent> terminal;
    bounds.staging.operationObserver=[&](const auto&,const std::string& op,
                                         const std::string&) {
        if(op=="splitEdge")++splits;
        if(op=="TryDiagonalFlip"||op=="TryCapFanCollapse"||
           op=="TryInteriorFairing"||op=="TryQualityRestoringSplit")
            declinedAttempts.push_back(op);
    };
    bounds.terminalObserver=[&](const auto& event) {
        terminal.push_back(event);
    };
    bool shapeRefused=false,tipCertified=false;
    std::string reason;
    try {
        const auto result=applyPrivateMixedClosure(
            f.cut,f.proposal,f.plan,bounds);
        // A geometrically fixed, planar acute R03 rim tip can legitimately
        // receive the EXISTING independent geometric quality certificate.
        // This is not an under-quality free interior face and MUST NOT be
        // forced through extra subdivision just to fit an impossible q.
        tipCertified=result.index&&
            !result.boundaryTipQualityExemptFaces.empty();
    }catch(const std::logic_error& e) {
        reason=e.what();
        shapeRefused=reason.find("R06-M17C under-quality at shape scale")!=
            std::string::npos;
    }
    require(splits==0&&(shapeRefused||tipCertified),
        "shape-scale tip may refuse or retain genuine fixed-rim certificate, never split to chase q");
    if(shapeRefused) {
        require(terminal.size()==1&&
                terminal.front().outcome==MixedClosureTerminalOutcome::Refused,
                "true M17C shape refusal must preserve typed terminal provenance");
        require(declinedAttempts.size()>=4&&
                declinedAttempts[0]=="TryDiagonalFlip"&&
                declinedAttempts[1]=="TryCapFanCollapse"&&
                declinedAttempts[2]=="TryInteriorFairing"&&
                declinedAttempts[3]=="TryQualityRestoringSplit",
                "the step debugger must show every failed shape operation and strictly-restorative split preflight");
    }
    std::cout<<"[r06-m17c] shape priority; immutable fixed-rim tip certificate or refusal without splits PASS\n";
}
void refusalOneRealSavedCaseAttemptTrace() {
    // This is the ORIGINAL 531KB captured post-stamp physical refusal,
    // not a toy tetra and not recreated brush motion. M17C is opt-in.
    const auto path=std::filesystem::path(__FILE__).parent_path().parent_path()/
        "repair-cases"/"refusal-1.r06case";
    const auto saved=readRepairCase(path);
    const auto initialRetiredFaces=saved.candidate.index?
        saved.candidate.index->retiredFaceCount():0;
    require(saved.candidate.index&&!saved.contacts.empty(),
            "real refusal-1 saved candidate and R01 contacts must load");
    const auto excision=previewApproximateExcision(
        *saved.candidate.index,saved.contacts);
    require(excision.complete,"real refusal-1 must produce full R02 cut");
    const auto cut=applyPrivateExcision(saved.candidate.index,excision);
    const auto plan=planPrivateAutomaticClosure(
        *saved.candidate.index,cut,excision,saved.contacts);
    require(plan.ready(),"real refusal-1 must reach R06 closure staging");
    // M17D evidence BEFORE permanent provisional-ID resolution or any
    // refinement: distinguish initial geometric construction from later
    // operations. Keep the original captured R03 source/contour unchanged.
    const auto staged=stagePrivateMixedClosure(cut,excision,plan,
        [&] {
            MixedClosureStageSettings bounds;
            bounds.requireReflectedGeometry=saved.symmetricSource;
            return bounds;
        }());
    struct InitialFace {
        double quality{};
        std::array<VertexId,3> id{};
        std::size_t temporaryCorners{};
    };
    std::vector<InitialFace> firstFaces;
    for(const auto& face:staged.faces()) {
        if(face.kind!=MixedClosureFaceKind::Cap)continue;
        const auto ids=face.triangle();
        const auto a=staged.positions().at(ids[0]);
        const auto b=staged.positions().at(ids[1]);
        const auto d=staged.positions().at(ids[2]);
        const double sum=dot(a-b,a-b)+dot(b-d,b-d)+dot(d-a,d-a);
        const double q=sum>0?
            2*std::sqrt(3.)*length(cross(b-a,d-a))/sum:0.;
        std::size_t provisional=0;
        for(const auto id:ids)
            if(std::find(staged.provisionalSpatialVertices().begin(),
                         staged.provisionalSpatialVertices().end(),id)!=
               staged.provisionalSpatialVertices().end())++provisional;
        firstFaces.push_back({q,ids,provisional});
    }
    std::sort(firstFaces.begin(),firstFaces.end(),
        [](const auto& a,const auto& b){return a.quality<b.quality;});
    require(!firstFaces.empty(),"refusal-1 must have initial cap triangles");
    std::cout<<std::setprecision(17)
             <<"[r06-m17d-baseline] initial_cap_faces="<<firstFaces.size()
             <<" provisional_vertices="<<staged.provisionalSpatialVertices().size()
             <<" min_q="<<firstFaces.front().quality<<"\n";
    for(std::size_t k=0;k<std::min<std::size_t>(8,firstFaces.size());++k) {
        const auto& face=firstFaces[k];
        std::cout<<"[r06-m17d-baseline] rank="<<k
                 <<" q="<<face.quality
                 <<" provisional_corners="<<face.temporaryCorners;
        for(const auto id:face.id) {
            const auto p=staged.positions().at(id);
            std::cout<<" vertex="<<id<<"@"
                     <<p.x<<","<<p.y<<","<<p.z;
        }
        std::cout<<"\n";
    }
    for(const auto loopId:plan.cappedContours) {
        const auto& rim=excision.contours.at(loopId);
        std::vector<Vec3> ordered,normalGuide;
        for(std::size_t k=0;k<rim.vertexIds.size();++k) {
            const auto index=rim.vertexIds.size()-k-1;
            ordered.push_back(cut.index->vertex(rim.vertexIds[index]).position);
            const auto src=cut.index->face(rim.survivingFaces[index]).vertices;
            const auto a=cut.index->vertex(src[0]).position;
            const auto b=cut.index->vertex(src[1]).position;
            const auto d=cut.index->vertex(src[2]).position;
            normalGuide.push_back(cross(b-a,d-a));
        }
        for(const auto alternative:{false,true})
        for(const auto corrected:{false,true}) {
            SpatialCapSettings bounds;
            bounds.requireMirrorPlane=saved.symmetricSource&&
                std::any_of(ordered.begin(),ordered.end(),
                    [](Vec3 p){return p.x<0;})&&
                std::any_of(ordered.begin(),ordered.end(),
                    [](Vec3 p){return p.x>0;});
            bounds.alternateBandDiagonals=alternative;
            bounds.repairTangentialOvershoot=corrected;
            try {
                const auto local=buildSpatialCapFrontPoisson(
                    ordered,normalGuide,bounds);
                // M18A exact immutable refusal-1 regression: diagnose
                // normal discontinuity independently from q and verify
                // outgoing CAP rim edges use the original surviving FaceId.
                const auto profile=measureSpatialCapRimProfile(local,normalGuide);
                require(profile.edges.size()==ordered.size()&&
                        profile.measuredEdges==ordered.size()&&
                        profile.unavailableEdges==0,
                        "M18A original refusal-1 must have complete sourced cap/rim diagnostics");
                std::size_t affected=0;
                for(const auto& edge:profile.edges) {
                    const auto k=edge.rimStart;
                    const auto n=ordered.size();
                    const auto sourceOrdinal=(2*n-2-k)%n;
                    const auto survivorId=rim.survivingFaces[sourceOrdinal];
                    const auto a=rim.vertexIds[n-1-k];
                    const auto b=rim.vertexIds[n-1-((k+1)%n)];
                    const auto& original=cut.index->face(survivorId).vertices;
                    require(std::find(original.begin(),original.end(),a)!=original.end()&&
                            std::find(original.begin(),original.end(),b)!=original.end(),
                            "M18A CAP rim edge must match its EXACT original indexed surviving FaceId");
                    const auto neighborhood=
                        collectSurvivorCurvatureNeighborhood(*cut.index,survivorId);
                    require(!neighborhood.missingSeed&&
                            !neighborhood.faces.empty()&&
                            neighborhood.faces.front().face==survivorId,
                            "M18B must anchor to exact R03 surviving FaceId");
                    const auto curvature=estimateSurvivorDirectionalCurvature(
                        cut.index->vertex(a).position,
                        cut.index->vertex(b).position,neighborhood);
                    require(curvature.contributingFaces<=
                            SurvivorCurvatureLimits{}.maxFaces&&
                            curvature.edgeLength>0&&
                            std::isfinite(curvature.maximumNormalAngle),
                            "M18B refusal-1 neighborhood must be bounded and finite");
                    if((a==3207&&b==5470)||(a==5470&&b==3207)) {
                        std::cout<<std::setprecision(17)
                                 <<"[r06-m18b-refusal-1] alternate="<<alternative
                                 <<" optimized="<<corrected
                                 <<" rim_edge="<<a<<","<<b
                                 <<" survivor_face="<<survivorId
                                 <<" confidence="
                                 <<survivorCurvatureConfidenceName(curvature.confidence)
                                 <<" sample_count="<<curvature.contributingFaces
                                 <<" truncated="<<neighborhood.truncated
                                 <<" k_tt="<<curvature.seam
                                 <<" k_tu="<<curvature.mixed
                                 <<" k_uu="<<curvature.transverse
                                 <<" rms="<<curvature.rmsNormalResidual
                                 <<" max_normal_angle="<<curvature.maximumNormalAngle
                                 <<"\n";
                    }
                    if((a==3207&&b==5470)||(a==5470&&b==3207)) {
                        ++affected;
                        std::cout<<std::setprecision(17)
                                 <<"[r06-m18a-refusal-1] alternate="<<alternative
                                 <<" optimized="<<corrected
                                 <<" rim_edge="<<a<<","<<b
                                 <<" survivor_face="<<survivorId
                                 <<" signed_dihedral_deg="
                                 <<edge.signedDihedralRadians*(180./std::acos(-1.))
                                 <<" abs_dihedral_deg="
                                 <<edge.absoluteDihedralRadians*(180./std::acos(-1.))
                                 <<" first_ring_normal_offset_over_edge="
                                 <<edge.normalizedFirstRingNormalOffset
                                 <<" foot_t="<<edge.oppositeFootParameter<<"\\n";
                    }
                }
                if(!alternative)
                    require(affected==1,
                        "M18A initial refusal-1 must include exact boundary edge 3207,5470");
                std::cout<<std::setprecision(17)
                         <<"[r06-m18a-refusal-1-summary] alternate="<<alternative
                         <<" optimized="<<corrected
                         <<" max_abs_dihedral_deg="
                         <<profile.maximumAbsoluteDihedralRadians*(180./std::acos(-1.))
                         <<" mean_abs_dihedral_deg="
                         <<profile.meanAbsoluteDihedralRadians*(180./std::acos(-1.))
                         <<" max_abs_first_ring_offset_over_edge="
                         <<profile.maximumAbsoluteNormalizedOffset<<"\\n";
                double worst=1.;
                std::array<std::uint32_t,3> worstIds{};
                for(const auto& tri:local.faces) {
                    const auto a=local.positions[tri[0]];
                    const auto b=local.positions[tri[1]];
                    const auto d=local.positions[tri[2]];
                    const auto sum=dot(a-b,a-b)+dot(b-d,b-d)+dot(d-a,d-a);
                    const auto q=sum>0?
                        2*std::sqrt(3.)*length(cross(b-a,d-a))/sum:0.;
                    if(q<worst){worst=q;worstIds=tri;}
                }
                std::cout<<"[r06-m17d-candidate] loop="<<loopId
                         <<" alternate="<<alternative<<" corrected="<<corrected
                         <<" q="<<worst
                         <<" reported_q="<<local.minimumQuality
                         <<" worst_local="<<worstIds[0]<<","
                         <<worstIds[1]<<","<<worstIds[2]
                         <<" Poisson="<<local.poissonIterations
                         <<" faces="<<local.faces.size()<<"\n";
            }catch(const std::exception& e) {
                std::cout<<"[r06-m17d-candidate] loop="<<loopId
                         <<" alternate="<<alternative<<" corrected="<<corrected
                         <<" rejected="<<e.what()<<"\n";
            }
        }
    }
    MixedClosureStageSettings improvedBounds;
    improvedBounds.requireReflectedGeometry=saved.symmetricSource;
    improvedBounds.optimizeSpatialFront=true;
    improvedBounds.targetInitialCapQuality=.1;
    const auto improved=stagePrivateMixedClosure(cut,excision,plan,
                                                 improvedBounds);
    double newWorst=1.;
    std::size_t newWorstFace=0;
    for(std::size_t j=0;j<improved.faces().size();++j) {
        const auto& face=improved.faces()[j];
        if(face.kind!=MixedClosureFaceKind::Cap)continue;
        const auto ids=face.triangle();
        const auto a=improved.positions().at(ids[0]);
        const auto b=improved.positions().at(ids[1]);
        const auto d=improved.positions().at(ids[2]);
        const double s=dot(a-b,a-b)+dot(b-d,b-d)+dot(d-a,d-a);
        const double q=s>0?
            2*std::sqrt(3.)*length(cross(b-a,d-a))/s:0.;
        if(q<newWorst) {newWorst=q;newWorstFace=j;}
    }
    require(newWorst>=.1&&newWorst>firstFaces.front().quality+.03,
        "M17D exact refusal-1 inward-ring correction must eliminate both initial mirrored slivers without relaxing q");
    std::cout<<"[r06-m17d-optimized] initial_q_before="
             <<firstFaces.front().quality<<" initial_q_after="<<newWorst
             <<" worst_face_index="<<newWorstFace
             <<" provisional_vertices="<<improved.provisionalSpatialVertices().size()
             <<"\n";
    MixedPrivateClosureLimits correctedPolicy;
    correctedPolicy.requireReflectedGeometry=saved.symmetricSource;
    correctedPolicy.scaleAwareRefinement=true;
    correctedPolicy.staging.optimizeSpatialFront=true;
    const auto closed=applyPrivateMixedClosure(
        cut,excision,plan,correctedPolicy);
    require(closed.index&&closed.minimumCapQuality>=.1&&
        closed.refinementSteps<16&&
        saved.candidate.index->retiredFaceCount()==initialRetiredFaces,
        "strict source-backed M17D repair must close original 5K refusal-1 with healthy cap q and bounded refinement, source unchanged");
    std::cout<<"[r06-m17d-optimized] strict_result=CLOSED"
             <<" refinements="<<closed.refinementSteps
             <<" quality="<<closed.minimumCapQuality<<"\n";
    const auto certified=replayPrivateAutomaticRepair(
        saved,AutoClosureClassifier::Area,false,false,false,{},true,true);
    require(certified&&certified->privateAuto&&
            certified->privateAuto->index&&
            saved.candidate.index->retiredFaceCount()==initialRetiredFaces,
        "full M04C certified replay must also accept M17D refusal-1, with no worker publication");
    std::cout<<"[r06-m17d-certified] saved refusal-1 now certifies a complete private closure PASS\n";
    MixedPrivateClosureLimits limits;
    limits.requireReflectedGeometry=saved.symmetricSource;
    limits.scaleAwareRefinement=true;
    std::vector<std::string> declined;
    std::size_t splitsBeforeFirstAttempt=0, totalSplits=0;
    limits.staging.operationObserver=[&](
        const MixedClosureStaging&,const std::string& operation,
        const std::string& arguments) {
        if(operation=="splitEdge")++totalSplits;
        if(operation=="TryDiagonalFlip"||
           operation=="TryCapFanCollapse"||
           operation=="TryInteriorFairing"||
           operation=="TryQualityRestoringSplit") {
            if(declined.empty())splitsBeforeFirstAttempt=totalSplits;
            declined.push_back(operation+":"+arguments);
        }
    };
    bool refused=false;
    try {
        const auto legacyClosed=applyPrivateMixedClosure(
            cut,excision,plan,limits);
        require(legacyClosed.index!=nullptr,
            "accepted private mixed closure must produce a full index");
    }catch(const std::logic_error&) {refused=true;}
    require(!declined.empty()&&splitsBeforeFirstAttempt==0,
        "refusal-1 step 92 must expose shape attempts before any split");
    require(declined[0].starts_with("TryDiagonalFlip:")&&
            declined[0].find("result=NO_ADMISSIBLE")!=std::string::npos,
        "first no-op flip must be visible in source-backed step trace");
    require(std::any_of(declined.begin(),declined.end(),
        [](const std::string& op) {
            return op.starts_with("TryInteriorFairing:");
        }),"fairing attempt must be visible rather than mistaken for a skipped phase");
    require(saved.candidate.index->retiredFaceCount()==initialRetiredFaces,
        "source post-stamp tombstone count remains unchanged even on refusal");
    std::cout<<"[r06-m17c] exact refusal-1 saved case: "
             <<declined.size()<<" no-change repair diagnostics, "
             <<totalSplits<<" staged split vertices, "
             <<(refused?"strict refusal":"complete private closure")
             <<" PASS\n";
}
void budgetIsFiniteAndValidates() {
    const auto f=openTetra();
    MixedPrivateClosureLimits bad;
    bad.scaleAwareRefinement=true;
    bad.maxScaleAwareSplitOperations=0;
    bool refused=false;
    try {(void)applyPrivateMixedClosure(f.cut,f.proposal,f.plan,bad);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"zero budget must be invalid before staging");
    bad.maxScaleAwareSplitOperations=16;
    bad.maxScaleAwareSplitDepth=0;
    refused=false;
    try {(void)applyPrivateMixedClosure(f.cut,f.proposal,f.plan,bad);}
    catch(const std::invalid_argument&){refused=true;}
    require(refused,"zero ancestry depth must be invalid");
    std::cout<<"[r06-m17c] hard finite iteration/depth constraints PASS\n";
}
}
int main() {
    try {
        ordinaryUnchangedAndEnabledDensity();
        underQualityHandsOffInsteadOfSplitting();
        budgetIsFiniteAndValidates();
        refusalOneRealSavedCaseAttemptTrace();
        return 0;
    }catch(const std::exception& e) {
        std::cerr<<"[r06-m17c] FAILED "<<e.what()<<"\n";
        return 1;
    }
}
